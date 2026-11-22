#include "home_ui.h"
#include "home_ui_font.h"
#include "vendor/qrcodegen/qrcodegen.h"
#include <stdio.h>
#include <string.h>

static void rect(uint8_t *f,int x,int y,int w,int h,bool black)
{
    int x0=x<0?0:x,y0=y<0?0:y,x1=x+w>800?800:x+w,y1=y+h>480?480:y+h;
    if(w<=0||h<=0||x0>=x1||y0>=y1)return;
    for(int yy=y0;yy<y1;++yy)for(int xx=x0;xx<x1;++xx){
        uint8_t mask=(uint8_t)(128U>>(xx&7));
        if(black)f[yy*100+xx/8]|=mask;else f[yy*100+xx/8]&=(uint8_t)~mask;
    }
}
static void box(uint8_t *f,int x,int y,int w,int h)
{ rect(f,x,y,w,1,true);rect(f,x,y+h-1,w,1,true);rect(f,x,y,1,h,true);rect(f,x+w-1,y,1,h,true); }

static unsigned utf8(const char **s)
{
    const unsigned char *p=(const unsigned char *)*s;
    unsigned code=*p++;
    if(code>=0xc2&&code<0xe0&&(*p&0xc0)==0x80){code=((code&31)<<6)|(*p++&63);}
    else if(code>=128)code='?';
    *s=(const char *)p;return code;
}
static const home_font_glyph_t *glyph(const home_font_t *font,unsigned code)
{
    unsigned lo=0,hi=font->count;
    while(lo<hi){unsigned mid=(lo+hi)/2;if(font->glyphs[mid].code<code)lo=mid+1;else hi=mid;}
    return lo<font->count&&font->glyphs[lo].code==code?&font->glyphs[lo]:NULL;
}
static int text_width(const char *s,const home_font_t *font)
{
    int width=0;
    while(*s){const home_font_glyph_t *g=glyph(font,utf8(&s));if(!g)return 10000;width+=g->width;}
    return width;
}
static bool text(uint8_t *f,int x,int y,const char *s,const home_font_t *font,bool black,int max_width)
{
    if(text_width(s,font)>max_width||y<0||y+font->height>480)return false;
    while(*s){
        const home_font_glyph_t *g=glyph(font,utf8(&s));if(!g)return false;
        unsigned stride=(g->width+7)/8;
        for(unsigned yy=0;yy<font->height;++yy)for(unsigned xx=0;xx<g->width;++xx)
            if(font->bits[g->offset+yy*stride+xx/8]&(128>>(xx&7)))rect(f,x+(int)xx,y+(int)yy,1,1,black);
        x+=g->width;
    }
    return true;
}
static const char *device_name(unsigned d)
{
    static const char *names[]={"Свет · гостиная","Свет · кухня","Отопление","Шторы"};
    return d<4?names[d]:"?";
}
static void device_state(const home_ui_t *u,unsigned d,char *s,size_t n)
{
    if(d<2)snprintf(s,n,"%s",u->light_on[d]?"Включён":"Выключен");
    else if(d==2){if(u->heat_on)snprintf(s,n,"%u °C · демо",(unsigned)u->temperature);else snprintf(s,n,"Выключено");}
    else if(u->curtains==100)snprintf(s,n,"Открыты");
    else if(!u->curtains)snprintf(s,n,"Закрыты");
    else snprintf(s,n,"Открыто: %u%%",(unsigned)u->curtains);
}
static bool device_card(uint8_t *f,const home_ui_t *u,unsigned d,int x,int y)
{
    box(f,x,y,240,138);char value[64];device_state(u,d,value,sizeof(value));
    bool ok=text(f,x+14,y+8,device_name(d),&home_font_20,true,212);
    return text(f,x+14,y+40,value,&home_font_18,true,212)&&ok;
}

/* Clip external names instead of rejecting a complete frame. */
static bool fit(uint8_t *f,int x,int y,const char *s,const home_font_t *font,bool black,int width)
{
    char clipped[256];size_t n=0;int used=0;
    while(*s&&n+5<sizeof(clipped)){
        const char *start=s;unsigned cp=utf8(&s);
        const home_font_glyph_t *g=glyph(font,cp);
        if(!g)g=glyph(font,'?');
        if(!g)continue;
        if(used+g->width>width-18){memcpy(clipped+n,"...",3);n+=3;break;}
        if(g->code==cp){size_t bytes=(size_t)(s-start);memcpy(clipped+n,start,bytes);n+=bytes;}
        else clipped[n++]='?';
        used+=g->width;
    }
    clipped[n]=0;return text(f,x,y,clipped,font,black,width);
}
/* Integer modules and a four-module quiet zone: scan the physical E-Ink,
 * never a scaled/antialiased bitmap. Scratch stays below 1 KB on this task. */
