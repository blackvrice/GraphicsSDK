import ctypes as c,ctypes.wintypes as w,pathlib,json,struct,uuid,gzip,hashlib,collections
root=pathlib.Path.cwd();out=root/'benchmarks/reports/validation/2026-10-22-phase2-fault-investigation';cache=root/'out/fault-public-symbols';downloads=json.loads((cache/'symbols.json').read_text())
class Symbol(c.Structure):
    _fields_=[('SizeOfStruct',w.ULONG),('TypeIndex',w.ULONG),('Reserved',c.c_ulonglong*2),('Index',w.ULONG),('Size',w.ULONG),('ModBase',c.c_ulonglong),('Flags',w.ULONG),('Value',c.c_ulonglong),('Address',c.c_ulonglong),('Register',w.ULONG),('Scope',w.ULONG),('Tag',w.ULONG),('NameLen',w.ULONG),('MaxNameLen',w.ULONG),('Name',c.c_char*1)]
class ModuleInfo(c.Structure):
    _fields_=[('SizeOfStruct',w.DWORD),('BaseOfImage',c.c_ulonglong),('ImageSize',w.DWORD),('TimeDateStamp',w.DWORD),('CheckSum',w.DWORD),('NumSyms',w.DWORD),('SymType',c.c_int),('ModuleName',c.c_char*32),('ImageName',c.c_char*256),('LoadedImageName',c.c_char*256),('LoadedPdbName',c.c_char*256),('CVSig',w.DWORD),('CVData',c.c_char*780),('PdbSig',w.DWORD),('PdbSig70',c.c_ubyte*16),('PdbAge',w.DWORD),('PdbUnmatched',w.BOOL),('DbgUnmatched',w.BOOL),('LineNumbers',w.BOOL),('GlobalSymbols',w.BOOL),('TypeInfo',w.BOOL),('SourceIndexed',w.BOOL),('Publics',w.BOOL),('MachineType',w.DWORD),('Reserved',w.DWORD)]
assert c.sizeof(Symbol)==88
dbg=c.WinDLL('dbghelp',use_last_error=True);kernel=c.WinDLL('kernel32',use_last_error=True);kernel.GetCurrentProcess.restype=w.HANDLE;handle=kernel.GetCurrentProcess()
dbg.SymSetOptions.argtypes=[w.DWORD];dbg.SymSetOptions.restype=w.DWORD
dbg.SymInitializeW.argtypes=[w.HANDLE,w.LPCWSTR,w.BOOL];dbg.SymInitializeW.restype=w.BOOL
dbg.SymLoadModuleExW.argtypes=[w.HANDLE,w.HANDLE,w.LPCWSTR,w.LPCWSTR,c.c_ulonglong,w.DWORD,c.c_void_p,w.DWORD];dbg.SymLoadModuleExW.restype=c.c_ulonglong
dbg.SymFromAddr.argtypes=[w.HANDLE,c.c_ulonglong,c.POINTER(c.c_ulonglong),c.POINTER(Symbol)];dbg.SymFromAddr.restype=w.BOOL
dbg.SymGetModuleInfo64.argtypes=[w.HANDLE,c.c_ulonglong,c.POINTER(ModuleInfo)];dbg.SymGetModuleInfo64.restype=w.BOOL
dbg.SymCleanup.argtypes=[w.HANDLE];dbg.SymCleanup.restype=w.BOOL
dbg.SymSetOptions(0x2|0x4|0x200|0x1000|0x80000);assert dbg.SymInitializeW(handle,str(cache),False),c.get_last_error()
loaded={};memo={}
try:
    for i,r in enumerate(downloads,1):
        image=pathlib.Path(r['image']);data=image.read_bytes();assert hashlib.sha256(data).hexdigest()==r['image_sha256'];pe=struct.unpack_from('<I',data,0x3c)[0];size=struct.unpack_from('<I',data,pe+24+56)[0];base=i*0x100000000
        assert dbg.SymLoadModuleExW(handle,None,str(image),None,base,size,None,0)==base,c.get_last_error()
        loaded[image.name.lower()]=dict(base=base,metadata=r)
    def resolve(module,offset):
        key=(module.lower(),offset)
        if key in memo:return memo[key]
        data=loaded[key[0]];base=data['base'];storage=c.create_string_buffer(c.sizeof(Symbol)+4096);symbol=c.cast(storage,c.POINTER(Symbol));symbol.contents.SizeOfStruct=c.sizeof(Symbol);symbol.contents.MaxNameLen=4096;displacement=c.c_ulonglong()
        success=dbg.SymFromAddr(handle,base+offset,c.byref(displacement),symbol)
        result=dict(module=module,offset=offset,resolved=bool(success))
        if success:
            result.update(symbol=c.string_at(c.addressof(storage)+Symbol.Name.offset,symbol.contents.NameLen).decode('utf-8',errors='replace'),displacement=displacement.value,symbol_size=symbol.contents.Size,symbol_tag=symbol.contents.Tag)
        else:result['win32_error']=c.get_last_error()
        memo[key]=result;return result
    paths=json.loads((out/'virtual-call-paths.json').read_text());results=[]
    for case in paths:
        chains=collections.Counter();examples={}
        data=gzip.decompress((root/case['filtered_events']['path']).read_bytes()).decode()
        for line in data.splitlines():
            event=json.loads(line)
            if event['event']!='VirtualAlloc' or 'COMMIT' not in event['flags']:continue
            resolved=[resolve(f['module'],f['offset']) for f in event['stack'] if f['module'] and f['module'].lower() in loaded]
            signature=tuple((r['module'],r.get('symbol','unresolved')) for r in resolved)
            if signature:
                chains[signature]+=event['bytes'];examples.setdefault(signature,dict(bytes=event['bytes'],time_us=event['time_us'],frames=resolved))
        top=[dict(traced_commit_call_bytes=size,chain=list(signature),example=examples[signature]) for signature,size in chains.most_common(10)]
        results.append(dict(case=case['case'],top_public_symbol_chains=top))
        if top:print(case['case'],top[0]['traced_commit_call_bytes'],[r[1] for r in top[0]['chain']],flush=True)
    identities=[]
    for name,entry in loaded.items():
        info=ModuleInfo();info.SizeOfStruct=c.sizeof(info);assert dbg.SymGetModuleInfo64(handle,entry['base'],c.byref(info)),c.get_last_error();expected=entry['metadata']
        assert info.SymType==3 and not info.PdbUnmatched and str(uuid.UUID(bytes_le=bytes(info.PdbSig70)))==expected['guid'] and info.PdbAge==expected['age'],(name,info.SymType,info.PdbAge)
        identities.append(dict(module=name,sym_type=info.SymType,pdb_guid=expected['guid'],pdb_age=info.PdbAge,pdb_sha256=expected['pdb_sha256'],image_sha256=expected['image_sha256'],url=expected['url'],pdb_redistributed=False))
    (out/'resolved-virtual-call-paths.json').write_text(json.dumps(dict(public_PDB_identity_matches=identities,cases=results,unique_frames=list(memo.values())),indent=2)+'\n',encoding='utf-8')
finally:assert dbg.SymCleanup(handle)
