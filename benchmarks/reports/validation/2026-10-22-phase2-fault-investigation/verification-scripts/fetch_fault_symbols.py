import pathlib,struct,uuid,urllib.request,json,hashlib
root=pathlib.Path.cwd();cache=root/'out/fault-public-symbols';cache.mkdir(exist_ok=False)
records=[]
for dll in ('D3D12Core.dll','d3d12SDKLayers.dll'):
    image=pathlib.Path('C:/Windows/System32')/dll;data=image.read_bytes();pe=struct.unpack_from('<I',data,0x3c)[0];assert data[pe:pe+4]==b'PE\0\0'
    count=struct.unpack_from('<H',data,pe+6)[0];optional=pe+24;size=struct.unpack_from('<H',data,pe+20)[0];assert struct.unpack_from('<H',data,optional)[0]==0x20b
    sections=[]
    for i in range(count):
        p=optional+size+i*40;virtualSize,virtualAddress,rawSize,rawPointer=struct.unpack_from('<IIII',data,p+8);sections.append((virtualAddress,max(virtualSize,rawSize),rawPointer))
    def offset(rva):
        for start,length,raw in sections:
            if start<=rva<start+length:return raw+rva-start
        raise ValueError('RVA not in PE sections')
    rva,debugSize=struct.unpack_from('<II',data,optional+112+6*8);debug=offset(rva);found=None
    for i in range(debugSize//28):
        values=struct.unpack_from('<IIHHIIII',data,debug+i*28)
        if values[4]!=2:continue
        block=data[values[7]:values[7]+values[5]]
        if not block.startswith(b'RSDS'):continue
        guid=uuid.UUID(bytes_le=block[4:20]);age=struct.unpack_from('<I',block,20)[0];name=pathlib.PureWindowsPath(block[24:].split(b'\0')[0].decode()).name
        found=(guid,age,name);break
    assert found is not None
    guid,age,name=found;key=guid.hex.upper()+format(age,'X');url=f'https://msdl.microsoft.com/download/symbols/{name}/{key}/{name}'
    target=cache/name;print('Fetching official public symbol '+name+' '+key,flush=True)
    request=urllib.request.Request(url,headers={'User-Agent':'GraphicsSDK-own-software-diagnostics'})
    with urllib.request.urlopen(request,timeout=60) as response:
        payload=response.read(96*1024*1024+1);assert len(payload)<=96*1024*1024
    assert payload.startswith(b'Microsoft C/C++ MSF'),payload[:32]
    target.write_bytes(payload)
    records.append(dict(image=str(image),image_sha256=hashlib.sha256(data).hexdigest(),pdb=str(target),pdb_sha256=hashlib.sha256(payload).hexdigest(),bytes=len(payload),guid=str(guid),age=age,url=url,redistributed=False))
(cache/'symbols.json').write_text(json.dumps(records,indent=2)+'\n')
print('Public PDBs remain local; only identities/hashes and resolved function names are evidence',flush=True)
