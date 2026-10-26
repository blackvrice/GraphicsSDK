import hashlib,json,pathlib,sys,tempfile,zipfile
ROOT=next(p for p in pathlib.Path(__file__).resolve().parents if (p/'tools/audit_licenses.py').exists())
F=pathlib.Path(__file__).resolve().parent
def read(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def sha(b):return hashlib.sha256(b).hexdigest()
def record(p):return dict(path=p.relative_to(ROOT).as_posix(),bytes=p.stat().st_size,sha256=sha(p.read_bytes()))
manifest=read(F/'source-manifest.json');archive=ROOT/manifest['archive']['path'];assert record(archive)==manifest['archive']
tmp=tempfile.TemporaryDirectory();snapshot=pathlib.Path(tmp.name).resolve()
with zipfile.ZipFile(archive) as z:
 assert len(z.namelist())==len(set(z.namelist()))==len(manifest['files'])+1
 for row in manifest['files']:
  target=snapshot/row['path'];assert target.resolve().is_relative_to(snapshot)
  assert pathlib.PurePosixPath(row['path']).suffix.lower() not in ('.exe','.dll','.pdb','.dmp','.lib')
  data=z.read(row['path']);assert len(data)==row['bytes'] and sha(data)==row['sha256']
  target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(data)
sys.path.insert(0,str(snapshot/'tools'))
from manual_presentation_evidence import validate
from audit_licenses import sdk_fingerprint
assert sdk_fingerprint(snapshot)==manifest['SDK_source_fingerprint']=='2e511b8e53dbef4bbb2bf3d86ab4b19cf03e8765111de2b3dad4fe45fa551401'
s=read(F/'completed/session.json');native=(F/'completed/native.jsonl').read_bytes();human=read(F/'human-observation.json')
checked=validate(s,native,human);assert checked['interactive_acceptance'] and not validate(s,native)['interactive_acceptance']
accepted=read(F/'acceptance.json');assert all(accepted[k]==v for k,v in checked.items())
assert accepted['original_native_acceptance_unchanged'] and not any(accepted[k] for k in ('actual_DPI_acceptance','unexpected_loss_acceptance','other_adapter_acceptance'))
assert human['first_session_native_sha256']==read(F/'first/session.json')['native_sha256']
try:validate(read(F/'first/session.json'),(F/'first/native.jsonl').read_bytes())
except ValueError:assert not read(F/'first/assessment.json')['accepted']
else:raise AssertionError('Missing minimize accepted')
for name in ('first','completed'):
 session=read(F/name/'session.json');assert session['inputs_unchanged'] and session['native_exit']==0
 assert sha((F/name/'native.jsonl').read_bytes())==session['native_sha256']
 for row in manifest['files']:
  if row['path'].startswith('out/run_manual_window') or row['path'].startswith(('include/','src/','examples/presentation/')) or row['path']=='CMakeLists.txt':
   frozen={p.replace('\\','/').split('/GraphicsSDK/',1)[-1]:h for p,h in session['frozen_inputs'].items()}
   if row['path'] in frozen:assert frozen[row['path']]==row['sha256']
full=read(F/'setup/full-python-receipt.json');assert full['successful'] and full['tests']==662 and full['inputs_unchanged']
assert not any(full[k] for k in ('errors','failures','skipped','expected_failures','unexpected_successes'))
log=(F/'setup/full-python-unittest.log').read_bytes();assert len(log)==full['log']['bytes'] and sha(log)==full['log']['sha256']
assert (F/'setup/presentation-ctest.log').read_bytes().count(b'Test Passed.')==7
summary=dict(schema=1,task='P2-ACC-03',status='done',gate='required',SDK_source_fingerprint=manifest['SDK_source_fingerprint'],
 SDK_source_base_commit=s['source_base_commit'],SDK_code_modified=False,interactive_acceptance=True,acceptance=accepted,
 human_observation=record(F/'human-observation.json'),first_insufficient_session_preserved=True,full_Python_tests=662,presentation_CTests=7,
 source_archive=manifest['archive'],evidence=[record(p) for p in sorted(F.rglob('*')) if p.is_file()])
target=ROOT/'benchmarks/reports/2026-10-26-phase2-manual-presentation/summary.json'
if '--summary' in sys.argv:
 target.parent.mkdir(parents=True,exist_ok=True);target.write_text(json.dumps(summary,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
else:assert read(target)==summary
print('PASS session-bound human acceptance, actual minimize/restore, no minimized frames, resize/close, 7 CTests, 662 Python tests and exact source ZIP')
