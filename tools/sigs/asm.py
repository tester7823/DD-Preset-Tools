import re,bisect,os,sys,subprocess
from pe import PE
EXE=os.environ.get('DDPT_EXE','Darkest.exe')
p=PE(EXE)
IB=0x140000000
_asm=EXE+'.asm'
if not os.path.exists(_asm) or os.path.getmtime(_asm)<os.path.getmtime(EXE):
    with open(_asm,'w') as fo:
        subprocess.run(['llvm-objdump','-d','--no-show-raw-insn','--x86-asm-syntax=intel',EXE],stdout=fo,check=True)
lines=open(_asm).read().split('\n')
addrs=[];txt=[]
for l in lines:
    m=re.match(r'\s*([0-9a-f]+):\s*(.*)',l)
    if m: addrs.append(int(m.group(1),16)-IB); txt.append(m.group(2).strip())
def fasm(rva):
    f=p.func_at(rva); a,e=f if f else (rva,rva+0x80)
    i=bisect.bisect_left(addrs,a); out=[]
    while i<len(addrs) and addrs[i]<e: out.append((addrs[i],txt[i])); i+=1
    return out
def calls(rva):
    r=[]
    for a,t in fasm(rva):
        m=re.match(r'(call|jmp)\s+0x([0-9a-f]+)\b',t)
        if m: r.append((a,int(m.group(2),16)-IB,m.group(1)))
    return r
# xref index for direct calls
import collections
callers=collections.defaultdict(list)
for a,t in zip(addrs,txt):
    m=re.match(r'(call|jmp)\s+0x([0-9a-f]+)\b',t)
    if m: callers[int(m.group(2),16)-IB].append(a)
def ripref(t):
    m=re.search(r'# 0x([0-9a-f]+)',t); return int(m.group(1),16)-IB if m else None
def cstr(rva,n=60):
    b=p.img[rva:rva+n]; z=b.find(b'\0'); b=b[:z if z>=0 else n]
    try:
        s=b.decode('ascii')
        return s if s and all(32<=ord(c)<127 for c in s) else None
    except: return None
def strs(rva):
    out=[]
    for a,t in fasm(rva):
        r=ripref(t)
        if r and 'lea' in t:
            s=cstr(r)
            if s and len(s)>2: out.append(s)
    return out
_xr=None
def xrefs(target,lo=0,hi=8):
    global _xr
    if _xr is None:
        _xr=collections.defaultdict(list)
        for a,t in zip(addrs,txt):
            r=ripref(t)
            if r: _xr[r].append((a,t))
    out=[]
    for d in range(lo,hi): out+= [(target+d,a,t) for a,t in _xr.get(target+d,[])]
    return out
