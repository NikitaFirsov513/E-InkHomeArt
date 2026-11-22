#define _CRT_SECURE_NO_WARNINGS
#include "home_ui.h"
#include "../../components/home_ui/home_ui_layout.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

bool home_ui_test_preview(unsigned page,uint8_t *frame,size_t n)
{
    if(page==6)return home_ui_render_empty_art(frame,n);
    home_ui_t u;home_ui_init(&u);u.active=true;u.page=(home_ui_page_t)page;u.draft=70;
    return home_ui_render(frame,n,&u);
}
static void shown(home_ui_t *u,int64_t now)
{ home_ui_presented(u,u->active,u->revision,now); }
static bool tap(home_ui_t *u,int x,int y,int64_t now)
{
    home_ui_touch(u,1,x,y,0,now);
    return home_ui_touch(u,0,-1,-1,0,now+1000);
}
static void live_fixture(home_live_data_t *l)
{
    memset(l,0,sizeof(*l));l->epoch=7;l->online=l->time_ready=l->authorized=l->loaded=true;
    l->device_count=8;l->room_count=2;l->scene_count=2;
    strcpy(l->message,"Устройства обновлены");strcpy(l->ip,"192.168.1.25");
    strcpy(l->rooms[0].name,"Гостиная");strcpy(l->rooms[1].name,"Кухня");strcpy(l->scenes[0].name,"Я дома");strcpy(l->scenes[1].name,"Уйти из дома");
    for(unsigned i=0;i<HOME_MAX_DEVICES;++i){
        home_live_device_t *d=&l->devices[i];snprintf(d->name,sizeof(d->name),"Свет %u / очень длинное название",i+1);
        strcpy(d->state,"Выключено / 40");strcpy(d->range_name,"Яркость");d->available=d->has_toggle=d->has_range=true;
        d->minimum=1;d->maximum=100;d->step=1;d->value=40;d->room=i%2;
        strcpy(d->unit,"%");d->icon=i==2?HOME_ICON_HEAT:i==3?HOME_ICON_CURTAIN:HOME_ICON_LIGHT;
    }
    const char *names[]={"Свет · гостиная","Свет · кухня","Отопление","Шторы"};
    for(unsigned i=0;i<4;++i)snprintf(l->devices[i].name,sizeof(l->devices[i].name),"%s",names[i]);
}
bool home_ui_live_preview(unsigned page,uint8_t *frame,size_t n)
{
    home_ui_t u;home_ui_init(&u);static home_live_data_t l;live_fixture(&l);home_ui_set_live(&u,&l);u.active=true;
    u.page=(home_ui_page_t)page;u.live_draft=65;l.device_count=4;
    l.devices[0].on=true;l.devices[0].value=65;strcpy(l.devices[0].state,"Включён");
    strcpy(l.devices[1].state,"Выключен");strcpy(l.devices[2].state,"22 °C");strcpy(l.devices[3].state,"Открыты");
    l.devices[2].has_toggle=false;l.devices[3].has_toggle=false;
    l.devices[2].minimum=16;l.devices[2].maximum=30;l.devices[2].value=22;strcpy(l.devices[2].unit,"°C");
    strcpy(l.devices[2].range_name,"Температура");l.devices[3].value=100;
    if(page==HOME_PAGE_ACCOUNT){l.authorized=false;l.waiting=true;strcpy(l.code,"123-456");strcpy(l.url,"https://oauth.yandex.ru/device");strcpy(l.message,"Ожидаем подтверждение на телефоне");}
    return home_ui_render(frame,n,&u);
}
bool home_ui_login_preview(const char *url,uint8_t *frame,size_t n)
{
    home_ui_t u;home_ui_init(&u);static home_live_data_t l;live_fixture(&l);
    l.authorized=false;l.waiting=true;strcpy(l.code,"123-456");snprintf(l.url,sizeof(l.url),"%s",url);
    home_ui_set_live(&u,&l);u.active=true;u.page=HOME_PAGE_ACCOUNT;
    return home_ui_render(frame,n,&u);
}
/* Native bytes sent to the controller, not the logical preview bitmap. */
bool home_ui_login_theme_preview(const char *url,bool dark,uint8_t *frame,size_t n)
{
    home_ui_t u;home_ui_init(&u);static home_live_data_t l;live_fixture(&l);
    l.authorized=false;l.waiting=true;strcpy(l.code,"123-456");snprintf(l.url,sizeof(l.url),"%s",url);
    home_ui_set_live(&u,&l);u.active=true;u.page=HOME_PAGE_ACCOUNT;home_ui_set_dark_theme(&u,dark);
    return home_ui_render_panel(frame,n,&u);
}

