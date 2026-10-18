"""Build icons from icons.txt: shape maps -> auto top-left shading -> 1px outline -> 16x16.

Reads art/palette.gpl next to this folder (outline, steel0-3, panel, white). The path is
this file's directory, so the working directory does not matter.
Writes the runtime sheet, accent sheet, and icons_sheet.json to assets/icons/.
Per-icon PNGs and rarity_preview.png stay beside this script.
"""
import os,re,json
from PIL import Image,ImageDraw
HERE=os.path.dirname(os.path.abspath(__file__))
RUNTIME=os.path.normpath(os.path.join(HERE,'..','..','assets','icons'))
PALETTE=os.path.normpath(os.path.join(HERE,'..','palette.gpl'))
P={}
for l in open(PALETTE):
    m=re.match(r'\s*(\d+)\s+(\d+)\s+(\d+)\s+(\S+)',l)
    if m: P[m.group(4)]=tuple(map(int,m.groups()[:3]))
O=P['outline']; S=[P['steel0'],P['steel1'],P['steel2'],P['steel3']]
icons={};cur=None
for line in open(HERE+'/icons.txt'):
    line=line.rstrip('\n')
    if not line or line.startswith('#') and not cur: continue
    if line.startswith('['): cur=line[1:-1]; icons[cur]=[]; continue
    if cur: icons[cur].append(line)
R=range(-2,3)   # 5x5 window smooths staircase edges on diagonals
def shade(solid,x,y):
    """Direction-aware shade of (x,y) in a pixel set: 3 = top-left corner, 2 = light, 1 = mid, 0 = dark."""
    nx=sum(dx for dx in R for dy in R if (dx or dy) and (x+dx,y+dy) not in solid)
    ny=sum(dy for dx in R for dy in R if (dx or dy) and (x+dx,y+dy) not in solid)
    edge=any((x+dx,y+dy) not in solid for dx in(-1,0,1) for dy in(-1,0,1))
    lit=-(nx+ny) if edge else 0                       # only edge pixels shade; interiors stay mid
    if edge and lit==0 and (nx or ny): lit=-ny or -nx # diagonal tie: prefer the top/bottom side consistently
    tl=((x-1,y) not in solid) and ((x,y-1) not in solid)
    return 3 if (tl and lit>0) else 2 if lit>0 else 0 if lit<0 else 1
ACC=[(160,160,160),(208,208,208),(255,255,255),(255,255,255)]   # accent ramp: shade, mid, light, highlight
def build(rows):
    shape={(x,y):ch for y,r in enumerate(rows) for x,ch in enumerate(r) if ch in '#xa'}
    xs=[p[0] for p in shape]; ys=[p[1] for p in shape]
    w,h=max(xs)-min(xs)+1,max(ys)-min(ys)+1; ox=2+(12-w)//2-min(xs); oy=2+(12-h)//2-min(ys)
    shape={(x+ox,y+oy):c for (x,y),c in shape.items()}
    metal={p for p,c in shape.items() if c in '#a'}     # accents shade as part of the solid
    accent={p for p,c in shape.items() if c=='a'}
    im=Image.new('RGBA',(16,16),(0,0,0,0)); ac=Image.new('RGBA',(16,16),(0,0,0,0))
    for p in accent: ac.putpixel(p,ACC[shade(accent,*p)]+(255,))
    for (x,y) in metal:
        if (x,y) in accent: im.putpixel((x,y),S[1]+(255,)); continue   # base: accent = flat mid grey
        im.putpixel((x,y),S[shade(metal,x,y)]+(255,))
    for (x,y) in []:
        # outward normal from all 8 empty neighbours; light comes from the top-left (-1,-1)
        nx=sum(dx for dx in R for dy in R if (dx or dy) and (x+dx,y+dy) not in metal)
        ny=sum(dy for dx in R for dy in R if (dx or dy) and (x+dx,y+dy) not in metal)
        edge=any((x+dx,y+dy) not in metal for dx in(-1,0,1) for dy in(-1,0,1))
        lit=-(nx+ny) if edge else 0   # only edge pixels shade; interiors stay mid                      # >0 faces the light, <0 faces away
        if edge and lit==0 and (nx or ny): lit=-ny or -nx   # diagonal tie: prefer the top/bottom side consistently
        tl=((x-1,y) not in metal) and ((x,y-1) not in metal)
        c=S[3] if (tl and lit>0) else S[2] if lit>0 else S[0] if lit<0 else S[1]
        im.putpixel((x,y),c+(255,))
    for p,c in shape.items():
        if c=='x': im.putpixel(p,O+(255,))
    for (x,y) in list(shape):
        for d in((1,0),(-1,0),(0,1),(0,-1)):
            q=(x+d[0],y+d[1])
            if q not in shape: im.putpixel(q,O+(255,))
    return im,ac
