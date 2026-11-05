# DD Preset Tools: 게임 실행 파일에서 실행 시 패턴(src/sigs.inc)을 만들고 검증하는 도구.
#   DDPT_EXE=path/to/Darkest.exe python3 tools/sigs/gen_sigs.py
# 필요: python3, llvm-objdump. 각 패턴이 .text 에서 정확히 한 곳에만 맞고 기대값을 돌려주는지 확인함.
# 아래 '대상 목록'의 주소는 2026-10-30 빌드(stamp 0x6AE4C882) 기준. 새 빌드에서 패턴이 깨지면
# 그 빌드에서 해당 함수/참조를 다시 찾아 주소를 고친 뒤 다시 실행.
import re, sys, bisect, struct, os
from asm import p, addrs, txt, fasm, IB, ripref

TEXT_VA, TEXT_VS = p.secs['.text'][0], p.secs['.text'][1]
IMG = bytes(p.img)

def ins_index(a):
    i = bisect.bisect_left(addrs, a)
    assert addrs[i] == a, hex(a)
    return i

def ins_len(i):
    return addrs[i + 1] - addrs[i]

def find_in(func, regex, nth=0):
    hits = [a for a, t in fasm(func) if re.search(regex, t)]
    assert len(hits) > nth, (hex(func), regex, len(hits))
    return hits[nth]

def wild_positions(i):
    """byte positions (relative to instruction start) that change between builds"""
    a, L, t = addrs[i], ins_len(i), txt[i]
    b = IMG[a:a + L]
    w = set()
    tgt = None
    r = ripref(t)
    if r is not None and 'rip' in t:
        tgt = r
    m = re.match(r'(call|jmp|j\w+)\s+0x([0-9a-f]+)', t)
    if m:
        tgt = int(m.group(2), 16) - IB
    if tgt is not None:
        d = tgt - (a + L)
        for k in range(L - 4, -1, -1):
            if k + 4 <= L and struct.unpack_from('<i', b, k)[0] == d:
                w.update(range(k, k + 4)); break
        else:
            if m and L == 2:      # short jump rel8
                w.add(1)
            else:
                raise Exception('no disp found %x %s' % (a, t))
    return w

def window_bytes(i0, i1):
    """pattern bytes/mask for instructions i0..i1 inclusive"""
    pat, mask = [], []
    for i in range(i0, i1 + 1):
        a, L = addrs[i], ins_len(i)
        w = wild_positions(i)
        for k in range(L):
            pat.append(IMG[a + k]); mask.append(k not in w)
    return pat, mask

def count_matches(pat, mask, limit=3):
    # anchor on the longest run of fixed bytes
    best, cur, start, bstart = 0, 0, 0, 0
    for k, m in enumerate(mask + [False]):
        if m:
            if cur == 0: start = k
            cur += 1
            if cur > best: best, bstart = cur, start
        else:
            cur = 0
    key = bytes(pat[bstart:bstart + best])
    res = []
    lo, hi = TEXT_VA, TEXT_VA + TEXT_VS
    pos = IMG.find(key, lo, hi)
    while pos != -1:
        s = pos - bstart
        if all((not mask[k]) or IMG[s + k] == pat[k] for k in range(len(pat))):
            res.append(s)
            if len(res) >= limit: break
        pos = IMG.find(key, pos + 1, hi)
    return res

def build(site, forward_only=False, min_len=16):
    """grow an instruction window around site until the pattern is unique"""
    i = ins_index(site)
    i0, i1 = i, i
    turn = 0
    while True:
        pat, mask = window_bytes(i0, i1)
        if len(pat) >= min_len and sum(mask) >= 8:
            ms = count_matches(pat, mask)
            if len(ms) == 1:
                return pat, mask, addrs[i0]
        if forward_only or turn % 3 != 2:
            i1 += 1
        else:
            i0 -= 1
        turn += 1
        if i1 - i0 > 40:
            raise Exception('not unique around %x' % site)

# ---- target spec --------------------------------------------------------------------------
# kind: func  -> value = match address (must be a function start)
#       call  -> value = target of call/jmp rel32 at site instruction
#       rip   -> value = rip-relative target of site instruction (+adj)
#       imm   -> value = immediate/displacement found in site instruction equal to `expect`
T = []
def F(name, addr, nopdata=False): T.append(dict(name=name, kind='func', site=addr, expect=addr, nopdata=nopdata))
def C(name, site, expect): T.append(dict(name=name, kind='call', site=site, expect=expect))
def R(name, site, expect, adj=0): T.append(dict(name=name, kind='rip', site=site, expect=expect, adj=adj))
def I(name, site, expect, size=4): T.append(dict(name=name, kind='imm', site=site, expect=expect, size=size))

