import pathlib,json
root=pathlib.Path.cwd();v=root/'benchmarks/reports/validation/2026-10-22-phase2-crash-capture-final';v.mkdir()
initial=root/'benchmarks/reports/validation/2026-10-22-phase2-crash-capture'
pilot=json.loads((root/'out/crash-capture-pilot-text-length/summary.json').read_text())
(initial/'pilot-capture-rejected.json').write_text(json.dumps({'status':pilot['status'],'error':pilot['error'],'collector_exit_code':pilot['collector_exit_code'],'inputs_unchanged':pilot['inputs_unchanged'],'originals_local_only':'out/crash-capture-pilot-text-length','old_capture_not_publication_evidence':True},indent=2)+'\n')
source=(root/'out/verify_crash_capture.py').read_text().replace("phase2-crash-capture'","phase2-crash-capture-final'")
source=source.replace('out/crash-capture-controls-final','out/crash-capture-controls-final-v2')
source=source.replace('out/intel-gbv-crash-capture.dmp','out/intel-gbv-crash-capture-final.dmp')
exec(source)
