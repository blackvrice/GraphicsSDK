import pathlib,subprocess,sys,json
root=pathlib.Path.cwd();v=root/'benchmarks/reports/validation/2026-10-22-phase2-memory-campaign-final';receipts=[]
tasks=[('stress-create','sdk-stress','windows-sdk-memory-campaign-release',False),('stress-check','sdk-stress','windows-sdk-memory-campaign-release',True),('sdk-check','sdk','windows-sdk-adapters-release',True),('benchmark-check','benchmarks-phase1','windows-benchmarks-adapters-release',True),('adapter-check','sdk-adapters','windows-sdk-adapters-validation-release',True)]
for name,profile,folder,check in tasks:
 command=[sys.executable,'tools/audit_licenses.py','--profile',profile,'--output','licenses/generated/'+folder]
 if profile in ('sdk','sdk-adapters'):command+=['--package','out/GraphicsSDK-0.0.1-dev-windows-x64-adapters.zip']
 if check:command+=['--check']
 p=subprocess.run(command,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=180)
 (v/(name+'.log')).write_bytes(p.stdout);receipts.append({'name':name,'command':command,'returncode':p.returncode})
 (v/'audit-commands.json').write_text(json.dumps(receipts,indent=2)+'\n')
 print(p.stdout.decode(errors='replace'),flush=True)
 if p.returncode:sys.exit(p.returncode)
