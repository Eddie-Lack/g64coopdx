#!/usr/bin/env python3
"""Summarise probe/L*.txt (gdb output of probe.gdb): per level, the peak object matrices, vertices and display list bytes per frame."""
import glob,re,collections,os
rows=[]
for f in sorted(glob.glob(os.path.join(os.path.dirname(os.path.abspath(__file__)),'probe/L*.txt')),key=lambda s:int(re.search(r'L(\d+)',s).group(1))):
    lvl=int(re.search(r'L(\d+)',f).group(1))
    mo=0;vt=0;gl=0;tot=0;frames=0;cur=0;tasksum=0;gmax_task=0
    for l in open(f):
        m=re.match(r'FRAME objm=(-?\d+) glistbytes=(\d+) cur=(\d+) vtx0=(-?\d+) vtx1=(-?\d+)',l)
        if not m: continue
        o,g,c,v0,v1=map(int,m.groups())
        v=v1 if v0>=800 else v0
        if v<0 or v>800: v=min([x for x in (v0,v1) if 0<=x<=800] or [0])
        mo=max(mo,o);vt=max(vt,v);gmax_task=max(gmax_task,g)
        tasksum+=g
        if c==5: gl=max(gl,tasksum);tasksum=0;frames+=1
    rows.append((lvl,mo,vt,gmax_task,gl,frames))
print("| level | peak object matrices (of 256) | peak vertices (of 800) | peak bytes in one task (check 20000) | peak bytes in a frame | frames |")
print("| --- | --- | --- | --- | --- | --- |")
for r in rows: print("| %d | %d | %d | %d | %d | %d |"%r)
print("| max | %d | %d | %d | %d | |"%tuple(max(r[i] for r in rows) for i in range(1,5)))
