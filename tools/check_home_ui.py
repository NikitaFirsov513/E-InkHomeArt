"""Run actual portable UI code and generate 800x480 firmware raster previews."""
from pathlib import Path
import ctypes as ct
import os
import subprocess
from check_drivers import find_clang
from preview_diagnostics import png

root=Path(__file__).resolve().parents[1]
out=root/'build/host-ui';out.mkdir(parents=True,exist_ok=True)
sources=['tests/host/home_ui_test.c','components/home_ui/home_ui.c',
         'components/home_ui/home_ui_render.c','components/home_ui/home_ui_font.c','components/home_ui/vendor/qrcodegen/qrcodegen.c']
cmd=[str(find_clang()),'-std=c11','-DHOME_UI_LAYOUT_CHECK','-O2','-Wall','-Wextra','-Werror',
     '-I'+str(root/'components/home_ui/include'),*[str(root/p) for p in sources]]
if os.name!='nt':cmd+=['-lm']
exe=out/('home-ui-test.exe' if os.name=='nt' else 'home-ui-test')
subprocess.run([*cmd,'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True)
libpath=out/('home-ui.dll' if os.name=='nt' else 'home-ui.so')
link=['-Wl,/EXPORT:home_ui_state_preview','-Wl,/EXPORT:home_ui_test_preview','-Wl,/EXPORT:home_ui_live_preview','-Wl,/EXPORT:home_ui_login_preview','-Wl,/EXPORT:home_ui_login_theme_preview'] if os.name=='nt' else ['-fPIC']
subprocess.run([*cmd,'-shared',*link,'-o',str(libpath)],check=True)
lib=ct.CDLL(str(libpath));render=lib.home_ui_test_preview
render.argtypes=[ct.c_uint,ct.POINTER(ct.c_uint8),ct.c_size_t];render.restype=ct.c_bool
for page,name in enumerate(['home','rooms','room','scenes','device','settings','empty-art']):
    frame=(ct.c_uint8*48000)();assert render(page,frame,48000),name
    png(out/(name+'.png'),bytes(frame))
print('Firmware raster previews:',out)

live=lib.home_ui_live_preview
live.argtypes=render.argtypes;live.restype=ct.c_bool
for page,name in enumerate(['home','rooms','room','scenes','device','settings','account','network']):
    frame=(ct.c_uint8*48000)();assert live(page,frame,48000),name
    png(out/('live-'+name+'.png'),bytes(frame))

login=lib.home_ui_login_theme_preview
login.argtypes=[ct.c_char_p,ct.c_bool,ct.POINTER(ct.c_uint8),ct.c_size_t];login.restype=ct.c_bool
for dark in [False,True]:
    frame=(ct.c_uint8*48000)();assert login(b'https://oauth.yandex.ru/device',dark,frame,48000)
    png(out/('login-'+('dark' if dark else 'light')+'.png'),bytes(b^255 for b in frame))

state=lib.home_ui_state_preview
state.argtypes=render.argtypes;state.restype=ct.c_bool
for key,name in enumerate(['tiles','pending','offline','error','long-title','power-only','range-only','read-only','long-tiles','scenes','rooms','page-2','dark','empty','ready']):
    frame=(ct.c_uint8*48000)();assert state(key,frame,48000),name
    png(out/('rev3-'+name+'.png'),bytes(frame))
