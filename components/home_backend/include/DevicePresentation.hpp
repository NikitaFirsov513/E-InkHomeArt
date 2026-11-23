#pragma once
#include "Device.hpp"
#include "home_live.h"
#include <cstring>

/* UX preferences, not protocol restrictions. Every advertised capability is kept.
 * More specific rules precede family inheritance; unknown future types use the
 * generic overview. Wire identities and the order of mode options never change. */
namespace home_presentation {
struct Policy {home_device_profile_t profile;bool dashboard;const char *order;};
inline bool family(const std::string &type,const char *base)
{return type==base||type.rfind(std::string(base)+".",0)==0;}
inline Policy policy(std::string type)
{
    const std::string prefix="devices.types.";
    if(type.rfind(prefix,0)==0)type.erase(0,prefix.size());
    if(family(type,"light"))return {HOME_PROFILE_LIGHT,false,"brightness|temperature_k|hsv|rgb|scene|on|backlight"};
    if(family(type,"thermostat"))return {HOME_PROFILE_CLIMATE,false,"temperature|thermostat|fan_speed|heat|on|swing|oscillation"};
    if(type=="humidifier")return {HOME_PROFILE_HUMIDIFIER,false,"humidity|fan_speed|work_speed|on|ionization|backlight"};
    if(type=="purifier")return {HOME_PROFILE_AIR,false,"fan_speed|work_speed|on|ionization|humidity"};
    if(type=="ventilation.fan")return {HOME_PROFILE_FAN,false,"fan_speed|work_speed|on|oscillation|swing"};
    if(family(type,"ventilation"))return {HOME_PROFILE_AIR,false,"fan_speed|ventilation_mode|temperature|thermostat|on"};
    if(family(type,"media_device"))return {HOME_PROFILE_MEDIA,false,"volume|channel|input_source|on|mute|pause|backlight|controls_locked"};
    if(type=="vacuum_cleaner")return {HOME_PROFILE_VACUUM,true,"on|pause|cleanup_mode|work_speed|fan_speed"};
    if(type=="openable.door_lock"||type=="openable.door-lock")return {HOME_PROFILE_LOCK,true,"open|on|controls_locked"};
    if(family(type,"openable"))return {HOME_PROFILE_OPENABLE,false,"open|on|work_speed"};
    if(type=="socket"||family(type,"switch"))return {HOME_PROFILE_POWER,false,"on|backlight|controls_locked"};
    if(type=="cooking.kettle")return {HOME_PROFILE_KETTLE,false,"temperature|on|tea_mode|keep_warm|backlight|controls_locked|hsv|rgb|scene"};
    if(type=="cooking.coffee_maker")return {HOME_PROFILE_COFFEE,false,"coffee_mode|program|on|temperature|keep_warm"};
    if(type=="cooking.multicooker")return {HOME_PROFILE_COOKER,false,"program|temperature|on|keep_warm|controls_locked"};
    if(family(type,"cooking"))return {HOME_PROFILE_COOKER,false,"temperature|program|heat|on|keep_warm|controls_locked"};
    if(type=="dishwasher")return {HOME_PROFILE_DISHWASHER,false,"dishwashing|program|on|pause|controls_locked"};
    if(type=="washing_machine")return {HOME_PROFILE_WASHER,true,"program|on|pause|temperature|controls_locked"};
    if(type=="iron")return {HOME_PROFILE_IRON,false,"temperature|program|on"};
    if(type=="pet_feeder")return {HOME_PROFILE_PET_FEEDER,true,"on|program"};
    if(type=="pet_drinking_fountain")return {HOME_PROFILE_PET_WATER,true,"on|program|work_speed"};
    if(family(type,"sensor"))return {HOME_PROFILE_SENSOR,true,""};
    if(family(type,"smart_meter"))return {HOME_PROFILE_METER,true,""};
    if(type=="camera")return {HOME_PROFILE_CAMERA,true,"on|get_stream"};
    return {HOME_PROFILE_GENERIC,true,"brightness|temperature|open|humidity|volume|program|on"};
}
inline unsigned position(const char *list,const std::string &instance)
{
    unsigned n=0;const char *p=list;
    while(*p){const char *end=strchr(p,'|');size_t len=end?size_t(end-p):strlen(p);
        if(instance.size()==len&&!instance.compare(0,len,p,len))return n;
        if(!end)break;
        p=end+1;++n;
    }
    return 30;
}
inline unsigned rank(const Device &device,const DeviceControl &c)
{
    if(c.kind==ControlKind::Unsupported)return 900;
    auto p=policy(device.type);unsigned rank=position(p.order,c.instance)*10;
    if(p.profile==HOME_PROFILE_SENSOR||p.profile==HOME_PROFILE_METER)return 400+rank;
    return rank;
}
inline unsigned readingRank(const Device &device,const DeviceReading &r)
{
    auto p=policy(device.type);std::string preferred;
    auto at=device.type.rfind('.');std::string subtype=at==std::string::npos?"":device.type.substr(at+1);
    if(p.profile==HOME_PROFILE_SENSOR){
        preferred=subtype=="climate"?"temperature":subtype;
        if(r.instance==preferred)return 0;
        return 10+position("temperature|humidity|co2_level|illumination|motion|open|smoke|gas|water_leak|vibration|button|water_level|battery_level",r.instance)*5;
    }
    if(p.profile==HOME_PROFILE_METER){
        preferred=subtype=="electricity"?"electricity_meter":subtype=="gas"?"gas_meter":subtype=="heat"?"heat_meter":subtype=="cold_water"||subtype=="hot_water"?"water_meter":"meter";
        if(r.instance==preferred)return 0;
        return 10+position("meter|electricity_meter|water_meter|gas_meter|heat_meter|power|voltage|amperage|battery_level",r.instance)*5;
    }
    return 100+position(p.profile==HOME_PROFILE_VACUUM?"battery_level|water_level":"temperature|humidity|water_level|food_level|battery_level|power|voltage|amperage",r.instance)*5;
}
inline const char *controlName(const Device &d,const DeviceControl &c)
{
    auto p=policy(d.type);
    if(c.type=="devices.capabilities.on_off"&&p.profile==HOME_PROFILE_VACUUM)return "Уборка";
    if(c.type=="devices.capabilities.on_off"&&p.profile==HOME_PROFILE_KETTLE)return "Нагрев воды";
    if(c.type=="devices.capabilities.on_off"&&p.profile==HOME_PROFILE_WASHER)return "Работа машины";
    if(c.type=="devices.capabilities.on_off"&&p.profile==HOME_PROFILE_DISHWASHER)return "Работа посудомойки";
    if(c.type=="devices.capabilities.on_off"&&p.profile==HOME_PROFILE_PET_FEEDER)return "Подача корма";
    if(c.type=="devices.capabilities.on_off"&&p.profile==HOME_PROFILE_PET_WATER)return "Поилка";
    if(c.instance=="work_speed"&&p.profile==HOME_PROFILE_VACUUM)return "Интенсивность";
    if(c.instance=="pause")return p.profile==HOME_PROFILE_VACUUM?"Пауза уборки":"Пауза";
    if(c.instance=="temperature"&&p.profile==HOME_PROFILE_CLIMATE)return "Заданная температура";
    if(c.instance=="humidity"&&p.profile==HOME_PROFILE_HUMIDIFIER)return "Целевая влажность";
    if(c.instance=="open"&&c.kind==ControlKind::Range)return "Степень открытия";
    if(c.instance=="heat"&&c.kind==ControlKind::Mode)return "Уровень нагрева";
    return nullptr;
}
inline const char *onLabel(const Device &d,const DeviceControl &c)
{
    if(c.type=="devices.capabilities.on_off"&&policy(d.type).profile==HOME_PROFILE_VACUUM)return "Начать уборку";
    if(c.type=="devices.capabilities.on_off"&&policy(d.type).profile==HOME_PROFILE_KETTLE)return "Начать нагрев";
    if(c.type=="devices.capabilities.on_off"&&policy(d.type).profile==HOME_PROFILE_PET_FEEDER)return "Подать корм";
    if(c.type=="devices.capabilities.on_off"&&(policy(d.type).profile==HOME_PROFILE_WASHER||policy(d.type).profile==HOME_PROFILE_DISHWASHER))return "Запустить";
    if(c.instance=="pause")return "Пауза";
    if(c.instance=="mute")return "Без звука";
    if(c.instance=="controls_locked")return "Заблокировать";
    return "Включить";
}
inline const char *offLabel(const Device &d,const DeviceControl &c)
{
    if(c.type=="devices.capabilities.on_off"&&policy(d.type).profile==HOME_PROFILE_VACUUM)return "Остановить";
    if(c.type=="devices.capabilities.on_off"&&(policy(d.type).profile==HOME_PROFILE_KETTLE||policy(d.type).profile==HOME_PROFILE_PET_FEEDER||policy(d.type).profile==HOME_PROFILE_WASHER||policy(d.type).profile==HOME_PROFILE_DISHWASHER))return "Остановить";
    if(c.instance=="pause")return "Продолжить";
    if(c.instance=="mute")return "Вернуть звук";
    if(c.instance=="controls_locked")return "Разблокировать";
    return "Выключить";
}
inline const char *booleanState(const Device &d,const DeviceControl &c)
{
    if(c.type=="devices.capabilities.on_off"&&policy(d.type).profile==HOME_PROFILE_VACUUM)return c.boolean?"Уборка включена":"Уборка остановлена";
    if(c.instance=="pause")return c.boolean?"На паузе":"Пауза снята";
    if(c.instance=="mute")return c.boolean?"Звук выключен":"Звук включён";
    if(c.instance=="controls_locked")return c.boolean?"Кнопки заблокированы":"Кнопки разблокированы";
    return c.boolean?"Включено":"Выключено";
}
}