static bool qr_code(uint8_t *frame,int left,int top,int extent,const char *payload)
{
    enum { MAX_VERSION=9,QUIET=4 };
    uint8_t scratch[qrcodegen_BUFFER_LEN_FOR_VERSION(MAX_VERSION)];
    uint8_t code[qrcodegen_BUFFER_LEN_FOR_VERSION(MAX_VERSION)];
    if(!payload||!payload[0]||!qrcodegen_encodeText(payload,scratch,code,qrcodegen_Ecc_MEDIUM,
        qrcodegen_VERSION_MIN,MAX_VERSION,qrcodegen_Mask_AUTO,true))return false;
    int size=qrcodegen_getSize(code),scale=extent/(size+2*QUIET);
    if(scale<3)return false;
    int span=(size+2*QUIET)*scale;
    int x0=left+(extent-span)/2+QUIET*scale,y0=top+(extent-span)/2+QUIET*scale;
    rect(frame,left,top,extent,extent,false);
    for(int y=0;y<size;++y)for(int x=0;x<size;++x)
        if(qrcodegen_getModule(code,x,y))rect(frame,x0+x*scale,y0+y*scale,scale,scale,true);
    return true;
}
static void rounded(uint8_t *f,int x,int y,int w,int h,int r,bool fill)
{
    if(fill){
        for(int row=0;row<h;++row){
            int dy=row<r?r-row:row>=h-r?row-(h-1-r):0,inset=0;
            if(dy)while(inset<r&&(r-inset)*(r-inset)+dy*dy>r*r)++inset;
            rect(f,x+inset,y+row,w-2*inset,1,true);
        }
        return;
    }
    rect(f,x+r,y,w-2*r,1,true);rect(f,x+r,y+h-1,w-2*r,1,true);
    rect(f,x,y+r,1,h-2*r,true);rect(f,x+w-1,y+r,1,h-2*r,true);
    for(int yy=-r;yy<=0;++yy)for(int xx=-r;xx<=0;++xx){
        int d=xx*xx+yy*yy;if(d>r*r||d<=(r-1)*(r-1))continue;
        rect(f,x+r+xx,y+r+yy,1,1,true);rect(f,x+w-1-r-xx,y+r+yy,1,1,true);
        rect(f,x+r+xx,y+h-1-r-yy,1,1,true);rect(f,x+w-1-r-xx,y+h-1-r-yy,1,1,true);
    }
}
static void line(uint8_t *f,int x,int y,int x1,int y1)
{
    int dx=x1>x?x1-x:x-x1,sx=x<x1?1:-1,dy=y1>y?y-y1:y1-y,sy=y<y1?1:-1,err=dx+dy;
    for(;;){rect(f,x,y,1,1,true);if(x==x1&&y==y1)break;int e=2*err;if(e>=dy){err+=dy;x+=sx;}if(e<=dx){err+=dx;y+=sy;}}
}
static void circle(uint8_t *f,int cx,int cy,int r)
{
    int x=r,y=0,err=1-r;
    while(x>=y){
        rect(f,cx+x,cy+y,1,1,true);rect(f,cx+y,cy+x,1,1,true);rect(f,cx-y,cy+x,1,1,true);rect(f,cx-x,cy+y,1,1,true);
        rect(f,cx-x,cy-y,1,1,true);rect(f,cx-y,cy-x,1,1,true);rect(f,cx+y,cy-x,1,1,true);rect(f,cx+x,cy-y,1,1,true);
        ++y;if(err<0)err+=2*y+1;else{--x;err+=2*(y-x)+1;}
    }
}
static bool centered(uint8_t *f,int x,int y,int width,const char *label,const home_font_t *font,bool black)
{
    int actual=text_width(label,font);
    if(actual>width)return fit(f,x,y,label,font,black,width);
    return text(f,x+(width-actual)/2,y,label,font,black,width);
}
static void value_text(char *out,size_t cap,const home_live_device_t *d,double value)
{ snprintf(out,cap,"%g%s%s",value,d->unit[0]?" ":"",d->unit); }
static void device_icon(uint8_t *f,home_device_icon_t icon)
{
    const int x=166,y=184;
    if(icon==HOME_ICON_LIGHT){
        circle(f,x,y,55);circle(f,x,y,56);
        line(f,136,232,136,258);line(f,136,258,196,258);line(f,196,258,196,232);
        line(f,145,269,187,269);line(f,156,258,156,269);line(f,176,258,176,269);
        const int rays[8][4]={{0,-73,0,-85},{51,-51,60,-60},{73,0,85,0},{51,51,60,60},
            {-51,51,-60,60},{-73,0,-85,0},{-51,-51,-60,-60},{0,0,0,0}};
        for(unsigned i=0;i<7;++i)line(f,x+rays[i][0],y+rays[i][1],x+rays[i][2],y+rays[i][3]);
    }else if(icon==HOME_ICON_HEAT){
        rounded(f,148,112,36,124,18,false);circle(f,166,238,32);circle(f,166,238,31);
        rect(f,162,156,8,82,true);circle(f,166,238,14);
        for(int i=0;i<4;++i)line(f,196,134+i*24,210,134+i*24);
    }else if(icon==HOME_ICON_CURTAIN){
        rounded(f,98,122,136,140,8,false);line(f,90,116,242,116);line(f,166,123,166,261);
        for(int i=0;i<4;++i){line(f,112+i*12,124,112+i*12,250);line(f,184+i*12,124,184+i*12,250);}
    }else{
        circle(f,x,y+8,60);circle(f,x,y+8,59);rect(f,144,118,44,60,false);
        rect(f,164,116,4,80,true);line(f,122,151,111,163);line(f,210,151,221,163);
    }
}
static bool account_page(const home_ui_t *u)
{ return u->page==HOME_PAGE_ACCOUNT; }
static const char *subtitle(const home_ui_t *u)
{
    const home_live_data_t *l=u->live;
    if(l->pending)return "Отправка команды...";
    if(!l->online)return l->loaded?"Сохранённые данные · нет сети":"Нет домашней сети";
    if(!l->time_ready)return "Синхронизация времени для HTTPS";
    if(l->message[0]&&strcmp(l->message,"Устройства обновлены")&&strcmp(l->message,"Аккаунт подключён"))return l->message;
    if(u->page==HOME_PAGE_HOME)return "Быстрые действия";
    if(u->page==HOME_PAGE_ROOMS)return "Выберите комнату";
    if(u->page==HOME_PAGE_SCENES)return "Сценарии вашего дома";
    if(u->page==HOME_PAGE_ROOM)return "Устройства комнаты";
    return "";
}
static bool live_render(uint8_t *f,size_t length,const home_ui_t *u)
{
    if(length!=HOME_UI_BYTES||!u->live)return false;
    const home_live_data_t *l=u->live;memset(f,0,length);bool ok=true;
    bool device=u->page==HOME_PAGE_DEVICE&&u->device<l->device_count;
    bool detail=device||account_page(u)||u->page==HOME_PAGE_NETWORK;
    const char *title=u->page==HOME_PAGE_HOME?"Мой дом":u->page==HOME_PAGE_ROOMS?"Комнаты":u->page==HOME_PAGE_SCENES?"Сценарии":
        u->page==HOME_PAGE_ROOM&&u->room<l->room_count?l->rooms[u->room].name:
        device?l->devices[u->device].name:account_page(u)?"Яндекс Дом":u->page==HOME_PAGE_NETWORK?"Подключение":"Настройки";
    ok&=fit(f,detail?148:24,8,title,&home_font_28,true,detail?430:450);
    if(device)ok&=fit(f,610,22,l->devices[u->device].available?l->devices[u->device].state:"Недоступно",&home_font_18,true,166);
    else if(!detail){int x=u->page==HOME_PAGE_SETTINGS||u->page==HOME_PAGE_ROOM?560:610;ok&=text(f,x,20,l->online?"Wi-Fi":"Нет сети",&home_font_18,true,104);}
    rect(f,24,detail?70:64,752,1,true);rect(f,0,420,800,1,true);
    bool list=u->page==HOME_PAGE_HOME||u->page==HOME_PAGE_ROOM||u->page==HOME_PAGE_ROOMS||u->page==HOME_PAGE_SCENES;
    unsigned total=home_ui_tile_count(u);bool pages=list&&total>6;
    if(list){
        ok&=fit(f,24,72,subtitle(u),&home_font_18,true,pages?520:752);
        if(pages){char page[32];snprintf(page,sizeof(page),"%u / %u",u->offset/6+1,(total+5)/6);ok&=centered(f,554,73,60,page,&home_font_18,true);}
    }
    if(u->page==HOME_PAGE_SETTINGS){
        const char *labels[]={"Wi-Fi","Яндекс Дом","Тема","Картина"};
        const char *values[]={l->ip[0]?l->ip:"Нет сети",l->authorized?"Подключён":"Требуется вход",u->dark_theme?"Тёмная":"Светлая","Возврат через 45 секунд"};
        for(unsigned i=0;i<4;++i){
            int y=80+80*(int)i;
            ok&=text(f,24,y+3,labels[i],&home_font_20_bold,true,300);
            ok&=fit(f,356,y+8,values[i],&home_font_18,true,252);
            rect(f,24,y+72,752,1,true);
        }
    }else if(account_page(u)){
        ok&=fit(f,24,82,subtitle(u),&home_font_18,true,752);
        if(l->waiting){
            bool qr=qr_code(f,532,132,240,l->url);
            ok&=text(f,24,160,qr?"Наведите камеру на QR":"Откройте ссылку на телефоне",&home_font_20,true,480);
            ok&=text(f,24,202,"Введите код на странице Яндекса:",&home_font_20,true,480);
            char code[96];snprintf(code,sizeof(code),"Код: %s",l->code);ok&=fit(f,24,244,code,&home_font_28,true,480);
            ok&=fit(f,24,286,l->url,&home_font_18,true,480);
        }else{
            ok&=text(f,24,150,l->authorized?"Аккаунт подключён":"Подключите свой умный дом",&home_font_28,true,752);
            ok&=text(f,24,214,"Вход подтверждается на телефоне",&home_font_20,true,752);
            if(l->authorized)ok&=text(f,24,264,"Состояния и команды приходят из Яндекса",&home_font_18,true,752);
        }
    }else if(u->page==HOME_PAGE_NETWORK){
        ok&=text(f,24,106,"Домашняя сеть",&home_font_20_bold,true,752);
        ok&=fit(f,356,108,l->ip[0]?l->ip:"Нет подключения",&home_font_20,true,420);
        ok&=text(f,24,174,"Загрузка картины",&home_font_20_bold,true,752);
        char address[96];snprintf(address,sizeof(address),"http://%s",l->ip[0]?l->ip:"192.168.4.1");
        ok&=fit(f,356,176,address,&home_font_20,true,420);
        ok&=text(f,24,252,"Через точку доступа панели:",&home_font_20,true,752);
        ok&=text(f,24,292,"http://192.168.4.1",&home_font_28,true,752);
        ok&=text(f,24,366,"Откройте адрес в браузере телефона",&home_font_18,true,752);
    }else if(!l->authorized){
        ok&=text(f,80,152,"Подключите свой умный дом",&home_font_28,true,650);
        ok&=text(f,80,310,"Вход подтверждается на телефоне",&home_font_20,true,650);
    }else if(list){
        home_ui_tile_t tiles[6];unsigned count=home_ui_tiles(u,tiles);
        for(unsigned i=0;i<count;++i){
            unsigned slot=tiles[i].slot;int x=24+256*(int)(i%3),y=110+150*(int)(i/3);
            const char *name,*state;char info[96];
            if(tiles[i].kind==HOME_TILE_DEVICE){name=l->devices[slot].name;state=l->devices[slot].available?l->devices[slot].state:"Недоступно";}
            else if(tiles[i].kind==HOME_TILE_SCENE){name=l->scenes[slot].name;state="Сценарий";}
            else{
                unsigned devices=0;for(unsigned d=0;d<l->device_count;++d)if(l->devices[d].room==slot)++devices;
                snprintf(info,sizeof(info),"Устройств: %u",devices);name=l->rooms[slot].name;state=info;
            }
            rounded(f,x,y,240,138,10,false);
            ok&=fit(f,x+14,y+3,name,&home_font_20_bold,true,212);
            ok&=fit(f,x+14,y+33,state,&home_font_18,true,212);
        }
        if(!count)ok&=text(f,48,170,!l->loaded?"Загружаем устройства...":u->page==HOME_PAGE_ROOMS?"Комнаты не найдены":u->page==HOME_PAGE_SCENES?"Сценарии не найдены":"В этой комнате нет устройств",&home_font_20,true,704);
    }else if(device){
        const home_live_device_t *d=&l->devices[u->device];device_icon(f,d->icon);
        ok&=centered(f,24,286,292,d->room<l->room_count?l->rooms[d->room].name:"Без комнаты",&home_font_20_bold,true);
        const char *state=!d->available?"Недоступно":subtitle(u);
        if(state&&state[0])ok&=fit(f,346,76,state,&home_font_18,true,430);
        if(d->has_range){
            char value[64];ok&=fit(f,346,96,d->range_name,&home_font_20_bold,true,280);
            snprintf(value,sizeof(value),"Шаг %g%s%s",d->step,d->unit[0]?" ":"",d->unit);ok&=fit(f,646,101,value,&home_font_18,true,130);
            value_text(value,sizeof(value),d,u->live_draft);ok&=centered(f,346,128,430,value,&home_font_48,true);
            char actual[96];value_text(value,sizeof(value),d,d->value);snprintf(actual,sizeof(actual),"На устройстве: %s",value);
            ok&=centered(f,436,230,250,actual,&home_font_18,true);
        }else{
            ok&=text(f,346,112,d->has_toggle?"Управление питанием":"Состояние устройства",&home_font_20_bold,true,430);
            ok&=fit(f,346,176,d->state,&home_font_28,true,430);
            ok&=text(f,346,280,d->has_toggle?"Выберите действие слева":"Доступен просмотр показаний",&home_font_18,true,430);
        }
    }
    home_ui_control_t controls[24];unsigned n=home_ui_controls(u,controls,24);
    for(unsigned i=0;i<n;++i){
        const home_ui_control_t *c=&controls[i];char label[48];const char *caption=c->label;
        if(c->preset&&device){value_text(label,sizeof(label),&l->devices[u->device],home_ui_preset_value(&l->devices[u->device],c->preset-1));caption=label;}
        if(!caption)continue;
        bool fill=c->filled&&c->action!=0;rounded(f,c->x,c->y,c->w,c->h,c->w<80?7:6,fill);
        const home_font_t *font=c->action?&home_font_20:&home_font_18;
        ok&=centered(f,c->x+10,c->y+(c->h-font->height)/2,c->w-20,caption,font,!fill);
    }
    if(u->dark_theme){
        for(size_t i=0;i<length;++i)f[i]^=0xff;
        if(account_page(u)&&l->waiting)qr_code(f,532,132,240,l->url);
    }
    return ok;
}

