import pathlib,json,csv,re,collections,hashlib
root=pathlib.Path.cwd();base=root/'out/gbv-origin-heap-traces-v2';report=root/'benchmarks/reports/validation/2026-10-22-phase2-fault-investigation'
status=json.loads((root/'out/gbv-origin-heap-status-v2.json').read_text(encoding='utf-8-sig'));assert status['completed'] and status['error'] is None and len(status['processes'])==6
skip={'ntdll.dll','kernelbase.dll','kernel32.dll','ucrtbase.dll','vcruntime140.dll','vcruntime140_1.dll','msvcp140.dll'}
results=[]
for case in status['processes']:
    assert case['session_started'] and case['session_stopped'] and case['exit_code']==case['export_exit']==0
    folder=base/f"adapter-{case['adapter_index']}-{case['mode']}";native=[json.loads(s) for s in (folder/'memory.jsonl').read_text(encoding='utf-8').splitlines()]
    assert native[0]['pid']==case['pid'] and native[-1]['passed']
    modules=[r for r in native if r['type']=='module'];live={};pending=None;totals=collections.Counter();byModule=collections.Counter();byStack=collections.Counter();large=[];decommits=[];maxLive=0
    def module(pc):
        for m in modules:
            if m['base']<=pc<m['base']+m['bytes']:return dict(module=pathlib.PureWindowsPath(m['path']).name,offset=pc-m['base'],address=pc)
        return dict(module=None,offset=None,address=pc)
    def commit():
        global pending,maxLive
        if pending is None:return
        frames=[module(pc) for pc in pending['stack']]
        caller=next((f['module'] for f in frames if f['module'] and f['module'].lower() not in skip),'unresolved')
        size=pending['size'];totals['traced_allocation_bytes']+=size;totals['traced_allocations']+=1
        byModule[caller]+=size;stackKey=tuple((f['module'],f['offset'] if f['module'] else f['address']) for f in frames)
        byStack[stackKey]+=size
        key=(pending['heap'],pending['address'])
        if key in live:totals['reused_without_seen_free']+=1
        live[key]=size
        if size>=1024*1024:large.append(dict(bytes=size,time_us=pending['time'],first_resolved_non_allocator_module=caller,stack=frames))
        if pending['stack']:totals['allocations_with_stack']+=1
        pending=None
    path=folder/'heap-events.csv';header=''
    with path.open(encoding='utf-8-sig',errors='strict',newline='') as f:
        for raw in csv.reader(f):
            row=[x.strip() for x in raw]
            if not row:continue
            event=row[0]
            if event.startswith('OS Version:'):header=','.join(row);continue
            if event=='Stack' and len(row)>4 and row[1]!='TimeStamp':
                if pending is not None:
                    assert int(row[2])==pending['tid'],'Stack thread correlation mismatch'
                    pending['stack'].append(int(row[4],16))
                continue
            commit()
            if len(row)<2 or row[1]=='TimeStamp':continue
            if event=='HeapAlloc':
                pending=dict(heap=int(row[4],16),address=int(row[5],16),size=int(row[6],16),time=int(row[1]),tid=int(row[3]),stack=[])
            elif event=='HeapRealloc':
                old=(int(row[4],16),int(row[6],16));live.pop(old,None)
                pending=dict(heap=int(row[4],16),address=int(row[5],16),size=int(row[7],16),time=int(row[1]),tid=int(row[3]),stack=[])
            elif event=='HeapFree':
                if live.pop((int(row[4],16),int(row[5],16)),None) is None:totals['frees_of_pretrace_or_unseen_allocations']+=1
            elif event=='HeapDestroy':
                heap=int(row[4],16)
                for key in [key for key in live if key[0]==heap]:live.pop(key)
            elif event=='HeapDecommit':
                decommits.append(dict(time_us=int(row[1]),bytes=int(row[6],16),caller=module(int(row[7],16)) if row[7] else None))
    commit();assert 'Events Lost: 0' in header and 'Buffers lost: 0' in header,header
    assert totals['traced_allocations']>100 and totals['allocations_with_stack']>100
    result=dict(case=folder.name,adapter=native[0]['adapter'],pid=case['pid'],scope='only traced heap allocations after own-PID enable; excludes earlier allocations and direct VirtualAlloc',
        etl=dict(path=str(folder/'heap.etl'),sha256=hashlib.sha256((folder/'heap.etl').read_bytes()).hexdigest(),redistributed=False),
        csv=dict(path=str(path),sha256=hashlib.sha256(path.read_bytes()).hexdigest(),redistributed=False),trace_header=header,counters=dict(totals),
        first_resolved_non_allocator_modules=dict(byModule.most_common()),largest_traced_allocations=sorted(large,key=lambda r:-r['bytes'])[:20],
        decommits=sorted(decommits,key=lambda r:-r['bytes'])[:30],traced_outstanding_at_end=sum(live.values()),memory_acceptance=False)
    results.append(result)
    print(folder.name,dict(byModule.most_common(5)), 'stacks',totals['allocations_with_stack'],'decommit',sum(r['bytes'] for r in decommits),flush=True)
(report/'heap-call-paths.json').write_text(json.dumps(results,indent=2)+'\n',encoding='utf-8')
