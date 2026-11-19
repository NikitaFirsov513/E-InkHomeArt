"""Deterministic 800x480 UI proposals; creates new SVG and bilevel PNG assets.

Requires Pillow and Windows Segoe UI/Georgia fonts. No firmware is changed.
Run with the bundled workspace Python: python design/ui-rev1/render.py
"""
from pathlib import Path
from html import escape
import json
import math
import random
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent
OUT = ROOT / 'renders'
W, H, SCALE = 800, 480, 3
FONT_DIR = Path('C:/Windows/Fonts')
FONT_NAMES = {'regular': 'segoeui.ttf', 'medium': 'seguisb.ttf', 'serif': 'georgia.ttf'}


class Canvas:
    def __init__(self, title, width=W, height=H):
        self.width, self.height, self.title = width, height, title
        self.image = Image.new('L', (width*SCALE, height*SCALE), 255)
        self.draw = ImageDraw.Draw(self.image)
        self.svg = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}" role="img">',
                    f'<title>{escape(title)}</title>', '<rect width="100%" height="100%" fill="white"/>']
        self.text_bounds = []

    def rect(self, x, y, w, h, fill='white', stroke='black', sw=1, radius=0):
        assert w >= 0 and h >= 0
        self.svg.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{radius}" fill="{fill}" stroke="{stroke}" stroke-width="{sw}"/>')
        box = tuple(round(v*SCALE) for v in (x, y, x+w, y+h))
        colors = {'black': 0, 'white': 255, 'none': None}
        self.draw.rounded_rectangle(box, radius=round(radius*SCALE), fill=colors[fill], outline=colors[stroke], width=max(1, round(sw*SCALE)))

    def line(self, points, fill='black', sw=1):
        self.svg.append(f'<polyline points="{" ".join(f"{x},{y}" for x,y in points)}" fill="none" stroke="{fill}" stroke-width="{sw}" stroke-linecap="round" stroke-linejoin="round"/>')
        self.draw.line([(round(x*SCALE), round(y*SCALE)) for x,y in points], fill=0 if fill == 'black' else 255, width=max(1,round(sw*SCALE)), joint='curve')

    def poly(self, points, fill='black', stroke='none', sw=1):
        self.svg.append(f'<polygon points="{" ".join(f"{x},{y}" for x,y in points)}" fill="{fill}" stroke="{stroke}" stroke-width="{sw}"/>')
        points = [(round(x*SCALE), round(y*SCALE)) for x,y in points]
        self.draw.polygon(points, fill=0 if fill == 'black' else 255)
        if stroke != 'none': self.draw.line(points+[points[0]], fill=0 if stroke == 'black' else 255, width=round(sw*SCALE))

    def circle(self, x, y, radius, fill='white', stroke='black', sw=1):
        self.svg.append(f'<circle cx="{x}" cy="{y}" r="{radius}" fill="{fill}" stroke="{stroke}" stroke-width="{sw}"/>')
        self.draw.ellipse(tuple(round(v*SCALE) for v in (x-radius,y-radius,x+radius,y+radius)), fill=0 if fill == 'black' else 255,
                          outline=None if stroke == 'none' else 0 if stroke == 'black' else 255, width=max(1,round(sw*SCALE)))

    def text(self, x, y, value, size=22, weight='regular', color='black', anchor='left', max_width=None):
        font = ImageFont.truetype(str(FONT_DIR/FONT_NAMES[weight]), size*SCALE)
        bounds = self.draw.textbbox((0,0), value, font=font, anchor='lt')
        width, height = (bounds[2]-bounds[0])/SCALE, (bounds[3]-bounds[1])/SCALE
        if max_width is not None: assert width <= max_width, (self.title,value,width,max_width)
        start = x if anchor == 'left' else x-width/2 if anchor == 'middle' else x-width
        assert start >= 0 and start+width <= self.width and y >= 0 and y+height <= self.height, (self.title,value,start,y,width,height)
        self.text_bounds.append((value,round(start,2),y,round(width,2),round(height,2)))
        self.draw.text((round(start*SCALE),round(y*SCALE)), value, font=font, fill=0 if color == 'black' else 255, anchor='lt')
        family = 'Georgia' if weight == 'serif' else 'Segoe UI'
        self.svg.append(f'<text x="{start}" y="{y}" font-family="{family},sans-serif" font-size="{size}" font-weight="{600 if weight == "medium" else 400}" fill="{color}" dominant-baseline="text-before-edge">{escape(value)}</text>')

    def button(self, x, y, w, h, label, selected=False, size=20):
        assert h >= 44, (label,h)
        self.rect(x,y,w,h,fill='black' if selected else 'white',sw=2,radius=8)
        self.text(x+w/2,y+(h-size)/2-1,label,size,'medium',color='white' if selected else 'black',anchor='middle',max_width=w-18)

    def save(self, name):
        OUT.mkdir(parents=True,exist_ok=True)
        image = self.image.resize((self.width,self.height),Image.Resampling.LANCZOS).point(lambda p: 255 if p >= 150 else 0).convert('1')
        image.save(OUT/f'{name}.png')
        (OUT/f'{name}.svg').write_text('\n'.join(self.svg+['</svg>']),encoding='utf-8')
        assert image.mode == '1'
        return {'file':name,'title':self.title,'width':self.width,'height':self.height,'mode':'1','text_boxes':self.text_bounds}


