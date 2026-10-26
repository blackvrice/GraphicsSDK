import hashlib,json,os,pathlib,subprocess,sys,time
ROOT=pathlib.Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools'))
from audit_licenses import sdk_fingerprint
OUT=ROOT/'out/vulkan-marker-observed';BIN=ROOT/'out/vulkan-marker-observer/Release';VVL=ROOT/'out/vulkan-validation-runtime/build/Vulkan-ValidationLayers/layers/Release'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def save(p,x):p.write_text(json.dumps(x,indent=2)+'\n',encoding='utf-8')
OUT.mkdir(exist_ok=False)
paths=[pathlib.Path(__file__),ROOT/'CMakeLists.txt',ROOT/'src/backends/vulkan/Vulkan.cpp',ROOT/'tests/VulkanMarkerTests.cpp',*list((ROOT/'tests/vulkan_marker').glob('*')),BIN/'gfx_marker_observe.exe',BIN/'VkLayer_gfx_marker.dll',BIN/'VkLayer_gfx_marker.json',ROOT/'out/vulkan-marker-sdk/Release/GraphicsSDKCore.lib',ROOT/'out/vulkan-marker-install/lib/GraphicsSDKCore.lib',VVL/'VkLayer_khronos_validation.dll',VVL/'VkLayer_khronos_validation.json']
frozen={str(p):sha(p) for p in paths if p.is_file()};fp=sdk_fingerprint(ROOT)
assert fp.encode() in (BIN/'gfx_marker_observe.exe').read_bytes()
assert sha(ROOT/'out/vulkan-marker-sdk/Release/GraphicsSDKCore.lib')==sha(ROOT/'out/vulkan-marker-install/lib/GraphicsSDKCore.lib')
state=dict(SDK_source_fingerprint=fp,serialized=True,cases=[],status='running',frozen_inputs=frozen)
save(OUT/'campaign.json',state)
try:
 for name,validation,deny in [('normal',False,False),('validation',True,False),('missing-proc',False,True),('validation-missing-proc',True,True)]:
  env=os.environ.copy();env['VK_LAYER_PATH']=str(BIN)+';'+str(VVL);env['VK_INSTANCE_LAYERS']='VK_LAYER_GFX_marker'
  command=[str(BIN/'gfx_marker_observe.exe'),'--observe',str(BIN/'VkLayer_gfx_marker.dll')]
  if validation:command+=['--validation']
  if deny:command+=['--deny-proc']
  start=time.monotonic();p=subprocess.run(command,cwd=ROOT,env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=120)
  (OUT/(name+'.stdout')).write_bytes(p.stdout);(OUT/(name+'.stderr')).write_bytes(p.stderr)
  row=dict(name=name,command=command,returncode=p.returncode,wall_seconds=time.monotonic()-start,environment={k:env[k] for k in ('VK_LAYER_PATH','VK_INSTANCE_LAYERS')})
  state['cases'].append(row);save(OUT/'campaign.json',state)
  print(name+' exit='+str(p.returncode),flush=True)
  if p.returncode:raise ValueError(p.stderr.decode(errors='replace'))
 state['status']='passed'
except Exception as e:state.update(status='failed',error=str(e));print(str(e),flush=True)
state['inputs_unchanged']=sdk_fingerprint(ROOT)==fp and all(sha(pathlib.Path(p))==h for p,h in frozen.items());save(OUT/'campaign.json',state)
sys.exit(0 if state['status']=='passed' and state['inputs_unchanged'] else 1)
