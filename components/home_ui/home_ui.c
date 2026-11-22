#include "home_ui.h"
#include <string.h>

enum { NAV_HOME=1,NAV_ROOMS,NAV_SCENES,ART,SETTINGS,BACK,
       OPEN_DEVICE=10,TOGGLE_DEVICE=20,SCENE=30,ROOM=40,MINUS=50,PLUS,APPLY };

void home_ui_init(home_ui_t *u)
{
    if(!u)return;
    *u=(home_ui_t){.page=HOME_PAGE_HOME,.revision=1,.light_on={true,false},.heat_on=true,
                   .brightness={70,100},.temperature=22,.curtains=100};
}

static void changed(home_ui_t *u)
{
    ++u->revision;
    if(!u->revision)++u->revision;
    u->pressed=false;
}

void home_ui_set_dark_theme(home_ui_t *u,bool dark)
{
    if(u&&u->dark_theme!=dark){u->dark_theme=dark;changed(u);}
}

void home_ui_show_art(home_ui_t *u)
{
    if(!u)return;
    u->active=false;
    u->require_release=u->contact;
    changed(u);
}

void home_ui_open_menu(home_ui_t *u,int64_t now)
{
    if(!u)return;
    u->active=true;u->page=HOME_PAGE_HOME;u->require_release=u->contact;
    u->last_activity_us=now;changed(u);
}

void home_ui_presented(home_ui_t *u,bool menu,uint32_t revision,int64_t now)
{
    if(!u)return;
    u->visible=menu;
    u->shown_revision=revision;
    /* Start the inactivity period from the actual image, not the request. */
    if(menu&&u->active&&revision==u->revision)u->last_activity_us=now;
}

static void open_device(home_ui_t *u,unsigned device)
{
    if(device>=4)return;
    u->return_page=(uint8_t)u->page;
    u->page=HOME_PAGE_DEVICE;u->device=(uint8_t)device;
    u->draft=device<2?u->brightness[device]:device==2?u->temperature:u->curtains;
}

enum { LIVE_OPEN=100,LIVE_TOGGLE=140,LIVE_ROOM=180,LIVE_SCENE=200,
       LIVE_PREV=230,LIVE_NEXT,LIVE_LOGIN,LIVE_CANCEL,LIVE_LOGOUT,LIVE_REFRESH,LIVE_THEME,LIVE_ACCOUNT,LIVE_NETWORK,
       LIVE_ON=240,LIVE_OFF,LIVE_PRESET=242 };

