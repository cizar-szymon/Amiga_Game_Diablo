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
missed = collections.Counter()
head = []
for line in open(prof):
    if line.startswith('#'):
        head.append(line.strip())
        continue
    if line.startswith('m '):
        # outside the game code: absolute PC, summed per 64 KB
        missed[int(line[2:], 16) & ~0xFFFF] += 1
        continue
    off = int(line, 16)
    i = bisect.bisect_right(addrs, off) - 1
    count[names[i] if i >= 0 else '?'] += 1
total = sum(count.values()) or 1
print('\n'.join(head))
for name, n in count.most_common(top):
    print(f'{100.0 * n / total:6.1f}% {n:6d}  {name[:110]}')
if missed:
    print('outside the game code (64 KB blocks):')
    for a, n in missed.most_common(12):
        print(f'  {a:08x} {n}')

# Optional 4th argument: a function name; prints its hottest addresses.
if len(sys.argv) > 4:
    fn = sys.argv[4]
    hot = collections.Counter()
    for line in open(prof):
        if line.startswith('#') or line.startswith('m '):
            continue
        off = int(line, 16)
        i = bisect.bisect_right(addrs, off) - 1
        if i >= 0 and names[i].startswith(fn):
            hot[off] += 1
    print('hottest addresses in', fn)
    for off, n in hot.most_common(25):
        print(f'  {off:08x} {n}')
