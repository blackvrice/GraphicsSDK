"""Whole draw image references; never execute archived shader/native code."""
import math
import struct
from linux_comparison import require


def catalog():
    colors={'procedural':(1,0,0,1),'vertex':(0,1,0,1),'index16':(0,0,1,1),'index32':(0,0,1,1),'instance':(0,0,1,1),
        'snapshots':None,'sampled':(.5,.5,.5,1),'point-clamp':(0,1,0,1),'point-repeat':(0,0,1,1),'linear-center':(.5,.5,.5,1),
        'linear-repeat':(0,0,1,1),'mrt-color':(0,0,1,1),'mrt-bgra':(1,0,0,1),'mrt-depth':None,'blend-scissor':None,
        'depth-only':None,'hdr':(-2,.5,4,1),'bgra':(1,0,0,1)}
    rows=[]
    for name,color in colors.items():
        fmt=1 if name=='hdr' else 2 if name in ('mrt-depth','depth-only') else 3 if name in ('mrt-bgra','bgra') else 0
        row_bytes=65*(8 if fmt==1 else 4);pitch=(row_bytes+255)&~255
        rows.append(dict(name=name+'.bin',format=fmt,width=65,height=17,row_bytes=row_bytes,row_pitch=pitch,size=pitch*17,
            color=color,depth=.25 if name=='mrt-depth' else .5))
    return rows


def color_at(row,x,y):
    if row['name']=='snapshots.bin':return (1,0,0,1) if x<32 else (0,1,0,1)
    if row['name']=='blend-scissor.bin':return (.5,0,.5,1) if 5<=x<24 and 3<=y<14 else (0,0,1,1)
    return row['color']


def reference(row):
    data=bytearray(b'\xa7'*row['size']);fmt=row['format'];bpp=8 if fmt==1 else 4
    for y in range(row['height']):
        for x in range(row['width']):
            pos=y*row['row_pitch']+x*bpp
            if fmt==2:struct.pack_into('<f',data,pos,row['depth']);continue
            color=list(color_at(row,x,y))
            if fmt==3:color[0],color[2]=color[2],color[0]
            for c,value in enumerate(color):
                if fmt==1:struct.pack_into('<e',data,pos+c*2,value)
                else:data[pos+c]=min(255,max(0,int(value*255+.5)))
    return bytes(data)


def verify_bytes(row,data):
    require(type(data) is bytes and len(data)==row['size'],'Draw image byte count differs');fmt=row['format'];bpp=8 if fmt==1 else 4
    for y in range(row['height']):
        base=y*row['row_pitch'];require(data[base+row['row_bytes']:base+row['row_pitch']]==b'\xa7'*(row['row_pitch']-row['row_bytes']),'Draw row/final padding overwritten')
        for x in range(row['width']):
            pos=base+x*bpp
            if fmt==2:
                value=struct.unpack_from('<f',data,pos)[0];require(math.isfinite(value) and abs(value-row['depth'])<1e-6,'Rasterized depth differs');continue
            color=list(color_at(row,x,y))
            if fmt==3:color[0],color[2]=color[2],color[0]
            for c,wanted in enumerate(color):
                value=struct.unpack_from('<e',data,pos+c*2)[0] if fmt==1 else data[pos+c]/255
                require(math.isfinite(value) and abs(value-wanted)<=(.002 if fmt==1 else 1.01/255),'Full draw/vertex/index/instance/binding/sampler/depth/blend image differs')


def payloads(folder):
    rows=catalog();require({p.name for p in folder.iterdir()}=={r['name'] for r in rows},'Missing/extra graphics image payloads')
    for row in rows:verify_bytes(row,(folder/row['name']).read_bytes())
    return sum(r['size'] for r in rows)


def result(value,fp):
    expected=dict(schema_version=1,contract='metal_graphics_v1',SDK_source_fingerprint=fp,images=18,recorded_draws=20,
        verified_bytes=sum(r['size'] for r in catalog()),draw_executed=True,native_validation_accepted=False,physical_GPU_accepted=False,performance_accepted=False)
    require(all(k in value and type(value[k]) is type(v) and value[k]==v for k,v in expected.items()),'Graphics result source/count/scope differs')
    require(type(value['assertions']) is int and value['assertions']>=1000,'Graphics native assertions missing')