void home_ui_set_live(home_ui_t *u,const home_live_data_t *data)
{
    if(!u)return;
    bool reset=!u->live||!data||u->live->epoch!=data->epoch;
    u->live=data;
    if(reset){u->page=HOME_PAGE_HOME;u->offset=0;u->device=0;u->room=0;u->command.kind=HOME_CMD_NONE;}
    changed(u);
}
unsigned home_ui_tile_count(const home_ui_t *u)
{
    if(!u||!u->live)return 0;
    if(u->page==HOME_PAGE_HOME)return u->live->device_count+u->live->scene_count;
    if(u->page==HOME_PAGE_ROOMS)return u->live->room_count;
    if(u->page==HOME_PAGE_SCENES)return u->live->scene_count;
    unsigned n=0;
    for(unsigned i=0;i<u->live->device_count;++i)
        if(u->page!=HOME_PAGE_ROOM||u->live->devices[i].room==u->room)++n;
    return n;
}
unsigned home_ui_tiles(const home_ui_t *u,home_ui_tile_t out[6])
{
    if(!u||!u->live||!out)return 0;
    const home_live_data_t *l=u->live;unsigned n=0,skip=u->offset;
    if(u->page==HOME_PAGE_HOME){
        /* First page follows mock A: four devices, two real scenarios.
         * Remaining entries keep their native slots on subsequent pages. */
        unsigned d=l->device_count<4?l->device_count:4,s=l->scene_count<2?l->scene_count:2;
        for(unsigned i=0;i<l->device_count+l->scene_count;++i){
            home_ui_tile_t tile;
            if(i<d)tile=(home_ui_tile_t){HOME_TILE_DEVICE,i};
            else if(i<d+s)tile=(home_ui_tile_t){HOME_TILE_SCENE,i-d};
            else if(i<l->device_count+s)tile=(home_ui_tile_t){HOME_TILE_DEVICE,i-s};
            else tile=(home_ui_tile_t){HOME_TILE_SCENE,i-l->device_count};
            if(skip){--skip;continue;}if(n<6)out[n++]=tile;
        }
    }else if(u->page==HOME_PAGE_ROOMS||u->page==HOME_PAGE_SCENES){
        unsigned count=u->page==HOME_PAGE_ROOMS?l->room_count:l->scene_count;
        for(unsigned i=0;i<count;++i){if(skip){--skip;continue;}if(n<6)out[n++]=(home_ui_tile_t){u->page==HOME_PAGE_ROOMS?HOME_TILE_ROOM:HOME_TILE_SCENE,i};}
    }else for(unsigned i=0;i<l->device_count;++i){
        if(u->page==HOME_PAGE_ROOM&&l->devices[i].room!=u->room)continue;
        if(skip){--skip;continue;}if(n<6)out[n++]=(home_ui_tile_t){HOME_TILE_DEVICE,i};
    }
    return n;
}
unsigned home_live_slots(const home_ui_t *u,unsigned slots[6])
{
    home_ui_tile_t tiles[6];unsigned total=home_ui_tiles(u,tiles),n=0;
    for(unsigned i=0;i<total;++i)if(tiles[i].kind==HOME_TILE_DEVICE)slots[n++]=tiles[i].slot;
    return n;
}
double home_ui_preset_value(const home_live_device_t *d,unsigned preset)
{
    if(!d||!d->has_range||d->step<=0||preset>3)return 0;
    double ratio=(preset+1)*0.25;
    double target=!strcmp(d->unit,"%")?d->maximum*ratio:d->minimum+(d->maximum-d->minimum)*ratio;
    if(target<d->minimum)target=d->minimum;
    unsigned steps=(unsigned)((target-d->minimum)/d->step+0.5);
    unsigned max_steps=(unsigned)((d->maximum-d->minimum)/d->step+0.0000001);
    if(steps>max_steps)steps=max_steps;
    return d->minimum+steps*d->step;
}
static void live_action(home_ui_t *u,unsigned a)
{
    const home_live_data_t *l=u->live;
    home_command_t c={.epoch=l->epoch};
    if(a==NAV_HOME||a==NAV_ROOMS||a==NAV_SCENES||a==SETTINGS){
        u->page=a==NAV_HOME?HOME_PAGE_HOME:a==NAV_ROOMS?HOME_PAGE_ROOMS:a==NAV_SCENES?HOME_PAGE_SCENES:HOME_PAGE_SETTINGS;u->offset=0;
    }else if(a==ART){home_ui_show_art(u);return;}
    else if(a==BACK){
        bool from_device=u->page==HOME_PAGE_DEVICE;
        u->page=from_device?(home_ui_page_t)u->return_page:u->page==HOME_PAGE_ROOM?HOME_PAGE_ROOMS:
            (u->page==HOME_PAGE_ACCOUNT||u->page==HOME_PAGE_NETWORK)?HOME_PAGE_SETTINGS:HOME_PAGE_HOME;
        u->offset=from_device?u->return_offset:0;
    }
    else if(a==LIVE_PREV)u->offset=u->offset>=6?u->offset-6:0;
    else if(a==LIVE_NEXT&&u->offset+6<home_ui_tile_count(u))u->offset+=6;
    else if(a>=LIVE_OPEN&&a<LIVE_OPEN+HOME_MAX_DEVICES){
        unsigned d=a-LIVE_OPEN;if(d>=l->device_count)return;
        u->return_page=u->page;u->return_offset=u->offset;u->page=HOME_PAGE_DEVICE;u->device=d;u->live_draft=l->devices[d].value;
    }else if(a>=LIVE_ROOM&&a<LIVE_ROOM+HOME_MAX_ROOMS){u->room=a-LIVE_ROOM;u->page=HOME_PAGE_ROOM;u->offset=0;}
    else if(a==LIVE_ACCOUNT)u->page=HOME_PAGE_ACCOUNT;
    else if(a==LIVE_NETWORK)u->page=HOME_PAGE_NETWORK;
    else if(a==LIVE_LOGIN)c.kind=HOME_CMD_LOGIN;
    else if(a==LIVE_CANCEL)c.kind=HOME_CMD_CANCEL;
    else if(a==LIVE_LOGOUT)c.kind=HOME_CMD_LOGOUT;
    else if(a==LIVE_REFRESH)c.kind=HOME_CMD_REFRESH;
    else if(a==LIVE_THEME)u->dark_theme=!u->dark_theme;
    else if((a==LIVE_ON||a==LIVE_OFF)&&u->page==HOME_PAGE_DEVICE){c.kind=HOME_CMD_TOGGLE;c.slot=u->device;c.value=a==LIVE_ON;}
    else if(a>=LIVE_PRESET&&a<LIVE_PRESET+4&&u->page==HOME_PAGE_DEVICE&&u->device<l->device_count)
        u->live_draft=home_ui_preset_value(&l->devices[u->device],a-LIVE_PRESET);
    else if(a>=LIVE_SCENE&&a<LIVE_SCENE+HOME_MAX_SCENES){c.kind=HOME_CMD_SCENE;c.slot=a-LIVE_SCENE;}
    else if(a>=LIVE_TOGGLE&&a<LIVE_TOGGLE+HOME_MAX_DEVICES){
        c.kind=HOME_CMD_TOGGLE;c.slot=a-LIVE_TOGGLE;
        if(c.slot>=l->device_count)return;
        c.value=!l->devices[c.slot].on;
    }else if(u->page==HOME_PAGE_DEVICE&&u->device<l->device_count){
        const home_live_device_t *d=&l->devices[u->device];
        if(a==MINUS||a==PLUS){
            double v=u->live_draft+(a==PLUS?d->step:-d->step);
            u->live_draft=v<d->minimum?d->minimum:v>d->maximum?d->maximum:v;
        }else if(a==APPLY){c.kind=HOME_CMD_RANGE;c.slot=u->device;c.value=u->live_draft;}
        else return;
    }else return;
    if(c.kind!=HOME_CMD_NONE){
        if(l->pending)return;
        u->command=c;
    }
    changed(u);
}
static unsigned live_controls(const home_ui_t *u,home_ui_control_t *out,unsigned cap);
static void apply_action(home_ui_t *u,unsigned action)
{
    if(u->live){live_action(u,action);return;}
    if(action==NAV_HOME)u->page=HOME_PAGE_HOME;
    else if(action==NAV_ROOMS)u->page=HOME_PAGE_ROOMS;
    else if(action==NAV_SCENES)u->page=HOME_PAGE_SCENES;
    else if(action==SETTINGS)u->page=HOME_PAGE_SETTINGS;
    else if(action==ART){home_ui_show_art(u);return;}
    else if(action==BACK)u->page=u->page==HOME_PAGE_DEVICE?(home_ui_page_t)u->return_page:
                                 u->page==HOME_PAGE_ROOM?HOME_PAGE_ROOMS:HOME_PAGE_HOME;
    else if(action>=OPEN_DEVICE&&action<OPEN_DEVICE+4)open_device(u,action-OPEN_DEVICE);
    else if(action>=TOGGLE_DEVICE&&action<TOGGLE_DEVICE+4){
        unsigned d=action-TOGGLE_DEVICE;
        if(d<2)u->light_on[d]=!u->light_on[d];
        else if(d==2)u->heat_on=!u->heat_on;
        else u->curtains=u->curtains==100?0:100;
    }else if(action>=ROOM&&action<ROOM+3){u->room=(uint8_t)(action-ROOM);u->page=HOME_PAGE_ROOM;}
    else if(action==SCENE||action==SCENE+1){
        bool home=action==SCENE;
        u->light_on[0]=home;u->light_on[1]=false;u->heat_on=home;u->curtains=home?100:0;
        u->last_scene=home?1:2;
    }else if(u->page==HOME_PAGE_DEVICE&&(action==MINUS||action==PLUS)){
        int step=u->device<2?10:u->device==2?1:25;
        int value=(int)u->draft+(action==PLUS?step:-step);
        int lo=u->device==2?16:0,hi=u->device==2?30:100;
        u->draft=(uint8_t)(value<lo?lo:value>hi?hi:value);
    }else if(u->page==HOME_PAGE_DEVICE&&action==APPLY){
        if(u->device<2){u->brightness[u->device]=u->draft;u->light_on[u->device]=u->draft>0;}
        else if(u->device==2){u->temperature=u->draft;u->heat_on=true;}
        else u->curtains=u->draft;
    }else return;
    changed(u);
}

