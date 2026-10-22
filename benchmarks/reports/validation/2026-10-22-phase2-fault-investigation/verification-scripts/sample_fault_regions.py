import ctypes as c,ctypes.wintypes as w,pathlib,json,sys,datetime
root=pathlib.Path.cwd();out=root/'benchmarks/reports/validation/2026-10-22-phase2-fault-investigation';manifest=json.loads((out/'campaign.json').read_text());case=next(r for r in reversed(manifest['cases']) if r['status']=='running')
header=json.loads((out/case['case']/'capture.jsonl').read_text(encoding='utf-8').splitlines()[0]);pid=header['pid']
k=c.WinDLL('kernel32',use_last_error=True);p=c.WinDLL('psapi',use_last_error=True)
k.OpenProcess.argtypes=[w.DWORD,w.BOOL,w.DWORD];k.OpenProcess.restype=w.HANDLE
k.CloseHandle.argtypes=[w.HANDLE];k.QueryFullProcessImageNameW.argtypes=[w.HANDLE,w.DWORD,w.LPWSTR,c.POINTER(w.DWORD)]
class MBI(c.Structure):_fields_=[('base',c.c_void_p),('allocation_base',c.c_void_p),('allocation_protect',w.DWORD),('partition',w.WORD),('size',c.c_size_t),('state',w.DWORD),('protect',w.DWORD),('type',w.DWORD)]
k.VirtualQueryEx.argtypes=[w.HANDLE,c.c_void_p,c.POINTER(MBI),c.c_size_t];k.VirtualQueryEx.restype=c.c_size_t
p.GetMappedFileNameW.argtypes=[w.HANDLE,c.c_void_p,w.LPWSTR,w.DWORD];p.GetMappedFileNameW.restype=w.DWORD
h=k.OpenProcess(0x410,False,pid);assert h,c.get_last_error()
try:
    buf=c.create_unicode_buffer(32768);size=w.DWORD(len(buf));assert k.QueryFullProcessImageNameW(h,0,buf,c.byref(size));exe=buf.value
    assert pathlib.Path(exe).name in ('gfx_memory_stress.exe','gfx_memory_stress_symbols.exe')
    regions={};totals=dict(private=0,mapped=0,image=0);address=0
    while True:
        m=MBI();n=k.VirtualQueryEx(h,address,c.byref(m),c.sizeof(m))
        if not n:break
        if m.state==0x1000:
            name={0x20000:'private',0x40000:'mapped',0x1000000:'image'}.get(m.type,'other');totals[name]=totals.get(name,0)+m.size
            key=m.allocation_base or m.base or 0
            if key not in regions:
                file=''
                if name!='private' and p.GetMappedFileNameW(h,m.base,buf,len(buf)):file=buf.value
                regions[key]=dict(base=key,type=name,bytes=0,file=file)
            regions[key]['bytes']+=m.size
        next_address=(m.base or 0)+m.size
        if next_address<=address:break
        address=next_address
    destination=out/f"{case['case']}-virtual-map-{datetime.datetime.now().strftime('%H%M%S')}.json"
    destination.write_text(json.dumps(dict(pid=pid,executable=exe,utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),observational_only=True,totals=totals,allocations=sorted(regions.values(),key=lambda r:-r['bytes'])),indent=2)+'\n')
    print(json.dumps(dict(case=case['case'],pid=pid,totals=totals,output=str(destination))))
finally:k.CloseHandle(h)
