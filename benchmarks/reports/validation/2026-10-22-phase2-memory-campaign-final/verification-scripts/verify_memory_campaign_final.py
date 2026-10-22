import pathlib
source=pathlib.Path('out/verify_memory_campaign.py').read_text()
source=source.replace("phase2-memory-campaign'","phase2-memory-campaign-final'")
source=source.replace("'--output',str(v/f'gbv-guard-{i}')", "'--max-private-growth-mib',str(512 if i==0 else 64),'--output',str(v/f'gbv-guard-{i}')")
source=source.replace("[exe,'--seconds','2','--output',str(path),*flags]", "[exe,*([] if '--seconds' in flags else ['--seconds','2']),'--output',str(path),*flags]")
source=source.replace("assert frozen=={p:digest(root/p) for p in sources}", """
run('all-continue-after-abort',[sys.executable,'tools/run_memory_stress.py','--diagnostic-seconds','3','--max-private-growth-mib','64','--output',str(v/'all-continue')],1)
all_manifest=json.loads((v/'all-continue/campaign.json').read_text());assert len(all_manifest['processes'])==3 and all_manifest['processes'][-1]['mode']=='warp' and all_manifest['processes'][-1]['returncode']==0
run('runner-missing-adapter',[sys.executable,'tools/run_memory_stress.py','--mode','gpu','--adapter',str(missing),'--diagnostic-seconds','2','--output',str(v/'runner-missing')],1)
assert not json.loads((v/'runner-missing/acceptance.json').read_text())['accepted']
assert frozen=={p:digest(root/p) for p in sources}
""")
exec(source)