bool home_ui_touch(home_ui_t *u,unsigned count,int x,int y,unsigned id,int64_t now)
{
    if(!u)return false;
    bool had_contact=u->contact;
    u->contact=count>0;
    if(count||had_contact)u->last_activity_us=now;
    bool valid=x>=0&&y>=0&&x<(int)HOME_UI_WIDTH&&y<(int)HOME_UI_HEIGHT;
    if(!u->active){
        if(count&&!u->require_release&&valid){
            home_ui_open_menu(u,now);u->require_release=true;return true;
        }
        if(!count)u->require_release=false;
        return false;
    }
    if(u->require_release||!u->visible||u->shown_revision!=u->revision){
        u->pressed=false;
        u->require_release=count>0;
        return false;
    }
    if(count>1||(count&&!valid)){
        u->pressed=false;u->require_release=true;return false;
    }
    if(count==1){
        unsigned action=home_ui_target(u,x,y);
        if(!had_contact){u->pressed=action!=0;u->pressed_action=action;u->pointer_id=(uint8_t)id;}
        else if(u->pointer_id!=id||action!=u->pressed_action)u->pressed=false;
        return false;
    }
    if(had_contact&&u->pressed){
        unsigned action=u->pressed_action;
        u->pressed=false;
        apply_action(u,action);return true;
    }
    return false;
}

