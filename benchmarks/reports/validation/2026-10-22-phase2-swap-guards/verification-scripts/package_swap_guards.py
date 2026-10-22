import pathlib,json,hashlib,subprocess,sys,shutil,zipfile
root=pathlib.Path.cwd();v=root/'benchmarks/reports/validation/2026-10-22-phase2-swap-guards'
old=json.loads((root/'benchmarks/reports/2026-10-22-phase2-fence-wait/evidence.json').read_text(encoding='utf-8'))
for r in old['SDK_packages']:assert hashlib.sha256((root/r['path']).read_bytes()).hexdigest()==r['sha256'],r['path']
for source,name in [('README.md','README.md'),('docs/BUILD.md','BUILD.md')]:shutil.copyfile(root/source,root/'out/install/share/GraphicsSDK'/name)
package=root/'out/GraphicsSDK-0.0.1-dev-windows-x64-swap-guards.zip'
with zipfile.ZipFile(package,'x',compression=zipfile.ZIP_DEFLATED) as z:
 for p in sorted((root/'out/install').rglob('*')):
  if p.is_file():z.writestr(p.relative_to(root/'out/install').as_posix(),p.read_bytes())
rs=[]
for profile,folder in [('sdk','windows-sdk-swap-guards-release'),('benchmarks-phase1','windows-benchmarks-swap-guards-release'),('sdk-swaps','windows-sdk-swap-guards-validation-release'),('sdk-stress','windows-sdk-swap-guards-stress-release'),('sdk-crash','windows-sdk-swap-guards-crash-release')]:
 for check in (False,True):
  cmd=[sys.executable,'tools/audit_licenses.py','--profile',profile,'--output','licenses/generated/'+folder]
  if profile in ('sdk','sdk-swaps'):cmd+=['--package',str(package)]
  if check:cmd+=['--check']
  name=profile+('-check' if check else '-create');p=subprocess.run(cmd,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=180)
  (v/(name+'.log')).write_bytes(p.stdout);rs.append({'name':name,'command':cmd,'returncode':p.returncode});(v/'audit-commands.json').write_text(json.dumps(rs,indent=2)+'\n')
  print(p.stdout.decode(errors='replace'),flush=True)
  if p.returncode:sys.exit(p.returncode)
print('PASS new SDK ZIP/five current inventories and nine preserved SDK ZIPs',flush=True)
