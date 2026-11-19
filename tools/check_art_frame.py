"""Cross-check the JavaScript fixture with the actual C encoder and decoder.

Run check_art_client.js and check_drivers.py --optimization O2 first.
"""
from pathlib import Path
import argparse
import ctypes as ct
import os

ROOT=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--library',type=Path,default=ROOT/'build/host-review-o2'/('driver_regression.dll' if os.name=='nt' else 'driver_regression.so'))
args=parser.parse_args()

class View(ct.Structure):
    _fields_=[('pixels',ct.POINTER(ct.c_uint8)),('pixel_crc',ct.c_uint32)]

lib=ct.CDLL(str(args.library.resolve()))
lib.art_frame_parse.argtypes=[ct.POINTER(ct.c_uint8),ct.c_size_t,ct.POINTER(View)]
lib.art_frame_parse.restype=ct.c_int
lib.art_frame_encode.argtypes=[ct.POINTER(ct.c_uint8),ct.c_size_t,ct.POINTER(ct.c_uint8),ct.c_size_t]
lib.art_frame_encode.restype=ct.c_int
packet=(ROOT/'build/host-review/art-client.ehart').read_bytes()
data=(ct.c_uint8*len(packet)).from_buffer_copy(packet)
view=View()
assert lib.art_frame_parse(data,len(data),ct.byref(view))==0
encoded=(ct.c_uint8*len(packet))()
assert lib.art_frame_encode(encoded,len(packet),view.pixels,96000)==0
assert bytes(encoded)==packet
print('PASS: JavaScript and C agree on every byte of the four-level image packet.')
