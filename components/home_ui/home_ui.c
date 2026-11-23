#include "home_ui.h"
#include "home_ui_layout.h"
#include <string.h>
#include <math.h>
#include <stdio.h>

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
    if(u->page==HOME_PAGE_HOME)return u->live->device_count;
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
    if(u->page==HOME_PAGE_ROOMS||u->page==HOME_PAGE_SCENES){
        unsigned count=u->page==HOME_PAGE_ROOMS?l->room_count:l->scene_count;
        for(unsigned i=0;i<count;++i){if(skip){--skip;continue;}if(n<HOME_UI_PAGE_SIZE)out[n++]=(home_ui_tile_t){u->page==HOME_PAGE_ROOMS?HOME_TILE_ROOM:HOME_TILE_SCENE,i};}
    }else for(unsigned i=0;i<l->device_count;++i){
        if(u->page==HOME_PAGE_ROOM&&l->devices[i].room!=u->room)continue;
        if(skip){--skip;continue;}if(n<HOME_UI_PAGE_SIZE)out[n++]=(home_ui_tile_t){HOME_TILE_DEVICE,i};
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
const home_live_control_t *home_ui_selected_control(const home_ui_t *u)
{
    if(!home_ui_capabilities(u)||u->control>=u->live->control_count)return NULL;
    const home_live_device_t *d=&u->live->devices[u->device];
    return u->control>=d->control_start&&u->control<d->control_start+d->control_count&&u->live->controls[u->control].device==u->device?&u->live->controls[u->control]:NULL;
}
double home_ui_control_preset(const home_live_control_t *c,unsigned preset)
{
    if(!c||preset>3||!c->has_minimum||!c->has_maximum||c->step<=0)return 0;
    double ratio=(preset+1)*0.25;
    if(!strcmp(c->instance,"open")){static const double opening[]={0,0.25,0.5,1};ratio=opening[preset];}
    double target=c->minimum+(c->maximum-c->minimum)*ratio;
    if(!strcmp(c->unit,"%"))target=c->maximum*ratio;
    if(target<c->minimum)target=c->minimum;
    double steps=floor((target-c->minimum)/c->step+0.5),maximum=floor((c->maximum-c->minimum)/c->step+0.000001);
    return c->minimum+(steps>maximum?maximum:steps)*c->step;
}
static void select_control(home_ui_t *u,unsigned index)
{
    u->control=index;const home_live_control_t *c=home_ui_selected_control(u);if(!c)return;
    u->page=HOME_PAGE_CONTROL;u->control_direct=false;u->option_offset=0;u->draft_option=UINT32_MAX;
    u->live_draft=c->kind==HOME_CONTROL_TOGGLE?(c->known?c->boolean:-1):c->relative_only?0:c->known?c->value:c->has_minimum?c->minimum:0;
    if(c->kind==HOME_CONTROL_MODE&&c->known&&c->selected_option<c->option_count){u->draft_option=c->selected_option;u->option_offset=u->draft_option/6*6;}
    if(c->kind==HOME_CONTROL_RGB){unsigned rgb=(unsigned)c->value;u->color_draft[0]=(rgb>>16)&255;u->color_draft[1]=(rgb>>8)&255;u->color_draft[2]=rgb&255;}
    else {u->color_draft[0]=c->value;u->color_draft[1]=c->second;u->color_draft[2]=c->third;}
}
static void number_open(home_ui_t *u,unsigned channel)
{
    u->number_channel=channel;u->number_error=false;u->number_replace=true;
    snprintf(u->number_text,sizeof(u->number_text),"%.9g",channel<3?u->color_draft[channel]:u->live_draft);
    u->page=HOME_PAGE_NUMBER;
}
static void number_key(home_ui_t *u,unsigned key)
{
    static const char *keys[]={"1","2","3","del","4","5","6","-","7","8","9",".","0","00","clear","done"};
    if(key>=16)return;
    u->number_error=false;size_t n=strlen(u->number_text);
    if(u->number_replace&&key!=15){
        if(key!=3&&key!=7){u->number_text[0]=0;n=0;}
        u->number_replace=false;
    }
    if(key==3){if(n)u->number_text[n-1]=0;}
    else if(key==7){
        if(u->number_text[0]=='-')memmove(u->number_text,u->number_text+1,n);
        else if(n+1<sizeof(u->number_text)){memmove(u->number_text+1,u->number_text,n+1);u->number_text[0]='-';}
    }else if(key==14)u->number_text[0]=0;
    else if(key==15){
        const char *cursor=u->number_text;bool negative=*cursor=='-';if(negative)++cursor;
        double v=0,fraction=0.1;bool decimal=false,digit=false,syntax=true;
        for(;*cursor;++cursor){
            if(*cursor=='.'&&!decimal){decimal=true;continue;}
            if(*cursor<'0'||*cursor>'9'){syntax=false;break;}
            digit=true;if(decimal){v+=(*cursor-'0')*fraction;fraction*=0.1;}else v=v*10+(*cursor-'0');
        }
        if(negative)v=-v;
        const home_live_control_t *c=home_ui_selected_control(u);
        bool valid=c&&syntax&&digit&&isfinite(v);
        if(valid&&u->number_channel<3){
            double maximum=c->kind==HOME_CONTROL_RGB?255:u->number_channel==0?360:100;
            valid=v>=0&&v<=maximum&&floor(v)==v;if(valid)u->color_draft[u->number_channel]=v;
        }else if(valid){
            double anchor=c->relative_only?0:c->has_minimum?c->minimum:0;
            double steps=(v-anchor)/c->step;
            valid=isfinite(steps)&&fabs(steps-round(steps))<0.00001;
            if(!c->relative_only)valid=valid&&(!c->has_minimum||v>=c->minimum)&&(!c->has_maximum||v<=c->maximum);
            else if(c->has_minimum&&c->has_maximum)valid=valid&&fabs(v)<=c->maximum-c->minimum;
            if(valid)u->live_draft=v;
        }
        if(valid)u->page=HOME_PAGE_CONTROL;else u->number_error=true;
    }else if(key!=11||!strchr(u->number_text,'.')){
        size_t add=strlen(keys[key]);if(n+add<sizeof(u->number_text))memcpy(u->number_text+n,keys[key],add+1);
    }
}
static bool capability_action(home_ui_t *u,unsigned a)
{
    const home_live_data_t *l=u->live;
    if(u->page==HOME_PAGE_NUMBER){
        if(a==BACK)u->page=HOME_PAGE_CONTROL;
        else if(a>=NUMBER_KEY&&a<NUMBER_KEY+16)number_key(u,a-NUMBER_KEY);else return false;
        changed(u);return true;
    }
    if(u->page==HOME_PAGE_CONTROL&&a==BACK){
        u->page=u->control_direct?(home_ui_page_t)u->return_page:HOME_PAGE_DEVICE;
        if(u->control_direct)u->offset=u->return_offset;
        changed(u);return true;
    }
    if(u->page==HOME_PAGE_CONTROL&&a==CAP_ALL){
        u->page=HOME_PAGE_DEVICE;u->control_offset=0;u->control_direct=false;changed(u);return true;
    }
    if(u->page==HOME_PAGE_DEVICE&&home_ui_capabilities(u)){
        const home_live_device_t *d=&l->devices[u->device];
        if(a==CAP_PREV)u->control_offset=u->control_offset>=6?u->control_offset-6:0;
        else if(a==CAP_NEXT){if(u->control_offset+6<d->control_count)u->control_offset+=6;}
        else if(a>=CAP_OPEN&&a<CAP_OPEN+HOME_MAX_CONTROLS){select_control(u,a-CAP_OPEN);}
        else if(a>=CAP_QUICK&&a<CAP_QUICK+HOME_MAX_CONTROLS){
            unsigned index=a-CAP_QUICK;
            if(index>=l->control_count)return true;
            const home_live_control_t *c=&l->controls[index];
            if(c->device!=u->device||!c->quick||!c->known||c->kind!=HOME_CONTROL_TOGGLE||!l->online||!l->time_ready||!l->loaded||l->pending||!d->available||home_ui_device_failed(u))return true;
            u->command=(home_command_t){.kind=HOME_CMD_CONTROL,.epoch=l->epoch,.slot=u->device,.control=index,.value=!c->boolean};
        }
        else return false;
        changed(u);return true;
    }
    if(u->page!=HOME_PAGE_CONTROL)return false;
    const home_live_control_t *c=home_ui_selected_control(u);if(!c)return true;
    if(a==CAP_OPTIONS_PREV)u->option_offset=u->option_offset>=6?u->option_offset-6:0;
    else if(a==CAP_OPTIONS_NEXT){if(u->option_offset+6<c->option_count)u->option_offset+=6;}
    else if(a==LIVE_REFRESH)return false;
    else{
        if(!l->online||!l->time_ready||!l->loaded||l->pending||!l->devices[u->device].available||home_ui_device_failed(u))return true;
        if(a==CAP_EDIT)number_open(u,3);
        else if(a>=CAP_COLOR_EDIT&&a<CAP_COLOR_EDIT+3)number_open(u,a-CAP_COLOR_EDIT);
        else if(a>=CAP_COLOR_MINUS&&a<CAP_COLOR_MINUS+3){unsigned ch=a-CAP_COLOR_MINUS;if(u->color_draft[ch]>0)--u->color_draft[ch];}
        else if(a>=CAP_COLOR_PLUS&&a<CAP_COLOR_PLUS+3){unsigned ch=a-CAP_COLOR_PLUS;double max=c->kind==HOME_CONTROL_RGB?255:ch==0?360:100;if(u->color_draft[ch]<max)++u->color_draft[ch];}
        else if(a==MINUS||a==PLUS){
            double v=u->live_draft+(a==PLUS?c->step:-c->step);
            if(!c->relative_only){if(c->has_minimum&&v<c->minimum)v=c->minimum;if(c->has_maximum&&v>c->maximum)v=c->minimum+floor((c->maximum-c->minimum)/c->step+0.00001)*c->step;}
            else if(c->has_minimum&&c->has_maximum&&fabs(v)>c->maximum-c->minimum)v=u->live_draft;
            if(isfinite(v))u->live_draft=v;
        }else if(a>=LIVE_PRESET&&a<LIVE_PRESET+4)u->live_draft=home_ui_control_preset(c,a-LIVE_PRESET);
        else if(a==CAP_ON||a==CAP_OFF)u->live_draft=a==CAP_ON;
        else if(a>=CAP_OPTION&&a<CAP_OPTION+HOME_MAX_OPTIONS){unsigned option=a-CAP_OPTION;if(option<c->option_count)u->draft_option=option;}
        else if(a==APPLY||a==CAP_PULSE){
            home_command_t cmd={.kind=HOME_CMD_CONTROL,.epoch=l->epoch,.slot=u->device,.control=u->control,.option=u->draft_option,.value=u->live_draft,.relative=c->relative_only};
            if(c->kind==HOME_CONTROL_READING||c->kind==HOME_CONTROL_UNSUPPORTED)return true;
            if(c->kind==HOME_CONTROL_MODE&&u->draft_option>=c->option_count)return true;
            if(c->kind==HOME_CONTROL_TOGGLE&&!c->momentary&&u->live_draft!=0&&u->live_draft!=1)return true;
            if(c->kind==HOME_CONTROL_RANGE&&c->relative_only&&u->live_draft==0)return true;
            if(c->kind==HOME_CONTROL_RGB)cmd.value=(unsigned)u->color_draft[0]*65536+(unsigned)u->color_draft[1]*256+(unsigned)u->color_draft[2];
            else if(c->kind==HOME_CONTROL_HSV){cmd.value=u->color_draft[0];cmd.second=u->color_draft[1];cmd.third=u->color_draft[2];}
            if(a==CAP_PULSE)cmd.value=1;
            u->command=cmd;
        }else return false;
    }
    changed(u);return true;
}
static void live_action(home_ui_t *u,unsigned a)
{
    const home_live_data_t *l=u->live;
    if(capability_action(u,a))return;
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
    else if(a==LIVE_PREV)u->offset=u->offset>=HOME_UI_PAGE_SIZE?u->offset-HOME_UI_PAGE_SIZE:0;
    else if(a==LIVE_NEXT&&u->offset+HOME_UI_PAGE_SIZE<home_ui_tile_count(u))u->offset+=HOME_UI_PAGE_SIZE;
    else if(a>=LIVE_OPEN&&a<LIVE_OPEN+HOME_MAX_DEVICES){
        unsigned d=a-LIVE_OPEN;if(d>=l->device_count)return;
        u->return_page=u->page;u->return_offset=u->offset;u->page=HOME_PAGE_DEVICE;u->device=d;u->live_draft=l->devices[d].value;u->control_offset=0;u->control_direct=false;
        const home_live_device_t *view=&l->devices[d];
        if(view->control_count&&(!view->dashboard||view->control_count==1)&&view->primary_control<l->control_count&&l->controls[view->primary_control].device==d){
            select_control(u,view->primary_control);u->control_direct=true;
        }
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
    else if(action==LIVE_THEME)u->dark_theme=!u->dark_theme;
    else if(action==BACK)u->page=u->page==HOME_PAGE_DEVICE?(home_ui_page_t)u->return_page:
                                 u->page==HOME_PAGE_ROOM?HOME_PAGE_ROOMS:HOME_PAGE_HOME;
    else if(action>=OPEN_DEVICE&&action<OPEN_DEVICE+4)open_device(u,action-OPEN_DEVICE);
    else if(action>=TOGGLE_DEVICE&&action<TOGGLE_DEVICE+4){
        unsigned d=action-TOGGLE_DEVICE;
        if(d<2)u->light_on[d]=!u->light_on[d];
        else if(d==2)u->heat_on=!u->heat_on;
        else u->curtains=u->curtains==100?0:100;
    }else if(action>=ROOM&&action<ROOM+3){u->room=(uint8_t)(action-ROOM);u->page=HOME_PAGE_ROOM;}
    else if(action>=DEMO_PRESET&&action<DEMO_PRESET+4&&u->page==HOME_PAGE_DEVICE){
        unsigned p=action-DEMO_PRESET+1;
        u->draft=(uint8_t)(u->device==2?16+(14*p+2)/4:25*p);
    }
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
        if(!had_contact){u->pressed=action!=0;u->pressed_action=action;u->pointer_id=(uint8_t)id;u->press_x=x;u->press_y=y;}
        else if(u->pointer_id!=id||action!=u->pressed_action||x-u->press_x>16||u->press_x-x>16||y-u->press_y>16||u->press_y-y>16)u->pressed=false;
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

/* Explicit buttons always win over the card fallback in home_ui_target.
 * A disabled button absorbs the tap; it must never open or activate its card. */
static void chrome(const home_ui_t *u,home_ui_control_t *out,unsigned cap,unsigned *n)
{
    bool device=u->page==HOME_PAGE_DEVICE||u->page==HOME_PAGE_CONTROL||u->page==HOME_PAGE_NUMBER;
    bool account=u->page==HOME_PAGE_ACCOUNT;
    bool detail=device||account||u->page==HOME_PAGE_SETTINGS||u->page==HOME_PAGE_NETWORK;
    bool back=detail||u->page==HOME_PAGE_ROOM;
    if(!device&&!(account&&u->live&&u->live->waiting)){
        const char *tabs[]={"Главная","Комнаты","Сценарии","Картина"};
        for(unsigned i=0;i<4;++i)add(out,cap,n,i+1,24+192*(int)i,UI_NAV_Y,176,UI_TARGET,tabs[i],
            (i==0&&u->page==HOME_PAGE_HOME)||(i==1&&(u->page==HOME_PAGE_ROOMS||u->page==HOME_PAGE_ROOM))||
            (i==2&&u->page==HOME_PAGE_SCENES));
    }
    if(back)add(out,cap,n,BACK,24,8,112,UI_TARGET,"Назад",false);
    if(u->page==HOME_PAGE_CONTROL&&home_ui_capabilities(u)&&u->live->devices[u->device].control_count>1)
        add(out,cap,n,CAP_ALL,632,8,144,56,"Все функции",false);
    if(!detail){
        unsigned total=u->live?home_ui_tile_count(u):0;
        add(out,cap,n,u->offset?LIVE_PREV:0,488,8,56,UI_TARGET,"<",false);
        add(out,cap,n,u->offset+HOME_UI_PAGE_SIZE<total?LIVE_NEXT:0,560,8,56,UI_TARGET,">",false);
        add(out,cap,n,SETTINGS,632,8,144,UI_TARGET,"Настройки",false);
    }
}
static void range_controls(home_ui_control_t *out,unsigned cap,unsigned *n,bool ready,
                           unsigned preset,double value,const home_live_device_t *d)
{
    add(out,cap,n,ready?MINUS:0,UI_MINUS_X,UI_STEP_Y,96,72,"-",false);
    add(out,cap,n,ready?PLUS:0,UI_PLUS_X,UI_STEP_Y,96,72,"+",false);
    for(unsigned i=0;i<4;++i){
        unsigned before=*n;
        bool selected=d&&value==home_ui_preset_value(d,i);
        add(out,cap,n,ready?preset+i:0,UI_PRESET_X+UI_PRESET_PITCH*(int)i,UI_PRESET_Y,144,UI_TARGET,NULL,selected);
        if(*n>before)out[*n-1].preset=i+1;
    }
}
static void demo_tile_controls(const home_ui_t *u,unsigned d,unsigned row,
                              home_ui_control_t *out,unsigned cap,unsigned *n)
{
    int x=home_ui_tile_x(row),y=home_ui_tile_y(row)+UI_TILE_BUTTON_Y;
    if(d==2){
        add(out,cap,n,OPEN_DEVICE+d,x+UI_TILE_PAD,y,144,UI_TARGET,"Настроить",false);
        add(out,cap,n,OPEN_DEVICE+d,x+UI_TILE_MORE_X,y,56,UI_TARGET,"Настройки",false);return;
    }
    const char *label=d<2?(u->light_on[d]?"Выключить":"Включить"):u->curtains==100?"Закрыть":"Открыть";
    add(out,cap,n,TOGGLE_DEVICE+d,x+UI_TILE_PAD,y,144,UI_TARGET,label,d<2&&u->light_on[d]);
    add(out,cap,n,OPEN_DEVICE+d,x+UI_TILE_MORE_X,y,56,UI_TARGET,"Настройки",false);
}
unsigned home_ui_controls(const home_ui_t *u,home_ui_control_t *out,unsigned cap)
{
    if(!u||!out)return 0;
    if(u->live)return live_controls(u,out,cap);
    if(u->room>2||u->device>3)return 0;
    unsigned n=0;chrome(u,out,cap,&n);
    if(u->page==HOME_PAGE_HOME){
        for(unsigned d=0;d<4;++d)demo_tile_controls(u,d,d,out,cap,&n);
    }else if(u->page==HOME_PAGE_ROOMS){
        for(unsigned i=0;i<3;++i)add(out,cap,&n,ROOM+i,home_ui_tile_x(i)+UI_TILE_PAD,home_ui_tile_y(i)+UI_TILE_BUTTON_Y,216,UI_TARGET,"Открыть",false);
    }else if(u->page==HOME_PAGE_ROOM){
        static const unsigned devices[3][2]={{0,3},{1,4},{2,4}};
        for(unsigned i=0;i<2;++i)if(devices[u->room][i]<4)demo_tile_controls(u,devices[u->room][i],i,out,cap,&n);
    }else if(u->page==HOME_PAGE_SCENES){
        for(unsigned i=0;i<2;++i)add(out,cap,&n,SCENE+i,home_ui_tile_x(i)+UI_TILE_PAD,home_ui_tile_y(i)+UI_TILE_BUTTON_Y,216,UI_TARGET,"Запустить",u->last_scene==i+1);
    }else if(u->page==HOME_PAGE_DEVICE){
        add(out,cap,&n,TOGGLE_DEVICE+u->device,632,8,144,UI_TARGET,
            u->device<2?(u->light_on[u->device]?"Выключить":"Включить"):
            u->device==2?(u->heat_on?"Выключить":"Включить"):u->curtains==100?"Закрыть":"Открыть",false);
        range_controls(out,cap,&n,true,DEMO_PRESET,u->draft,NULL);
        add(out,cap,&n,APPLY,24,UI_NAV_Y,752,UI_TARGET,"Применить",true);
    }else if(u->page==HOME_PAGE_SETTINGS){
        add(out,cap,&n,LIVE_THEME,632,home_ui_row_y(2),144,UI_TARGET,"Изменить",false);
        add(out,cap,&n,ART,632,home_ui_row_y(3),144,UI_TARGET,"Показать",false);
    }
    return n;
}

static unsigned capability_controls(const home_ui_t *u,home_ui_control_t *out,unsigned cap,unsigned n)
{
    const home_live_data_t *l=u->live;
    if(u->page==HOME_PAGE_NUMBER){
        static const char *keys[]={"1","2","3","Удалить","4","5","6","-","7","8","9",".","0","00","Очистить","Готово"};
        for(unsigned i=0;i<16;++i)add(out,cap,&n,NUMBER_KEY+i,24+192*(int)(i%4),144+64*(int)(i/4),176,56,keys[i],i==15);
        return n;
    }
    if(u->page==HOME_PAGE_DEVICE){
        const home_live_device_t *d=&l->devices[u->device];
        unsigned end=u->control_offset+6;if(end>d->control_count)end=d->control_count;
        for(unsigned i=u->control_offset;i<end;++i){
            unsigned index=d->control_start+i;const home_live_control_t *c=&l->controls[index];unsigned tile=i-u->control_offset;
            bool toggle=c->kind==HOME_CONTROL_TOGGLE&&c->known&&c->quick;
            bool ready=l->online&&l->time_ready&&l->loaded&&!l->pending&&d->available&&!home_ui_device_failed(u);
            const char *label=c->kind==HOME_CONTROL_READING||c->kind==HOME_CONTROL_UNSUPPORTED?"Подробнее":toggle?
                (l->pending?"Ожидание":!ready?"Нет связи":c->boolean?c->off_label:c->on_label):"Настроить";
            add(out,cap,&n,toggle?(ready?CAP_QUICK+index:0):CAP_OPEN+index,home_ui_tile_x(tile)+12,home_ui_tile_y(tile)+64,216,56,label,toggle&&c->boolean);
        }
        add(out,cap,&n,u->control_offset?CAP_PREV:0,24,UI_NAV_Y,112,56,"<",false);
        add(out,cap,&n,u->control_offset+6<d->control_count?CAP_NEXT:0,152,UI_NAV_Y,112,56,">",false);
        add(out,cap,&n,l->online&&l->time_ready&&!l->pending?LIVE_REFRESH:0,288,UI_NAV_Y,488,56,"Обновить состояние",false);
        return n;
    }
    const home_live_control_t *c=home_ui_selected_control(u);if(!c)return n;
    bool read=c->kind==HOME_CONTROL_READING||c->kind==HOME_CONTROL_UNSUPPORTED;
    bool failed=home_ui_device_failed(u);
    bool ready=l->online&&l->time_ready&&l->loaded&&!l->pending&&l->devices[u->device].available&&!failed;
    if(c->kind==HOME_CONTROL_RANGE){
        add(out,cap,&n,ready?MINUS:0,168,168,96,72,"-",false);
        add(out,cap,&n,ready?PLUS:0,536,168,96,72,"+",false);
        add(out,cap,&n,ready?CAP_EDIT:0,280,165,240,72,NULL,false);
        if(!c->relative_only&&c->has_minimum&&c->has_maximum)for(unsigned i=0;i<4;++i){
            unsigned before=n;add(out,cap,&n,ready?LIVE_PRESET+i:0,88+160*(int)i,304,144,56,NULL,u->live_draft==home_ui_control_preset(c,i));
            if(n>before)out[n-1].preset=i+1;
        }
    }else if(c->kind==HOME_CONTROL_MODE){
        unsigned end=u->option_offset+6;if(end>c->option_count)end=c->option_count;
        for(unsigned i=u->option_offset;i<end;++i){unsigned tile=i-u->option_offset;
            add(out,cap,&n,ready?CAP_OPTION+i:0,24+256*(int)(tile%3),208+80*(int)(tile/3),240,56,l->options[c->option_start+i].name,u->draft_option==i);
        }
        add(out,cap,&n,u->option_offset?CAP_OPTIONS_PREV:0,24,UI_NAV_Y,112,56,"<",false);
        add(out,cap,&n,u->option_offset+6<c->option_count?CAP_OPTIONS_NEXT:0,152,UI_NAV_Y,112,56,">",false);
    }else if(c->kind==HOME_CONTROL_TOGGLE){
        if(c->momentary)add(out,cap,&n,ready?CAP_PULSE:0,216,240,368,72,l->devices[u->device].profile==HOME_PROFILE_VACUUM?"Команда уборки":l->devices[u->device].profile==HOME_PROFILE_PET_FEEDER?"Подать корм":l->devices[u->device].profile==HOME_PROFILE_KETTLE?"Команда нагрева":"Нажать питание",false);
        else{
            add(out,cap,&n,ready?CAP_ON:0,152,240,224,72,c->on_label[0]?c->on_label:"Включить",u->live_draft==1);
            add(out,cap,&n,ready?CAP_OFF:0,424,240,224,72,c->off_label[0]?c->off_label:"Выключить",u->live_draft==0);
        }
    }else if(c->kind==HOME_CONTROL_RGB||c->kind==HOME_CONTROL_HSV){
        for(unsigned i=0;i<3;++i){int y=136+80*(int)i;
            add(out,cap,&n,ready?CAP_COLOR_MINUS+i:0,344,y,56,56,"-",false);
            add(out,cap,&n,ready?CAP_COLOR_EDIT+i:0,416,y,208,56,NULL,false);
            add(out,cap,&n,ready?CAP_COLOR_PLUS+i:0,640,y,56,56,"+",false);
        }
    }
    bool refresh=read||c->momentary||failed;
    bool enabled=refresh?(l->online&&l->time_ready&&!l->pending):ready;
    if(c->kind==HOME_CONTROL_MODE&&u->draft_option>=c->option_count&&!refresh)enabled=false;
    if(c->kind==HOME_CONTROL_TOGGLE&&!c->momentary&&u->live_draft!=0&&u->live_draft!=1&&!refresh)enabled=false;
    if(c->kind==HOME_CONTROL_RANGE&&c->relative_only&&u->live_draft==0&&!refresh)enabled=false;
    add(out,cap,&n,enabled?(refresh?LIVE_REFRESH:APPLY):0,c->kind==HOME_CONTROL_MODE?288:24,UI_NAV_Y,c->kind==HOME_CONTROL_MODE?488:752,56,
        l->pending?"Ожидаем подтверждение":!l->online?"Нет сети":refresh?"Проверить состояние":c->relative_only?"Изменить на указанное":"Применить",enabled);
    return n;
}
static unsigned live_controls(const home_ui_t *u,home_ui_control_t *out,unsigned cap)
{
    const home_live_data_t *l=u->live;unsigned n=0;chrome(u,out,cap,&n);
    bool can=l->online&&l->time_ready&&!l->pending&&l->loaded;
    if(u->page==HOME_PAGE_CONTROL||u->page==HOME_PAGE_NUMBER||(u->page==HOME_PAGE_DEVICE&&home_ui_capabilities(u)))return capability_controls(u,out,cap,n);
    if(u->page==HOME_PAGE_SETTINGS){
        const unsigned actions[]={LIVE_NETWORK,LIVE_ACCOUNT,LIVE_THEME,ART};
        const char *labels[]={"Открыть","Аккаунт","Изменить","Показать"};
        for(unsigned i=0;i<4;++i)add(out,cap,&n,actions[i],632,home_ui_row_y(i),144,UI_TARGET,labels[i],false);
        return n;
    }
    if(u->page==HOME_PAGE_ACCOUNT){
        if(l->waiting)add(out,cap,&n,l->pending?0:LIVE_CANCEL,24,UI_NAV_Y,752,UI_TARGET,"Отменить вход",false);
        else if(!l->authorized)add(out,cap,&n,l->pending?0:LIVE_LOGIN,24,304,352,UI_TARGET,"Войти в Яндекс",true);
        else{
            add(out,cap,&n,l->pending||!l->online||!l->time_ready?0:LIVE_REFRESH,24,304,352,UI_TARGET,"Обновить устройства",false);
            add(out,cap,&n,l->pending?0:LIVE_LOGOUT,552,304,224,UI_TARGET,"Выйти",false);
        }
        return n;
    }
    if(u->page==HOME_PAGE_NETWORK)return n;
    if(!l->authorized){add(out,cap,&n,LIVE_ACCOUNT,224,230,352,UI_TARGET,"Подключить Яндекс",true);return n;}
    if(u->page==HOME_PAGE_DEVICE&&u->device<l->device_count){
        const home_live_device_t *d=&l->devices[u->device];
        bool ready=can&&d->available,failed=home_ui_device_failed(u);
        bool enabled=failed?(l->online&&l->time_ready&&!l->pending):ready;
        if(d->has_toggle)add(out,cap,&n,ready&&!failed?LIVE_TOGGLE+u->device:0,632,8,144,UI_TARGET,
            d->on?"Выключить":"Включить",d->on);
        if(d->has_range)range_controls(out,cap,&n,ready&&!failed,LIVE_PRESET,u->live_draft,d);
        if(d->has_range||failed)add(out,cap,&n,enabled?(failed?LIVE_REFRESH:APPLY):0,24,UI_NAV_Y,752,UI_TARGET,
            l->pending?"Ожидаем подтверждение":!enabled?"Нет связи":failed?"Проверить состояние":"Применить",enabled);
        return n;
    }
    home_ui_tile_t tiles[6];unsigned shown=home_ui_tiles(u,tiles);
    for(unsigned i=0;i<shown;++i){
        unsigned slot=tiles[i].slot;int x=home_ui_tile_x(i),y=home_ui_tile_y(i)+UI_TILE_BUTTON_Y;
        if(tiles[i].kind==HOME_TILE_DEVICE){
            const home_live_device_t *d=&l->devices[slot];bool ready=can&&d->available;
            /* Opening detail remains useful offline. It never submits a command. */
            if(d->has_toggle&&d->profile!=HOME_PROFILE_LOCK&&strcmp(d->type,"devices.types.openable.valve")){
                bool failed=l->command_failed&&l->failed_device==slot;
                bool enabled=failed?(l->online&&l->time_ready&&!l->pending):ready;
                const char *label=l->pending?"Ожидание":failed&&enabled?"Проверить":!d->available?"Недоступно":!can?"Нет связи":d->on?(d->quick_off[0]?d->quick_off:"Выключить"):(d->quick_on[0]?d->quick_on:"Включить");
                add(out,cap,&n,enabled?(failed?LIVE_REFRESH:LIVE_TOGGLE+slot):0,x+UI_TILE_PAD,y,144,UI_TARGET,label,!failed&&d->on);
                add(out,cap,&n,LIVE_OPEN+slot,x+UI_TILE_MORE_X,y,56,UI_TARGET,"Настройки",false);
            }else{
                add(out,cap,&n,LIVE_OPEN+slot,x+UI_TILE_PAD,y,144,UI_TARGET,d->has_range?"Настроить":"Показать",false);
                add(out,cap,&n,LIVE_OPEN+slot,x+UI_TILE_MORE_X,y,56,UI_TARGET,"Настройки",false);
            }
        }else if(tiles[i].kind==HOME_TILE_ROOM){
            add(out,cap,&n,LIVE_ROOM+slot,x+UI_TILE_PAD,y,216,UI_TARGET,"Открыть",false);
        }else add(out,cap,&n,can?LIVE_SCENE+slot:0,x+UI_TILE_PAD,y,216,UI_TARGET,l->pending?"Ожидание":!can?"Нет связи":"Запустить",false);
    }
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
    /* Card bodies and names open details only after all explicit buttons,
     * including disabled ones, have been considered. No overlapping targets. */
    if(u&&u->live&&u->page==HOME_PAGE_DEVICE&&home_ui_capabilities(u)){
        const home_live_device_t *d=&u->live->devices[u->device];
        for(unsigned i=0;i<6&&u->control_offset+i<d->control_count;++i){
            int left=home_ui_tile_x(i),top=home_ui_tile_y(i);
            if(x>=left&&x<left+240&&y>=top&&y<top+128)return CAP_OPEN+d->control_start+u->control_offset+i;
        }
    }else if(u&&u->live&&(u->page==HOME_PAGE_HOME||u->page==HOME_PAGE_ROOM)){
        home_ui_tile_t tiles[6];unsigned total=home_ui_tiles(u,tiles);
        for(unsigned i=0;i<total;++i){int left=home_ui_tile_x(i),top=home_ui_tile_y(i);
            if(tiles[i].kind==HOME_TILE_DEVICE&&x>=left&&x<left+240&&y>=top&&y<top+128)return LIVE_OPEN+tiles[i].slot;
        }
    }else if(u&&!u->live&&(u->page==HOME_PAGE_HOME||u->page==HOME_PAGE_ROOM)){
        static const unsigned devices[3][2]={{0,3},{1,4},{2,4}};
        unsigned count=u->page==HOME_PAGE_HOME?4:u->room<3?(devices[u->room][1]<4?2:1):0;
        for(unsigned i=0;i<count;++i){int left=home_ui_tile_x(i),top=home_ui_tile_y(i);
            if(x>=left&&x<left+240&&y>=top&&y<top+128)return OPEN_DEVICE+(u->page==HOME_PAGE_HOME?i:devices[u->room][i]);
        }
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
