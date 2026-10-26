import ctypes as c,hashlib,json,pathlib
from ctypes import wintypes as w
ROOT=pathlib.Path(__file__).resolve().parents[1]
binary=ROOT/'out/intel-crash-resume-capture/Release/gfx_memory_stress_symbols.exe'
class Symbol(c.Structure):
 _fields_=[('SizeOfStruct',w.ULONG),('TypeIndex',w.ULONG),('Reserved',c.c_ulonglong*2),('Index',w.ULONG),('Size',w.ULONG),('ModBase',c.c_ulonglong),('Flags',w.ULONG),('Value',c.c_ulonglong),('Address',c.c_ulonglong),('Register',w.ULONG),('Scope',w.ULONG),('Tag',w.ULONG),('NameLen',w.ULONG),('MaxNameLen',w.ULONG),('Name',c.c_char*1)]
dbg=c.WinDLL('dbghelp',use_last_error=True);kernel=c.WinDLL('kernel32',use_last_error=True)
kernel.GetCurrentProcess.restype=w.HANDLE;h=kernel.GetCurrentProcess()
dbg.SymSetOptions.argtypes=[w.DWORD];dbg.SymSetOptions(2|4|512|0x80000)
dbg.SymInitializeW.argtypes=[w.HANDLE,w.LPCWSTR,w.BOOL];assert dbg.SymInitializeW(h,str(binary.parent),False)
dbg.SymLoadModuleExW.argtypes=[w.HANDLE,w.HANDLE,w.LPCWSTR,w.LPCWSTR,c.c_ulonglong,w.DWORD,c.c_void_p,w.DWORD];dbg.SymLoadModuleExW.restype=c.c_ulonglong
base=dbg.SymLoadModuleExW(h,None,str(binary),None,0,0,None,0);assert base,c.get_last_error()
names=[];total=0
CALLBACK=c.WINFUNCTYPE(w.BOOL,c.POINTER(Symbol),w.ULONG,c.c_void_p)
@CALLBACK
def collect(p,size,context):
 global total
 total+=1;s=p.contents;name=c.string_at(c.addressof(s)+Symbol.Name.offset,s.NameLen).decode('utf-8',errors='replace')
 if 'gfx::' in name:names.append(name)
 return True
dbg.SymEnumSymbols.argtypes=[w.HANDLE,c.c_ulonglong,c.c_char_p,CALLBACK,c.c_void_p]
assert dbg.SymEnumSymbols(h,base,b'*',collect,None),c.get_last_error()
dbg.SymCleanup.argtypes=[w.HANDLE];assert dbg.SymCleanup(h)
assert len(names)>100,(total,names[:10])
receipt=dict(diagnostic_only=True,scope='Local PE/PDB symbol resolution; not a GPU execution, captured fault or driver symbol proof',
 executable_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),pdb_sha256=hashlib.sha256(binary.with_suffix('.pdb').read_bytes()).hexdigest(),
 total_symbols=total,sdk_symbols=len(names),sdk_symbol_examples=names[:20],symbol_server_used=False)
target=ROOT/'out/intel-crash-symbol-resolution.json';assert not target.exists();target.write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf-8')
print('PASS local workload PDB resolves '+str(len(names))+' gfx:: SDK symbols')
