import struct,bisect
class PE:
    def __init__(s,path):
        b=open(path,'rb').read(); s.raw=b
        pe=struct.unpack_from('<I',b,0x3c)[0]; s.stamp=struct.unpack_from('<I',b,pe+8)[0]
        n=struct.unpack_from('<H',b,pe+6)[0]; opt=struct.unpack_from('<H',b,pe+20)[0]
        s.secs={}
        for i in range(n):
            o=pe+24+opt+40*i; nm=b[o:o+8].rstrip(b'\0').decode(); vs,va,rs,rp=struct.unpack_from('<IIII',b,o+8); s.secs[nm]=(va,vs,rp,rs)
        size=max(va+max(vs,rs) for va,vs,rp,rs in s.secs.values())
        s.img=bytearray(size)
        for va,vs,rp,rs in s.secs.values(): s.img[va:va+rs]=b[rp:rp+rs]
        va,vs,_,_=s.secs['.pdata']
        s.funcs=[]
        for o in range(va,va+vs-11,12):
            a,e,u=struct.unpack_from('<III',s.img,o)
            if a: s.funcs.append((a,e))
        s.funcs.sort(); s.starts=[f[0] for f in s.funcs]
    def func_at(s,rva):
        i=bisect.bisect_right(s.starts,rva)-1
        return s.funcs[i] if i>=0 and s.funcs[i][0]<=rva<s.funcs[i][1] else None
    def is_start(s,rva):
        i=bisect.bisect_left(s.starts,rva); return i<len(s.starts) and s.starts[i]==rva
