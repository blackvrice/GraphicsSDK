"""Whole texture payload references. Parsing saved bytes never executes native/source code."""
import math
import struct
from linux_comparison import require

SIZES=((1,1),(3,2),(65,17),(129,7))
HALVES=(0,0x3400,0x3800,0x3a00,0x3c00,0xc000,0x4400)


def catalog():
    rows=[]
    def add(name,fmt,w,h,kind,color=None,depth=None):
        row=w*(8 if fmt==1 else 4);pitch=(row+255)&~255
        rows.append(dict(name=name+'.bin',format=fmt,width=w,height=h,row_bytes=row,row_pitch=pitch,size=pitch*h,kind=kind,color=color,depth=depth))
    for fmt in range(4):
        for w,h in SIZES:add(f'roundtrip-{fmt}-{w}x{h}',fmt,w,h,'roundtrip')
    for fmt in (0,1,3):
        for phase in range(3):add(f'clear-{fmt}-{phase}',fmt,65,17,'clear',(.25,.5,.75,1) if phase<2 else ((-2,.5,4,1) if fmt==1 else (1,0,.5,.25)))
    for phase in range(2):add(f'depth-{phase}',2,63,9,'clear',depth=.25)
    for count in (2,4,8):
        for n in range(count):add(f'mrt-{count}-{n}',(0,3,1)[n%3],65,17,'clear',(n&1,(n>>1)&1,(n>>2)&1,1))
        add(f'mrt-{count}-depth',2,65,17,'clear',depth=.75)
    return rows


def reference(row):
    data=bytearray(b'\xd3'*row['size']);fmt=row['format'];w=row['width'];seed=17+fmt*11+w;bpp=8 if fmt==1 else 4
    for y in range(row['height']):
        for x in range(w):
            base=y*row['row_pitch']+x*bpp
            if row['kind']=='roundtrip':
                if fmt==2:struct.pack_into('<f',data,base,((x+3*y+seed)%17)/16)
                elif fmt==1:
                    for c in range(4):struct.pack_into('<H',data,base+2*c,HALVES[(x*3+y*5+c+seed)%7])
                else:
                    for c in range(4):
                        byte=x*4+c;data[base+c]=(byte*37+y*19+seed*53+byte//251)&255
            elif fmt==2:struct.pack_into('<f',data,base,row['depth'])
            else:
                color=list(row['color'])
                if fmt==3:color[0],color[2]=color[2],color[0]
                for c,v in enumerate(color):
                    if fmt==1:struct.pack_into('<e',data,base+2*c,v)
                    else:data[base+c]=min(255,max(0,int(v*255+.5)))
    return bytes(data)


def verify_bytes(row,data):
    require(type(data) is bytes and len(data)==row['size'],'Texture payload byte count differs')
    if row['kind']=='roundtrip':require(data==reference(row),'Full texture roundtrip/row padding differs');return
    fmt=row['format'];bpp=8 if fmt==1 else 4
    for y in range(row['height']):
        base=y*row['row_pitch']
        require(data[base+row['row_bytes']:base+row['row_pitch']]==b'\xd3'*(row['row_pitch']-row['row_bytes']),'Clear readback padding overwritten')
        for x in range(row['width']):
            pos=base+x*bpp
            if fmt==2:
                value=struct.unpack_from('<f',data,pos)[0];require(math.isfinite(value) and abs(value-row['depth'])<1e-6,'Depth clear/load differs')
            else:
                color=list(row['color'])
                if fmt==3:color[0],color[2]=color[2],color[0]
                for c,wanted in enumerate(color):
                    value=struct.unpack_from('<e',data,pos+c*2)[0] if fmt==1 else data[pos+c]/255
                    require(math.isfinite(value) and abs(value-wanted)<=(.001 if fmt==1 else 1.01/255),'Color clear/load/HDR/swizzle differs')


def payloads(folder):
    rows=catalog();require({p.name for p in folder.iterdir()}=={r['name'] for r in rows},'Missing/extra full texture payloads')
    for row in rows:verify_bytes(row,(folder/row['name']).read_bytes())
    return sum(r['size'] for r in rows)


def result(value,fp):
    expected=dict(schema_version=1,contract='metal_texture_pass_v1',SDK_source_fingerprint=fp,images=44,passes=17,
        verified_bytes=sum(r['size'] for r in catalog()),texture_transfer_executed=True,clear_load_pass_executed=True,
        draw_executed=False,native_validation_accepted=False,physical_GPU_accepted=False,performance_accepted=False)
    require(all(k in value and type(value[k]) is type(v) and value[k]==v for k,v in expected.items()),'Texture result scope/count/source differs')
    require(type(value['assertions']) is int and value['assertions']>=1000,'Texture native assertions missing')
