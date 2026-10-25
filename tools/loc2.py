#!/usr/bin/env python3
"""Darkest Dungeon .loc2 encoder/decoder (reverse-engineered, round-trip verified).
layout: u32 a,b,c | 512 buckets (u32 off,u32 cnt) by hash>>23 | entries (u32 hash,u32 idx,u32 1) sorted by hash
        | a: per entry (u32 start,u32 count) into values | b: values (u32 off,u32 len_incl_nul,u32 flag) | c: utf-8 strings
hash: h = h*53 + byte (uint32) over the key. flag = 256, or 0 when the text has a line break."""
import struct, re, sys
from collections import OrderedDict

def h53(s):
    h = 0
    for ch in s.encode('utf-8'): h = (h * 53 + ch) & 0xffffffff
    return h

def decode(data):
    a, b, c = struct.unpack_from('<III', data, 0)
    n = (a - 12 - 512 * 8) // 12
    ents = [struct.unpack_from('<III', data, 12 + 512 * 8 + i * 12) for i in range(n)]
    sec2 = [struct.unpack_from('<II', data, a + i * 8) for i in range((b - a) // 8)]
    sec3 = [struct.unpack_from('<III', data, b + i * 12) for i in range((c - b) // 12)]
    out = []
    for h, idx, _ in ents:
        st, cnt = sec2[idx]
        vals = []
        for j in range(cnt):
            o, l, f = sec3[st + j]
            vals.append((data[c + o:c + o + l - 1], f))
        out.append((h, vals))
    return out

def encode_hashed(items):
    """items: list of (hash, [(bytes_without_nul, flag), ...])"""
    items = sorted(items, key=lambda x: x[0])
    n = len(items)
    a = 12 + 512 * 8 + n * 12
    b = a + n * 8
    nv = sum(len(v) for _, v in items)
    c = b + nv * 12
    buckets = [[0, 0] for _ in range(512)]
    ent = bytearray(); s2 = bytearray(); s3 = bytearray(); strs = bytearray()
    vi = 0
    for i, (h, vals) in enumerate(items):
        bk = buckets[h >> 23]
        if bk[1] == 0: bk[0] = 12 + 512 * 8 + i * 12
        bk[1] += 1
        ent += struct.pack('<III', h, i, 1)
        s2 += struct.pack('<II', vi, len(vals))
        for txt, flag in vals:
            s3 += struct.pack('<III', len(strs), len(txt) + 1, flag)
            strs += txt + b'\0'
            vi += 1
    head = struct.pack('<III', a, b, c) + b''.join(struct.pack('<II', *bk) for bk in buckets)
    return head + ent + s2 + s3 + strs

def encode(entries):
    """entries: OrderedDict key -> [text, ...] (several texts = random variants)"""
    items = []
    for k, texts in entries.items():
        vals = []
        for t in texts:
            bt = t.encode('utf-8')
            vals.append((bt, 0 if (b'\n' in bt) else 256))
        items.append((h53(k), vals))
    return encode_hashed(items)

def parse_xml(path):
    """string_table.xml -> {language: OrderedDict(key -> [texts])}"""
    xml = open(path, encoding='utf-8-sig').read()
    langs = {}
    for lm in re.finditer(r'<language id="([^"]+)">(.*?)</language>', xml, re.S):
        d = langs.setdefault(lm.group(1), OrderedDict())
        for k, v in re.findall(r'<entry id="([^"]+)"><!\[CDATA\[(.*?)\]\]></entry>', lm.group(2), re.S):
            d.setdefault(k, []).append(v)
    return langs

if __name__ == '__main__':
    if sys.argv[1] == 'roundtrip':
        data = open(sys.argv[2], 'rb').read()
        enc = encode_hashed(decode(data))
        print('identical' if enc == data else 'DIFFERENT', len(enc), len(data))
    elif sys.argv[1] == 'build':      # build <xml> <outdir> <prefix>
        import os
        for lang, ents in parse_xml(sys.argv[2]).items():
            if not ents: continue
            p = os.path.join(sys.argv[3], f'{sys.argv[4]}_{lang}.loc2')
            open(p, 'wb').write(encode(ents)); print('wrote', p, len(ents))
