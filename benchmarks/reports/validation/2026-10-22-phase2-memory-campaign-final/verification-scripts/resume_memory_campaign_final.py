import pathlib,json,hashlib,subprocess,sys
root=pathlib.Path.cwd();v=root/'benchmarks/reports/validation/2026-10-22-phase2-memory-campaign-final'
receipts=json.loads((v/'commands.json').read_text())
def run(name,command,expected):
 p=subprocess.run(command,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=180)
 (v/(name+'.log')).write_bytes(p.stdout);receipts.append({'name':name,'command':command,'returncode':p.returncode,'expected':expected})
 (v/'commands.json').write_text(json.dumps(receipts,indent=2)+'\n')
 print(p.stdout.decode(errors='replace'),flush=True);assert p.returncode==expected,name
run('all-continue-128',[sys.executable,'tools/run_memory_stress.py','--diagnostic-seconds','3','--max-private-growth-mib','128','--output',str(v/'all-continue-128')],1)
m=json.loads((v/'all-continue-128/campaign.json').read_text());assert len(m['processes'])==3 and [p['returncode'] for p in m['processes']]==[0,2,0]
run('runner-missing-adapter',[sys.executable,'tools/run_memory_stress.py','--mode','gpu','--adapter',str(2**64-1),'--diagnostic-seconds','2','--output',str(v/'runner-missing')],1)
assert not json.loads((v/'runner-missing/acceptance.json').read_text())['accepted']
before=json.loads((v/'inputs-before.json').read_text());assert all(hashlib.sha256((root/p).read_bytes()).hexdigest()==h for p,h in before['inputs'].items())
(v/'inputs-after.json').write_bytes((v/'inputs-before.json').read_bytes())
print('PASS final frozen sources, exact selection and guarded failures; native 0/2/0 continuation sequence',flush=True)