def header(c,title='Мой дом',offline=False):
    c.text(24,17,title,32,'medium',max_width=390)
    c.text(514,23,'Нет сети' if offline else 'Wi-Fi',18,'medium')
    c.text(657,23,'78 %',18,anchor='right')
    c.rect(671,22,28,16,sw=2,radius=2)
    c.rect(700,27,3,6,fill='black',stroke='none')
    c.rect(675,26,17,8,fill='black',stroke='none')
    c.rect(718,10,58,44,sw=2,radius=8)
    for x in (735,747,759): c.circle(x,32,2,fill='black',stroke='none')
    c.line([(24,64),(776,64)],sw=1)


def nav(c,active='Главная'):
    c.rect(0,420,800,60,stroke='none')
    c.line([(0,420),(800,420)],sw=2)
    labels=['Главная','Комнаты','Сценарии','Картина']
    for i,label in enumerate(labels):
        x=12+i*198
        c.button(x,428,182,44,label,selected=label==active,size=20)


def tile(c,x,y,w,h,name,state,action,active=False,small=False):
    c.rect(x,y,w,h,sw=1,radius=10)
    c.text(x+14,y+12,name,20 if small else 22,'medium',max_width=w-28)
    c.text(x+14,y+42,state,18,max_width=w-28)
    c.button(x+12,y+h-56,w-24,44,action,selected=active,size=19)


def art(c,x=0,y=0,w=800,h=480):
    """Project-local geometric print, no imported photograph or grayscale."""
    def p(a,b): return (round(x+a*w),round(y+b*h))
    c.rect(x,y,w,h,stroke='none')
    c.circle(*p(.73,.26),w*.093,fill='black',stroke='none')
    c.circle(*p(.752,.25),w*.087,stroke='none')
    rng=random.Random(413)
    # Fine isolated ink marks, kept clear of the quiet upper sky.
    for _ in range(360):
        a=rng.uniform(.08,.91); b=rng.uniform(.42,.62)
        if b>.43+.08*math.sin(a*9): c.circle(*p(a,b),max(.35,w/1500),fill='black',stroke='none')
    c.poly([p(0,.63),p(.19,.35),p(.32,.55),p(.46,.30),p(.63,.61),p(.82,.46),p(1,.64),p(1,.79),p(0,.79)],fill='black')
    c.poly([p(.19,.35),p(.223,.46),p(.203,.43),p(.178,.46),p(.16,.45)],fill='white')
    c.poly([p(.46,.30),p(.512,.43),p(.474,.40),p(.447,.44),p(.408,.42)],fill='white')
    c.poly([p(0,.78),p(.15,.66),p(.31,.72),p(.51,.63),p(.72,.72),p(.88,.65),p(1,.71),p(1,.88),p(0,.88)],fill='white')
    for n in range(10):
        yy=.74+n*.019
        pts=[p(k/48,yy+.018*math.sin(k/48*9+n*.23)) for k in range(49)]
        c.line(pts,sw=max(1,w/800))
    c.poly([p(0,.93),p(.13,.87),p(.32,.91),p(.48,.85),p(.71,.91),p(.88,.86),p(1,.90),p(1,1),p(0,1)],fill='black')


