"""Decode the real firmware framebuffer using independent ZXing-C++.
Run check_home_ui.py first. Optional host dependencies: tests/requirements-qr.txt.
"""
from pathlib import Path
import ctypes as ct
import os
import sys
root=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(root/'build/qr-check'))
try:
    from PIL import Image, ImageFilter
    import zxingcpp
except ImportError:
    raise SystemExit('Install host checker: python -m pip install --target build/qr-check -r tests/requirements-qr.txt')
libpath=root/'build/host-ui'/('home-ui.dll' if os.name=='nt' else 'home-ui.so')
lib=ct.CDLL(str(libpath));render=lib.home_ui_login_theme_preview
render.argtypes=[ct.c_char_p,ct.c_bool,ct.POINTER(ct.c_uint8),ct.c_size_t];render.restype=ct.c_bool
for dark in [False,True]:
    for url in ['https://ya.ru/device','https://oauth.yandex.ru/device']:
        frame=(ct.c_uint8*48000)();assert render(url.encode(),dark,frame,48000)
        # CDI DDX=01: native 1=white. No host-only inversion can hide a firmware polarity bug.
        im=Image.frombytes('1',(800,480),bytes(frame)).convert('L')
        assert im.getpixel((0,0))==(0 if dark else 255)
        variants=[im,im.rotate(90,expand=True),im.resize((600,360)),im.filter(ImageFilter.GaussianBlur(.7))]
        for image in variants:
            found=zxingcpp.read_barcodes(image)
            assert len(found)==1 and found[0].text==url, (url,dark,found)
    for url in ['', 'a'*255]:
        frame=(ct.c_uint8*48000)();assert render(url.encode(),dark,frame,48000)
        image=Image.frombytes('1',(800,480),bytes(frame)).convert('L')
        assert not zxingcpp.read_barcodes(image)
print('PASS: actual panel bytes: light/dark backgrounds correct; ZXing decoded both URLs in both themes with rotation/resize/blur; manual fallback retained.')