# hooked / called game functions
F('DrawUnequipButton', 0x6a8cb0); F('RIDragRender', 0x7cfda0); F('PaletteButton', 0x7fe910)
F('RosterPreShow', 0x6b5950); F('HeroSlotPreShow', 0x68d300); F('QuestSelectPreShow', 0x6994d0)
F('ProvisionPreShow', 0x695460); F('ItemGoldPrice', 0x57bbc0); F('ItemShardPrice', 0x57bc70)
F('RosterShowChar', 0x6b1460); F('TownRender', 0x4162b0); F('RenderAll', 0x273800)
F('DrawElement', 0x273fb0); F('ConfirmShow', 0x7c6b60); F('ModalRender', 0x7a6160)
F('SubtitleRender', 0x61cf70); F('HeroDragRender', 0x7cf060); F('SetInParty', 0x58b500)
F('RefreshParty', 0x697fa0); F('InvSetItem', 0x5df8f0); F('ItemCopy', 0x3cdf60)
F('ItemDtor', 0x3ce100); F('HeroRefresh', 0x481a40); F('Localize', 0x440e60); F('PlaySound', 0x2312d0)
F('ProvisionBuyOne', 0x697b60, nopdata=True)
# globals (site = an instruction that references them)
R('ItemClassList', find_in(0x6960c0, r'0x14118e0f8'), 0x118e0f8)
R('InputMode', find_in(0x6a8cb0, r'0x14118d514'), 0x118d514)
R('InputModeFlag', find_in(0x6a8cb0, r'0x14118525a'), 0x118525a)
R('GamePersist', [a for _, a, t in __import__('asm').xrefs(0x2a6d6b8, 0, 1)][0], 0x2a6d6b8)
R('TownUIPtr', find_in(0x697fa0, r'0x14118e360'), 0x118e360)
R('VT_QSlotIface', 0x7e3414, 0xee0838)
R('VT_HeroDragData', 0x1f7470, 0xeddca0)
R('MatrixCurrent', find_in(0x6a8cb0, r'0x1411852f8'), 0x11852f8)
R('SortButtonSpacing', find_in(0x6a8750, r'0x142b8e8b0'), 0x2b8e8b0)
R('CampaignPtr', find_in(0x556520, r'0x14118e138'), 0x118e138)
R('AltModeFlag', find_in(0x58b1f0, r'0x142a6d4bb'), 0x2a6d4bb)
I('ElemArray', 0x274d20, 0x1212ca0)
R('ElemCount', 0x274d0c, 0x2a96a84)
R('GlobalFade', 0x3e254e, 0x2a6e820)
# struct sizes / field offsets read from game code
I('HeroInvOffset', find_in(0x5d3590, r'lea\s+rcx, \[rsi \+ 0x1280\]'), 0x1280)
I('EntryStatusOffset', find_in(0x58b500, r'cmp\s+r8d, dword ptr \[rdi \+ 0x15b0\]'), 0x15b0)
I('ConfirmFadeOffset', 0x7c6b81, 0x3c, size=1)
I('AppearanceOffset', 0x5d3193 + 0x18 - 0x18, 0xb68)   # placeholder, fixed below

def fix_sites():
    # AppearanceOffset: 'cmp dword ptr [rsi + 0xb68], ecx' in 0x5d2330
    for t in T:
        if t['name'] == 'AppearanceOffset':
            t['site'] = find_in(0x5d2330, r'cmp\s+dword ptr \[rsi \+ 0xb68\], ecx')
fix_sites()

def lib_sites():
    import asm
    xs = asm.xrefs(0x2c3c570, 0, 1)
    # TrinketLibBegin: first reference followed (within 10 instr) by 'add rax, 0x488'
    for _, a, t in xs:
        i = ins_index(a)
        for k in range(i, i + 12):
            if re.match(r'add\s+\w+, 0x488$', txt[k]):
                return a, addrs[k]
    raise Exception('lib site')
a_lib, a_sz = lib_sites()
R('TrinketLibBegin', a_lib, 0x2c3c570)
I('LibEntrySize', a_sz, 0x488)

def find_ref(target, regex):
    import asm
    for _, a, t in asm.xrefs(target, 0, 1):
        if re.search(regex, t): return a
    raise Exception('ref %x' % target)
R('RarityBegin', find_ref(0x2c3c588, r'mov\s+\w+, qword ptr'), 0x2c3c588)
R('HeroClassBegin', find_ref(0x2c3c308, r'mov\s+\w+, qword ptr'), 0x2c3c308)
R('HeroClassBeginAlt', find_ref(0x2c3c2e8, r'mov\s+\w+, qword ptr'), 0x2c3c2e8)

