#include "home_catalog.hpp"
extern "C" {
#include "home_ui.h"
#include "../../components/home_ui/home_ui_layout.h"
}
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <vector>

struct Rect {int x,y,w,h;};
static std::vector<Rect> text_boxes;
static bool capture;
extern "C" void home_ui_check_text(int x,int y,int w,int h)
{if(capture&&w)text_boxes.push_back({x,y,w,h});}
static bool overlaps(Rect a,Rect b)
{return a.x<b.x+b.w&&b.x<a.x+a.w&&a.y<b.y+b.h&&b.y<a.y+a.h;}
static std::string fixture_text()
{
    std::ifstream f("tests/fixtures/yandex-capabilities.json");assert(f);
    return {std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>()};
}
static std::string file_text(const char *path)
{std::ifstream f(path);assert(f);return {std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>()};}
static void fixture(HomeCatalog &c,home_ui_t &u)
{
    auto *json=cJSON_Parse(fixture_text().c_str());assert(json&&c.load(json));cJSON_Delete(json);
    c.data.online=c.data.time_ready=c.data.authorized=true;
    home_ui_init(&u);home_ui_set_live(&u,&c.data);u.active=true;u.page=HOME_PAGE_DEVICE;
}
static unsigned index(const home_live_data_t &l,const char *name,unsigned device=0)
{
    const auto &d=l.devices[device];
    for(unsigned i=d.control_start;i<d.control_start+d.control_count;++i)if(!strcmp(l.controls[i].name,name))return i;
    assert(!"Missing capability");return 0;
}
static bool has_action(const home_ui_t &u,unsigned action)
{
    home_ui_control_t controls[24];unsigned n=home_ui_controls(&u,controls,24);
    for(unsigned i=0;i<n;++i)if(controls[i].action==action)return true;
    return false;
}
static void tap(home_ui_t &u,unsigned action)
{
    static int64_t now=1000;now+=10000;home_ui_presented(&u,true,u.revision,now);
    home_ui_control_t controls[24];unsigned n=home_ui_controls(&u,controls,24);
    for(unsigned i=0;i<n;++i)if(controls[i].action==action){
        const auto &b=controls[i];home_ui_touch(&u,1,b.x+b.w/2,b.y+b.h/2,0,now);
        assert(home_ui_touch(&u,0,-1,-1,0,now+1000));return;
    }
    fprintf(stderr,"Missing action %u on page %u\n",action,u.page);assert(false);
}
static bool point(home_ui_t &u,int x,int y)
{
    static int64_t now=2000000;now+=10000;home_ui_presented(&u,true,u.revision,now);
    home_ui_touch(&u,1,x,y,0,now);return home_ui_touch(&u,0,-1,-1,0,now+1000);
}
static void open(home_ui_t &u,unsigned control)
{
    u.page=HOME_PAGE_DEVICE;u.control_offset=(control-u.live->devices[u.device].control_start)/6*6;
    static int64_t now=500000;now+=10000;home_ui_presented(&u,true,u.revision,now);
    unsigned tile=control-u.live->devices[u.device].control_start-u.control_offset;
    assert(home_ui_target(&u,home_ui_tile_x(tile)+20,home_ui_tile_y(tile)+20)==CAP_OPEN+control);
    home_ui_touch(&u,1,home_ui_tile_x(tile)+20,home_ui_tile_y(tile)+20,0,now);
    assert(home_ui_touch(&u,0,-1,-1,0,now+1000));assert(u.page==HOME_PAGE_CONTROL&&u.control==control);
}
static void input(home_ui_t &u,const char *number)
{
    assert(u.page==HOME_PAGE_NUMBER);tap(u,NUMBER_KEY+14);
    for(const char *p=number;*p;++p){
        const char *keys="123x456-789.0";const char *k=strchr(keys,*p);assert(k);tap(u,NUMBER_KEY+(unsigned)(k-keys));
    }
    tap(u,NUMBER_KEY+15);
}
static void check_command(const HomeCatalog &c,const home_ui_t &u,const char *instance)
{
    std::string path,body;DeviceAction action;assert(u.command.kind==HOME_CMD_CONTROL);
    assert(c.command(u.command,path,body,action)&&path=="/v1.0/devices/actions"&&!strcmp(action.instance,instance));
    auto *json=cJSON_Parse(body.c_str());assert(json);
    auto *device=cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(json,"devices"),0);
    auto *cap=cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(device,"actions"),0);
    auto *state=cJSON_GetObjectItemCaseSensitive(cap,"state");
    assert(!strcmp(cJSON_GetObjectItemCaseSensitive(state,"instance")->valuestring,instance));cJSON_Delete(json);
}
static void render_checked(home_ui_t &u)
{
    static uint8_t buffer[HOME_UI_BYTES+2];memset(buffer,0xa5,sizeof(buffer));
    home_ui_control_t controls[24];unsigned n=home_ui_controls(&u,controls,24);assert(n<=20);
    for(unsigned i=0;i<n;++i){const auto &c=controls[i];Rect a{c.x,c.y,c.w,c.h};
        assert(a.x>=0&&a.y>=0&&a.x+a.w<=800&&a.y+a.h<=480&&a.w>=56&&a.h>=56);
        assert(home_ui_target(&u,a.x+a.w/2,a.y+a.h/2)==c.action);
        for(unsigned j=0;j<i;++j)assert(!overlaps(a,{controls[j].x,controls[j].y,controls[j].w,controls[j].h}));
    }
    text_boxes.clear();capture=true;assert(home_ui_render(buffer+1,HOME_UI_BYTES,&u));capture=false;
    assert(buffer[0]==0xa5&&buffer[sizeof(buffer)-1]==0xa5);
    for(unsigned i=0;i<text_boxes.size();++i){Rect a=text_boxes[i];
        assert(a.x>=0&&a.y>=0&&a.x+a.w<=800&&a.y+a.h<=480);
        for(unsigned j=0;j<i;++j)if(overlaps(a,text_boxes[j])){fprintf(stderr,"Text intersection page %u, control %u, texts %u/%u\n",u.page,u.control,i,j);assert(false);}
        for(unsigned j=0;j<n;++j){Rect b{controls[j].x,controls[j].y,controls[j].w,controls[j].h};
            bool inside=a.x>=b.x&&a.y>=b.y&&a.x+a.w<=b.x+b.w&&a.y+a.h<=b.y+b.h;
            if(!inside&&overlaps(a,b)){fprintf(stderr,"Text/button intersection page %u, control %u, text %u/button %u\n",u.page,u.control,i,j);assert(false);}
        }
    }
}
extern "C" bool capability_preview(unsigned state,uint8_t *frame,size_t length)
{
    auto c=std::make_unique<HomeCatalog>();home_ui_t u;fixture(*c,u);
    if(state==0){} // Full device overview, six capabilities at once.
    else if(state==1)u.control_offset=6;
    else{
        const char *names[]={"Температура света","Режим климата","Без звука","Цвет HSV","Цвет RGB","Канал","Влажность","Заряд батареи"};
        unsigned mode=state-2;if(mode>=8)mode=0;
        if(mode==4)u.device=1;
        open(u,index(c->data,names[mode],u.device));
        if(state==8)tap(u,CAP_EDIT);
        if(state==10)c->data.online=false;
        if(state==11)c->data.pending=true;
        if(state==12){c->data.command_failed=true;c->data.failed_device=0;}
        if(state==13)u.dark_theme=true;
    }
    return home_ui_render(frame,length,&u);
}
extern "C" bool presentation_preview(unsigned state,uint8_t *frame,size_t length)
{
    auto c=std::make_unique<HomeCatalog>();auto *root=cJSON_Parse(file_text("tests/fixtures/yandex-device-ux.json").c_str());
    assert(root&&c->load(root));cJSON_Delete(root);auto &l=c->data;l.online=l.time_ready=l.authorized=true;
    home_ui_t u;home_ui_init(&u);home_ui_set_live(&u,&l);home_ui_open_menu(&u,0);
    unsigned slot=state==2||state==3||state==8?1:state==4?2:state==5?4:state==6?3:state==7?5:0;
    if(state){assert(point(u,home_ui_tile_x(slot)+20,home_ui_tile_y(slot)+20));}
    if(state==3)open(u,index(l,"Режим уборки",1));
    if(state==8)open(u,index(l,"Пауза уборки",1));
    if(state==9)tap(u,CAP_ALL);
    return home_ui_render(frame,length,&u);
}
static void presentation_tests()
{
    auto c=std::make_unique<HomeCatalog>();
    auto *cases=cJSON_Parse(file_text("tests/fixtures/yandex-device-profiles.json").c_str());assert(cJSON_GetArraySize(cases)==54);
    unsigned checked=0,frames=0;const cJSON *entry=nullptr;
    cJSON_ArrayForEach(entry,cases){
        auto *root=cJSON_Parse(file_text("tests/fixtures/yandex-profile-capabilities.json").c_str());assert(root);
        auto *dev=cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(root,"devices"),0);
        const char *type=cJSON_GetObjectItemCaseSensitive(entry,"type")->valuestring;
        cJSON_ReplaceItemInObject(dev,"type",cJSON_CreateString(type));assert(c->load(root));cJSON_Delete(root);
        auto &l=c->data;l.online=l.time_ready=l.authorized=true;
        const auto &d=l.devices[0];assert(d.control_count==31&&d.primary_control<l.control_count);
        const auto &primary=l.controls[d.primary_control];
        const char *role=cJSON_GetObjectItemCaseSensitive(entry,"primary")->valuestring;
        if(strcmp(primary.instance,role)){fprintf(stderr,"Wrong primary for %s: %s instead of %s\n",type,primary.instance,role);assert(false);}
        bool reading=!strcmp(cJSON_GetObjectItemCaseSensitive(entry,"kind")->valuestring,"reading");
        assert((primary.kind==HOME_CONTROL_READING)==reading);
        bool dashboard=cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(entry,"dashboard"));assert(d.dashboard==dashboard);
        home_ui_t u;home_ui_init(&u);home_ui_set_live(&u,&l);home_ui_open_menu(&u,0);
        assert(home_ui_target(&u,224,204)==LIVE_OPEN); // Settings icon.
        assert(point(u,44,132)&&u.command.kind==HOME_CMD_NONE); // Title/card.
        assert(u.page==(dashboard?HOME_PAGE_DEVICE:HOME_PAGE_CONTROL));
        if(!dashboard){
            assert(u.control==d.primary_control&&u.control_direct);render_checked(u);++frames;
            tap(u,BACK);assert(u.page==HOME_PAGE_HOME&&u.command.kind==HOME_CMD_NONE);
            assert(point(u,224,204)&&u.page==HOME_PAGE_CONTROL); // Icon and body share entry policy.
            tap(u,CAP_ALL);assert(u.page==HOME_PAGE_DEVICE);
        }
        for(u.control_offset=0;u.control_offset<d.control_count;u.control_offset+=6){render_checked(u);++frames;}
        u.page=HOME_PAGE_HOME;bool quick=home_ui_target(&u,100,204)==LIVE_TOGGLE;l.pending=true;
        if(quick){
            assert(home_ui_target(&u,100,204)==0); // Disabled foreground button never falls through to its card.
            assert(!point(u,100,204)&&u.command.kind==HOME_CMD_NONE);
        }else assert(home_ui_target(&u,100,204)==LIVE_OPEN); // Inspection remains available.
        assert(home_ui_target(&u,44,132)==LIVE_OPEN);
        // A drag within the same card cancels a click too.
        home_ui_presented(&u,true,u.revision,10000000);home_ui_touch(&u,1,44,132,0,10000001);
        home_ui_touch(&u,1,100,132,0,10000002);assert(!home_ui_touch(&u,0,-1,-1,0,10000003));
        ++checked;
    }
    cJSON_Delete(cases);
    // Realistic vacuum: pause means pause, not a second power switch.
    auto *root=cJSON_Parse(file_text("tests/fixtures/yandex-device-ux.json").c_str());assert(root&&c->load(root));cJSON_Delete(root);
    auto &l=c->data;l.online=l.time_ready=l.authorized=true;
    home_ui_t u;home_ui_init(&u);home_ui_set_live(&u,&l);home_ui_open_menu(&u,0);assert(point(u,300,132)&&u.page==HOME_PAGE_DEVICE);
    unsigned pause=index(l,"Пауза уборки",1),clean=index(l,"Уборка",1),mode=index(l,"Режим уборки",1);
    assert(!strcmp(l.controls[pause].on_label,"Пауза")&&!strcmp(l.controls[pause].off_label,"Продолжить"));
    assert(!strcmp(l.controls[clean].on_label,"Начать уборку")&&!strcmp(l.controls[clean].off_label,"Остановить"));
    tap(u,CAP_QUICK+pause);check_command(*c,u,"pause");assert(u.command.value==1&&!l.controls[pause].boolean);u.command.kind=HOME_CMD_NONE;
    tap(u,CAP_QUICK+clean);check_command(*c,u,"on");assert(u.command.value==1&&!l.controls[clean].boolean);u.command.kind=HOME_CMD_NONE;
    assert(!strcmp(l.options[l.controls[mode].option_start+5].name,"Влажная уборка"));
    assert(!strcmp(l.options[l.controls[mode].option_start+6].name,"Сухая уборка"));
    assert(!strcmp(l.options[l.controls[mode].option_start+7].name,"Смешанная уборка"));
    u.page=HOME_PAGE_HOME;assert(point(u,44,132)&&u.page==HOME_PAGE_CONTROL&&!strcmp(home_ui_selected_control(&u)->instance,"brightness"));
    tap(u,CAP_ALL);assert(u.page==HOME_PAGE_DEVICE);open(u,index(l,"Температура света"));tap(u,BACK);assert(u.page==HOME_PAGE_DEVICE);
    u.page=HOME_PAGE_HOME;assert(point(u,44,276)&&!strcmp(home_ui_selected_control(&u)->instance,"open"));
    assert(home_ui_control_preset(home_ui_selected_control(&u),0)==0&&home_ui_control_preset(home_ui_selected_control(&u),3)==100);
    // Unavailable primary function falls back to an actually advertised one.
    root=cJSON_Parse(file_text("tests/fixtures/yandex-device-ux.json").c_str());
    auto *dev=cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(root,"devices"),0);
    auto *caps=cJSON_GetObjectItemCaseSensitive(dev,"capabilities");cJSON_DeleteItemFromArray(caps,1);
    assert(c->load(root));cJSON_Delete(root);const auto &lamp=c->data.devices[0];
    assert(!strcmp(c->data.controls[lamp.primary_control].instance,"temperature_k"));
    root=cJSON_Parse(file_text("tests/fixtures/yandex-device-ux.json").c_str());
    dev=cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(root,"devices"),1);
    cJSON_ReplaceItemInObject(dev,"type",cJSON_CreateString("devices.types.washing_machine"));
    caps=cJSON_GetObjectItemCaseSensitive(dev,"capabilities");cJSON_DeleteItemFromArray(caps,0);cJSON_DeleteItemFromArray(caps,0);
    assert(c->load(root));cJSON_Delete(root);const auto &washer=c->data.devices[1];
    assert(!strcmp(c->data.controls[washer.primary_control].instance,"on")); // No program: power before pause.
    printf("PASS: %u documented device types; priority entry/fallback; card/gear navigation; disabled-action precedence; in-card drag rejection; vacuum commands; %u profile frames.\n",checked,frames);
}
int main()
{
    presentation_tests();
    auto c=std::make_unique<HomeCatalog>();home_ui_t u;fixture(*c,u);
    assert(c->data.devices[0].control_count==13&&c->data.control_count==20&&c->data.option_count==16);
    auto &l=c->data;
    assert(!strcmp(l.devices[4].state,"80 %"));
    const auto temperature=index(l,"Температура света"),mode=index(l,"Режим климата"),hsv=index(l,"Цвет HSV"),mute=index(l,"Без звука"),relative=index(l,"Канал"),humidity=index(l,"Влажность");
    assert(!l.controls[temperature].known&&l.controls[temperature].minimum==2700&&l.controls[temperature].maximum==6500);
    assert(l.controls[mode].selected_option==1&&l.controls[mode].option_count==8);
    assert(l.controls[relative].relative_only&&!l.controls[relative].has_minimum&&!l.controls[relative].has_maximum);
    assert(l.controls[hsv].known&&l.controls[hsv].value==210&&l.controls[hsv].second==35&&l.controls[hsv].third==70);
    std::string path,body;DeviceAction action;
    for(unsigned i=0;i<l.control_count;++i){const auto &v=l.controls[i];home_command_t cmd{};
        cmd.kind=HOME_CMD_CONTROL;cmd.epoch=l.epoch;cmd.slot=v.device;cmd.control=i;cmd.relative=v.relative_only;
        cmd.value=v.kind==HOME_CONTROL_TOGGLE?1:v.relative_only?-1:v.kind==HOME_CONTROL_RGB?0x123456:v.has_minimum?v.minimum:0;
        cmd.second=35;cmd.third=70;
        bool writable=v.kind!=HOME_CONTROL_READING&&v.kind!=HOME_CONTROL_UNSUPPORTED;
        assert(c->command(cmd,path,body,action)==writable);
        if(writable){auto cross=cmd;cross.slot=(v.device+1)%l.device_count;assert(!c->command(cross,path,body,action));}
    }
    open(u,temperature);tap(u,CAP_EDIT);input(u,"5100");assert(u.page==HOME_PAGE_CONTROL&&u.live_draft==5100);tap(u,APPLY);
    check_command(*c,u,"temperature_k");u.command.kind=HOME_CMD_NONE;
    open(u,mode);tap(u,CAP_OPTIONS_NEXT);tap(u,CAP_OPTION+7);assert(u.command.kind==HOME_CMD_NONE);tap(u,APPLY);
    check_command(*c,u,"thermostat");assert(c->command(u.command,path,body,action)&&!strcmp(action.text,"turbo"));u.command.kind=HOME_CMD_NONE;
    open(u,mute);tap(u,CAP_ON);assert(u.command.kind==HOME_CMD_NONE);tap(u,APPLY);check_command(*c,u,"mute");u.command.kind=HOME_CMD_NONE;
    open(u,humidity);tap(u,CAP_EDIT);input(u,"56");assert(u.page==HOME_PAGE_NUMBER&&u.number_error&&u.command.kind==HOME_CMD_NONE);
    input(u,"55");assert(u.page==HOME_PAGE_CONTROL);tap(u,APPLY);check_command(*c,u,"humidity");u.command.kind=HOME_CMD_NONE;
    tap(u,CAP_EDIT);tap(u,NUMBER_KEY+5);assert(!strcmp(u.number_text,"5"));tap(u,NUMBER_KEY+5);tap(u,NUMBER_KEY+15);assert(u.page==HOME_PAGE_CONTROL&&u.live_draft==55);
    open(u,relative);tap(u,MINUS);tap(u,MINUS);tap(u,APPLY);check_command(*c,u,"channel");assert(u.command.relative&&u.command.value==-2);u.command.kind=HOME_CMD_NONE;
    open(u,hsv);tap(u,CAP_COLOR_EDIT);input(u,"125");tap(u,APPLY);check_command(*c,u,"hsv");assert(u.command.value==125&&u.command.second==35&&u.command.third==70);u.command.kind=HOME_CMD_NONE;
    u.device=1;open(u,l.devices[1].control_start);tap(u,CAP_COLOR_EDIT);input(u,"255");tap(u,APPLY);check_command(*c,u,"rgb");assert(u.command.value==0xff3456);u.command.kind=HOME_CMD_NONE;
    u.device=2;open(u,l.devices[2].control_start);assert(has_action(u,CAP_PULSE)&&!has_action(u,APPLY));tap(u,CAP_PULSE);check_command(*c,u,"on");u.command.kind=HOME_CMD_NONE;
    u.device=3;open(u,l.devices[3].control_start);assert(has_action(u,CAP_ON)&&has_action(u,CAP_OFF)&&!has_action(u,CAP_PULSE)&&!has_action(u,APPLY));
    u.device=4;open(u,l.devices[4].control_start);assert(!has_action(u,APPLY)&&has_action(u,LIVE_REFRESH));
    auto invalid=u.command;invalid.kind=HOME_CMD_CONTROL;invalid.epoch=l.epoch;invalid.slot=0;invalid.control=mode;invalid.option=8;assert(!c->command(invalid,path,body,action));
    invalid.control=temperature;invalid.value=2700.5;invalid.relative=false;assert(!c->command(invalid,path,body,action));
    unsigned frames=0;
    for(unsigned i=0;i<l.control_count;++i)for(unsigned flags=0;flags<16;++flags){
        l.online=!(flags&1);l.pending=!!(flags&2);l.command_failed=!!(flags&4);l.failed_device=l.controls[i].device;
        u.device=l.controls[i].device;u.page=HOME_PAGE_DEVICE;l.online=true;l.pending=false;l.command_failed=false;open(u,i);
        l.online=!(flags&1);l.pending=!!(flags&2);l.command_failed=!!(flags&4);u.dark_theme=!!(flags&8);
        render_checked(u);++frames;
        assert(l.online&&!l.pending&&!l.command_failed||!has_action(u,APPLY));
        if(l.controls[i].kind==HOME_CONTROL_MODE&&l.controls[i].option_count>6){u.option_offset=6;render_checked(u);++frames;}
    }
    l.online=true;l.pending=l.command_failed=false;u.device=0;u.dark_theme=false;open(u,humidity);tap(u,CAP_EDIT);render_checked(u);++frames;
    input(u,"123");assert(u.number_error);render_checked(u);++frames;
    u.page=HOME_PAGE_DEVICE;
    for(u.control_offset=0;u.control_offset<l.devices[0].control_count;u.control_offset+=6){render_checked(u);++frames;}
    auto before=l.epoch;auto *json=cJSON_Parse(fixture_text().c_str());
    auto *caps=cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(json,"devices"),0),"capabilities");
    auto *modes=cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(caps,4),"parameters"),"modes");
    cJSON_DeleteItemFromArray(modes,7);assert(c->load(json)&&c->data.epoch!=before);cJSON_Delete(json);
    invalid.epoch=before;assert(!c->command(invalid,path,body,action));
    // Capacity limits are explicit and never expose an out-of-bounds target.
    json=cJSON_Parse(fixture_text().c_str());assert(json);
    auto *devices=cJSON_GetObjectItemCaseSensitive(json,"devices");
    while(cJSON_GetArraySize(devices)>1)cJSON_DeleteItemFromArray(devices,1);
    for(unsigned i=1;i<HOME_MAX_DEVICES;++i){
        auto *device=cJSON_Duplicate(cJSON_GetArrayItem(devices,0),true);assert(device);
        std::string id="device-"+std::to_string(i);cJSON_ReplaceItemInObject(device,"id",cJSON_CreateString(id.c_str()));cJSON_AddItemToArray(devices,device);
    }
    assert(c->load(json)&&c->data.truncated&&c->data.control_count==HOME_MAX_CONTROLS&&c->data.devices[HOME_MAX_DEVICES-1].controls_truncated);
    for(unsigned i=0;i<c->data.device_count;++i)assert(c->data.devices[i].control_start+c->data.devices[i].control_count<=HOME_MAX_CONTROLS);
    cJSON_Delete(json);
    json=cJSON_Parse(fixture_text().c_str());assert(json);
    devices=cJSON_GetObjectItemCaseSensitive(json,"devices");
    caps=cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(devices,0),"capabilities");
    modes=cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(caps,4),"parameters"),"modes");
    for(unsigned i=8;i<HOME_MAX_OPTIONS+8;++i){
        auto *option=cJSON_CreateObject();std::string value="custom-"+std::to_string(i);
        cJSON_AddStringToObject(option,"value",value.c_str());cJSON_AddItemToArray(modes,option);
    }
    assert(c->load(json)&&c->data.option_count==HOME_MAX_OPTIONS&&c->data.devices[0].controls_truncated);
    cJSON_Delete(json);
    printf("PASS: API JSON -> all capabilities/properties -> C UI -> validated action JSON; %u new screen states; model=%zu bytes.\n",frames,sizeof(home_live_data_t));
}
