import pathlib,json,statistics,hashlib
root=pathlib.Path.cwd();out=root/'benchmarks/reports/validation/2026-10-22-phase2-fault-investigation'
policy=json.loads((root/'tools/memory_stress_policy.json').read_text())
manifest=json.loads((out/'campaign.json').read_text());results=[]
for case in manifest['cases']:
    if case['status']!='completed':continue
    path=out/case['case']/'memory.jsonl';records=[json.loads(s) for s in path.read_text().splitlines()];h,f=records[0],records[-1]
    samples=[r for r in records if r.get('type')=='sample'];start=next(r for r in samples if r['phase']=='measurement_start');drained=[r for r in samples if r['phase']=='drained']
    tail=[r for r in drained if r['elapsed']>=start['elapsed']+h['seconds']*(1-policy['tail_fraction'])]
    times=[start['elapsed'],*[r['elapsed'] for r in drained],samples[-1]['elapsed']];metrics={}
    for name,limits in policy['limits'].items():
        x=[(r['elapsed']-tail[0]['elapsed'])/60 for r in tail];values=[r[name] for r in tail];xm,ym=statistics.mean(x),statistics.mean(values)
        slope=sum((a-xm)*(b-ym) for a,b in zip(x,values))/sum((a-xm)**2 for a in x);q=max(2,len(values)//4);drift=statistics.median(values[-q:])-statistics.median(values[:q]);spread=max(values)-min(values)
        metrics[name]=dict(median_drift=drift,slope_per_minute=slope,range=spread,within_original_numeric_limits=drift<=limits['median_drift'] and slope<=limits['slope_per_minute'] and spread<=limits['range'])
    results.append(dict(case=case['case'],data_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),observational_only=True,memory_acceptance=False,max_drained_gap_seconds=max(b-a for a,b in zip(times,times[1:])),tail_samples=len(tail),frames=f['frames'],metrics=metrics))
destination=out/f"observational-trends-{len(results)}-completed.json";assert not destination.exists();destination.write_text(json.dumps(results,indent=2)+'\n')
for r in results:print(r['case'],json.dumps(r['metrics']['private_bytes']), 'max gap',r['max_drained_gap_seconds'])
