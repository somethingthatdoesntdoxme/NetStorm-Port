import struct
from PIL import Image
class Shp:
    def __init__(s,path):
        b=s.b=open(path,'rb').read(); p=0; s.conts=[]
        while b[p:p+4]==b'1.10':
            n=struct.unpack_from('<I',b,p+4)[0]
            s.conts.append([p+struct.unpack_from('<I',b,p+8+k*8)[0] for k in range(n)]); p+=8+n*8
    def frame(s,f):
        b=s.b
        hdr=struct.unpack_from('<6f6h',b,f-36)
        x0,y0,x1,y1=struct.unpack_from('<4i',b,f+8)
        w,h=x1-x0+1,y1-y0+1
        px=bytearray(w*h); a=bytearray(w*h); p=f+24
        for y in range(h):
            x=0
            while True:
                c=b[p]; p+=1
                if c==0: break
                if c==1: x+=b[p]; p+=1
                elif c&1==0:
                    v=b[p]; p+=1
                    for i in range(c>>1):
                        if x<w: px[y*w+x]=v; a[y*w+x]=255
                        x+=1
                else:
                    for i in range(c>>1):
                        if x<w: px[y*w+x]=b[p]; a[y*w+x]=255
                        x+=1; p+=1
        return dict(w=w,h=h,px=px,a=a,x0=x0,y0=y0,hdr=hdr)
    def image(s,fr,pal):
        im=Image.frombytes('P',(fr['w'],fr['h']),bytes(fr['px'])); im.putpalette(pal); im=im.convert('RGBA')
        im.putalpha(Image.frombytes('L',(fr['w'],fr['h']),bytes(fr['a']))); return im
