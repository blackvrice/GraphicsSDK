import pathlib,json,csv,re,collections,hashlib,gzip
root=pathlib.Path.cwd();base=root/'out/gbv-origin-virtual-traces-v2';out=root/'benchmarks/reports/validation/2026-10-22-phase2-fault-investigation'
status=json.loads((root/'out/gbv-origin-virtual-status-v2.json').read_text(encoding='utf-8-sig'));assert status['completed'] and status['error'] is None and len(status['processes'])==4
skip={'ntdll.dll','kernelbase.dll','kernel32.dll','ucrtbase.dll','vcruntime140.dll','vcruntime140_1.dll','msvcp140.dll','msvcp_win.dll','win32u.dll'}
results=[];dest=out/'virtual-origin';dest.mkdir(exist_ok=False)
for case in status['processes']:
    folder=base/f"adapter-{case['adapter_index']}-{case['mode']}";native=[json.loads(s) for s in (folder/'memory.jsonl').read_text(encoding='utf-8').splitlines()]
    controller=json.loads((folder/'controller.json').read_text());assert all(controller[k]==0 for k in ('started','stack_status','flags_status','stop_status','events_lost'))
    assert native[0]['pid']==case['pid'] and native[-1]['passed'] and case['exit_code']==case['controller_exit']==case['export_exit']==0
    modules=[r for r in native if r['type']=='module'];pending=None;events=[];totals=collections.Counter();byModule=collections.Counter();header=''
    def frame(pc):
        for m in modules:
            if m['base']<=pc<m['base']+m['bytes']:return dict(module=pathlib.PureWindowsPath(m['path']).name,offset=pc-m['base'],address=pc)
        return dict(module=None,offset=None,address=pc,kernel_address=pc>=0xffff000000000000)
    def commit():
        global pending
        if pending is None:return
        pending['stack']=[frame(pc) for pc in pending['stack']]
        pending['first_resolved_non_allocator_module']=next((r['module'] for r in pending['stack'] if r['module'] and r['module'].lower() not in skip),'unresolved')
        pending['bytes']=pending['end']-pending['base'];events.append(pending)
        totals[pending['event']+'_events']+=1
        if pending['stack']:totals[pending['event']+'_with_stack']+=1
        if pending['event']=='VirtualAlloc' and 'COMMIT' in pending['flags']:
            totals['traced_commit_call_bytes']+=pending['bytes'];byModule[pending['first_resolved_non_allocator_module']]+=pending['bytes']
        pending=None
    path=folder/'virtual-events.csv'
    with path.open(encoding='utf-8-sig',newline='') as f:
        for raw in csv.reader(f):
            row=[s.strip() for s in raw]
            if not row:continue
            event=row[0]
            if event.startswith('OS Version:'):header=','.join(row);continue
            if event=='Stack' and len(row)>4 and row[1]!='TimeStamp':
                if pending is not None:pending['stack'].append(int(row[4],16))
                continue
            commit()
            if event not in ('VirtualAlloc','VirtualFree') or len(row)<6 or row[1]=='TimeStamp':continue
            pid=re.search(r'\(\s*(\d+)\)\s*$',row[2])
            if pid is None or int(pid[1])!=case['pid']:continue
            pending=dict(event=event,time_us=int(row[1]),pid=case['pid'],base=int(row[3],16),end=int(row[4],16),flags=','.join(row[5:]),stack=[])
    commit();assert 'Events Lost: 0' in header and 'Buffers lost: 0' in header,header
    assert totals['VirtualAlloc_with_stack']>0
    encoded=('\n'.join(json.dumps(e,separators=(',',':')) for e in events)+'\n').encode();compressed=dest/(folder.name+'-target-events.jsonl.gz');compressed.write_bytes(gzip.compress(encoded,mtime=0))
    largest=sorted([e for e in events if e['event']=='VirtualAlloc' and 'COMMIT' in e['flags']],key=lambda e:-e['bytes'])[:16]
    result=dict(case=folder.name,adapter=native[0]['adapter'],pid=case['pid'],scope='target-process virtual allocation API events; byte sums are commit calls, not a process-private peak or net new unique pages',
        trace_header=header,counters=dict(totals),first_resolved_non_allocator_modules=dict(byModule.most_common()),largest_commit_calls=largest,
        etl=dict(path=str(folder/'virtual.etl'),sha256=hashlib.sha256((folder/'virtual.etl').read_bytes()).hexdigest(),redistributed=False),
        csv=dict(path=str(path),sha256=hashlib.sha256(path.read_bytes()).hexdigest(),redistributed=False),
        filtered_events=dict(path=compressed.relative_to(root).as_posix(),sha256=hashlib.sha256(compressed.read_bytes()).hexdigest(),bytes=compressed.stat().st_size),controller=controller,memory_acceptance=False)
    results.append(result)
    (dest/(folder.name+'-native.jsonl')).write_bytes((folder/'memory.jsonl').read_bytes())
    print(folder.name,dict(byModule.most_common(6)),'alloc/free',totals['VirtualAlloc_events'],totals['VirtualFree_events'],flush=True)
(out/'virtual-call-paths.json').write_text(json.dumps(results,indent=2)+'\n',encoding='utf-8')
