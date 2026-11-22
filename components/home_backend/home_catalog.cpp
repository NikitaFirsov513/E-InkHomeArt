#include "home_catalog.hpp"
#include "YandexAuth.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>

static std::string field(const cJSON *j,const char *key)
{
    const auto *v=cJSON_GetObjectItemCaseSensitive(j,key);
    return cJSON_IsString(v)&&v->valuestring?v->valuestring:"";
}
/* Keep supported ASCII/Cyrillic; consume complete UTF-8 sequences for emoji. */
void homeText(char *out,size_t cap,const std::string &value)
{
    if(!cap)return;
    size_t n=0;
    for(size_t i=0;i<value.size()&&n+1<cap;){
        unsigned char c=value[i];size_t bytes=1;unsigned cp=c;
        if(c>=0xc2&&c<0xe0)bytes=2;else if(c>=0xe0&&c<0xf0)bytes=3;else if(c>=0xf0&&c<0xf5)bytes=4;
        bool valid=i+bytes<=value.size();
        if(bytes>1){cp=c&((1U<<(7-bytes))-1);for(size_t k=1;k<bytes&&valid;++k){unsigned char b=value[i+k];valid=(b&0xc0)==0x80;cp=(cp<<6)|(b&63);}}
        bool supported=valid&&((cp>=32&&cp<=126)||(cp>=0x410&&cp<=0x44f)||cp==0x401||cp==0x451||cp==0xb0||cp==0xb7);
        if(supported&&n+bytes<cap){memcpy(out+n,value.data()+i,bytes);n+=bytes;}
        else out[n++]='?';
        i+=valid?bytes:1;
    }
    out[n]=0;
}
void HomeCatalog::clear()
{
    devices_.clear();rooms_.clear();scenes_.clear();signature_.clear();
    uint32_t epoch=data.epoch+1;data={};data.epoch=epoch?epoch:1;
}
bool HomeCatalog::load(cJSON *root)
{
    if(field(root,"status")!="ok"||!cJSON_IsArray(cJSON_GetObjectItemCaseSensitive(root,"rooms"))||
       !cJSON_IsArray(cJSON_GetObjectItemCaseSensitive(root,"scenarios")))return false;
    DeviceManager parsed;
    if(parsed.createDevicesFromJson(root)!=ESP_OK)return false;
    std::vector<Device> devices;
    if(parsed.getDevices(devices)!=ESP_OK)return false;
    std::vector<std::string> rooms,scenes,signature;
    auto storage=std::make_unique<home_live_data_t>();
    auto &next=*storage;next.epoch=data.epoch;next.loaded=true;
    auto names=[&](const char *key,std::vector<std::string> &ids,home_live_name_t *names,size_t max)->bool{
        const cJSON *entry=nullptr;
        cJSON_ArrayForEach(entry,cJSON_GetObjectItemCaseSensitive(root,key)){
            auto id=field(entry,"id"),name=field(entry,"name");
            if(id.empty()||id.size()>127||std::find(ids.begin(),ids.end(),id)!=ids.end())return false;
            if(ids.size()>=max){next.truncated=true;continue;}
            homeText(names[ids.size()].name,sizeof(names[0].name),name);ids.push_back(id);signature.push_back(std::string(key)+":"+id);
        }
        return true;
    };
    if(!names("rooms",rooms,next.rooms,HOME_MAX_ROOMS)||!names("scenarios",scenes,next.scenes,HOME_MAX_SCENES))return false;
    next.room_count=rooms.size();next.scene_count=scenes.size();
    if(devices.size()>HOME_MAX_DEVICES){devices.resize(HOME_MAX_DEVICES);next.truncated=true;}
    next.device_count=devices.size();
    for(size_t i=0;i<devices.size();++i){
        auto &d=devices[i];auto &out=next.devices[i];out.room=255;
        if(d.id.size()>127)return false;
        const auto *raw=cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(root,"devices"),i);
        const auto room=field(raw,"room"),status=field(raw,"state");
        auto r=std::find(rooms.begin(),rooms.end(),room);if(r!=rooms.end())out.room=static_cast<uint8_t>(r-rooms.begin());
        d.available=status.empty()||status=="online";out.available=d.available;
        signature.push_back("device:"+d.id+":"+room);
        homeText(out.name,sizeof(out.name),d.name);
        out.icon=d.type.find("light")!=std::string::npos?HOME_ICON_LIGHT:
                 d.type.find("thermostat")!=std::string::npos||d.type.find("heater")!=std::string::npos?HOME_ICON_HEAT:
                 d.type.find("curtain")!=std::string::npos||d.type.find("openable")!=std::string::npos?HOME_ICON_CURTAIN:HOME_ICON_POWER;
        std::string label="Состояние неизвестно";
        for(const auto &c:d.controls){
            signature.push_back(d.id+":"+c.type+":"+c.instance+":"+std::to_string(c.minimum)+":"+std::to_string(c.maximum)+":"+std::to_string(c.step)+":"+c.unit);
            if(c.type=="devices.capabilities.on_off"&&c.known){out.has_toggle=true;out.on=c.boolean;label=c.boolean?"Включено":"Выключено";}
            if(c.kind==ControlKind::Range&&!c.relative_only&&c.known&&!out.has_range){
                out.has_range=true;out.value=c.number;out.minimum=c.minimum;out.maximum=c.maximum;out.step=c.step;
                const auto name=c.instance=="brightness"?"Яркость":c.instance=="temperature"?"Температура":c.instance=="open"?"Открытие":c.instance=="volume"?"Громкость":c.instance=="temperature_k"?"Температура света":c.instance;
                homeText(out.range_name,sizeof(out.range_name),name);
                const char *unit=c.unit=="unit.percent"?"%":c.unit=="unit.temperature.celsius"?"°C":
                                 c.unit=="unit.temperature.kelvin"?"K":"";
                homeText(out.unit,sizeof(out.unit),unit);
            }
        }
        if(out.has_range){char value[32];snprintf(value,sizeof(value),"%g%s%s",out.value,out.unit[0]?" ":"",out.unit);label=out.has_toggle?label+" / "+value:std::string(out.range_name)+": "+value;}
        else if(!out.has_toggle&&!d.readings.empty())label=d.readings.front();
        homeText(out.state,sizeof(out.state),label);
    }
    if(signature!=signature_){++next.epoch;if(!next.epoch)++next.epoch;}
    data=next;devices_=std::move(devices);rooms_=std::move(rooms);scenes_=std::move(scenes);signature_=std::move(signature);return true;
}
bool HomeCatalog::command(const home_command_t &c,std::string &path,std::string &body,DeviceAction &action) const
{
    path.clear();body.clear();action={};
    if(!data.loaded||c.epoch!=data.epoch)return false;
    if(c.kind==HOME_CMD_SCENE){
        if(c.slot>=scenes_.size())return false;
        path="/v1.0/scenarios/"+YandexAuth::encode(scenes_[c.slot])+"/actions";body="{}";return true;
    }
    if(c.slot>=devices_.size())return false;
    const auto &d=devices_[c.slot];const auto &view=data.devices[c.slot];const DeviceControl *control=nullptr;
    if(c.kind==HOME_CMD_TOGGLE&&view.has_toggle){
        for(const auto &v:d.controls)if(v.type=="devices.capabilities.on_off"&&v.known){control=&v;break;}
        if(c.value!=0&&c.value!=1)return false;
        action.kind=ActionValue::Boolean;action.boolean=c.value!=0;
    }else if(c.kind==HOME_CMD_RANGE&&view.has_range&&std::isfinite(c.value)){
        for(const auto &v:d.controls)if(v.kind==ControlKind::Range&&!v.relative_only&&v.known){control=&v;break;}
        if(!control)return false;
        double steps=(c.value-control->minimum)/control->step;
        if(std::abs(steps-std::round(steps))>0.001)return false;
        action.kind=ActionValue::Number;action.number=c.value;
    }else return false;
    if(!control||control->type.size()>=sizeof(action.type)||control->instance.size()>=sizeof(action.instance))return false;
    snprintf(action.device_id,sizeof(action.device_id),"%s",d.id.c_str());
    snprintf(action.type,sizeof(action.type),"%s",control->type.c_str());
    snprintf(action.instance,sizeof(action.instance),"%s",control->instance.c_str());
    if(buildDeviceAction(d,action,body)!=ESP_OK)return false;
    path="/v1.0/devices/actions";return true;
}
