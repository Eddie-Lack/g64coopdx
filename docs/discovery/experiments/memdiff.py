#!/usr/bin/env python3
"""Compares three memory dumps of weemitt (--dump) per level: after frames 600, 601 and 620.

Make them outside any repository (they hold game data), for example:
  weemitt --rom ROM --headless 601 --level 5 --input IN --dump 600 dump-5-600.bin
and the same for 601 and 620. Then: memdiff.py 5 13. Delete the dumps afterwards.
Prints how many bytes and 4 KB pages change in 1 and 20 frames, and how well the dump and the change compress.
"""
import sys, zlib, lzma
HEAP=0x2B0D10
def load(p): return open(p,'rb').read()
for L in sys.argv[1:]:
    a=load(f'dump-{L}-600.bin'); b=load(f'dump-{L}-601.bin'); c=load(f'dump-{L}-620.bin')
    nz=a.rstrip(b'\0'); 
    print(f'level {L}: memory up to last nonzero byte {len(nz)/1e6:.2f} MB')
    print(f'  whole 24 MB: zlib-6 {len(zlib.compress(a,6))/1e6:.2f} MB, xz {len(lzma.compress(a))/1e6:.2f} MB')
    for name,x in (('1 frame',b),('20 frames',c)):
        d=bytes(p^q for p,q in zip(a,x))
        n=sum(1 for v in d if v)
        lo=sum(1 for v in d[:HEAP] if v)
        pages=len({i>>12 for i,v in enumerate(d) if v})
        print(f'  {name}: {n} bytes differ ({lo} below the heap, {n-lo} in the heap), {pages} pages of 4 KB; xor delta zlib {len(zlib.compress(d,6))} bytes')