void home_ui_cancel_touch(home_ui_t *u)
{ if(u){u->pressed=false;u->contact=false;u->require_release=true;} }

bool home_ui_tick(home_ui_t *u,int64_t now)
{
    if(!u||!u->active||u->contact||now-u->last_activity_us<HOME_UI_IDLE_US)return false;
    home_ui_show_art(u);return true;
}

static void add(home_ui_control_t *out,unsigned capacity,unsigned *n,unsigned id,
                int x,int y,int w,int h,const char *label,bool filled)
{
    if(*n<capacity)out[(*n)++]=(home_ui_control_t){.action=id,.x=x,.y=y,.w=w,.h=h,.label=label,.filled=filled};
}

static void device_controls(const home_ui_t *u,unsigned d,int x,int y,
                             home_ui_control_t *out,unsigned cap,unsigned *n)
{
    add(out,cap,n,OPEN_DEVICE+d,x,y,240,138,NULL,false);
    const char *label=d<2?(u->light_on[d]?"Выключить":"Включить"):
                      d==2?"Настроить":u->curtains==100?"Закрыть":"Открыть";
    add(out,cap,n,d==2?OPEN_DEVICE+d:TOGGLE_DEVICE+d,x+12,y+82,216,44,label,
        d<2&&u->light_on[d]);
}

unsigned home_ui_controls(const home_ui_t *u,home_ui_control_t *out,unsigned cap)
{
    if(!u||!out)return 0;
    if(u->live)return live_controls(u,out,cap);
    if(u->room>2||u->device>3)return 0;
    unsigned n=0;
    const char *tabs[]={"Главная","Комнаты","Сценарии","Картина"};
    const unsigned actions[]={NAV_HOME,NAV_ROOMS,NAV_SCENES,ART};
    for(unsigned i=0;i<4;++i)add(out,cap,&n,actions[i],12+198*(int)i,428,182,44,tabs[i],
        (i==0&&u->page==HOME_PAGE_HOME)||(i==1&&(u->page==HOME_PAGE_ROOMS||u->page==HOME_PAGE_ROOM))||
        (i==2&&u->page==HOME_PAGE_SCENES));
    bool back=u->page==HOME_PAGE_DEVICE||u->page==HOME_PAGE_ROOM||u->page==HOME_PAGE_SETTINGS;
    add(out,cap,&n,back?BACK:SETTINGS,626,14,150,44,back?"Назад":"Настройки",false);
    if(u->page==HOME_PAGE_HOME){
        for(unsigned d=0;d<4;++d)device_controls(u,d,24+256*(int)(d%3),104+150*(int)(d/3),out,cap,&n);
        add(out,cap,&n,SCENE,292,336,216,44,"Запустить",false);
        add(out,cap,&n,SCENE+1,548,336,216,44,"Запустить",false);
    }else if(u->page==HOME_PAGE_ROOMS){
        const char *names[]={"Гостиная","Кухня","Спальня"};
        for(unsigned i=0;i<3;++i)add(out,cap,&n,ROOM+i,24+256*(int)i,116,240,94,names[i],false);
    }else if(u->page==HOME_PAGE_ROOM){
        static const unsigned devices[3][2]={{0,3},{1,4},{2,4}};
        for(unsigned i=0;i<2;++i)if(devices[u->room][i]<4)
            device_controls(u,devices[u->room][i],24+256*(int)i,116,out,cap,&n);
    }else if(u->page==HOME_PAGE_SCENES){
        add(out,cap,&n,SCENE,36,246,344,52,"Я дома",u->last_scene==1);
        add(out,cap,&n,SCENE+1,420,246,344,52,"Уйти из дома",u->last_scene==2);
    }else if(u->page==HOME_PAGE_DEVICE){
        add(out,cap,&n,TOGGLE_DEVICE+u->device,432,126,320,52,
            u->device<2?(u->light_on[u->device]?"Выключить":"Включить"):
            u->device==2?(u->heat_on?"Выключить":"Включить"):
            u->curtains==100?"Закрыть":"Открыть",false);
        add(out,cap,&n,MINUS,48,284,100,64,"-",false);
        add(out,cap,&n,PLUS,332,284,100,64,"+",false);
        add(out,cap,&n,APPLY,464,284,288,64,"Применить",true);
    }
    return n;
}

