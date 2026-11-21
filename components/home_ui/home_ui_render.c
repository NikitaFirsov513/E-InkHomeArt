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
static bool live_render(uint8_t *f,size_t length,const home_ui_t *u)
{
    if(length!=HOME_UI_BYTES||!u->live)return false;
    const home_live_data_t *l=u->live;memset(f,0,length);bool ok=true;
    const char *title=u->page==HOME_PAGE_HOME?"Мой дом":u->page==HOME_PAGE_ROOMS?"Комнаты":u->page==HOME_PAGE_SCENES?"Сценарии":
        u->page==HOME_PAGE_ROOM&&u->room<l->room_count?l->rooms[u->room].name:
        u->page==HOME_PAGE_DEVICE&&u->device<l->device_count?l->devices[u->device].name:"Настройки";
    ok&=fit(f,24,8,title,&home_font_28,true,450);
    ok&=text(f,510,20,l->online?"Wi-Fi":"OFFLINE",&home_font_18,true,106);
    rect(f,24,64,752,1,true);rect(f,0,420,800,2,true);
    const char *status=l->pending?"Отправка команды...":!l->online?"Нет домашней сети":!l->time_ready?"Синхронизация времени для HTTPS":l->message;
    ok&=fit(f,24,72,status,&home_font_18,true,752);
    if(u->page==HOME_PAGE_SETTINGS){
        char line[128];snprintf(line,sizeof(line),"Домашняя сеть: %s",l->ip[0]?l->ip:"нет подключения");
        ok&=fit(f,24,114,line,&home_font_20,true,752);
        if(l->waiting){
            bool qr=qr_code(f,532,132,240,l->url);
            ok&=text(f,24,160,qr?"Наведите камеру на QR":"Откройте ссылку на телефоне",&home_font_20,true,480);
            ok&=text(f,24,202,"Введите код на странице Яндекса:",&home_font_20,true,480);
            snprintf(line,sizeof(line),"Код: %s",l->code);ok&=fit(f,24,244,line,&home_font_28,true,480);
            ok&=fit(f,24,286,l->url,&home_font_18,true,480);
        }else{
            ok&=text(f,24,172,l->authorized?"Яндекс: аккаунт подключён":"Яндекс: требуется вход",&home_font_20,true,752);
            ok&=text(f,24,228,"Фото: http://192.168.4.1",&home_font_20,true,752);
            ok&=text(f,24,278,"Возврат к картине через 45 секунд",&home_font_20,true,752);
        }
    }else if(!l->authorized){
        ok&=text(f,80,152,"Подключите свой умный дом",&home_font_28,true,650);
        ok&=text(f,80,310,"Вход подтверждается на телефоне",&home_font_20,true,650);
    }else if(u->page==HOME_PAGE_HOME||u->page==HOME_PAGE_ROOM){
        unsigned slots[6],n=home_live_slots(u,slots);
        for(unsigned i=0;i<n;++i){
            const home_live_device_t *d=&l->devices[slots[i]];
            int x=24+256*(int)(i%3),y=104+140*(int)(i/3);box(f,x,y,240,130);
            ok&=fit(f,x+12,y+7,d->name,&home_font_20,true,216);
            ok&=fit(f,x+12,y+39,d->available?d->state:"Недоступно",&home_font_18,true,216);
        }
        if(!n)ok&=text(f,48,170,l->loaded?"В этом разделе нет устройств":"Загружаем устройства...",&home_font_20,true,704);
    }else if(u->page==HOME_PAGE_DEVICE&&u->device<l->device_count){
        const home_live_device_t *d=&l->devices[u->device];
        ok&=fit(f,48,128,d->available?d->state:"Недоступно",&home_font_20,true,350);
        if(d->has_range){
            char value[48];snprintf(value,sizeof(value),"%g",u->live_draft);
            ok&=fit(f,48,218,d->range_name,&home_font_20,true,704);
            ok&=fit(f,176,292,value,&home_font_28,true,144);
        }else ok&=text(f,48,242,d->has_toggle?"Управление питанием":"Пока только просмотр",&home_font_20,true,704);
        ok&=text(f,48,366,"Состояние подтверждается ответом Яндекса",&home_font_18,true,704);
    }else if(u->page==HOME_PAGE_ROOMS||u->page==HOME_PAGE_SCENES){
        unsigned count=u->page==HOME_PAGE_ROOMS?l->room_count:l->scene_count;
        if(!count)ok&=text(f,48,170,u->page==HOME_PAGE_ROOMS?"Комнаты не найдены":"Сценарии не найдены",&home_font_20,true,704);
    }
    if(u->page!=HOME_PAGE_DEVICE){
        char count[100];snprintf(count,sizeof(count),"Устройств: %u   Комнат: %u   Сценариев: %u%s",l->device_count,l->room_count,l->scene_count,l->truncated?" +":"");
        ok&=fit(f,156,388,count,&home_font_18,true,488);
    }
    home_ui_control_t controls[24];unsigned n=home_ui_controls(u,controls,24);
    for(unsigned i=0;i<n;++i){
        const home_ui_control_t *c=&controls[i];if(!c->label)continue;
        rect(f,c->x,c->y,c->w,c->h,c->filled);box(f,c->x,c->y,c->w,c->h);
        ok&=fit(f,c->x+10,c->y+(c->h-home_font_20.height)/2,c->label,&home_font_20,!c->filled,c->w-20);
    }
    if(u->dark_theme){
        for(size_t i=0;i<length;++i)f[i]^=0xff;
        /* Login QR always keeps standard black modules on a white quiet zone. */
        if(u->page==HOME_PAGE_SETTINGS&&l->waiting)qr_code(f,532,132,240,l->url);
    }
    return ok;
}

bool home_ui_render(uint8_t *f,size_t length,const home_ui_t *u)
{
    if(!f||!u||length!=HOME_UI_BYTES||u->page>HOME_PAGE_SETTINGS)return false;
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