def hero_class_site():
    for i in range(len(txt) - 3):
        m = re.match(r'mov\s+(\w+), qword ptr \[\w+ \+ 0x1320\]', txt[i])
        if m and re.search(r'\[' + m.group(1) + r' \+ 0xd4\]', txt[i + 1]):
            return addrs[i]
I('HeroClassOffset', hero_class_site(), 0x1320)

def guid_site():
    # HeroDragRender reads the hero guid; fall back to the first 'mov e.., [r.. + 0x137c]'
    for a, t in fasm(0x7cf060):
        if '+ 0x137c]' in t: return a
    for a, t in zip(addrs, txt):
        if re.match(r'mov\s+e\w+, dword ptr \[\w+ \+ 0x137c\]', t): return a
I('HeroGuidOffset', guid_site(), 0x137c)

# ---- resolve like the runtime does --------------------------------------------------------
def runtime_resolve(t, pat, mask, start):
    ms = count_matches(pat, mask)
    if len(ms) != 1: return None, 'matches=%d' % len(ms)
    s = ms[0]
    off = t['site'] - start          # offset of site instruction inside pattern
    i = ins_index(t['site']); L = ins_len(i); a = s + off
    if t['kind'] == 'func':
        return s, ''
    if t['kind'] in ('call', 'rip'):
        w = sorted(wild_positions(i))
        d = struct.unpack_from('<i', IMG, a + w[0])[0]
        return a + L + d + t.get('adj', 0), ''
    if t['kind'] == 'imm':
        return t['_val'], ''

out, bad = [], 0
for t in T:
    i = ins_index(t['site'])
    if t['kind'] == 'func':
        assert t['nopdata'] or p.is_start(t['site']), t['name']
        pat, mask, start = build(t['site'], forward_only=True, min_len=24)
        info = dict(field=-1, next=-1)
    elif t['kind'] in ('call', 'rip'):
        import asm
        cands = [t['site']] + [a for _, a, x in asm.xrefs(t['expect'] - t.get('adj', 0), 0, 1) if a != t['site']]
        for c in cands[:60]:
            try:
                pat, mask, start = build(c); t['site'] = c; break
            except Exception:
                continue
        else:
            raise Exception('no unique site for ' + t['name'])
        i = ins_index(t['site'])
        w = sorted(wild_positions(i))
        info = dict(field=t['site'] - start + w[0], next=t['site'] - start + ins_len(i))
    else:
        a, L = t['site'], ins_len(i)
        b = IMG[a:a + L]
        fmt = '<i' if t['size'] == 4 else '<B'
        pos = [k for k in range(L - t['size'] + 1) if struct.unpack_from(fmt, b, k)[0] == t['expect']]
        assert pos, (t['name'], txt[i])
        pat, mask, start = build(t['site'])
        # wildcard the field itself, then re-check uniqueness by growing if necessary
        fpos = t['site'] - start + pos[-1]
        for k in range(t['size']): mask[fpos + k] = False
        while len(count_matches(pat, mask)) != 1:
            i1 = ins_index(start + len(pat))
            ext, emask = window_bytes(i1, i1)
            pat += ext; mask += emask
        t['_val'] = struct.unpack_from(fmt, IMG, t['site'] + pos[-1])[0]
        info = dict(field=fpos, next=-1)
    val, err = runtime_resolve(t, pat, mask, start)
    ok = val == t['expect']
    bad += not ok
    sig = ' '.join('%02X' % b if m else '??' for b, m in zip(pat, mask))
    print('%-20s %-4s %s -> %s %s' % (t['name'], t['kind'], hex(t['expect']), hex(val) if val is not None else None, 'OK' if ok else 'FAIL ' + err), file=sys.stderr)
    out.append((t, sig, info))

kindc = dict(func='SigKind::Func', call='SigKind::Rel', rip='SigKind::Rel', imm='SigKind::Imm')
with open(os.environ.get('DDPT_OUT', 'sigs.inc'), 'w') as f:
    f.write('// Generated by tools/gen_sigs.py from Darkest.exe stamp 0x%08X. Do not edit by hand.\n' % p.stamp)
    f.write('// name, kind, pattern, field offset, instruction-end offset (Rel), field size (Imm), adjust, flags, value in that build\n')
    for t, sig, info in out:
        f.write('{ "%s", %s, "%s", %d, %d, %d, %d, %d, 0x%X },\n' % (
            t['name'], kindc[t['kind']], sig, info['field'], info['next'], t.get('size', 4) if t['kind'] == 'imm' else 0,
            t.get('adj', 0), 1 if t.get('nopdata') else 0, t['expect']))
print('failures:', bad, file=sys.stderr)