static unsigned live_controls(const home_ui_t *u,home_ui_control_t *out,unsigned cap)
{
    const home_live_data_t *l=u->live;unsigned n=0;
    const char *tabs[]={"Главная","Комнаты","Сценарии","Картина"};
    for(unsigned i=0;i<4;++i)add(out,cap,&n,i+1,12+198*(int)i,428,182,44,tabs[i],
        (i==0&&u->page==HOME_PAGE_HOME)||(i==1&&(u->page==HOME_PAGE_ROOMS||u->page==HOME_PAGE_ROOM))||(i==2&&u->page==HOME_PAGE_SCENES));
    bool detail=u->page==HOME_PAGE_DEVICE||u->page==HOME_PAGE_ACCOUNT||u->page==HOME_PAGE_NETWORK;
    if(detail)add(out,cap,&n,BACK,24,12,104,44,"Назад",false);
    else if(u->page==HOME_PAGE_ROOM||u->page==HOME_PAGE_SETTINGS)add(out,cap,&n,BACK,672,10,104,44,"Назад",false);
    else add(out,cap,&n,SETTINGS,720,10,56,44,"...",false);
    bool can=l->online&&l->time_ready&&!l->pending&&l->loaded;
    if(u->page==HOME_PAGE_SETTINGS){
        add(out,cap,&n,LIVE_NETWORK,626,84,150,56,"Открыть",false);
        add(out,cap,&n,LIVE_ACCOUNT,626,164,150,56,"Аккаунт",false);
        add(out,cap,&n,LIVE_THEME,626,244,150,56,"Изменить",false);
        add(out,cap,&n,ART,626,324,150,56,"Показать",false);
        return n;
    }
    if(u->page==HOME_PAGE_ACCOUNT){
        if(l->waiting)add(out,cap,&n,l->pending?0:LIVE_CANCEL,24,326,216,48,"Отменить",false);
        else if(!l->authorized)add(out,cap,&n,l->pending?0:LIVE_LOGIN,24,326,300,48,"Войти в Яндекс",true);
        else {add(out,cap,&n,l->pending?0:LIVE_REFRESH,24,326,300,48,"Обновить устройства",false);add(out,cap,&n,l->pending?0:LIVE_LOGOUT,548,326,216,48,"Выйти",false);}
        return n;
    }
    if(u->page==HOME_PAGE_NETWORK)return n;
    if(!l->authorized){add(out,cap,&n,LIVE_ACCOUNT,224,230,352,60,"Подключить Яндекс",true);return n;}
    if(u->page==HOME_PAGE_DEVICE&&u->device<l->device_count){
        const home_live_device_t *d=&l->devices[u->device];bool ready=can&&d->available;
        if(d->has_toggle){
            add(out,cap,&n,ready?LIVE_ON:0,24,341,138,56,"Включить",!d->on);
            add(out,cap,&n,ready?LIVE_OFF:0,178,341,138,56,"Выключить",d->on);
        }
        if(d->has_range){
            add(out,cap,&n,ready?MINUS:0,346,220,80,56,"-",false);
            add(out,cap,&n,ready?PLUS:0,696,220,80,56,"+",false);
            for(unsigned i=0;i<4;++i){
                unsigned before=n;add(out,cap,&n,ready?LIVE_PRESET+i:0,346+110*(int)i,289,100,44,NULL,false);
                if(n>before)out[n-1].preset=i+1;
            }
            add(out,cap,&n,ready?APPLY:0,346,352,430,44,"Применить",true);
        }
        return n;
    }
    unsigned count=home_ui_tile_count(u);home_ui_tile_t tiles[6];unsigned shown=home_ui_tiles(u,tiles);
    for(unsigned i=0;i<shown;++i){
        unsigned slot=tiles[i].slot;int x=24+256*(int)(i%3),y=110+150*(int)(i/3);
        unsigned card=0,action=0;const char *label="Открыть";bool filled=false;
        if(tiles[i].kind==HOME_TILE_DEVICE){
            const home_live_device_t *d=&l->devices[slot];card=LIVE_OPEN+slot;
            if(!d->available)label="Недоступно";
            else if(!l->online||!l->time_ready||!l->loaded)label="Нет связи";
            else if(l->pending)label="Ожидание";
            else {action=d->has_toggle?LIVE_TOGGLE+slot:LIVE_OPEN+slot;label=d->has_toggle?(d->on?"Выключить":"Включить"):"Настроить";filled=d->has_toggle&&d->on;}
        }else if(tiles[i].kind==HOME_TILE_ROOM){card=action=LIVE_ROOM+slot;}
        else {action=can?LIVE_SCENE+slot:0;label=!l->online||!l->time_ready||!l->loaded?"Нет связи":l->pending?"Ожидание":"Запустить";}
        add(out,cap,&n,card,x,y,240,138,NULL,false);
        add(out,cap,&n,action,x+12,y+80,216,44,label,filled);
    }
    if(u->offset)add(out,cap,&n,LIVE_PREV,626,64,68,44,"<",false);
    if(u->offset+6<count)add(out,cap,&n,LIVE_NEXT,708,64,68,44,">",false);
    return n;
}

