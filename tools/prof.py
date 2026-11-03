"""Map the samples of RAM:dune-prof.txt (code offsets) to functions.
   usage: python tools/prof.py PROFILE.txt SYMS.txt [N]
   SYMS.txt: m68k-amigaos-nm -n -C of the unstripped binary (tools/prof.sh makes both)."""
import bisect, collections, sys

prof, syms = sys.argv[1], sys.argv[2]
top = int(sys.argv[3]) if len(sys.argv) > 3 else 40
addrs, names = [], []
for line in open(syms, encoding='latin-1'):
    parts = line.rstrip('\n').split(' ', 2)
    if len(parts) == 3 and parts[1] in 'tTwW':
        addrs.append(int(parts[0], 16))
        names.append(parts[2])
count = collections.Counter()
head = ''
for line in open(prof):
    if line.startswith('#'):
        head = line.strip()
        continue
    off = int(line, 16)
    i = bisect.bisect_right(addrs, off) - 1
    count[names[i] if i >= 0 else '?'] += 1
total = sum(count.values()) or 1
print(head)
for name, n in count.most_common(top):
    print(f'{100.0 * n / total:6.1f}% {n:6d}  {name[:110]}')

# Optional 4th argument: a function name; prints its hottest addresses.
if len(sys.argv) > 4:
    fn = sys.argv[4]
    hot = collections.Counter()
    for line in open(prof):
        if line.startswith('#'):
            continue
        off = int(line, 16)
        i = bisect.bisect_right(addrs, off) - 1
        if i >= 0 and names[i].startswith(fn):
            hot[off] += 1
    print('hottest addresses in', fn)
    for off, n in hot.most_common(25):
        print(f'  {off:08x} {n}')
