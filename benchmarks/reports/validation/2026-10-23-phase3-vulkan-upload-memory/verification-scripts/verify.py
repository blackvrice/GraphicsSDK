import hashlib
import json
import pathlib
import subprocess
import sys
import xml.etree.ElementTree as ET
import zipfile

root=pathlib.Path(__file__).resolve().parents[5]
sys.path.insert(0,str(root/'tools'))
import audit_licenses as audit
base=root/'benchmarks/reports/2026-10-23-phase3-vulkan-upload-memory'
v=root/'benchmarks/reports/validation/2026-10-23-phase3-vulkan-upload-memory'
def load(p):return json.loads(p.read_text(encoding='utf-8'))
def sha(data):return hashlib.sha256(data).hexdigest()
e=load(base/'evidence.json')
records=e['files']+e['source_files']+e['preserved_inputs']+e['historical_packages']+e['local_artifacts']
records+=e.get('superseded_source_archives',[])
records += [e[k] for k in ('summary','metrics','source_archive','previous_evidence','SDK_package')]
for r in records:
    data=(root/r['path']).read_bytes();assert len(data)==r['bytes'] and sha(data)==r['sha256'],r['path']
assert audit.sdk_fingerprint(root)==e['SDK_source_fingerprint']
commands=load(v/'commands.json');assert len(commands)==49 and all(c['returncode']==c['expected'] for c in commands)
for name,count in (('ctest.xml',55),('only-ctest.xml',30),('portable-ctest.xml',11)):
    xml=ET.parse(v/name).getroot()
    assert len(xml.findall('.//testcase'))==count and not xml.findall('.//failure') and not xml.findall('.//skipped')
draw='PASS draw_assertions=637 draws=62 images=66 verified_texel_bytes=249491416 padded_images=58 adapters=2'
vertex='PASS vertex_assertions=771 draws=150 indexed_draws=78 images=146 verified_texel_bytes=250813720 padded_images=138 adapters=2'
vertex_cpu='PASS SPIRV_vertex_assertions=61 full_semantic_validator=false GPU_calls=0'
binding='PASS binding_assertions=573 draws=156 snapshots=258 images=80 verified_texel_bytes=249889536 padded_images=72 adapters=2'
binding_cpu='PASS SPIRV_binding_assertions=43 full_semantic_validator=false GPU_calls=0'
depth='PASS depth_assertions=883 passes=206 draws=258 images=278 depth_images=126 verified_texel_bytes=334855104 padded_images=262 adapters=2'
timestamp='PASS timestamp_assertions=311 measured_pairs=52 dropped_query_recordings=6 draws=6 images=18 depth_images=6 verified_texel_bytes=331852960 verified_buffer_bytes=75644964 padded_images=6 adapters=2'
presentation='PASS presentation_assertions=721 presents=145 images=132 draws=60 resizes=7 shown_windows=1 verified_texel_bytes=35918112 supported_adapters=1 unsupported_adapters=1 adapters=2'

timestamp_cpu='PASS timestamp_math_assertions=3775 GPU_calls=0'
cpu='PASS SPIRV_interface_assertions=181 full_semantic_validator=false GPU_calls=0'
color_pass='PASS assertions=593 passes=128 images=138 verified_texel_bytes=747063512 padded_images=114 adapters=2'
buffer='PASS assertions=717 transfers=48 verified_bytes=57042546 adapters=2'
texture='PASS assertions=835 roundtrips=74 verified_texel_bytes=499371832 padded_roundtrips=40 adapters=2'
probe='PASS assertions=274 adapters=2 native_core_errors=64 native_sync_hazards=2 sync_disabled_controls=2 synthetic_errors=2050 invalid_GPU_submissions=0'
for name in ('native-tests.log','only-native-tests.log'):
    text=(v/name).read_text(errors='replace');assert text.count(buffer)==2 and text.count(texture)==2 and text.count(color_pass)==2 and text.count(draw)==2 and text.count(vertex)==2 and text.count(binding)==2 and text.count(depth)==2 and text.count(timestamp)==2 and text.count(presentation)==2 and timestamp_cpu in text and cpu in text and vertex_cpu in text and binding_cpu in text and probe in text
    assert text.count('source='+e['tested_candidate_SDK_source_fingerprint']+' validation=1')==9
    assert text.count('loaded_layer=')==6 and 'Unexpected validation:' not in text
for name,suite in (('consumer-native.log',buffer),('consumer-texture.log',texture),('consumer-render.log',color_pass),('consumer-draw.log',draw),('consumer-vertex.log',vertex),('consumer-binding.log',binding),('consumer-depth.log',depth),('consumer-timestamp.log',timestamp),('consumer-presentation.log',presentation)):
    text=(v/name).read_text();assert suite in text and 'source='+e['tested_candidate_SDK_source_fingerprint']+' validation=1' in text
