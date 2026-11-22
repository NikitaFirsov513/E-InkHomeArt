#include "home_catalog.hpp"
#include <cassert>
#include <cstring>
#include <memory>
#include <cstdio>
static bool load(HomeCatalog &c,const char *text){auto *r=cJSON_Parse(text);bool ok=c.load(r);cJSON_Delete(r);return ok;}
static const char *fixture=R"({"status":"ok","rooms":[{"id":"r1","name":"Kitchen"},{"id":"r2","name":"Kitchen"}],"scenarios":[{"id":"s/one","name":"Evening"}],"devices":[{"id":"lamp","name":"Lamp","type":"devices.types.light","room":"r2","capabilities":[{"type":"devices.capabilities.on_off","state":{"instance":"on","value":false}},{"type":"devices.capabilities.range","parameters":{"instance":"brightness","unit":"unit.percent","random_access":true,"range":{"min":1,"max":100,"precision":1}},"state":{"instance":"brightness","value":42}}]}]})";
int main(){
 HomeCatalog c;assert(load(c,fixture));assert(c.data.loaded&&c.data.device_count==1&&c.data.devices[0].room==1);
 assert(!strcmp(c.data.devices[0].unit,"%")&&c.data.devices[0].icon==HOME_ICON_LIGHT&&strstr(c.data.devices[0].state,"%"));
 auto epoch=c.data.epoch;assert(epoch&&load(c,fixture)&&c.data.epoch==epoch);
 home_command_t cmd{HOME_CMD_TOGGLE,epoch,0,1};std::string path,body;DeviceAction action;
 assert(c.command(cmd,path,body,action)&&path=="/v1.0/devices/actions"&&action.boolean);
 assert(!c.data.devices[0].on); // Building a command never changes observed state.
 cmd.kind=HOME_CMD_RANGE;cmd.value=75;assert(c.command(cmd,path,body,action));
 cmd.value=101;assert(!c.command(cmd,path,body,action));cmd.value=75.5;assert(!c.command(cmd,path,body,action));
 cmd.kind=HOME_CMD_SCENE;assert(c.command(cmd,path,body,action)&&path=="/v1.0/scenarios/s%2Fone/actions");
 cmd.epoch=epoch-1;assert(!c.command(cmd,path,body,action));cmd.epoch=epoch;cmd.slot=1;assert(!c.command(cmd,path,body,action));
 assert(!load(c,"{}")&&c.data.epoch==epoch&&c.data.device_count==1);
 std::string changed=fixture;changed.replace(changed.find("lamp"),4,"other");assert(load(c,changed.c_str())&&c.data.epoch!=epoch);
 cmd.slot=0;assert(!c.command(cmd,path,body,action));
 char name[80];homeText(name,sizeof(name),"Кухня 🌈");assert(!strcmp(name,"Кухня ?"));
 char shortname[5];homeText(shortname,sizeof(shortname),"АБВ");assert(!strcmp(shortname,"АБ"));
 std::string offline=fixture;offline.insert(offline.find("\"room\":\"r2\""),"\"state\":\"offline\",");assert(load(c,offline.c_str()));
 cmd={HOME_CMD_TOGGLE,c.data.epoch,0,1};assert(!c.data.devices[0].available&&!c.command(cmd,path,body,action));
 c.clear();assert(!c.data.loaded&&!c.data.device_count&&!c.command(cmd,path,body,action));
 puts("PASS: real catalog, duplicate room names by ID, stable/stale epochs, bounds, command validation, no optimistic state, offline devices and Unicode.");
}