os.makedirs(RUNTIME,exist_ok=True)
sheet=Image.new('RGBA',(6*16,((len(icons)+5)//6)*16),(0,0,0,0)); asheet=sheet.copy(); meta={}; ACCS={}
for i,(n,rows) in enumerate(icons.items()):
    im,ac=build(rows); im.save(f'{HERE}/icon_{n}.png'); ac.save(f'{HERE}/icon_{n}_accent.png'); ACCS[n]=(im,ac)
    x,y=(i%6)*16,(i//6)*16; sheet.paste(im,(x,y)); asheet.paste(ac,(x,y)); meta[n]=dict(x=x,y=y,w=16,h=16)
sheet.save(RUNTIME+'/icons_sheet.png'); asheet.save(RUNTIME+'/icons_accent_sheet.png'); json.dump(meta,open(RUNTIME+'/icons_sheet.json','w'),indent=1)
bg=P['panel']; L=P['white']; cw=84
pv=Image.new('RGB',(6*cw+8,2*(16+64+28)+12),bg); d=ImageDraw.Draw(pv)
for i,n in enumerate(icons):
    im=Image.open(f'{HERE}/icon_{n}.png'); x=8+(i%6)*cw; y=8+(i//6)*(16+64+28)
    pv.paste(im,(x,y),im); b=im.resize((64,64),Image.NEAREST); pv.paste(b,(x+18,y),b)  # 1x at top-left, 4x beside/below
    d.text((x,y+68),n,fill=L)
pv.save(HERE+'/icons_preview.png'); print(len(icons),'icons')

# rarity preview: base + accent * tint, 5 rarities, 1x and 4x
RAR=[('common','9DA3AD'),('uncommon','5EC45E'),('rare','4A90E2'),('epic','A35BD9'),('legendary','F0902E')]
def tinted(im,ac,hexc):
    t=tuple(int(hexc[i:i+2],16) for i in (0,2,4)); out=im.copy()
    for x in range(16):
        for y in range(16):
            a=ac.getpixel((x,y))
            if a[3]: out.putpixel((x,y),tuple(a[k]*t[k]//255 for k in range(3))+(255,))
    return out
names=list(icons); CW=88; RH=16+64+10
rp=Image.new('RGB',(110+len(names)*CW,len(RAR)*RH+28),bg); d=ImageDraw.Draw(rp)
for j,n in enumerate(names): d.text((110+j*CW,4),n[:10],fill=L)
for r,(rn,hx) in enumerate(RAR):
    y=20+r*RH; d.text((6,y+4),rn,fill=tuple(int(hx[i:i+2],16) for i in (0,2,4))); d.text((6,y+18),'#'+hx,fill=L)
    for j,n in enumerate(names):
        t=tinted(*ACCS[n],hx); x=110+j*CW
        rp.paste(t,(x,y),t); b=t.resize((64,64),Image.NEAREST); rp.paste(b,(x+18,y),b)
rp.save(HERE+'/rarity_preview.png')
