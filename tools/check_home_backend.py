"""Native tests of the shipped OAuth, HTTPS, device validation and live catalog."""
from pathlib import Path
import os, subprocess
from check_drivers import find_clang
root=Path(__file__).resolve().parents[1]; out=root/'build/host-home';out.mkdir(parents=True,exist_ok=True)
clang=find_clang(); cxx=clang.with_name('clang++.exe' if os.name=='nt' else 'clang++')
include=['tests/home_backend/stubs','components/home_backend/include','components/home_ui/include','components/cjson']
flags=['-D_CRT_SECURE_NO_WARNINGS','-O2','-Wall','-Wextra','-Werror','-Wno-missing-field-initializers',*['-I'+str(root/p) for p in include]]
obj=out/('cJSON.obj' if os.name=='nt' else 'cJSON.o')
subprocess.run([str(clang),'-std=c11',*flags,'-c',str(root/'components/cjson/cJSON.c'),'-o',str(obj)],check=True)
for name,sources,json in [
 ('oauth',['yandex_auth_test.cpp','YandexAuth.cpp'],True),
 ('https',['https_client_test.cpp','HttpsClient.cpp'],False),
 ('device_actions',['device_manager_test.cpp','DeviceManager.cpp','DeviceAction.cpp'],True),
 ('backend_worker',['backend_worker_test.cpp','home_backend.cpp','home_catalog.cpp','DeviceManager.cpp','DeviceAction.cpp','YandexAuth.cpp'],True),
 ('live_catalog',['live_catalog_test.cpp','home_catalog.cpp','DeviceManager.cpp','DeviceAction.cpp','YandexAuth.cpp'],True)]:
    files=[root/('tests/home_backend/'+s if s.endswith('_test.cpp') else 'components/home_backend/'+s) for s in sources]
    binary=out/(name+('.exe' if os.name=='nt' else ''))
    subprocess.run([str(cxx),'-std=c++17',*flags,*map(str,files),*([str(obj)] if json else []),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
