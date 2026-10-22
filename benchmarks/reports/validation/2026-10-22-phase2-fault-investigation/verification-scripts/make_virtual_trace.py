import pathlib
root=pathlib.Path.cwd();text=(root/'out/trace_own_heap.ps1').read_text()
for a,b in [('out/gbv-origin-heap-traces-v2','out/gbv-origin-virtual-traces'),('out/gbv-origin-heap-status-v2.json','out/gbv-origin-virtual-status.json'),('allocation-origin/inputs-after.json','retention-controls/inputs-after.json'),("@('cpu','full','no-tracking')","@('full','no-tracking')"),("'heap.etl'","'virtual.etl'"),("'heap-events.csv'","'virtual-events.csv'"),('GraphicsSDKOwnHeap-','GraphicsSDKPrivateVirtual-'),('-heap -Pids $child.Id','-on PROC_THREAD+LOADER+VIRT_ALLOC'),('-stackwalk HeapAlloc+HeapRealloc','-stackwalk VirtualAlloc+VirtualFree')]:
    assert a in text,a;text=text.replace(a,b)
text=text.replace("kernel_logger_touched=$false","nt_kernel_logger_touched=$false;private_system_session=$true;raw_system_metadata_local_only=$true")
text=text.replace("target='one directly launched native child'","target='own child; raw system-session metadata stays local; only target allocation rows are published'")
text=text.replace('heap trace','virtual allocation trace').replace('heap session','private virtual session')
(root/'out/trace_own_virtual.ps1').write_text(text)
print('Prepared separately named private system session; no NT Kernel Logger stop/control, no registry changes')
