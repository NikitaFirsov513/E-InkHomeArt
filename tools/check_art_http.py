"""Exercise the actual HTTP C handlers against a simulated SDK boundary, no ESP."""
from pathlib import Path
import os
import subprocess
from check_drivers import find_clang

root = Path(__file__).resolve().parents[1]
build = root / 'build/host-http'
build.mkdir(parents=True, exist_ok=True)
includes = ['tests/host/stubs', 'main', 'components/art_manager/include',
            'components/art_store/include', 'components/art_frame/include', 'components/uc8179/include',
            'components/home_ui/include']
sources = ['tests/host/art_http_test.c', 'main/art_http.c', 'components/art_frame/art_frame.c']
binary = build / ('art-http-test.exe' if os.name == 'nt' else 'art-http-test')
subprocess.run([str(find_clang()), '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
                *['-I'+str(root/p) for p in includes], *[str(root/p) for p in sources],
                '-o', str(binary)], check=True)
subprocess.run([str(binary)], check=True)

manager = build / ('art-manager-test.exe' if os.name == 'nt' else 'art-manager-test')
manager_sources = ['tests/host/art_manager_test.c', 'components/art_manager/art_manager.c',
                   'components/art_store/art_store.c', 'components/art_frame/art_frame.c',
                   'components/home_ui/home_ui.c','components/home_ui/home_ui_render.c','components/home_ui/home_ui_font.c','components/home_ui/vendor/qrcodegen/qrcodegen.c']
subprocess.run([str(find_clang()), '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
                *['-I'+str(root/p) for p in includes], *[str(root/p) for p in manager_sources],
                '-o', str(manager)], check=True)
subprocess.run([str(manager)], check=True)
subprocess.run([str(manager), 'restore'], check=True)
subprocess.run([str(manager), 'ui'], check=True)

subprocess.run([str(manager), 'home'], check=True)
