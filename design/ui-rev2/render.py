"""800x480 proposals only. Firmware sources are never modified.
Pixel geometry is shared by drawing, hit regions and the collision checker.
"""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
import base64, json, itertools

ROOT=Path(__file__).resolve().parent
OUT=ROOT.parents[1]/'build/ui-rev2/renders'
FONT=ROOT.parents[1]/'build/references/NotoSans.ttf'
W,H,S=800,480,2
DEVICES=[('Свет в гостиной','Гостиная','Вкл.','Выключить'),
         ('Свет на кухне','Кухня','Выкл.','Включить'),
         ('Отопление','Спальня','22 °C','Настроить'),
         ('Шторы','Гостиная','Открыты','Закрыть')]
LONG=['Потолочный свет над обеденным столом','Подсветка рабочей зоны кухни',
      'Термостат у окна в большой спальне','Электрокарниз панорамного окна']
ASSETS={}

class Canvas:
    def __init__(self,name):
        self.name=name;self.im=Image.new('L',(W*S,H*S),255);self.d=ImageDraw.Draw(self.im)
        self.buttons=[];self.texts=[];self._fonts={}
    def font(self,size,bold=False):
        key=(size,bold)
        if key not in self._fonts:
            f=ImageFont.truetype(str(FONT),size*S);f.set_variation_by_axes([600 if bold else 400,100]);self._fonts[key]=f
        return self._fonts[key]
    def rect(self,x,y,w,h,fill=False,r=0,stroke=1):
        self.d.rounded_rectangle((x*S,y*S,(x+w-1)*S,(y+h-1)*S),radius=r*S,fill=0 if fill else 255,outline=0 if stroke else None,width=max(1,stroke*S))
    def line(self,x,y,x1,y1):self.d.line((x*S,y*S,x1*S,y1*S),fill=0,width=S)
    def text(self,x,y,s,size=20,bold=False,width=None,align='left',white=False,owner=None):
        f=self.font(size,bold)
        if width:
            while s and self.d.textlength(s,font=f)>width*S:
                s=s[:-1]
                if self.d.textlength(s+'...',font=f)<=width*S:s+='...';break
        box=self.d.textbbox((0,0),s,font=f,anchor='lt');tw=(box[2]-box[0])/S;th=(box[3]-box[1])/S
        if align=='center':x-=tw/2
        elif align=='right':x-=tw
        self.texts.append(dict(text=s,box=[x,y,tw,th],owner=owner))
        self.d.text((round(x*S),round(y*S)),s,font=f,anchor='lt',fill=255 if white else 0)
    def button(self,x,y,w,h,label,action,fill=False,disabled=False):
        identity=f'b{len(self.buttons)}'
        self.rect(x,y,w,h,fill and not disabled,8)
        self.buttons.append(dict(id=identity,box=[x,y,w,h],label=label,action=action,disabled=disabled))
        self.text(x+w/2,y+(h-24)/2,label,20,True,w-24,'center',fill and not disabled,identity)
    def finish(self):
        for b in self.buttons:
            x,y,w,h=b['box'];assert x>=0 and y>=0 and x+w<=W and y+h<=H,(self.name,b)
            assert w>=56 and h>=56,(self.name,'small button',b)
        def intersect(a,b):
            x,y,w,h=a;xx,yy,ww,hh=b
            return min(x+w,xx+ww)>max(x,xx) and min(y+h,yy+hh)>max(y,yy)
        for a,b in itertools.combinations(self.buttons,2):assert not intersect(a['box'],b['box']),(self.name,'buttons overlap',a,b)
        for t in self.texts:
            x,y,w,h=t['box'];assert x>=0 and y>=0 and x+w<=W+.1 and y+h<=H,(self.name,'text bounds',t)
            for b in self.buttons:
                if t['owner']!=b['id']:assert not intersect(t['box'],b['box']),(self.name,'text over button',t,b)
        for a,b in itertools.combinations(self.texts,2):assert not intersect(a['box'],b['box']),(self.name,'text overlap',a,b)
        image=self.im.resize((W,H),Image.Resampling.LANCZOS).point(lambda p:255 if p>=150 else 0).convert('1')
        OUT.mkdir(parents=True,exist_ok=True);path=OUT/(self.name+'.png');image.save(path)
        ASSETS[self.name]=dict(image='data:image/png;base64,'+base64.b64encode(path.read_bytes()).decode(),buttons=self.buttons)
        return dict(file=path.name,buttons=self.buttons,texts=self.texts)

