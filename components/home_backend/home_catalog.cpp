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
namespace {
std::string numberSignature(double value)
{char text[48];snprintf(text,sizeof(text),"%.17g",value);return text;}
struct Label {const char *id,*text;};
const char *label(const std::string &id)
{
    static const Label names[]={
        {"on","Питание"},{"brightness","Яркость"},{"temperature","Температура"},{"temperature_k","Температура света"},
        {"rgb","Цвет RGB"},{"hsv","Цвет HSV"},{"scene","Световые эффекты"},{"volume","Громкость"},{"channel","Канал"},
        {"open","Открытие"},{"humidity","Влажность"},{"thermostat","Режим климата"},{"fan_speed","Скорость вентилятора"},
        {"work_speed","Скорость работы"},{"cleanup_mode","Режим уборки"},{"program","Программа"},{"input_source","Источник сигнала"},
        {"swing","Поворот жалюзи"},{"tea_mode","Вид чая"},{"coffee_mode","Вид кофе"},{"dishwashing","Режим мойки"},
        {"heat","Нагрев"},{"pause","Пауза"},{"mute","Без звука"},{"backlight","Подсветка"},{"controls_locked","Блокировка кнопок"},
        {"ionization","Ионизация"},{"keep_warm","Поддержание тепла"},{"oscillation","Вращение"},{"auto","Авто"},
        {"cool","Охлаждение"},{"dry","Осушение"},{"fan_only","Вентиляция"},{"eco","Эко"},{"quiet","Тихий"},
        {"turbo","Турбо"},{"low","Низкий"},{"medium","Средний"},{"high","Высокий"},{"min","Минимум"},{"max","Максимум"},
        {"normal","Обычный"},{"fast","Быстрый"},{"slow","Медленный"},{"sleep","Ночной"},{"horizontal","Горизонтально"},
        {"vertical","Вертикально"},{"stationary","Неподвижно"},{"stop","Стоп"},{"off","Выключено"},{"smart","Умный"},
        {"one","Первый"},{"two","Второй"},{"three","Третий"},{"four","Четвёртый"},{"five","Пятый"},{"six","Шестой"},
        {"seven","Седьмой"},{"eight","Восьмой"},{"nine","Девятый"},{"ten","Десятый"},{"eleven","Одиннадцатый"},
        {"twelve","Двенадцатый"},{"thirteen","Тринадцатый"},{"fourteen","Четырнадцатый"},{"fifteen","Пятнадцатый"},
        {"sixteen","Шестнадцатый"},{"seventeen","Семнадцатый"},{"eighteen","Восемнадцатый"},{"nineteen","Девятнадцатый"},{"twenty","Двадцатый"},
        {"party","Вечеринка"},{"alarm","Тревога"},{"fantasy","Фантазия"},{"reading","Чтение"},{"romance","Романтика"},
        {"sunrise","Рассвет"},{"sunset","Закат"},{"night","Ночь"},{"candle","Свеча"},{"movie","Кино"},
        {"battery_level","Заряд батареи"},{"co2_level","Уровень CO2"},{"power","Мощность"},{"voltage","Напряжение"},
        {"amperage","Ток"},{"illumination","Освещённость"},{"pressure","Давление"},{"water_level","Уровень воды"},
        {"food_level","Уровень корма"},{"water_meter","Счётчик воды"},{"electricity_meter","Счётчик энергии"},
        {"gas_meter","Счётчик газа"},{"heat_meter","Счётчик тепла"},{"meter","Показание счётчика"},
        {"pm1_density","Частицы PM1"},{"pm2.5_density","Частицы PM2.5"},{"pm10_density","Частицы PM10"},
        {"tvoc","Летучие вещества"},{"motion","Движение"},{"water_leak","Протечка"},{"smoke","Дым"},
        {"gas","Газ"},{"button","Нажатие кнопки"},{"vibration","Вибрация"},{"opened","Открыто"},{"closed","Закрыто"},
        {"detected","Обнаружено"},{"not_detected","Не обнаружено"},{"leak","Протечка"},{"dry","Сухо"},
        {"normal","Норма"},{"low","Низкий"},{"empty","Пусто"},{"full","Полный"},{"high","Высокий"},
        {"click","Короткое нажатие"},{"double_click","Двойное нажатие"},{"long_press","Долгое нажатие"},
        {"fall","Падение"},{"tilt","Наклон"},{"vibration","Вибрация"},{"get_stream","Видеопоток"},{"devices.capabilities.video_stream","Видеопоток"}
    };
    for(const auto &n:names)if(id==n.id)return n.text;
    return id.c_str();
}
const char *unit(const std::string &id)
{
    static const Label names[]={ {"unit.percent","%"},{"unit.temperature.celsius","°C"},{"unit.temperature.kelvin","K"},
        {"unit.ampere","A"},{"unit.volt","V"},{"unit.watt","W"},{"unit.kilowatt_hour","kWh"},{"unit.lux","lux"},
        {"unit.ppm","ppm"},{"unit.pressure.pascal","Pa"},{"unit.pressure.mmhg","mmHg"},{"unit.pressure.atm","atm"},
        {"unit.pressure.bar","bar"},{"unit.microgram_m3","ug/m3"},{"unit.cubic_meter","m3"},{"unit.gigacalorie","Gcal"} };
    for(const auto &n:names)if(id==n.id)return n.text;
    auto dot=id.rfind('.');return id.empty()?"":dot==std::string::npos?id.c_str():id.c_str()+dot+1;
}
void exportControl(home_live_data_t &next,home_live_device_t &device,const DeviceControl &c,unsigned owner,unsigned source)
{
    if(next.control_count>=HOME_MAX_CONTROLS){next.truncated=device.controls_truncated=true;return;}
    auto &out=next.controls[next.control_count++];++device.control_count;
    out.device=owner;out.source=source;
    homeText(out.name,sizeof(out.name),label(c.instance.empty()?c.type:c.instance));
    homeText(out.unit,sizeof(out.unit),unit(c.unit));
    out.known=c.known;out.boolean=c.boolean;out.relative_only=c.relative_only;
    out.has_minimum=c.has_minimum;out.has_maximum=c.has_maximum;
    out.momentary=c.type=="devices.capabilities.on_off"&&!c.retrievable&&!c.split;
    out.value=c.number;out.second=c.saturation;out.third=c.value;out.minimum=c.minimum;out.maximum=c.maximum;out.step=c.step;
    out.kind=c.kind==ControlKind::Toggle?HOME_CONTROL_TOGGLE:c.kind==ControlKind::Range?HOME_CONTROL_RANGE:
        c.kind==ControlKind::Mode?HOME_CONTROL_MODE:c.kind==ControlKind::Rgb?HOME_CONTROL_RGB:c.kind==ControlKind::Hsv?HOME_CONTROL_HSV:HOME_CONTROL_UNSUPPORTED;
    out.option_start=next.option_count;out.selected_option=UINT16_MAX;
    for(const auto &o:c.options){
        if(next.option_count>=HOME_MAX_OPTIONS){next.truncated=device.controls_truncated=true;break;}
        if(c.known&&c.text==o.value)out.selected_option=out.option_count;
        homeText(next.options[next.option_count++].name,sizeof(next.options[0].name),label(o.value));++out.option_count;
    }
    std::string state="Состояние неизвестно";
    char number[96];
    if(out.kind==HOME_CONTROL_UNSUPPORTED)state="Недоступно на этой панели";
    else if(c.known){
        if(c.kind==ControlKind::Toggle)state=c.boolean?"Включено":"Выключено";
        else if(c.kind==ControlKind::Mode)state=label(c.text);
        else if(c.kind==ControlKind::Rgb){snprintf(number,sizeof(number),"#%06X",(unsigned)c.number);state=number;}
        else if(c.kind==ControlKind::Hsv){snprintf(number,sizeof(number),"H %g / S %g / V %g",c.number,c.saturation,c.value);state=number;}
        else {snprintf(number,sizeof(number),"%g%s%s",c.number,out.unit[0]?" ":"",out.unit);state=number;}
    }
    homeText(out.state,sizeof(out.state),state);
}
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
        out.control_start=next.control_count;
        for(size_t index=0;index<d.controls.size();++index)exportControl(next,out,d.controls[index],i,index);
        for(const auto &p:d.properties){
            if(next.control_count>=HOME_MAX_CONTROLS){next.truncated=out.controls_truncated=true;break;}
            auto &v=next.controls[next.control_count++];++out.control_count;v.device=i;v.kind=HOME_CONTROL_READING;v.known=p.known;
            homeText(v.name,sizeof(v.name),label(p.instance));homeText(v.unit,sizeof(v.unit),unit(p.unit));
            homeText(v.state,sizeof(v.state),p.known?std::string(p.type=="devices.properties.event"&&p.text=="dry"?"Сухо":p.type=="devices.properties.event"&&p.text=="normal"?"Норма":label(p.text))+(v.unit[0]?std::string(" ")+v.unit:""):"Нет данных");
        }
        std::string label="Состояние неизвестно";
        for(const auto &c:d.controls){
            signature.push_back(d.id+":"+c.type+":"+c.instance+":"+numberSignature(c.minimum)+":"+numberSignature(c.maximum)+":"+numberSignature(c.step)+":"+c.unit);
            signature.push_back(std::to_string(c.has_minimum)+std::to_string(c.has_maximum)+std::to_string(c.relative_only)+std::to_string(c.retrievable)+std::to_string(c.split));
            for(const auto &o:c.options)signature.push_back("option:"+o.value);
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
        for(const auto &p:d.properties)signature.push_back(d.id+":property:"+p.type+":"+p.instance+":"+p.unit);
        if(out.has_range){char value[32];snprintf(value,sizeof(value),"%g%s%s",out.value,out.unit[0]?" ":"",out.unit);label=out.has_toggle?label+" / "+value:std::string(out.range_name)+": "+value;}
        else if(!out.has_toggle){
            for(unsigned n=out.control_start;n<out.control_start+out.control_count;++n)
                if(next.controls[n].kind==HOME_CONTROL_READING&&next.controls[n].known){label=next.controls[n].state;break;}
        }
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
    if(c.kind==HOME_CMD_CONTROL){
        if(c.control>=data.control_count||c.control<view.control_start||c.control>=view.control_start+view.control_count)return false;
        const auto &v=data.controls[c.control];
        if(v.device!=c.slot||v.kind==HOME_CONTROL_READING||v.kind==HOME_CONTROL_UNSUPPORTED||v.source>=d.controls.size())return false;
        control=&d.controls[v.source];
        if(c.relative&&control->kind!=ControlKind::Range)return false;
        if(control->kind==ControlKind::Toggle){if(c.value!=0&&c.value!=1)return false;action.kind=ActionValue::Boolean;action.boolean=c.value!=0;}
        else if(control->kind==ControlKind::Mode){
            if(c.option>=control->options.size()||c.option>=v.option_count||control->options[c.option].value.size()>=sizeof(action.text))return false;
            action.kind=ActionValue::String;snprintf(action.text,sizeof(action.text),"%s",control->options[c.option].value.c_str());
        }else if(control->kind==ControlKind::Hsv){action.kind=ActionValue::Hsv;action.number=c.value;action.saturation=c.second;action.value=c.third;}
        else {action.kind=ActionValue::Number;action.number=c.value;action.relative=c.relative;}
    }else if(c.kind==HOME_CMD_TOGGLE&&view.has_toggle){
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
