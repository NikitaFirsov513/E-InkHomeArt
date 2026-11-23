#include "home_ui.h"
#include "home_ui_layout.h"
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
static unsigned utf8(const char **s)
{
    const unsigned char *p=(const unsigned char *)*s;
    unsigned code=*p++,extra=0;
    if(code>=0xc2&&code<=0xdf){code&=31;extra=1;}
    else if(code>=0xe0&&code<=0xef){code&=15;extra=2;}
    else if(code>=0xf0&&code<=0xf4){code&=7;extra=3;}
    else if(code>=128)code='?';
    for(unsigned i=0;i<extra;++i){
        if((*p&0xc0)!=0x80){code='?';break;}
        code=(code<<6)|(*p++&63);
    }
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
#ifdef HOME_UI_LAYOUT_CHECK
    extern void home_ui_check_text(int,int,int,int);
    home_ui_check_text(x,y,text_width(s,font),font->height);
#endif
    while(*s){
        const home_font_glyph_t *g=glyph(font,utf8(&s));if(!g)return false;
        unsigned stride=(g->width+7)/8;
        for(unsigned yy=0;yy<font->height;++yy)for(unsigned xx=0;xx<g->width;++xx)
            if(font->bits[g->offset+yy*stride+xx/8]&(128>>(xx&7)))rect(f,x+(int)xx,y+(int)yy,1,1,black);
        x+=g->width;
    }
    return true;
}
/* Clip external names instead of rejecting a complete frame. */
static bool fit(uint8_t *f,int x,int y,const char *s,const home_font_t *font,bool black,int width)
{
    char clipped[256];size_t n=0;
    while(*s&&n+5<sizeof(clipped)){
        const char *start=s;unsigned cp=utf8(&s);const home_font_glyph_t *g=glyph(font,cp);
        if(!g)g=glyph(font,'?');
        if(!g)continue;
        if(g->code==cp){size_t bytes=(size_t)(s-start);memcpy(clipped+n,start,bytes);n+=bytes;}
        else clipped[n++]='?';
    }
    clipped[n]=0;
    if(text_width(clipped,font)<=width)return text(f,x,y,clipped,font,black,width);
    int used=0,dots=text_width("...",font);const char *cursor=clipped;size_t keep=0;
    while(*cursor){
        const char *start=cursor;const home_font_glyph_t *g=glyph(font,utf8(&cursor));
        if(used+g->width+dots>width)break;
        used+=g->width;keep+=(size_t)(cursor-start);
    }
    if(dots<=width){memcpy(clipped+keep,"...",4);}else clipped[keep]=0;
    return text(f,x,y,clipped,font,black,width);
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
/* Integer-pixel settings gear, independent of the text font. */
static void settings_icon(uint8_t *f,int cx,int cy)
{
    for(int y=-11;y<=11;++y)for(int x=-11;x<=11;++x){int r=x*x+y*y;if(r<=121&&r>=25)rect(f,cx+x,cy+y,1,1,true);}
    rect(f,cx-3,cy-15,6,8,true);rect(f,cx-3,cy+8,6,8,true);
    rect(f,cx-15,cy-3,8,6,true);rect(f,cx+8,cy-3,8,6,true);
    rect(f,cx-12,cy-12,6,6,true);rect(f,cx+7,cy-12,6,6,true);
    rect(f,cx-12,cy+7,6,6,true);rect(f,cx+7,cy+7,6,6,true);
}
static bool centered(uint8_t *f,int x,int y,int width,const char *label,const home_font_t *font,bool black)
{
    int actual=text_width(label,font);
    if(actual>width)return fit(f,x,y,label,font,black,width);
    return text(f,x+(width-actual)/2,y,label,font,black,width);
}
static void value_text(char *out,size_t cap,const home_live_device_t *d,double value)
{ snprintf(out,cap,"%g%s%s",value,d->unit[0]?" ":"",d->unit); }
static const char *subtitle(const home_ui_t *u)
{
    const home_live_data_t *l=u->live;
    if(!l)return "Демонстрация · изменения только на панели";
    if(u->page==HOME_PAGE_NUMBER)return u->number_error?"Проверьте число, допустимый диапазон и шаг":"Введите число и нажмите Готово";
    if(l->pending)return "Ожидаем подтверждение команды";
    if(!l->online)return l->loaded?"Нет сети · показаны последние данные":"Нет домашней сети";
    if(!l->time_ready)return "Синхронизация времени для HTTPS";
    if(l->command_failed)return "Результат команды не подтверждён · проверьте состояние";
    if(home_ui_capabilities(u)&&l->devices[u->device].controls_truncated)return "Показана часть настроек: достигнут лимит каталога";
    if(l->truncated)return "Показана часть каталога: достигнут лимит панели";
    if(l->message[0]&&strcmp(l->message,"Устройства обновлены")&&strcmp(l->message,"Аккаунт подключён"))return l->message;
    if(u->page==HOME_PAGE_HOME)return "Устройства вашего дома";
    if(u->page==HOME_PAGE_ROOMS)return "Выберите комнату";
    if(u->page==HOME_PAGE_SCENES)return "Сценарии вашего дома";
    if(u->page==HOME_PAGE_ROOM)return "Устройства комнаты";
    if(u->page==HOME_PAGE_DEVICE)return home_ui_capabilities(u)?"Настройки и показания устройства":"Новое значение отправится после применения";
    if(u->page==HOME_PAGE_CONTROL)return "Изменения отправляются после применения";
    if(u->page==HOME_PAGE_SETTINGS)return "Параметры панели";
    return "";
}
/* Two fixed header lines for a long device name, with UTF-8-safe clipping. */
static bool detail_title(uint8_t *f,const char *title,int width)
{
    if(text_width(title,&home_font_28)<=width)return text(f,160,8,title,&home_font_28,true,width);
    char first[256];size_t n=0,split=0;int used=0;const char *s=title;
    while(*s&&n+5<sizeof(first)){
        const char *start=s;unsigned cp=utf8(&s);const home_font_glyph_t *g=glyph(&home_font_20_bold,cp);
        if(!g)g=glyph(&home_font_20_bold,'?');
        if(used+g->width>width)break;
        size_t bytes=(size_t)(s-start);memcpy(first+n,start,bytes);n+=bytes;used+=g->width;
        if(cp==' ')split=n;
    }
    if(!n)return fit(f,160,4,title,&home_font_20_bold,true,width);
    if(*s&&split)n=split;
    first[n]=0;const char *rest=title+n;
    while(*rest==' ')++rest;
    bool ok=fit(f,160,4,first,&home_font_20_bold,true,width);
    return fit(f,160,32,rest,&home_font_20_bold,true,width)&&ok;
}
static bool header(uint8_t *f,const home_ui_t *u,const char *title)
{
    bool detail=u->page==HOME_PAGE_DEVICE||u->page==HOME_PAGE_CONTROL||u->page==HOME_PAGE_NUMBER||u->page==HOME_PAGE_SETTINGS||u->page==HOME_PAGE_ACCOUNT||u->page==HOME_PAGE_NETWORK;
    bool back=detail||u->page==HOME_PAGE_ROOM;
    bool device_title=u->page==HOME_PAGE_DEVICE||u->page==HOME_PAGE_CONTROL;
    bool all=u->page==HOME_PAGE_CONTROL&&home_ui_capabilities(u)&&u->live->devices[u->device].control_count>1;
    int width=all||(u->page==HOME_PAGE_DEVICE&&!home_ui_capabilities(u))?448:detail?616:back?304:440;
    bool ok=device_title?detail_title(f,title,width):fit(f,back?160:24,8,title,&home_font_28,true,width);
    rect(f,24,UI_HEADER_LINE,752,1,true);rect(f,0,UI_FOOTER,800,1,true);
    unsigned total=u->live?home_ui_tile_count(u):0;
    bool overview=u->page==HOME_PAGE_DEVICE&&home_ui_capabilities(u);
    if(overview)total=u->live->devices[u->device].control_count;
    bool list=u->page==HOME_PAGE_HOME||u->page==HOME_PAGE_ROOMS||u->page==HOME_PAGE_ROOM||u->page==HOME_PAGE_SCENES;
    ok&=fit(f,24,UI_STATUS_Y,subtitle(u),&home_font_18,true,list||overview?640:752);
    if((list||overview)&&total){
        char page[32];snprintf(page,sizeof(page),"%u / %u",(overview?u->control_offset:u->offset)/HOME_UI_PAGE_SIZE+1,(total+HOME_UI_PAGE_SIZE-1)/HOME_UI_PAGE_SIZE);
        ok&=centered(f,688,UI_STATUS_Y,88,page,&home_font_18,true);
    }
    return ok;
}
static bool draw_device_body(uint8_t *f,const home_ui_t *u,const home_live_device_t *d,double draft)
{
    bool ok=true;
    if(d->has_range){
        char value[64],actual[120];
        ok&=fit(f,24,112,d->range_name,&home_font_28,true,472);
        snprintf(value,sizeof(value),"Шаг: %g%s%s",d->step,d->unit[0]?" ":"",d->unit);
        ok&=fit(f,552,120,value,&home_font_18,true,224);
        value_text(value,sizeof(value),d,draft);
        /* The numeric font is intentionally limited. Other unit alphabets fall back. */
        const home_font_t *font=text_width(value,&home_font_48)<=240?&home_font_48:&home_font_28;
        ok&=centered(f,280,UI_VALUE_Y,240,value,font,true);
        value_text(value,sizeof(value),d,d->value);
        snprintf(actual,sizeof(actual),"%s: %s",u->live&&(!u->live->online||home_ui_device_failed(u))?"Последнее значение":"На устройстве",value);
        ok&=centered(f,24,260,752,actual,&home_font_20,true);
    }else{
        ok&=fit(f,24,128,d->has_toggle?"Управление питанием":"Состояние устройства",&home_font_28,true,752);
        ok&=fit(f,24,192,d->available?d->state:"Недоступно",&home_font_28,true,752);
        ok&=fit(f,24,296,d->has_toggle?"Кнопка питания находится справа вверху":"Доступен просмотр показаний",&home_font_20,true,752);
    }
    return ok;
}
static void capability_value(char *out,size_t cap,const home_live_control_t *c,double value)
{snprintf(out,cap,"%g%s%s",value,c->unit[0]?" ":"",c->unit);}
static bool capability_body(uint8_t *f,const home_ui_t *u)
{
    const home_live_data_t *l=u->live;bool ok=true;
    if(u->page==HOME_PAGE_NUMBER){
        char value[96];snprintf(value,sizeof(value),"Значение: %s",u->number_text);
        bool ok=fit(f,24,112,value,&home_font_20_bold,true,752);
        const home_live_control_t *c=home_ui_selected_control(u);char limits[160];
        if(c){
            if(u->number_channel<3)snprintf(limits,sizeof(limits),"Допустимо: 0 .. %u · целое число",c->kind==HOME_CONTROL_RGB?255:u->number_channel==0?360:100);
            else if(c->relative_only)snprintf(limits,sizeof(limits),"Изменение со знаком · шаг %g%s%s",c->step,c->unit[0]?" ":"",c->unit);
            else if(c->has_minimum&&c->has_maximum)snprintf(limits,sizeof(limits),"Допустимо: %g .. %g%s%s · шаг %g",c->minimum,c->maximum,c->unit[0]?" ":"",c->unit,c->step);
            else if(c->has_minimum)snprintf(limits,sizeof(limits),"Не ниже %g%s%s · шаг %g",c->minimum,c->unit[0]?" ":"",c->unit,c->step);
            else if(c->has_maximum)snprintf(limits,sizeof(limits),"Не выше %g%s%s · шаг %g",c->maximum,c->unit[0]?" ":"",c->unit,c->step);
            else snprintf(limits,sizeof(limits),"Шаг: %g%s%s",c->step,c->unit[0]?" ":"",c->unit);
            ok&=fit(f,24,416,limits,&home_font_18,true,752);
        }
        return ok;
    }
    if(u->page==HOME_PAGE_DEVICE){
        const home_live_device_t *d=&l->devices[u->device];unsigned end=u->control_offset+6;
        if(end>d->control_count)end=d->control_count;
        for(unsigned i=u->control_offset;i<end;++i){
            const home_live_control_t *c=&l->controls[d->control_start+i];unsigned tile=i-u->control_offset;
            int x=home_ui_tile_x(tile),y=home_ui_tile_y(tile);rounded(f,x,y,240,128,8,false);
            ok&=fit(f,x+12,y+2,c->name,&home_font_20_bold,true,216);
            ok&=fit(f,x+12,y+32,c->state,&home_font_18,true,216);
        }
        return ok;
    }
    const home_live_control_t *c=home_ui_selected_control(u);if(!c)return fit(f,24,144,"Настройка больше недоступна",&home_font_28,true,752);
    if(c->kind==HOME_CONTROL_RGB||c->kind==HOME_CONTROL_HSV){
        static const char *rgb[]={"Красный (R)","Зелёный (G)","Синий (B)"};
        static const char *hsv[]={"Цветовой тон (H)","Насыщенность (S)","Яркость (V)"};
        for(unsigned i=0;i<3;++i){
            int y=136+80*(int)i;char value[32];snprintf(value,sizeof(value),"%g",u->color_draft[i]);
            ok&=fit(f,24,y+14,c->kind==HOME_CONTROL_RGB?rgb[i]:hsv[i],&home_font_20_bold,true,296);
            rounded(f,416,y,208,56,8,false);ok&=centered(f,424,y+8,192,value,&home_font_28,true);
        }
        char state[160];snprintf(state,sizeof(state),"На устройстве: %s · нажмите число для ввода",c->state);
        ok&=fit(f,24,366,state,&home_font_18,true,752);
        return ok;
    }
    ok&=fit(f,24,112,c->name,&home_font_28,true,c->kind==HOME_CONTROL_RANGE?472:752);
    if(c->kind==HOME_CONTROL_RANGE){
        char value[80],state[128];snprintf(value,sizeof(value),"Шаг: %g%s%s",c->step,c->unit[0]?" ":"",c->unit);
        ok&=fit(f,552,120,value,&home_font_18,true,224);
        capability_value(value,sizeof(value),c,u->live_draft);
        const home_font_t *font=text_width(value,&home_font_48)<=224?&home_font_48:&home_font_28;
        rounded(f,280,165,240,72,8,false);ok&=centered(f,288,165,224,value,font,true);
        snprintf(state,sizeof(state),"%s: %s",c->relative_only?"Текущее значение":"На устройстве",c->state);
        ok&=centered(f,24,260,752,state,&home_font_20,true);
        ok&=fit(f,24,366,c->relative_only?"Указано изменение, а не итог · нажмите число для ввода":"Нажмите число для прямого ввода",&home_font_18,true,752);
    }else if(c->kind==HOME_CONTROL_MODE){
        char state[128];snprintf(state,sizeof(state),"На устройстве: %s",c->state);
        ok&=fit(f,24,158,state,&home_font_18,true,752);
        char page[96];snprintf(page,sizeof(page),"Варианты: %u / %u",u->option_offset/6+1,(c->option_count+5)/6);
        ok&=fit(f,24,362,page,&home_font_18,true,752);
    }else if(c->kind==HOME_CONTROL_TOGGLE){
        ok&=fit(f,24,176,c->state,&home_font_28,true,752);
        ok&=fit(f,24,350,c->momentary?"Команда без обратной связи · состояние неизвестно":"Выберите состояние, затем нажмите Применить",&home_font_18,true,752);
    }else{
        ok&=fit(f,24,184,c->state,&home_font_28,true,752);
        ok&=fit(f,24,296,c->kind==HOME_CONTROL_READING?"Показание датчика · доступно только чтение":"Это умение не поддерживается экраном панели",&home_font_20,true,752);
    }
    return ok;
}
static bool draw_controls(uint8_t *f,const home_ui_t *u,const home_live_device_t *device)
{
    home_ui_control_t controls[24];unsigned n=home_ui_controls(u,controls,24);bool ok=true;
    for(unsigned i=0;i<n;++i){
        const home_ui_control_t *c=&controls[i];char label[64];const char *caption=c->label;
        if(c->preset&&u->page==HOME_PAGE_CONTROL){
            const home_live_control_t *selected=home_ui_selected_control(u);
            if(selected){capability_value(label,sizeof(label),selected,home_ui_control_preset(selected,c->preset-1));caption=label;}
        }else if(c->preset&&device){value_text(label,sizeof(label),device,home_ui_preset_value(device,c->preset-1));caption=label;}
        if(!caption)continue;
        bool fill=c->filled&&c->action!=0;
        rounded(f,c->x,c->y,c->w,c->h,8,fill);
        if(c->w==56&&!strcmp(caption,"Настройки")){settings_icon(f,c->x+28,c->y+28);continue;}
        const home_font_t *font=c->w<=UI_TARGET||c->action==CAP_ALL?&home_font_18:c->action?&home_font_20_bold:&home_font_18;
        ok&=centered(f,c->x+8,c->y+(c->h-font->height)/2,c->w-16,caption,font,!fill);
    }
    return ok;
}
static bool live_render(uint8_t *f,size_t length,const home_ui_t *u)
{
    const home_live_data_t *l=u->live;memset(f,0,length);bool ok=true;
    bool device=(u->page==HOME_PAGE_DEVICE||u->page==HOME_PAGE_CONTROL||u->page==HOME_PAGE_NUMBER)&&u->device<l->device_count;
    const char *title=u->page==HOME_PAGE_HOME?"Мой дом":u->page==HOME_PAGE_ROOMS?"Комнаты":u->page==HOME_PAGE_SCENES?"Сценарии":
        u->page==HOME_PAGE_ROOM&&u->room<l->room_count?l->rooms[u->room].name:
        u->page==HOME_PAGE_NUMBER?"Введите значение":device?l->devices[u->device].name:u->page==HOME_PAGE_ACCOUNT?"Яндекс Дом":u->page==HOME_PAGE_NETWORK?"Подключение":"Настройки";
    ok&=header(f,u,title);
    if(u->page==HOME_PAGE_CONTROL||u->page==HOME_PAGE_NUMBER||(u->page==HOME_PAGE_DEVICE&&home_ui_capabilities(u))){
        ok&=capability_body(f,u);
    }else if(u->page==HOME_PAGE_SETTINGS){
        const char *labels[]={"Домашняя сеть","Яндекс Дом","Тема","Картина"};
        const char *values[]={l->ip[0]?l->ip:"Нет сети",l->authorized?"Аккаунт подключён":"Требуется вход",u->dark_theme?"Тёмная":"Светлая","Возврат через 45 с"};
        for(unsigned i=0;i<4;++i){
            int y=home_ui_row_y(i);
            ok&=text(f,24,y+14,labels[i],&home_font_20_bold,true,312);
            ok&=fit(f,360,y+16,values[i],&home_font_18,true,232);
            if(i<3)rect(f,24,y+64,752,1,true);
        }
    }else if(u->page==HOME_PAGE_ACCOUNT){
        if(l->waiting){
            bool qr=qr_code(f,532,128,240,l->url);
            ok&=text(f,24,140,qr?"Наведите камеру на QR":"Откройте ссылку на телефоне",&home_font_20_bold,true,480);
            char code[96];snprintf(code,sizeof(code),"Код: %s",l->code);ok&=fit(f,24,198,code,&home_font_28,true,480);
            ok&=fit(f,24,254,l->url,&home_font_20,true,480);
            ok&=text(f,24,304,"Введите код на странице Яндекса",&home_font_18,true,480);
        }else{
            ok&=text(f,24,142,l->authorized?"Аккаунт подключён":"Подключите свой умный дом",&home_font_28,true,752);
            ok&=text(f,24,210,"Вход подтверждается на телефоне",&home_font_20,true,752);
            if(l->authorized)ok&=text(f,24,252,"Состояния и команды приходят из Яндекса",&home_font_18,true,752);
        }
    }else if(u->page==HOME_PAGE_NETWORK){
        ok&=text(f,24,120,"Домашняя сеть",&home_font_20_bold,true,300);
        ok&=fit(f,356,120,l->ip[0]?l->ip:"Нет подключения",&home_font_20,true,420);
        ok&=text(f,24,190,"Загрузка картины",&home_font_20_bold,true,300);
        char address[96];snprintf(address,sizeof(address),"http://%s",l->ip[0]?l->ip:"192.168.4.1");
        ok&=fit(f,356,190,address,&home_font_20,true,420);
        ok&=text(f,24,266,"Через точку доступа панели:",&home_font_20,true,752);
        ok&=text(f,24,310,"http://192.168.4.1",&home_font_28,true,752);
        ok&=text(f,24,366,"Откройте адрес в браузере телефона",&home_font_18,true,752);
    }else if(!l->authorized){
        ok&=text(f,80,152,"Подключите свой умный дом",&home_font_28,true,650);
        ok&=text(f,80,310,"Вход подтверждается на телефоне",&home_font_20,true,650);
    }else if(device){
        ok&=draw_device_body(f,u,&l->devices[u->device],u->live_draft);
    }else{
        home_ui_tile_t tiles[6];unsigned count=home_ui_tiles(u,tiles);
        for(unsigned i=0;i<count;++i){
            unsigned slot=tiles[i].slot;int x=home_ui_tile_x(i),y=home_ui_tile_y(i);
            rounded(f,x,y,UI_TILE_WIDTH,UI_TILE_HEIGHT,8,false);
            if(tiles[i].kind==HOME_TILE_DEVICE){
                const home_live_device_t *d=&l->devices[slot];
                ok&=fit(f,x+12,y+2,d->name,&home_font_20_bold,true,216);
                ok&=fit(f,x+12,y+32,d->room<l->room_count?l->rooms[d->room].name:"Без комнаты",&home_font_18,true,128);
                char value[64];const char *state=d->available?d->state:"Нет связи";
                if(d->available){
                    const home_live_control_t *primary=d->control_count&&d->primary_control<l->control_count?&l->controls[d->primary_control]:NULL;
                    if(d->profile==HOME_PROFILE_VACUUM){
                        state=d->has_toggle?(d->on?"Уборка":"Стоп"):"Неизв.";
                        for(unsigned v=d->control_start;v<d->control_start+d->control_count;++v)
                            if(!strcmp(l->controls[v].instance,"pause")&&l->controls[v].known&&l->controls[v].boolean)state="Пауза";
                    }else if(primary&&primary->known&&(primary->kind==HOME_CONTROL_RANGE||primary->kind==HOME_CONTROL_READING))state=primary->state;
                    else if(d->has_toggle&&!d->on)state="Выкл.";
                    else if(d->has_range){value_text(value,sizeof(value),d,d->value);state=value;}
                    else if(d->has_toggle)state="Вкл.";
                }
                ok&=fit(f,x+156,y+32,state,&home_font_18,true,72);
            }else{
                const char *name=tiles[i].kind==HOME_TILE_ROOM?l->rooms[slot].name:l->scenes[slot].name;
                ok&=fit(f,x+12,y+2,name,&home_font_20_bold,true,216);
                if(tiles[i].kind==HOME_TILE_ROOM){
                    unsigned devices=0;for(unsigned d=0;d<l->device_count;++d)if(l->devices[d].room==slot)++devices;
                    char info[48];snprintf(info,sizeof(info),"Устройств: %u",devices);
                    ok&=fit(f,x+12,y+32,info,&home_font_18,true,216);
                }else ok&=text(f,x+12,y+32,"Сценарий",&home_font_18,true,216);
            }
        }
        if(!count)ok&=fit(f,48,170,!l->loaded?"Загружаем устройства...":u->page==HOME_PAGE_ROOMS?"Комнаты не найдены":u->page==HOME_PAGE_SCENES?"Сценарии не найдены":"Устройства не найдены",&home_font_20,true,704);
    }
    ok&=draw_controls(f,u,device?&l->devices[u->device]:NULL);
    if(u->dark_theme){
        for(size_t i=0;i<length;++i)f[i]^=0xff;
        if(u->page==HOME_PAGE_ACCOUNT&&l->waiting)qr_code(f,532,128,240,l->url);
    }
    return ok;
}
static void demo_device(const home_ui_t *u,unsigned d,home_live_device_t *out)
{
    static const char *names[]={"Свет в гостиной","Свет на кухне","Отопление","Шторы"};
    *out=(home_live_device_t){.available=true,.has_toggle=true,.has_range=true,.minimum=d==2?16:0,.maximum=d==2?30:100,.step=d==2?1:d<2?10:25};
    snprintf(out->name,sizeof(out->name),"%s",names[d]);
    snprintf(out->range_name,sizeof(out->range_name),"%s",d<2?"Яркость":d==2?"Температура":"Открытие штор");
    snprintf(out->unit,sizeof(out->unit),"%s",d==2?"°C":"%");
    out->on=d<2?u->light_on[d]:d==2?u->heat_on:u->curtains>0;
    out->value=d<2?u->brightness[d]:d==2?u->temperature:u->curtains;
    if(d<2)snprintf(out->state,sizeof(out->state),"%s",out->on?"Вкл.":"Выкл.");
    else if(d==2)snprintf(out->state,sizeof(out->state),"%u °C",u->temperature);
    else snprintf(out->state,sizeof(out->state),"%s",u->curtains?"Открыты":"Закрыты");
}
bool home_ui_render(uint8_t *f,size_t length,const home_ui_t *u)
{
    if(!f||!u||length!=HOME_UI_BYTES||u->page>HOME_PAGE_NUMBER)return false;
    if(u->live)return live_render(f,length,u);
    if(u->room>2||u->device>3)return false;
    memset(f,0,length);bool ok=true;
    static const char *rooms[]={"Гостиная","Кухня","Спальня"};
    static const unsigned devices[3][2]={{0,3},{1,4},{2,4}};
    home_live_device_t d;demo_device(u,u->device,&d);
    const char *title=u->page==HOME_PAGE_HOME?"Мой дом":u->page==HOME_PAGE_ROOMS?"Комнаты":
        u->page==HOME_PAGE_ROOM?rooms[u->room]:u->page==HOME_PAGE_SCENES?"Сценарии":u->page==HOME_PAGE_DEVICE?d.name:"Настройки";
    ok&=header(f,u,title);
    if(u->page==HOME_PAGE_HOME||u->page==HOME_PAGE_ROOM){
        unsigned count=u->page==HOME_PAGE_HOME?4:devices[u->room][1]<4?2:1;
        for(unsigned i=0;i<count;++i){
            unsigned slot=u->page==HOME_PAGE_HOME?i:devices[u->room][i];demo_device(u,slot,&d);int x=home_ui_tile_x(i),y=home_ui_tile_y(i);
            rounded(f,x,y,UI_TILE_WIDTH,UI_TILE_HEIGHT,8,false);
            ok&=fit(f,x+12,y+2,d.name,&home_font_20_bold,true,216);
            ok&=fit(f,x+12,y+32,rooms[slot==1?1:slot==2?2:0],&home_font_18,true,128);
            ok&=fit(f,x+156,y+32,d.state,&home_font_18,true,72);
        }
    }else if(u->page==HOME_PAGE_DEVICE)ok&=draw_device_body(f,u,&d,u->draft);
    else if(u->page==HOME_PAGE_ROOMS||u->page==HOME_PAGE_SCENES){
        const char *scenes[]={"Я дома","Уйти из дома"};
        for(unsigned i=0;i<(u->page==HOME_PAGE_ROOMS?3U:2U);++i){
            int x=home_ui_tile_x(i),y=home_ui_tile_y(i);
            rounded(f,x,y,UI_TILE_WIDTH,UI_TILE_HEIGHT,8,false);
            ok&=fit(f,x+12,y+2,u->page==HOME_PAGE_ROOMS?rooms[i]:scenes[i],&home_font_20_bold,true,216);
            ok&=text(f,x+12,y+32,u->page==HOME_PAGE_ROOMS?"Комната":"Сценарий",&home_font_18,true,216);
        }
    }else if(u->page==HOME_PAGE_SETTINGS){
        const char *labels[]={"Домашняя сеть","Яндекс Дом","Тема","Картина"};
        for(unsigned i=0;i<4;++i){
            int y=home_ui_row_y(i);ok&=text(f,24,y+14,labels[i],&home_font_20_bold,true,312);
            ok&=text(f,360,y+16,i<2?"Демо":i==2?(u->dark_theme?"Тёмная":"Светлая"):"Возврат через 45 с",&home_font_18,true,232);
        }
    }
    demo_device(u,u->device,&d);ok&=draw_controls(f,u,u->page==HOME_PAGE_DEVICE?&d:NULL);
    if(u->dark_theme)for(size_t i=0;i<length;++i)f[i]^=0xff;
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
