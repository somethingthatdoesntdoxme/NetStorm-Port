import struct
def entries(path):
    b=open(path,'rb').read()
    h=struct.unpack_from('<7I',b,16); d0=h[6]
    p=h[4];out=[]
    while p<h[4]+h[5]:
        o,s=struct.unpack_from('<II',b,p);e=b.index(b'\0',p+8);out.append((b[p+8:e].decode(),b[d0+o:d0+o+s]));p=e+1
    return out
