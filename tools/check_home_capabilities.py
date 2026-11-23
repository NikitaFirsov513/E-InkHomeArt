"""Exercise the real Yandex catalog + C UI end to end, then render preview PNGs."""
from pathlib import Path
import ctypes as ct
import os,subprocess
from check_drivers import find_clang
from preview_diagnostics import png

root=Path(__file__).resolve().parents[1]
out=root/'build/host-capabilities';out.mkdir(parents=True,exist_ok=True)
clang=find_clang();cxx=clang.with_name('clang++.exe' if os.name=='nt' else 'clang++')
includes=['tests/home_backend/stubs','components/home_backend/include','components/home_ui/include','components/cjson']
flags=['-D_CRT_SECURE_NO_WARNINGS','-DHOME_UI_LAYOUT_CHECK','-O2','-Wall','-Wextra','-Werror','-Wno-missing-field-initializers',*['-I'+str(root/p) for p in includes]]
objects=[]
for i,source in enumerate(['components/cjson/cJSON.c','components/home_ui/home_ui.c','components/home_ui/home_ui_render.c','components/home_ui/home_ui_font.c','components/home_ui/vendor/qrcodegen/qrcodegen.c']):
    obj=out/(str(i)+('.obj' if os.name=='nt' else '.o'))
    subprocess.run([str(clang),'-std=c11',*flags,*([] if os.name=='nt' else ['-fPIC']),'-c',str(root/source),'-o',str(obj)],check=True);objects.append(str(obj))
sources=[str(root/p) for p in ['tests/home_backend/capability_ui_test.cpp','components/home_backend/home_catalog.cpp','components/home_backend/DeviceManager.cpp','components/home_backend/DeviceAction.cpp','components/home_backend/YandexAuth.cpp']]
cmd=[str(cxx),'-std=c++17',*flags,*sources,*objects]
exe=out/('capabilities.exe' if os.name=='nt' else 'capabilities')
subprocess.run([*cmd,'-o',str(exe)],check=True);subprocess.run([str(exe)],cwd=root,check=True)
library=out/('capabilities.dll' if os.name=='nt' else 'capabilities.so')
subprocess.run([*cmd,'-shared',*(['-Wl,/EXPORT:capability_preview'] if os.name=='nt' else ['-fPIC']),'-o',str(library)],check=True)
lib=ct.CDLL(str(library));render=lib.capability_preview
render.argtypes=[ct.c_uint,ct.POINTER(ct.c_uint8),ct.c_size_t];render.restype=ct.c_bool
for i,name in enumerate(['overview','overview-2','temperature','mode','toggle','hsv','rgb','relative','keypad','reading','offline','pending','error','dark']):
    frame=(ct.c_uint8*48000)();assert render(i,frame,48000),name;png(out/(name+'.png'),bytes(frame))
print('Capability previews:',out)