bool home_ui_render(uint8_t *f,size_t length,const home_ui_t *u)
{
    if(!f||!u||length!=HOME_UI_BYTES||u->page>HOME_PAGE_NETWORK)return false;
    if(u->live)return live_render(f,length,u);
    if(u->room>2||u->device>3)return false;
    memset(f,0,length);bool ok=true;
    static const char *rooms[]={"Гостиная","Кухня","Спальня"};
    const char *title=u->page==HOME_PAGE_HOME?"Мой дом":u->page==HOME_PAGE_ROOMS?"Комнаты":
        u->page==HOME_PAGE_ROOM?rooms[u->room]:u->page==HOME_PAGE_SCENES?"Сценарии":
        u->page==HOME_PAGE_DEVICE?device_name(u->device):"Настройки";
    ok&=text(f,24,8,title,&home_font_28,true,450);
    ok&=text(f,520,20,"ДЕМО",&home_font_18,true,90);
    rect(f,24,64,752,1,true);rect(f,0,420,800,2,true);
    const char *subtitle=u->page==HOME_PAGE_HOME?"Быстрые действия":u->page==HOME_PAGE_ROOMS?"Выберите комнату":
        u->page==HOME_PAGE_SCENES?"Демонстрационные сценарии":u->page==HOME_PAGE_DEVICE?"Демонстрационное управление":
        u->page==HOME_PAGE_ROOM?"Устройства комнаты":"Параметры панели";
    ok&=text(f,24,72,subtitle,&home_font_18,true,752);
    if(u->page==HOME_PAGE_HOME){
        for(unsigned d=0;d<4;++d)ok&=device_card(f,u,d,24+256*(int)(d%3),104+150*(int)(d/3));
        const char *scenes[]={"Я дома","Уйти из дома"};
        for(unsigned i=0;i<2;++i){
            int x=280+256*(int)i;box(f,x,254,240,138);
            ok&=text(f,x+14,262,scenes[i],&home_font_20,true,212);
            ok&=text(f,x+14,294,u->last_scene==i+1?"Демо: выбран":"Сценарий",&home_font_18,true,212);
        }
    }else if(u->page==HOME_PAGE_ROOMS){
        ok&=text(f,24,246,"Набор комнат и устройств демонстрационный.",&home_font_20,true,752);
    }else if(u->page==HOME_PAGE_ROOM){
        static const unsigned devices[3][2]={{0,3},{1,4},{2,4}};
        for(unsigned i=0;i<2;++i)if(devices[u->room][i]<4)ok&=device_card(f,u,devices[u->room][i],24+256*(int)i,116);
    }else if(u->page==HOME_PAGE_SCENES){
        box(f,24,116,368,202);box(f,408,116,368,202);
        ok&=text(f,40,136,"Я дома",&home_font_28,true,328);
        ok&=text(f,424,136,"Уйти из дома",&home_font_28,true,328);
        ok&=text(f,40,188,"Свет, тепло, открытые шторы",&home_font_18,true,328);
        ok&=text(f,424,188,"Выключить свет, закрыть шторы",&home_font_18,true,328);
    }else if(u->page==HOME_PAGE_DEVICE){
        char value[64];device_state(u,u->device,value,sizeof(value));
        ok&=text(f,48,134,value,&home_font_28,true,350);
        ok&=text(f,48,218,u->device<2?"Яркость":u->device==2?"Температура":"Открытие штор",&home_font_20,true,360);
        snprintf(value,sizeof(value),u->device==2?"%u °C":"%u%%",(unsigned)u->draft);
        int width=text_width(value,&home_font_28);
        ok&=text(f,240-width/2,292,value,&home_font_28,true,176);
        ok&=text(f,48,366,"Изменения только в демонстрационной модели",&home_font_18,true,704);
    }else if(u->page==HOME_PAGE_SETTINGS){
        ok&=text(f,24,122,"Возврат к картине: 45 секунд",&home_font_20,true,752);
        ok&=text(f,24,180,"Умный дом: демонстрационный режим",&home_font_20,true,752);
        ok&=text(f,24,238,"Загрузка фото: http://192.168.4.1",&home_font_20,true,752);
        ok&=text(f,24,296,"Первое касание открывает меню",&home_font_20,true,752);
    }
    if(u->page!=HOME_PAGE_DEVICE)ok&=text(f,24,392,"Демонстрация · изменения только на этой панели",&home_font_18,true,752);
    home_ui_control_t controls[20];unsigned count=home_ui_controls(u,controls,20);
    for(unsigned i=0;i<count;++i){
        const home_ui_control_t *c=&controls[i];if(!c->label)continue;
        rect(f,c->x,c->y,c->w,c->h,c->filled);box(f,c->x,c->y,c->w,c->h);
        int width=text_width(c->label,&home_font_20);
        ok&=text(f,c->x+(c->w-width)/2,c->y+(c->h-home_font_20.height)/2,c->label,&home_font_20,!c->filled,c->w-12);
    }
    return ok;
}

bool home_ui_render_empty_art(uint8_t *f,size_t length)
{
    if(!f||length!=HOME_UI_BYTES)return false;
    memset(f,0,length);
    bool ok=text(f,64,146,"Выберите свою картину",&home_font_28,true,672);
    ok&=text(f,64,212,"Подключитесь к Wi-Fi панели",&home_font_20,true,672);
    ok&=text(f,64,254,"Откройте http://192.168.4.1 и загрузите фото",&home_font_20,true,672);
    ok&=text(f,64,350,"Коснитесь экрана, чтобы открыть меню",&home_font_18,true,672);
    return ok;
}

bool home_ui_render_panel(uint8_t *frame,size_t length,const home_ui_t *ui)
{
    if(!home_ui_render(frame,length,ui))return false;
    for(size_t i=0;i<length;++i)frame[i]^=0xff;
    return true;
}
bool home_ui_render_empty_art_panel(uint8_t *frame,size_t length)
{
    if(!home_ui_render_empty_art(frame,length))return false;
    for(size_t i=0;i<length;++i)frame[i]^=0xff;
    return true;
}