unsigned home_ui_target(const home_ui_t *u,int x,int y)
{
    home_ui_control_t controls[20];
    unsigned n=home_ui_controls(u,controls,20);
    while(n){
        const home_ui_control_t *c=&controls[--n];
        if(x>=c->x&&y>=c->y&&x<c->x+c->w&&y<c->y+c->h)return c->action;
    }
    return 0;
}

bool home_ui_map_touch(uint16_t raw_x,uint16_t raw_y,uint16_t w,uint16_t h,
                       unsigned orientation,int *x,int *y)
{
    if(!x||!y||w<2||h<2||raw_x>=w||raw_y>=h||orientation>7)return false;
    unsigned xx=raw_x,yy=raw_y;
    if((w<h)!=((orientation&1U)!=0)){
        unsigned tmp=xx;xx=yy;yy=tmp;uint16_t t=w;w=h;h=t;
    }
    *x=(int)(xx*(HOME_UI_WIDTH-1)/(w-1));*y=(int)(yy*(HOME_UI_HEIGHT-1)/(h-1));
    if(orientation&2U)*x=(int)HOME_UI_WIDTH-1-*x;
    if(orientation&4U)*y=(int)HOME_UI_HEIGHT-1-*y;
    return true;
}

bool home_ui_changed_region(const uint8_t *before,const uint8_t *after,size_t length,home_ui_region_t *r)
{
    if(!before||!after||!r||length!=HOME_UI_BYTES)return false;
    *r=(home_ui_region_t){0};
    unsigned left=100,right=0,top=480,bottom=0;
    for(unsigned y=0;y<480;++y)for(unsigned x=0;x<100;++x){
        unsigned i=y*100+x;if(before[i]==after[i])continue;
        if(x<left)left=x;
        if(x>right)right=x;
        if(y<top)top=y;
        if(y>bottom)bottom=y;
    }
    if(top==480)return false;
    *r=(home_ui_region_t){left*8,top,(right-left+1)*8,bottom-top+1};return true;
}
