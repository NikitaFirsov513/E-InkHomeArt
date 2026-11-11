"""Build actual driver C sources against a simulated SDK and exercise faults.

Windows: requires host Clang and lld-link (Visual Studio LLVM is auto-detected).
Linux: requires Clang with its normal shared-library linker.
No connected device, ESP-IDF installation or third-party Python package is needed.
"""
import argparse
import ctypes as ct
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]


def find_clang():
    if clang := shutil.which('clang'):
        return Path(clang)
    vswhere = Path(os.environ.get('ProgramFiles(x86)', 'C:/Program Files (x86)')) / 'Microsoft Visual Studio/Installer/vswhere.exe'
    if vswhere.exists():
        roots = subprocess.check_output([str(vswhere), '-all', '-property', 'installationPath'], text=True)
        for root in roots.splitlines():
            for relative in ['VC/Tools/Llvm/x64/bin/clang.exe', 'VC/Tools/Llvm/bin/clang.exe']:
                candidate = Path(root) / relative
                if candidate.exists():
                    return candidate
    raise SystemExit('Host Clang not found. Pass --compiler <path-to-clang>.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path)
    parser.add_argument('--linker', type=Path)
    parser.add_argument('--build', type=Path, default=ROOT / 'build/host-review')
    args = parser.parse_args()
    compiler = args.compiler or find_clang()
    build = args.build.resolve()
    build.mkdir(parents=True, exist_ok=True)
    sources = [
        'tests/host/driver_regression.c', 'components/uc8179/uc8179.c',
        'components/gt911/gt911.c', 'components/lis2dw12/lis2dw12_idf.c',
        'components/lis2dw12/vendor/lis2dw12_reg.c', 'main/diagnostic_screen.c',
    ]
    includes = ['tests/host/stubs', 'components/uc8179/include', 'components/gt911/include',
                'components/lis2dw12/include', 'components/lis2dw12/vendor']
    objects = []
    for index, source in enumerate(sources):
        obj = build / f'{index}.obj'
        cmd = [str(compiler), '-std=c11', '-ffreestanding', '-fno-builtin', '-fno-stack-protector',
               '-O0', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter']
        if os.name != 'nt':
            cmd += ['-fPIC']
        cmd += [f'-I{ROOT / include}' for include in includes]
        subprocess.run(cmd + ['-c', str(ROOT / source), '-o', str(obj)], check=True)
        objects.append(str(obj))
    library = build / ('driver_regression.dll' if os.name == 'nt' else 'driver_regression.so')
    exports = ['host_case_count', 'host_case_name', 'host_case_run', 'host_failure',
               'diagnostic_screen_render', 'diagnostic_map_touch', 'diagnostic_target_hit']
    if os.name == 'nt':
        linker = args.linker or compiler.with_name('lld-link.exe')
        cmd = [str(linker)]
        if linker.name.lower() == 'lld.exe':
            cmd += ['-flavor', 'link']
        cmd += ['/dll', '/noentry', '/nodefaultlib', '/machine:x64', f'/out:{library}']
        cmd += [f'/export:{name}' for name in exports]
    else:
        cmd = [str(compiler), '-shared', '-nostdlib', '-Wl,-Bsymbolic', '-Wl,--no-undefined', '-o', str(library)]
    subprocess.run(cmd + objects, check=True)
    lib = ct.CDLL(str(library))
    lib.host_case_count.restype = ct.c_int
    lib.host_case_name.argtypes = [ct.c_int]
    lib.host_case_name.restype = ct.c_char_p
    lib.host_case_run.argtypes = [ct.c_int]
    lib.host_case_run.restype = ct.c_int
    lib.host_failure.restype = ct.c_char_p
    results = []
    for index in range(lib.host_case_count()):
        name = lib.host_case_name(index).decode()
        line = lib.host_case_run(index)
        detail = lib.host_failure().decode()
        results.append({'name': name, 'passed': line == 0, 'line': line, 'failure': detail})
        print(f'{"PASS" if not line else "FAIL"}: {name}' + (f' (line {line}: {detail})' if line else ''))
    (build / 'results.json').write_text(json.dumps(results, indent=2), encoding='utf-8')
    if any(not item['passed'] for item in results):
        raise SystemExit(1)
    subprocess.run([sys.executable, str(ROOT / 'tools/preview_diagnostics.py'),
                    '--library', str(library), '--output', str(build / 'previews')], check=True)
    print(f'Passed {len(results)} driver scenarios. Results: {build / "results.json"}')


if __name__ == '__main__':
    main()