REPORT=[]
def save(c):REPORT.append(c.finish())
def top(c,title,status='Wi-Fi подключён',detail=False,power=True,power_disabled=False):
    if detail:
        c.button(24,8,112,56,'Назад','back')
        if isinstance(title,list):
            for i,label in enumerate(title):c.text(160,10+i*28,label,22,True,448)
        else:c.text(160,20,title,28,True,448)
        if power:c.button(632,8,144,56,'Выключить','power',True,disabled=power_disabled)
    else:
        c.text(24,19,title,28,True,440)
        c.button(488,8,56,56,'<','prev')
        c.button(560,8,56,56,'>','next')
        c.button(632,8,144,56,'Настройки','settings')
    c.line(24,72,776,72)
    c.text(24,84,status,18,width=584)
    if not detail:c.text(776,84,'1 / 2',18,width=144,align='right')
def nav(c,active='Главная'):
    c.line(0,400,800,400)
    for i,(name,action) in enumerate([('Главная','home'),('Комнаты','rooms'),('Сценарии','scenes'),('Картина','art')]):
        c.button(24+192*i,416,176,56,name,action,name==active)

def home(kind,state='normal',long=False,pending=0):
    name=f'{kind}-{state}'+('-long' if long else '')+(f'-{pending}' if state=='pending' else '')
    c=Canvas(name)
    status='Команда отправляется · дождитесь результата' if state=='pending' else 'Нет сети · показаны последние данные' if state=='offline' else 'Быстрые действия' if kind=='cards' else 'Устройства вашего дома' if kind=='list' else 'Гостиная'
    top(c,'Мой дом',status)
    if kind=='cards':
        for i,(label,room,value,action) in enumerate(DEVICES):
            x=24+(i%2)*384;y=112+(i//2)*144
            c.rect(x,y,368,128,r=10)
            c.text(x+16,y+16,LONG[i] if long else label,22,True,224)
            c.text(x+352,y+18,value,20,width=104,align='right')
            caption='Отправляем' if state=='pending' and i==pending else 'Нет сети' if state=='offline' else action
            if action=='Настроить':c.button(x+16,y+56,336,56,caption,f'device:{i}',disabled=state=='pending')
            else:
                c.button(x+16,y+56,240,56,caption,f'toggle:{i}',i==0,disabled=state!='normal')
                c.button(x+272,y+56,80,56,'Ещё',f'device:{i}',disabled=state=='pending')
    elif kind=='list':
        for i,(label,room,value,action) in enumerate(DEVICES):
            y=112+i*72
            c.text(24,y+2,LONG[i] if long else label,22,True,376)
            c.text(24,y+31,room,18,width=376)
            c.text(520,y+18,value,20,width=104,align='right')
            caption='Отправляем' if state=='pending' and i==pending else 'Нет сети' if state=='offline' else action
            if action=='Настроить':c.button(536,y,240,56,caption,f'device:{i}',disabled=state=='pending')
            else:
                c.button(536,y,144,56,caption,f'toggle:{i}',i==0,disabled=state!='normal')
                c.button(696,y,80,56,'Ещё',f'device:{i}',disabled=state=='pending')
            if i<3:c.line(24,y+64,776,y+64)
    else:
        for i,label in enumerate(['Гостиная','Кухня','Спальня','Кабинет']):c.button(24,112+i*72,184,56,label,f'room:{i}',i==0)
        labels=['Основной свет','Подсветка','Шторы'];values=['65 %','Выкл.','Открыты']
        for i in range(3):
            y=112+i*96
            c.text(232,y+4,LONG[i] if long else labels[i],22,True,288)
            c.text(232,y+40,values[i],18,width=288)
            caption='Отправляем' if state=='pending' and i==pending else 'Нет сети' if state=='offline' else 'Выключить' if i==0 else 'Включить' if i==1 else 'Закрыть'
            c.button(536,y+12,144,56,caption,f'toggle:{i}',i==0,disabled=state!='normal')
            c.button(696,y+12,80,56,'Ещё',f'device:{i}',disabled=state=='pending')
            if i<2:c.line(232,y+88,776,y+88)
    nav(c);save(c)

def detail(value=75,state='ready',long=False):
    c=Canvas(f'device-{state}-{value}'+('-long' if long else ''))
    status='Нет подтверждения команды · проверьте устройство' if state=='error' else 'Нет сети · последнее значение: 65 %' if state=='offline' else 'Ожидаем подтверждение команды' if state=='pending' else 'Новое значение отправится после «Применить»'
    if long:
        top(c,['Потолочный свет','над обеденным столом'],status,True,power=True,power_disabled=state!='ready')
    else:top(c,'Свет в гостиной',status,True,power=True,power_disabled=state!='ready')
    c.text(24,116,'Яркость',24,True,width=400)
    c.text(776,119,'Шаг: 5 %',18,width=200,align='right')
    c.button(168,168,96,72,'-','minus',disabled=state!='ready')
    c.text(400,177,f'{value} %',48,True,224,'center')
    c.button(536,168,96,72,'+','plus',disabled=state!='ready')
    c.text(400,260,'Последнее подтверждённое: 65 %' if state=='error' else 'На устройстве: 65 %',20,width=620,align='center')
    for i,v in enumerate([25,50,75,100]):c.button(88+i*160,304,144,56,f'{v} %',f'preset:{v}',value==v,disabled=state!='ready')
    c.line(0,400,800,400)
    caption='Проверить состояние' if state=='error' else 'Нет связи' if state=='offline' else 'Отправляем...' if state=='pending' else 'Применить'
    c.button(24,416,752,56,caption,'refresh' if state=='error' else 'apply',state in ('ready','error'),disabled=state not in ('ready','error'))
    save(c)

def settings(dark=False):
    c=Canvas('settings-dark' if dark else 'settings');top(c,'Настройки','Параметры панели',True,power=False)
    for i,(title,value,label,action) in enumerate([('Домашняя сеть','Подключена','Открыть','network'),('Яндекс Дом','Аккаунт подключён','Аккаунт','account'),('Тема','Тёмная' if dark else 'Светлая','Изменить','theme'),('Картина','Возврат через 45 с','Показать','art')]):
        y=112+72*i;c.text(24,y+16,title,22,True,300);c.text(360,y+18,value,18,width=232);c.button(632,y,144,56,label,action)
        if i<3:c.line(24,y+64,776,y+64)
    nav(c)
    if dark:
        from PIL import ImageChops
        c.im=ImageChops.invert(c.im)
    save(c)

def qr(dark=False):
    # A real QR raster from the portable firmware renderer; example code only.
    raster=Image.open(ROOT.parents[1]/'docs/ui-yandex-login.png')
    assert raster.size==(800,480)
    raster=raster.crop((532,132,772,372)).convert('L').resize((240*S,240*S),Image.Resampling.NEAREST)
    c=Canvas('account-dark' if dark else 'account');top(c,'Вход в Яндекс','Откройте страницу и введите код',True,power=False)
    c.text(24,144,'Наведите камеру на QR',24,True,480)
    c.text(24,204,'Код: 123-456',28,True,480)
    c.text(24,256,'https://oauth.yandex.ru/device',20,width=480)
    c.im.paste(raster,(532*S,128*S))
    c.line(0,400,800,400);c.button(24,416,752,56,'Отменить вход','back')
    if dark:
        from PIL import ImageChops
        c.im=ImageChops.invert(c.im);c.im.paste(raster,(532*S,128*S))
    save(c)

def scenarios():
    c=Canvas('scenes');top(c,'Сценарии','Запуск одним нажатием')
    for i,label in enumerate(['Я дома','Уйти из дома','Вечер','Выключить весь свет']):
        y=112+i*72;c.text(24,y+16,label,22,True,480);c.button(552,y,224,56,'Запустить',f'scene:{i}')
        if i<3:c.line(24,y+64,776,y+64)
    nav(c,'Сценарии');save(c)

def art():
    c=Canvas('art')
    sample=ROOT.parent/'ui-rev1/renders/04-art.png'
    c.im=Image.open(sample).convert('L').resize((W*S,H*S),Image.Resampling.NEAREST)
    c.buttons.append(dict(id='wake',box=[0,0,800,480],label='Открыть управление',action='home',disabled=False))
    save(c)

if __name__=='__main__':
    for kind in ['cards','list','rooms']:
        home(kind);home(kind,'offline');home(kind,long=True)
        for i in range(4 if kind!='rooms' else 3):home(kind,'pending',pending=i)
    for value in range(0,101,5):
        for state in ['ready','pending','offline']:detail(value,state)
    detail(75,long=True);detail(75,'error');settings();settings(True);qr();qr(True);scenarios();art()
    selected=['cards-normal','list-normal','rooms-normal','device-ready-75','device-pending-75','device-offline-75',
              'list-normal-long','device-ready-75-long','device-error-75','settings','settings-dark','account','account-dark','scenes']
    import shutil
    export=ROOT/'renders';export.mkdir(exist_ok=True)
    for name in selected:shutil.copy2(OUT/(name+'.png'),export/(name+'.png'))
    summary=dict(width=W,height=H,min_target=56,pixel_pitch_mm=.204,screen_count=len(REPORT),
                 button_count=sum(len(r['buttons']) for r in REPORT),button_collisions=0,text_collisions=0,
                 checks=['all controls >=56px','all controls pairwise disjoint','text does not overlap other text or controls'],
                 examples=[r for r in REPORT if r['file'][:-4] in selected])
    (ROOT/'manifest.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2),encoding='utf-8')
    (OUT.parent/'assets.json').write_text(json.dumps(ASSETS,ensure_ascii=False,separators=(',',':')),encoding='utf-8')
    print('PASS:',len(REPORT),'screens,',summary['button_count'],'buttons; zero button/text collisions. All PNGs 800x480, mode=1.')
