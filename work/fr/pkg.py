import struct,zlib,sys
class Pkg:
    def __init__(s,path):
        s.f=open(path,'rb')  # lecture seule
        h=s.f.read(16)
        assert h[:4]==b'PKG\n'
        s.hs,s.es,s.n,s.nt=struct.unpack('>HHII',h[4:16])
        s.f.seek(s.hs)
        raw=s.f.read(s.n*s.es)
        s.ents=[]
        s.names_base=s.hs+s.n*s.es
        s.f.seek(s.names_base); s.names=s.f.read(s.nt)
        for i in range(s.n):
            hh,no,off,st,un=struct.unpack('>IIIII',raw[i*20:i*20+20])
            fl=no>>24; no&=0xffffff
            e=s.names.index(b'\0',no)
            s.ents.append((s.names[no:e].decode('latin1'),fl,off,st,un))
    def read(s,e):
        n,fl,off,st,un=e
        s.f.seek(off); d=s.f.read(st)
        if st!=un or fl: 
            try: d=zlib.decompress(d)
            except Exception: pass
        return d