text=(v/'controls-loader-trace.log').read_text();assert probe in text and text.count('loaded_layer=')==6 and 'Unexpected validation:' not in text
for name in ('unknown-option.log','extra-option.log','texture-unknown.log','texture-extra.log','render-unknown.log','render-extra.log','draw-unknown.log','draw-extra.log','vertex-unknown.log','vertex-extra.log','binding-unknown.log','binding-extra.log','depth-unknown.log','depth-extra.log','timestamp-unknown.log','timestamp-extra.log','presentation-unknown.log','presentation-extra.log'):
    text=(v/name).read_text();assert 'Usage:' in text and 'source=' not in text
for name in ('missing-layer.log','texture-missing-layer.log','render-missing-layer.log','draw-missing-layer.log','vertex-missing-layer.log','binding-missing-layer.log','depth-missing-layer.log','timestamp-missing-layer.log','presentation-missing-layer.log'):
    assert 'VK_LAYER_KHRONOS_validation is not installed' in (v/name).read_text()
text=(v/'python-tests.log').read_text();assert 'Ran 252 tests' in text and '\nOK\n' in text
receipt=load(v/'runtime-receipt.json');assert sha((root/'tools/vulkan-validation.lock.json').read_bytes())==receipt['lock_sha256']
for r in receipt['artifacts']:assert sha((root/r['path']).read_bytes())==r['sha256']
assert cpu in (v/'portable-ctest.xml').read_text() and vertex_cpu in (v/'portable-ctest.xml').read_text() and binding_cpu in (v/'portable-ctest.xml').read_text() and timestamp_cpu in (v/'portable-ctest.xml').read_text()
assert '-E' in next(c for c in commands if c['name']=='ctest')['argv']
assert '^memory_reclamation_' in next(c for c in commands if c['name']=='ctest')['argv']
tool=load(v/'reused-shader-receipt.json');assert len(tool['fixtures'])==4
assert tool==load(root/'benchmarks/reports/validation/2026-10-23-phase3-vulkan-depth/shader-tools/receipt.json')
for f in tool['fixtures']:
    for r in (f['assembly'],f['binary']):assert sha((root/r['path']).read_bytes())==r['sha256']
    data=(root/f['binary']['path']).read_bytes();assert data[:8]==bytes.fromhex('0302230700030100')
with zipfile.ZipFile(root/e['source_archive']['path']) as z:
    assert len(z.namelist())==len(e['source_files'])+1
    for r in e['source_files']:assert sha(z.read(r['path']))==r['sha256']
    manifest=json.loads(z.read('source-manifest.json'));assert manifest['files']==e['source_files']
    assert sha(json.dumps(manifest['files'],sort_keys=True,separators=(',',':')).encode())==manifest['source_manifest_sha256']
if '--staged' in sys.argv:
    rows=subprocess.check_output(['git','ls-files','--stage','-z'],cwd=root).decode().split('\0')
    ids={line.split('\t',1)[1]:line.split()[1] for line in rows if line}
    paths={r['path'] for r in e['source_files']+e['files']}|{e[k]['path'] for k in ('summary','metrics','source_archive')}|{base.relative_to(root).as_posix()+'/evidence.json'}
    pairs=[(p,ids[p]) for p in sorted(paths)]
    result=subprocess.run(['git','cat-file','--batch'],cwd=root,input=('\n'.join(i for _,i in pairs)+'\n').encode(),stdout=subprocess.PIPE,check=True).stdout
    position=0
    for path,oid in pairs:
        end=result.index(b'\n',position);header=result[position:end].split();assert header[:2]==[oid.encode(),b'blob']
        size=int(header[2]);blob=result[end+1:end+1+size];position=end+2+size;data=(root/path).read_bytes()
        assert blob==data or blob==data.replace(b'\r\n',b'\n'),path
import report_vulkan_upload_compare as report_tool
x=report_tool.review(root/'benchmarks/results/2026-10-23-vulkan-upload-memory')
assert len(e['historical_packages'])==22
assert audit.sha256((root/'tools/memory_stress_policy.json').read_bytes())=='377e0136d26f15f2262d624ca4cee0d1fb629fff4ebee7d34ba031f5919233ba'
for name in ('native-tests.log','only-native-tests.log','portable-ctest.xml'):
    assert 'PASS memory_type_assertions=57 GPU_calls=0 Upload_cached_preference=true Readback_policy_changed=false' in (v/name).read_text(errors='replace')
for label,fp in [('before',x['before_SDK_source_fingerprint']),('trial','7f3845c751ae9ac808b9a5089329a8781d53e971afa6da883a88b07a9c97f35f')]:
    folder=v/('profile-'+label);cmds=load(folder/'commands.json');assert len(cmds)==6 and all(c['returncode']==0 for c in cmds)
    for p in folder.glob('*.stdout'):
        r=load(p);pref='high' if r['device_name']==x['probes']['high']['device_name'] else 'low'
        import compare_vulkan_upload as cmp
        cmp.validate_profile(r,x['probes'][pref],fp)
        assert r['profile_source_fingerprint']==report_tool.profile_fingerprint()
print(f'PASS: {len(records)} hashes; 96 selected CTests / installed nine suites / 252 Python tests; 84 comparison commands, source ZIP and 22 historical SDK packages verified')