/* Raster text measurements come from the actual bitmap renderer. Checking the
 * font rectangles also reserves whitespace, not just individual black pixels. */
static struct {int x,y,w,h;} texts[96];
static unsigned text_count;
static bool capture;
void home_ui_check_text(int x,int y,int w,int h)
{
    if(!capture||!w)return;
    assert(text_count<96);
    texts[text_count].x=x;texts[text_count].y=y;texts[text_count].w=w;texts[text_count].h=h;++text_count;
}
static bool overlap(int x,int y,int w,int h,int xx,int yy,int ww,int hh)
{ return x<xx+ww&&xx<x+w&&y<yy+hh&&yy<y+h; }
static bool action_tap(home_ui_t *u,unsigned action,int64_t now)
{
    home_ui_control_t cs[24];unsigned n=home_ui_controls(u,cs,24);
    for(unsigned i=0;i<n;++i)if(cs[i].action==action)
        return tap(u,cs[i].x+cs[i].w/2,cs[i].y+cs[i].h/2,now);
    assert(!"Requested action is not available");return false;
}
static void checked_frame(home_ui_t *u)
{
    static uint8_t frame[HOME_UI_BYTES+2];memset(frame,0xa5,sizeof(frame));
    home_ui_control_t cs[24];unsigned n=home_ui_controls(u,cs,24);
    for(unsigned i=0;i<n;++i){
        const home_ui_control_t *a=&cs[i];
        assert(a->x>=0&&a->y>=0&&a->x+a->w<=800&&a->y+a->h<=480&&a->w>=56&&a->h>=56);
        assert(home_ui_target(u,a->x+a->w/2,a->y+a->h/2)==a->action);
        for(unsigned j=0;j<i;++j)assert(!overlap(a->x,a->y,a->w,a->h,cs[j].x,cs[j].y,cs[j].w,cs[j].h));
    }
    text_count=0;capture=true;assert(home_ui_render(frame+1,HOME_UI_BYTES,u));capture=false;
    assert(frame[0]==0xa5&&frame[sizeof(frame)-1]==0xa5);
    for(unsigned i=0;i<text_count;++i){
        assert(texts[i].x>=0&&texts[i].y>=0&&texts[i].x+texts[i].w<=800&&texts[i].y+texts[i].h<=480);
        for(unsigned j=0;j<i;++j)if(overlap(texts[i].x,texts[i].y,texts[i].w,texts[i].h,texts[j].x,texts[j].y,texts[j].w,texts[j].h)){
            fprintf(stderr,"Text collision page=%u text=%u/%u\n",u->page,i,j);assert(false);
        }
        for(unsigned j=0;j<n;++j){
            const home_ui_control_t *c=&cs[j];
            bool inside=texts[i].x>=c->x&&texts[i].y>=c->y&&texts[i].x+texts[i].w<=c->x+c->w&&texts[i].y+texts[i].h<=c->y+c->h;
            if(!inside&&overlap(texts[i].x,texts[i].y,texts[i].w,texts[i].h,c->x,c->y,c->w,c->h)){
                fprintf(stderr,"Text/button collision page=%u text=%u button=%u\n",u->page,i,j);assert(false);
            }
        }
    }
}
bool home_ui_state_preview(unsigned state,uint8_t *frame,size_t n)
{
    static home_live_data_t l;live_fixture(&l);l.device_count=8;
    home_ui_t u;home_ui_init(&u);home_ui_set_live(&u,&l);u.active=true;u.page=HOME_PAGE_DEVICE;u.live_draft=75;
    l.devices[0].on=true;l.devices[0].value=65;strcpy(l.devices[0].name,"Свет в гостиной");strcpy(l.devices[0].state,"65 %");
    if(state==0)u.page=HOME_PAGE_HOME;
    if(state==1)l.pending=true;
    if(state==2)l.online=false;
    if(state==3){l.command_failed=true;l.failed_device=0;strcpy(l.message,"Нет подтверждения команды. Проверьте устройство");}
    if(state==4)strcpy(l.devices[0].name,"Потолочный свет над обеденным столом");
    if(state==5)l.devices[0].has_range=false;
    if(state==6)l.devices[0].has_toggle=false;
    if(state==7)l.devices[0].has_toggle=l.devices[0].has_range=false;
    if(state==8){u.page=HOME_PAGE_HOME;strcpy(l.devices[0].name,"Потолочный свет над обеденным столом");}
    if(state==9)u.page=HOME_PAGE_SCENES;
    if(state==10)u.page=HOME_PAGE_ROOMS;
    if(state==11){u.page=HOME_PAGE_HOME;u.offset=4;}
    if(state==12)u.dark_theme=true;
    if(state==13){u.page=HOME_PAGE_HOME;l.device_count=0;}
    return home_ui_render(frame,n,&u);
}
static void geometry_tests(void)
{
    home_ui_t u;static home_live_data_t l;unsigned frames=0;
    for(unsigned page=0;page<8;++page)for(unsigned flags=0;flags<64;++flags)for(unsigned offset=0;offset<2;++offset){
        live_fixture(&l);l.online=!!(flags&1);l.time_ready=!!(flags&2);l.pending=!!(flags&4);
        l.authorized=!!(flags&8);l.waiting=!(flags&8);l.command_failed=!!(flags&16);l.failed_device=0;
        strcpy(l.code,"123-456");strcpy(l.url,"https://oauth.yandex.ru/device");
        strcpy(l.devices[0].name,"Потолочный свет над обеденным столом");
        strcpy(l.rooms[0].name,"Гостиная с очень длинным названием");
        strcpy(l.message,"Очень длинный статус ответа сервера, который не должен перекрывать кнопки");
        home_ui_init(&u);home_ui_set_live(&u,&l);u.page=(home_ui_page_t)page;u.offset=offset*4;u.live_draft=75;u.dark_theme=!!(flags&32);
        checked_frame(&u);++frames;
    }
    for(unsigned kind=0;kind<4;++kind){
        live_fixture(&l);l.devices[0].has_toggle=!!(kind&1);l.devices[0].has_range=!!(kind&2);
        home_ui_init(&u);home_ui_set_live(&u,&l);u.page=HOME_PAGE_DEVICE;checked_frame(&u);++frames;
    }
    live_fixture(&l);l.device_count=HOME_MAX_DEVICES;l.scene_count=HOME_MAX_SCENES;l.room_count=HOME_MAX_ROOMS;
    for(unsigned i=0;i<HOME_MAX_ROOMS;++i)snprintf(l.rooms[i].name,sizeof(l.rooms[i].name),"Room %u",i);
    for(unsigned i=0;i<HOME_MAX_SCENES;++i)snprintf(l.scenes[i].name,sizeof(l.scenes[i].name),"Scene %u",i);
    home_ui_init(&u);home_ui_set_live(&u,&l);
    for(unsigned page=0;page<4;++page){
        u.page=(home_ui_page_t)page;
        for(u.offset=0;u.offset<home_ui_tile_count(&u);u.offset+=HOME_UI_PAGE_SIZE){checked_frame(&u);++frames;}
    }
    for(unsigned page=0;page<8;++page){
        home_ui_init(&u);u.page=(home_ui_page_t)page;checked_frame(&u);++frames;
    }
    printf("PASS: %u actual C frames; no intersecting buttons/text; targets >=56px; frame guards intact.\n",frames);
}
static void live_tests(void)
{
    home_ui_t u;home_ui_init(&u);static home_live_data_t l;live_fixture(&l);home_ui_set_live(&u,&l);home_ui_open_menu(&u,0);shown(&u,0);
    /* Row labels/background are not secondary hidden buttons. */
    assert(home_ui_target(&u,100,130)==0&&home_ui_target(&u,410,130)==0);
    assert(action_tap(&u,LIVE_TOGGLE,1000));assert(u.command.kind==HOME_CMD_TOGGLE&&u.command.epoch==7&&u.command.slot==0&&u.command.value==1);
    assert(!l.devices[0].on);u.command.kind=HOME_CMD_NONE;shown(&u,2000);
    unsigned seen_d[HOME_MAX_DEVICES]={0},seen_s[HOME_MAX_SCENES]={0};
    for(u.offset=0;u.offset<home_ui_tile_count(&u);u.offset+=HOME_UI_PAGE_SIZE){
        home_ui_tile_t tiles[6];unsigned n=home_ui_tiles(&u,tiles);assert(n<=4);
        for(unsigned i=0;i<n;++i){assert(tiles[i].kind==HOME_TILE_DEVICE);++seen_d[tiles[i].slot];}
    }
    for(unsigned i=0;i<l.device_count;++i)assert(seen_d[i]==1);
    u.offset=0;assert(action_tap(&u,NAV_SCENES,2100));shown(&u,2200);
    home_ui_tile_t tiles[6];unsigned n=home_ui_tiles(&u,tiles);
    for(unsigned i=0;i<n;++i){assert(tiles[i].kind==HOME_TILE_SCENE);++seen_s[tiles[i].slot];}
    for(unsigned i=0;i<l.scene_count;++i)assert(seen_s[i]==1);
    assert(action_tap(&u,LIVE_SCENE,2300)&&u.command.kind==HOME_CMD_SCENE&&u.command.slot==0);
    u.command.kind=HOME_CMD_NONE;shown(&u,2400);assert(action_tap(&u,NAV_HOME,2500));shown(&u,2600);
    assert(action_tap(&u,LIVE_NEXT,3000)&&u.offset==4);shown(&u,4000);
    assert(action_tap(&u,LIVE_OPEN+4,5000)&&u.page==HOME_PAGE_DEVICE&&u.device==4);shown(&u,6000);
    assert(action_tap(&u,PLUS,7000)&&u.live_draft==41&&l.devices[4].value==40);shown(&u,8000);
    assert(action_tap(&u,APPLY,9000)&&u.command.kind==HOME_CMD_RANGE&&u.command.slot==4&&u.command.value==41);
    u.command.kind=HOME_CMD_NONE;shown(&u,10000);
    assert(action_tap(&u,LIVE_PRESET+2,10200)&&u.live_draft==75&&u.command.kind==HOME_CMD_NONE&&l.devices[4].value==40);shown(&u,10500);
    assert(action_tap(&u,APPLY,10600)&&u.command.kind==HOME_CMD_RANGE&&u.command.value==75);u.command.kind=HOME_CMD_NONE;shown(&u,10800);
    assert(action_tap(&u,BACK,10900)&&u.page==HOME_PAGE_HOME&&u.offset==4);shown(&u,11000);
    assert(action_tap(&u,LIVE_OPEN+4,11100)&&u.device==4);shown(&u,11200);
    for(unsigned state=0;state<2;++state){
        l.pending=state==0;l.online=state!=1;home_ui_set_live(&u,&l);shown(&u,12000);
        home_ui_control_t controls[24];unsigned count=home_ui_controls(&u,controls,24);
        for(unsigned i=0;i<count;++i)assert(controls[i].action==BACK||controls[i].action==0);
        assert(!tap(&u,400,440,13000)&&u.command.kind==HOME_CMD_NONE);
    }
    l.online=true;l.pending=false;l.command_failed=true;l.failed_device=4;home_ui_set_live(&u,&l);shown(&u,14000);
    assert(action_tap(&u,LIVE_REFRESH,15000)&&u.command.kind==HOME_CMD_REFRESH);u.command.kind=HOME_CMD_NONE;
    l.loaded=false;l.devices[4].available=false;home_ui_set_live(&u,&l);shown(&u,15100);
    assert(action_tap(&u,LIVE_REFRESH,15200)&&u.command.kind==HOME_CMD_REFRESH);u.command.kind=HOME_CMD_NONE;
    l.loaded=true;l.devices[4].available=true;
    /* Other devices do not inherit this failed device's recovery action. */
    u.device=0;home_ui_control_t controls[24];unsigned count=home_ui_controls(&u,controls,24);
    for(unsigned i=0;i<count;++i)assert(controls[i].action!=LIVE_REFRESH);
    home_ui_init(&u);memset(&l,0,sizeof(l));home_ui_set_live(&u,&l);home_ui_open_menu(&u,0);shown(&u,0);
    assert(action_tap(&u,LIVE_ACCOUNT,1000));shown(&u,2000);
    assert(action_tap(&u,LIVE_LOGIN,3000)&&u.command.kind==HOME_CMD_LOGIN);u.command.kind=HOME_CMD_NONE;shown(&u,4000);
    assert(action_tap(&u,BACK,4100)&&u.page==HOME_PAGE_SETTINGS);shown(&u,4500);
    assert(action_tap(&u,LIVE_THEME,5000)&&u.dark_theme&&u.command.kind==HOME_CMD_NONE);shown(&u,6000);
    assert(action_tap(&u,LIVE_THEME,7000)&&!u.dark_theme);shown(&u,8000);
    assert(action_tap(&u,LIVE_NETWORK,8100));shown(&u,8300);
    assert(action_tap(&u,BACK,8400)&&u.page==HOME_PAGE_SETTINGS);shown(&u,8600);
    assert(action_tap(&u,LIVE_ACCOUNT,8700));shown(&u,8900);
    assert(action_tap(&u,BACK,9000));shown(&u,9200);
    static uint8_t guarded[HOME_UI_BYTES+2];memset(guarded,0xa5,sizeof(guarded));
    assert(home_ui_render_panel(guarded+1,HOME_UI_BYTES,&u)&&guarded[1]==0xff);
    home_ui_set_dark_theme(&u,true);assert(home_ui_render_panel(guarded+1,HOME_UI_BYTES,&u)&&guarded[1]==0);
    assert(home_ui_render_empty_art_panel(guarded+1,HOME_UI_BYTES)&&guarded[1]==0xff);
    for(unsigned dark=0;dark<2;++dark){
        assert(home_ui_login_theme_preview("https://ya.ru/device",dark,guarded+1,HOME_UI_BYTES));
        assert(guarded[1]==(dark?0:0xff));unsigned black=0;
        for(unsigned y=128;y<368;++y)for(unsigned x=532;x<772;++x){
            bool white=!!(guarded[1+y*100+x/8]&(128U>>(x&7)));
            if(x<544||x>=760||y<140||y>=356)assert(white);
            if(!white)++black;
        }
        assert(black>1000&&guarded[0]==0xa5&&guarded[sizeof(guarded)-1]==0xa5);
    }
    assert(home_ui_login_preview("",guarded+1,HOME_UI_BYTES));
    char long_url[256];memset(long_url,'a',sizeof(long_url));long_url[255]=0;
    assert(home_ui_login_preview(long_url,guarded+1,HOME_UI_BYTES));
    puts("PASS: four-row device/scenario pagination, actual slots, draft/apply, pending/offline gating, explicit failure recovery GET, theme and QR quiet zone.");
}
int main(void)
{
    live_tests();geometry_tests();
    home_ui_t u;home_ui_init(&u);
    assert(!u.active&&u.light_on[0]);
    assert(home_ui_touch(&u,1,600,130,0,1000)&&u.active&&u.light_on[0]);
    shown(&u,2000);home_ui_touch(&u,1,600,130,0,3000);
    assert(!home_ui_touch(&u,0,-1,-1,0,4000)&&u.light_on[0]);
    assert(tap(&u,600,130,5000)&&!u.light_on[0]);
    assert(!tap(&u,600,130,6000)&&!u.light_on[0]);
    shown(&u,7000);assert(tap(&u,600,130,8000)&&u.light_on[0]);shown(&u,9000);
    home_ui_touch(&u,1,600,130,0,10000);home_ui_touch(&u,1,790,100,0,11000);
    assert(!home_ui_touch(&u,0,-1,-1,0,12000)&&u.light_on[0]);
    home_ui_touch(&u,1,600,130,0,13000);home_ui_touch(&u,2,600,130,0,14000);
    assert(!home_ui_touch(&u,0,-1,-1,0,15000)&&u.light_on[0]);
    home_ui_touch(&u,1,600,130,0,16000);home_ui_touch(&u,1,600,130,1,17000);
    assert(!home_ui_touch(&u,0,-1,-1,1,18000)&&u.light_on[0]);
    home_ui_touch(&u,1,600,130,0,19000);home_ui_cancel_touch(&u);
    assert(!home_ui_touch(&u,0,-1,-1,0,20000)&&u.light_on[0]);
    assert(action_tap(&u,NAV_ROOMS,21000));shown(&u,22000);
    assert(action_tap(&u,ROOM,23000));shown(&u,24000);
    assert(action_tap(&u,OPEN_DEVICE,25000));shown(&u,26000);
    assert(action_tap(&u,PLUS,27000)&&u.draft==80&&u.brightness[0]==70);shown(&u,28000);
    assert(action_tap(&u,APPLY,29000)&&u.brightness[0]==80);shown(&u,30000);
    assert(action_tap(&u,BACK,30500));shown(&u,30700);
    assert(action_tap(&u,ART,31000)&&!u.active);shown(&u,32000);
    assert(home_ui_touch(&u,1,700,450,0,33000)&&u.active&&u.page==HOME_PAGE_HOME);
    shown(&u,34000);assert(!home_ui_touch(&u,0,-1,-1,0,35000)&&u.active);shown(&u,40000);
    assert(!home_ui_tick(&u,40000+HOME_UI_IDLE_US-1));assert(home_ui_tick(&u,40000+HOME_UI_IDLE_US)&&!u.active);
    home_ui_open_menu(&u,50000);shown(&u,50000);home_ui_touch(&u,1,600,130,0,60000);
    assert(!home_ui_tick(&u,60000+HOME_UI_IDLE_US));
    static uint8_t before[HOME_UI_BYTES],after[HOME_UI_BYTES];home_ui_region_t r;
    assert(!home_ui_changed_region(before,after,HOME_UI_BYTES,&r));after[HOME_UI_BYTES-1]=1;
    assert(home_ui_changed_region(before,after,HOME_UI_BYTES,&r)&&r.x==792&&r.y==479&&r.width==8&&r.height==1);
    for(unsigned m=0;m<8;++m)for(unsigned corner=0;corner<4;++corner){
        int x,y;assert(home_ui_map_touch(corner&1?799:0,corner&2?479:0,800,480,m,&x,&y));assert(x>=0&&x<800&&y>=0&&y<480);
    }
    puts("PASS: wake swallowing, presented-revision gating, drag/multitouch rejection, navigation, 45s idle and dirty-region edges.");return 0;
}
