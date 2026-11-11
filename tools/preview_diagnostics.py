"""Exercise the actual portable C renderer and write PNG previews, stdlib only.

Build main/diagnostic_screen.c as a host shared library first, exporting
diagnostic_screen_render, diagnostic_map_touch and diagnostic_target_hit.
Then run: python tools/preview_diagnostics.py --library build/diagnostic_renderer.dll
The sample device values in these previews are illustrative, not hardware results.
"""
import argparse
import ctypes as ct
import struct
import zlib
from pathlib import Path

WIDTH, HEIGHT = 800, 480
FRAME_BYTES = WIDTH * HEIGHT // 8


class Screen(ct.Structure):
    _fields_ = [
        ('page', ct.c_uint32), ('inverted', ct.c_uint32), ('orientation', ct.c_uint32),
        ('touch_samples', ct.c_uint32), ('target_mask', ct.c_uint32),
        ('touch_x', ct.c_int32), ('touch_y', ct.c_int32),
        ('lines', (ct.c_char * 40) * 8), ('details', (ct.c_char * 64) * 2),
    ]


def png(path: Path, frame: bytes) -> None:
    def chunk(tag, data):
        return struct.pack('>I', len(data)) + tag + data + struct.pack('>I', zlib.crc32(tag + data))

    rows = bytearray()
    for y in range(HEIGHT):
        rows.append(0)
        for x in range(WIDTH):
            rows.append(0 if frame[y * 100 + x // 8] & (0x80 >> (x % 8)) else 255)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(b'\x89PNG\r\n\x1a\n' +
                     chunk(b'IHDR', struct.pack('>IIBBBBB', WIDTH, HEIGHT, 8, 0, 0, 0, 0)) +
                     chunk(b'IDAT', zlib.compress(bytes(rows), 9)) + chunk(b'IEND', b''))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--library', type=Path, required=True)
    parser.add_argument('--output', type=Path, default=Path('docs'))
    args = parser.parse_args()
    lib = ct.CDLL(str(args.library.resolve()))
    lib.diagnostic_screen_render.argtypes = [ct.POINTER(ct.c_uint8), ct.c_size_t, ct.POINTER(Screen)]
    lib.diagnostic_screen_render.restype = ct.c_bool
    lib.diagnostic_map_touch.argtypes = [ct.c_uint16] * 4 + [ct.c_uint, ct.POINTER(ct.c_int), ct.POINTER(ct.c_int)]
    lib.diagnostic_map_touch.restype = ct.c_bool
    lib.diagnostic_target_hit.argtypes = [ct.c_int, ct.c_int]
    lib.diagnostic_target_hit.restype = ct.c_uint32

    def render(screen):
        # Canary bytes check both ends of the framebuffer on every rendering.
        guarded = (ct.c_uint8 * (FRAME_BYTES + 32))(*([0xA5] * (FRAME_BYTES + 32)))
        pixels = ct.cast(ct.byref(guarded, 16), ct.POINTER(ct.c_uint8))
        assert lib.diagnostic_screen_render(pixels, FRAME_BYTES, ct.byref(screen))
        assert bytes(guarded[:16]) == b'\xa5' * 16
        assert bytes(guarded[-16:]) == b'\xa5' * 16
        return bytes(guarded[16:-16])

    status = Screen(touch_x=-1, touch_y=-1)
    for row, value in zip(status.lines, [
        b'FLASH OK 16 MB', b'PSRAM OK 8 MB', b'I2C0 READY / 100 KHZ',
        b'TOUCH OK 480X800', b'ACCEL OK / 2G / 25 HZ', b'ADC1 READY / CAL 3/3',
        b'USB HOST CONNECTED', b'UART0 READY / 115200',
    ]):
        row.value = value
    status.details[0].value = b'BAT=4.120V  CC1=0.410V  CC2=0.008V'
    status.details[1].value = b'AX=12 AY=-8 AZ=1002 MG  TOUCH=18'
    normal = render(status)
    png(args.output / 'diagnostics-preview.png', normal)
    status.inverted = 1
    inverse = render(status)
    assert inverse == bytes(value ^ 255 for value in normal)

    grid = Screen(page=1, touch_samples=18, target_mask=31, touch_x=400, touch_y=250)
    grid.details[0].value = b'X=400 Y=250 PTS=1 HIT=5/5'
    grid.details[1].value = b'RAW=250,400 MAP=0 FRAMES=18'
    png(args.output / 'diagnostics-touch-preview.png', render(grid))
    assert render(Screen(page=2)) == b'\x00' * FRAME_BYTES
    assert render(Screen(page=3)) == b'\xff' * FRAME_BYTES
    assert render(Screen(page=2, inverted=1)) == b'\xff' * FRAME_BYTES
    render(Screen(page=4))
    # A bogus cursor must never index outside the frame.
    render(Screen(page=1, touch_samples=1, touch_x=2**31 - 1, touch_y=2**31 - 1))
    invalid = (ct.c_uint8 * FRAME_BYTES)(*([0xA5] * FRAME_BYTES))
    assert not lib.diagnostic_screen_render(invalid, FRAME_BYTES - 1, ct.byref(status))
    assert bytes(invalid) == b'\xa5' * FRAME_BYTES
    assert not lib.diagnostic_screen_render(invalid, FRAME_BYTES, ct.byref(Screen(page=5)))

    for width, height in [(480, 800), (800, 480)]:
        for orientation in range(8):
            for raw_x, raw_y in [(0, 0), (width-1, 0), (0, height-1), (width-1, height-1)]:
                x, y = ct.c_int(-1), ct.c_int(-1)
                assert lib.diagnostic_map_touch(raw_x, raw_y, width, height, orientation, ct.byref(x), ct.byref(y))
                assert x.value in (0, WIDTH-1) and y.value in (0, HEIGHT-1)
    x, y = ct.c_int(-1), ct.c_int(-1)
    assert lib.diagnostic_map_touch(0, 799, 480, 800, 0, ct.byref(x), ct.byref(y))
    assert (x.value, y.value) == (799, 0)
    assert not lib.diagnostic_map_touch(480, 0, 480, 800, 0, ct.byref(x), ct.byref(y))
    assert not lib.diagnostic_map_touch(0, 0, 1, 800, 0, ct.byref(x), ct.byref(y))
    assert not lib.diagnostic_map_touch(0, 0, 480, 800, 8, ct.byref(x), ct.byref(y))
    for index, point in enumerate([(40, 110), (760, 110), (40, 390), (760, 390), (400, 250)]):
        assert lib.diagnostic_target_hit(*point) == 1 << index
    assert lib.diagnostic_target_hit(-2**31, 0) == 0
    print('PASS: actual C renderer, guarded buffers, fills, inversion, 64 corner mappings and five touch targets.')
    print('Previews:', args.output.resolve())


if __name__ == '__main__':
    main()
