import pathlib,json,gzip,collections
root=pathlib.Path.cwd();out=root/'benchmarks/reports/validation/2026-10-22-phase2-fault-investigation'
symbols=json.loads((out/'resolved-virtual-call-paths.json').read_text());frames={(r['module'].lower(),r['offset']):r for r in symbols['unique_frames']};cases=json.loads((out/'virtual-call-paths.json').read_text());results=[]
for case in cases:
    totals=collections.Counter()
    for line in gzip.decompress((root/case['filtered_events']['path']).read_bytes()).decode().splitlines():
        event=json.loads(line);assert event['pid']==case['pid']
        if event['event']!='VirtualAlloc' or 'COMMIT' not in event['flags']:continue
        names=[frames[(r['module'].lower(),r['offset'])].get('symbol','') for r in event['stack'] if r.get('module') and (r['module'].lower(),r['offset']) in frames]
        if 'NDebug::CGBVDataController::AddResourceInfo' in names:
            totals['add_resource_info_commit_call_bytes']+=event['bytes'];totals['add_resource_info_commit_calls']+=1
            if 'NDXGI::CDevice::AllocateCB_0022' in names:
                totals['add_resource_info_allocate_cb_commit_call_bytes']+=event['bytes'];totals['add_resource_info_allocate_cb_commit_calls']+=1
    results.append(dict(case=case['case'],**dict(totals)))
    print(case['case'],dict(totals))
(out/'quantified-gbv-path.json').write_text(json.dumps(results,indent=2)+'\n')
