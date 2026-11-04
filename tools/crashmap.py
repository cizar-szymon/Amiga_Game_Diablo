# Map RAM:Diablo-crash.txt (port/sdl/src/msdl_core.c) to functions:
#   python tools/crashmap.py crash.txt build/amiga/devilutionx.syms
# (syms: python tools/map2syms.py build/amiga/devilutionx.map build/amiga/devilutionx.syms)
import sys, re, bisect
syms=[]
for l in open(sys.argv[2]):
    p=l.split(None,2)
    if len(p)==3 and p[1] in 'tT':
        try: syms.append((int(p[0],16),p[2].strip()))
        except: pass
syms.sort(); addrs=[a for a,_ in syms]
land=[a for a,n in syms if n.endswith('msdl_trap_land')][0]
txt=open(sys.argv[1]).read()
base=int(re.search(r'msdl_trap_land=([0-9a-f]+)',txt).group(1),16)-land
end=addrs[-1]
words=re.findall(r'\b[0-9a-f]{8}\b',txt.split('\n',3)[3])
for i,w in enumerate(words):
    v=int(w,16)-base
    if 0<=v<=end:
        j=bisect.bisect_right(addrs,v)-1
        print(f"usp+{i*4:04x} {w} {syms[j][1][:110]}+{v-syms[j][0]:x}")
