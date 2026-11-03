"""Lists which known files a Diablo save (.sv, an MPQ archive) contains.
   usage: python tools/svinfo.py single_0.sv"""
import struct
import sys

crypt = [0] * 0x500
seed = 0x00100001
for i in range(0x100):
    idx = i
    for _ in range(5):
        seed = (seed * 125 + 3) % 0x2AAAAB
        t1 = (seed & 0xFFFF) << 16
        seed = (seed * 125 + 3) % 0x2AAAAB
        t2 = seed & 0xFFFF
        crypt[idx] = t1 | t2
        idx += 0x100


def hash_str(s, t):
    s1, s2 = 0x7FED7FED, 0xEEEEEEEE
    for ch in s.upper().replace('/', '\\'):
        c = ord(ch)
        s1 = (crypt[t * 0x100 + c] ^ (s1 + s2)) & 0xFFFFFFFF
        s2 = (c + s1 + s2 + (s2 << 5) + 3) & 0xFFFFFFFF
    return s1


def decrypt(data, key):
    out = []
    s2 = 0xEEEEEEEE
    for i in range(0, len(data), 4):
        s2 = (s2 + crypt[0x400 + (key & 0xFF)]) & 0xFFFFFFFF
        v = struct.unpack_from('<I', data, i)[0]
        v = (v ^ (key + s2)) & 0xFFFFFFFF
        key = ((((~key) << 0x15) + 0x11111111) | (key >> 0x0B)) & 0xFFFFFFFF
        s2 = (v + s2 + (s2 << 5) + 3) & 0xFFFFFFFF
        out.append(v)
    return out


d = open(sys.argv[1], 'rb').read()
magic, hsize, asize, ver, sshift, htoff, btoff, hcount, bcount = struct.unpack_from('<4sIIHHIIII', d, 0)
print('magic', magic, 'archive', asize, 'hash entries', hcount, 'blocks', bcount)
ht = decrypt(d[htoff:htoff + hcount * 16], hash_str('(hash table)', 3))
bt = decrypt(d[btoff:btoff + bcount * 16], hash_str('(block table)', 3))
names = ['hero', 'game', 'heroitems'] + ['perml%02d' % i for i in range(17)] + ['perms%02d' % i for i in range(17)] \
    + ['templ%02d' % i for i in range(17)] + ['temps%02d' % i for i in range(17)]
for n in names:
    a, b, i0 = hash_str(n, 1), hash_str(n, 2), hash_str(n, 0) % hcount
    i = i0
    while True:
        ha, hb, loc, blk = ht[i * 4], ht[i * 4 + 1], ht[i * 4 + 2], ht[i * 4 + 3]
        if blk == 0xFFFFFFFF:
            break
        if ha == a and hb == b and blk < bcount:
            off, csize, fsize, flags = bt[blk * 4:blk * 4 + 4]
            print('%-10s size %6d packed %6d flags %08x' % (n, fsize, csize, flags))
            break
        i = (i + 1) % hcount
        if i == i0:
            break