def make_home_a(offline=False):
    c=Canvas('A · Быстрые действия'+(' · Нет сети' if offline else ''))
    header(c,offline=offline)
    c.text(24,75,'Сохранённые данные · 22:10' if offline else 'Быстрые действия',20)
    if not offline: c.text(776,77,'Обновлено 22:10',16,anchor='right')
    data=[('Свет · гостиная','Включён','Выключить',True),('Свет · кухня','Выключен','Включить',False),
          ('Отопление','22 °C · авто','Настроить',False),('Шторы','Открыты','Закрыть',False),
          ('Я дома','Сценарий','Запустить',False),('Уйти из дома','Сценарий','Запустить',False)]
    for i,(name,state,action,selected) in enumerate(data):
        tile(c,24+(i%3)*256,108+(i//3)*150,240,138,name,state,'Нет связи' if offline else action,selected and not offline)
    nav(c)
    return c


def make_home_b():
    c=Canvas('B · Сначала комната')
    header(c,'Комнаты')
    for i,(room,count) in enumerate([('Гостиная','4 устройства'),('Кухня','3 устройства'),('Спальня','5 устройств'),('Весь дом','12 устройств')]):
        y=86+i*79
        c.rect(16,y,182,69,fill='black' if i==0 else 'white',sw=1,radius=8)
        color='white' if i==0 else 'black'
        c.text(30,y+11,room,22,'medium',color=color,max_width=154)
        c.text(30,y+40,count,16,color=color)
    c.text(222,83,'Гостиная',28,'medium')
    c.text(776,92,'22:10',16,anchor='right')
    for i,(name,state,action,selected) in enumerate([
        ('Основной свет','Включён','Выключить',True),('Торшер','Выключен','Включить',False),
        ('Шторы','Открыты','Закрыть',False),('Температура','22 °C · авто','Настроить',False)]):
        tile(c,222+(i%2)*282,132+(i//2)*139,272,127,name,state,action,selected,True)
    nav(c,'Комнаты')
    return c


def make_home_c():
    c=Canvas('C · Картина рядом с управлением')
    header(c,'Дом')
    art(c,24,88,480,288)
    c.rect(24,88,480,288,fill='none',sw=1)
    c.text(24,389,'Текущая картина',16)
    c.text(530,84,'Быстрый доступ',23,'medium')
    c.button(530,128,246,72,'Вечер',True,24)
    c.button(530,216,246,72,'Выключить свет',size=22)
    c.button(530,304,246,72,'Все устройства',size=22)
    nav(c)
    return c


def make_device(pending=False):
    c=Canvas('Устройство · '+('Команда выполняется' if pending else 'Свет в гостиной'))
    c.button(24,12,104,44,'Назад')
    c.text(150,20,'Свет в гостиной',30,'medium')
    c.text(776,27,'Отправляем…' if pending else 'Включён',18,'medium',anchor='right')
    c.line([(24,70),(776,70)],sw=1)
    # Large line-art luminaire, deliberately two-color.
    c.circle(166,184,57,sw=3)
    c.line([(135,230),(135,257),(197,257),(197,230)],sw=3)
    c.line([(145,268),(187,268)],sw=3)
    for a in [0,45,90,135,180,225,270,315]:
        r=math.radians(a)
        c.line([(166+math.cos(r)*74,184+math.sin(r)*74),(166+math.cos(r)*86,184+math.sin(r)*86)],sw=2)
    c.text(166,292,'Гостиная',22,'medium',anchor='middle')
    c.button(24,340,138,56,'Включить',size=19)
    c.button(178,340,138,56,'Выключить',True,19)
    c.text(346,95,'Яркость',24,'medium')
    c.text(776,103,'Шаг 10 %',16,anchor='right')
    c.text(560,142,'75 %' if pending else '65 %',58,'medium',anchor='middle')
    c.button(346,220,80,56,'−',size=28)
    c.button(696,220,80,56,'+',size=28)
    c.text(560,240,'На устройстве: 65 %',18,anchor='middle')
    for i,value in enumerate(['25 %','50 %','75 %','100 %']):
        c.button(346+i*110,288,100,44,value,selected=value==('75 %' if pending else ''),size=19)
    c.button(346,348,430,48,'Отправляем…' if pending else 'Применить яркость',selected=not pending,size=20)
    nav(c)
    return c


def make_settings():
    c=Canvas('Настройки · MVP')
    header(c,'Настройки')
    rows=[('Wi-Fi','Домашняя сеть','Изменить'),('Яндекс Дом','Подключён','Аккаунт'),
          ('Возврат к картине','Через 45 секунд','Изменить'),('Изображение','Горный пейзаж','Выбрать')]
    for i,(name,state,action) in enumerate(rows):
        y=82+i*80
        c.text(24,y+8,name,23,'medium',max_width=310)
        c.text(356,y+12,state,20,max_width=230)
        c.button(626,y,150,56,action,size=20)
        c.line([(24,y+69),(776,y+69)],sw=1)
    nav(c)
    return c


def overview(names):
    c=Canvas('Варианты E-Ink Home Art',1680,1312)
    c.text(32,20,'E-Ink Home Art',36,'medium')
    c.text(32,70,'Три направления · 800 × 480 · демонстрационные состояния',22)
    captions=[('A / Быстрые действия','Предлагаю для MVP'),('B / Сначала комната','Если устройств и комнат много'),
              ('C / Картина + управление','Если важнее интерьерный характер'),('Art Mode','Только изображение · без постоянного меню')]
    for i,name in enumerate(names):
        x=32+(i%2)*816; y=130+(i//2)*580
        c.text(x,y,captions[i][0],25,'medium')
        c.text(x,y+35,captions[i][1],19)
        c.rect(x,y+67,800,480,stroke='black',sw=1)
        image=Image.open(OUT/f'{name}.png').convert('L').resize((800*SCALE,480*SCALE),Image.Resampling.NEAREST)
        c.image.paste(image,(x*SCALE,(y+67)*SCALE))
    # Montage is PNG only; each individual screen has its own editable SVG.
    c.image.resize((c.width,c.height),Image.Resampling.LANCZOS).point(lambda p:255 if p>=150 else 0).convert('1').save(OUT/'00-overview.png')


def main():
    screens=[('01-home-a',make_home_a()),('02-home-b',make_home_b()),('03-home-c',make_home_c()),
             ('04-art',Canvas('Art Mode · Только изображение')),('05-device',make_device()),
             ('06-pending',make_device(True)),('07-offline',make_home_a(True)),('08-settings',make_settings())]
    art(screens[3][1])
    manifest=[c.save(name) for name,c in screens]
    overview([name for name,_ in screens[:4]])
    (OUT/'manifest.json').write_text(json.dumps({'screens':manifest,'data':'Illustrative, not live device data','min_button_height_px':44},ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(f'Rendered {len(screens)} screens as 800x480 bilevel PNG + SVG; all text and buttons within bounds.')


if __name__=='__main__': main()
