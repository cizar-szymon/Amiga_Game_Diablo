"""Linker map (-Wl,-Map) -> nm -n style list "addr t name" for prof.py.
   The Amiga hunk executable has no symbols after an LTO link; the map has
   the address of every input section (.text.<function> with
   -ffunction-sections, else whole .text of each object).
   usage: python tools/map2syms.py devilutionx.map out.syms"""
import re
import sys

src, dst = sys.argv[1], sys.argv[2]
syms = []
pending = None
sec_re = re.compile(r'^ (\.text[^\s]*)\s*(0x[0-9a-f]+)?\s*(0x[0-9a-f]+)?\s*(.*)$')
addr_re = re.compile(r'^\s+(0x[0-9a-f]+)\s+(0x[0-9a-f]+)\s+(.*)$')
sym_re = re.compile(r'^\s{8,}(0x[0-9a-f]+)\s{8,}(\S.*)$')
in_text = False
for line in open(src, encoding='latin-1'):
    line = line.rstrip('\n')
    if line.startswith('.text'):
        in_text = True
    elif line.startswith('.data') or line.startswith('.bss'):
        in_text = False
    if not in_text:
        continue
    m = sec_re.match(line)
    if m:
        name = m.group(1)
        if m.group(2):
            syms.append((int(m.group(2), 16), name[6:] or m.group(4)))
            pending = None
        else:
            pending = name[6:]
        continue
    m = addr_re.match(line)
    if m and pending is not None:
        syms.append((int(m.group(1), 16), pending or m.group(3)))
        pending = None
        continue
    m = sym_re.match(line)
    if m:
        syms.append((int(m.group(1), 16), m.group(2)))
syms = sorted(set(s for s in syms if s[0] != 0 or s[1]))
with open(dst, 'w') as f:
    for a, n in syms:
        f.write('%08x t %s\n' % (a, n))
print(len(syms), 'symbols')
