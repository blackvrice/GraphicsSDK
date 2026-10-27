"""Capture a bounded XCB visual-check session; never grant human/hardware acceptance."""
import argparse
import hashlib
import json
import os
import pathlib
import platform
import posixpath
import re
import subprocess
import sys
import uuid
from audit_licenses import ROOT,sdk_fingerprint

CHECKS=('visible_animation','resize','minimize_restore','maximize_restore','cover_uncover','responsive','close_button')
WRAPPER='import os,subprocess,sys\nwith open(sys.argv[1],"wb") as f:\n r=subprocess.run(sys.argv[2:],stderr=f,env={**os.environ,"LD_DEBUG":"libs"},timeout=310)\nsys.exit(r.returncode)\n'


def require(ok,message):
    if not ok:raise ValueError(message)


def digest(data):return hashlib.sha256(data).hexdigest()
def read(path):return json.loads(path.read_text(encoding='utf-8-sig'))
def save(path,value):path.write_text(json.dumps(value,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')


def select(identity,fp,adapter,software,smoke):
    require(identity['schema_version']==1 and identity['case']=='selection' and identity['SDK_source_fingerprint']==fp,'Wrong native identity source')
    candidates=identity['adapters']
    if smoke:
        require(software and len(candidates)==1 and candidates[0]['device_type']==4,'Smoke requires exactly one explicit software ICD')
        adapter=candidates[0]['id']
    require(isinstance(adapter,str) and re.fullmatch(r'[1-9][0-9]*',adapter) and int(adapter)<2**64,'Explicit decimal adapter ID required')
    found=[a for a in candidates if a['id']==adapter];require(len(found)==1,'Exact native adapter not found; no fallback')
    chosen=found[0];require(chosen['device_type'] in ((4,) if software else (1,2)),'Native adapter kind differs')
    require(all(isinstance(chosen[k],str) and re.fullmatch('[0-9a-f]{32}',chosen[k]) and int(chosen[k],16) for k in ('device_uuid','driver_uuid')),'Native UUIDs missing')
    value=14695981039346656037
    for b in bytes.fromhex(chosen['device_uuid']):value=((value^b)*1099511628211)&((1<<64)-1)
    require(str(value)==chosen['id'],'UUID-derived adapter identity differs')
    require(all(type(chosen[k]) is int and chosen[k]>0 for k in ('vendor_id','api_version_raw','driver_version_raw')) and type(chosen['device_id']) is int and chosen['device_id']>=0,'Native driver metadata missing')
    return chosen


def validate_rows(rows,receipt):
    require(rows and rows[0]['event']=='session' and rows[-1]['event']=='manual_result','Incomplete manual native trace')
    first,last=rows[0],rows[-1];require(sum(r.get('event')=='session' for r in rows)==sum(r.get('event')=='manual_result' for r in rows)==1,'Duplicate session/result')
    automated=receipt['mode']=='smoke';selected=receipt['selected_adapter'];fp=receipt['SDK_source_fingerprint']
    require(first['source']==fp and first['adapter_id']==selected['id'] and first['software'] is (selected['device_type']==4) and first['validation'] is receipt['validation'] and first['automated'] is automated and first['target_fps']==60,'Wrong source/adapter/mode')
    for r in (first,last):require(r['session_id']==receipt['session_id'] and r['human_display_accepted'] is False and r['physical_gpu_accepted'] is False,'Changed session or acceptance scope')
    frames=[r for r in rows if r.get('event')=='frame'];states=[r for r in rows if r.get('event')=='window_state'];count=len(frames)
    require(type(last['frames']) is int and count==last['frames']>=2 and last['captures']==(count if automated else 0),'Incomplete animated frames')
    require(states and all(type(s['frames']) is int and 0<=s['frames']<=count for s in states) and all(a['frames']<=b['frames'] for a,b in zip(states,states[1:])),'Invalid state sequence')
    for i,r in enumerate(frames):
        w,h=r['width'],r['height'];require(type(w) is int and type(h) is int and 0<w<=65535 and 0<h<=65535,'Invalid extent')
        rw,rh=max(1,w//4),max(1,h//3);require(r['ordinal']==i+1 and r['captured'] is automated and (r['x'],r['y'],r['rect_width'],r['rect_height'])==((i*7)%(w-rw+1),(h-rh)//2,rw,rh),'Wrong moving rectangle/reference sequence')
    require(len({(f['x'],f['width']) for f in frames})>=2,'Static image cannot demonstrate animation')
    require(last['validation_errors']==0 and type(last['validation_errors']) is int and type(last['recoveries']) is int and last['recoveries']==0 and last['presents']==last['waited_presents']==count and last['window_alive'] is True,'Interrupted session, failed completion or teardown')
    for k in ('chains','surfaces','instances','devices'):require(type(last[k]) is int and last[k]>0 and last[k]==last['destroyed_'+k],'Native resource closure failed')
    if automated:require(count==12 and last['close_reason']=='frame_limit' and all((f['width'],f['height'])==(129,73) for f in frames),'Wrong bounded smoke scope')
    else:require(last['close_reason']=='wm_delete' and states[-1]['closing'] is True and states[-1]['frames']==count,'No ordinary close-protocol exit')
    return last


def check(folder):
    r=read(folder/'receipt.json');require(r['mode'] in ('smoke','run') and r['status']==('smoke-passed' if r['mode']=='smoke' else 'ready-for-human-review') and r['native_exit']==0 and type(r['native_exit']) is int and r['inputs_unchanged'] is True,'Failed/incomplete/changed session')
    for name,h in r['logs'].items():require(digest((folder/name).read_bytes())==h,'Changed session log: '+name)
    identity=read(folder/'identity.stdout');selected=select(identity,r['SDK_source_fingerprint'],r['selected_adapter']['id'],r['selected_adapter']['device_type']==4,r['mode']=='smoke');require(selected==r['selected_adapter'],'Selected identity changed')
    rows=[json.loads(line) for line in (folder/'native.stdout').read_text().splitlines()];require(validate_rows(rows,r)==r['result'],'Changed native result')
    stderr=(folder/'native.stderr').read_text();require(not re.search(r'^validation:',stderr,re.M),'Delivered validation error including teardown')
    if r['validation']:
        loaded={posixpath.normpath(p.strip()) for p in re.findall(r'calling init:\s+([^\n]*libVkLayer_khronos_validation\.so)\s*$',stderr,re.M)};require(loaded=={r['layer_path']},'Pinned validation layer not observed')
    form=read(folder/'human-observation.json');require(form['session_id']==r['session_id'] and form['native_sha256']==r['logs']['native.stdout'] and form['SDK_source_fingerprint']==r['SDK_source_fingerprint'] and form['device_uuid']==selected['device_uuid'] and set(form['checks'])==set(CHECKS) and all(v is None or type(v) is bool for v in form['checks'].values()),'Human form belongs to a different session')
    require(r['human_display_accepted'] is False and r['physical_gpu_accepted'] is False and r['task_completed'] is False,'Preparation mislabeled as acceptance')
    return dict(task='P3-WSI-08',preparation_verified=True,automated=r['mode']=='smoke',human_display_accepted=False,physical_gpu_accepted=False,task_completed=False)


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('stage',choices=('run','smoke','check'));p.add_argument('--output',type=pathlib.Path,required=True)
    for n in ('binary','proxy','sdk-library','identity-binary','icd','validation-dir'):p.add_argument('--'+n,type=pathlib.Path)
    p.add_argument('--adapter-id');p.add_argument('--software',action='store_true');p.add_argument('--display-route',choices=('local-monitor','remote-desktop'))
    args=p.parse_args()
    if args.stage=='check':print(json.dumps(check(args.output),indent=2));return 0
    require(platform.system()=='Linux','Actual Linux host required; no Windows/WSL installation is performed')
    smoke=args.stage=='smoke';require(all(getattr(args,n.replace('-','_')) for n in ('binary','proxy','sdk-library','identity-binary','icd')),'Explicit installed consumer, observer, Core, native identity and ICD required')
    if not smoke:require(args.adapter_id and args.display_route and os.environ.get('DISPLAY') and os.environ.get('XDG_SESSION_TYPE')!='wayland','Explicit adapter/display route and X11 desktop required; Wayland session not accepted')
    require(not args.output.exists(),'Fresh output required');args.output.mkdir(parents=True);empty=args.output/'empty-settings';empty.mkdir();fp=sdk_fingerprint(ROOT)
    paths=[args.binary,args.proxy,args.sdk_library,args.identity_binary,args.icd,pathlib.Path(__file__),ROOT/'tools/xcb_window_manager.py',ROOT/'tests/BgraSupport.hpp',ROOT/'tests/fixtures/SpirvFixtures.hpp',ROOT/'examples/presentation/PresentationPolicy.hpp']
    paths += [x for n in ('tests/linux_xcb_manual','tests/linux_xcb_surface','tests/linux_vulkan_device','examples/xcb_presentation') for x in (ROOT/n).iterdir() if x.is_file()]
    if args.validation_dir:paths += [args.validation_dir/'libVkLayer_khronos_validation.so',args.validation_dir/'VkLayer_khronos_validation.json']
    require(all(x.is_file() for x in paths),'Missing frozen input');frozen={str(x.resolve()):digest(x.read_bytes()) for x in paths};require(fp.encode() in args.binary.read_bytes() and fp.encode() in args.identity_binary.read_bytes(),'Stale installed consumer')
    r=dict(task='P3-WSI-08',mode=args.stage,source_commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),SDK_source_fingerprint=fp,session_id=uuid.uuid4().hex,validation=bool(args.validation_dir),status='running',frozen_inputs=frozen,display_route='private-xvfb' if smoke else args.display_route,human_display_accepted=False,physical_gpu_accepted=False,task_completed=False)
    def write():save(args.output/'receipt.json',r)
    write()
    try:
        env={k:v for k,v in os.environ.items() if not k.upper().startswith(('VK_','VVL_','LD_','GFX_'))};env.update(VK_DRIVER_FILES=str(args.icd.resolve()),VK_LOADER_LAYERS_DISABLE='~implicit~',VK_LAYER_PATH=str((args.validation_dir or empty).resolve()),VK_LAYER_SETTINGS_PATH=str(empty.resolve()),LP_NUM_THREADS='2')
        identity=subprocess.run([str(args.identity_binary.resolve()),'selection'],cwd=ROOT,env={**env,'LD_DEBUG':'libs'},capture_output=True,timeout=120);(args.output/'identity.stdout').write_bytes(identity.stdout);(args.output/'identity.stderr').write_bytes(identity.stderr);require(identity.returncode==0,'Native identity query failed');selected=select(read(args.output/'identity.stdout'),fp,args.adapter_id,args.software,smoke);r['selected_adapter']=selected
        declared=pathlib.Path(read(args.icd)['ICD']['library_path']);loads=[pathlib.Path(p.strip()) for p in re.findall(r'calling init:\s+(/[^\n]+)\s*$',identity.stderr.decode(),re.M)];drivers={p.resolve() for p in loads if p.name==declared.name or ('/' in str(declared) and p.resolve()==(args.icd.parent/declared).resolve())};require(len(drivers)==1,'Actual ICD library load not observed unambiguously');runtime=drivers|{pathlib.Path(read(args.output/'identity.stdout')['loader_path']).resolve()};require(all(p.is_file() for p in runtime),'Loaded runtime no longer present');frozen.update({str(p):digest(p.read_bytes()) for p in runtime});r['runtime_libraries']={str(p):frozen[str(p)] for p in sorted(runtime)}
        save(args.output/'environment.json',dict(platform=platform.platform(),display_route=r['display_route'],launcher_session_environment={k:os.environ.get(k) for k in ('DISPLAY','XDG_SESSION_TYPE','XDG_CURRENT_DESKTOP','DESKTOP_SESSION')},os_release=pathlib.Path('/etc/os-release').read_text() if pathlib.Path('/etc/os-release').exists() else None))
        env.update(GFX_SURFACE_CONTROL='0',GFX_SURFACE_REAL_LOADER=str(pathlib.Path('/usr/lib/x86_64-linux-gnu/libvulkan.so.1').resolve()),LD_LIBRARY_PATH=str(args.proxy.parent.resolve()))
        native=[str(args.binary.resolve()),'--adapter',selected['id'],'--session',r['session_id'],'--proxy',str(args.proxy.resolve())]+(['--software'] if args.software else [])+(['--validation'] if r['validation'] else [])+(['--frames','12'] if smoke else [])
        command=[sys.executable,'-c',WRAPPER,str((args.output/'native.stderr').resolve()),*native]
        if smoke:command=['xvfb-run','-a','-s','-screen 0 1024x768x24',sys.executable,str(ROOT/'tools/xcb_window_manager.py'),str((args.output/'window-manager.log').resolve()),str(ROOT/'examples/xcb_presentation/openbox.xml'),*command]
        r['command']=command;r['environment']={k:v for k,v in env.items() if k.startswith(('VK_','LD_','GFX_'))};write();child=subprocess.run(command,cwd=ROOT,env=env,capture_output=True,timeout=335);(args.output/'native.stdout').write_bytes(child.stdout);(args.output/'wrapper.stderr').write_bytes(child.stderr);r['native_exit']=child.returncode;require(child.returncode==0,'Native visual-check process failed')
        r['result']=validate_rows([json.loads(line) for line in child.stdout.decode().splitlines()],r);r['layer_path']=str((args.validation_dir/'libVkLayer_khronos_validation.so').resolve()) if r['validation'] else None
        r['logs']={name:digest((args.output/name).read_bytes()) for name in ('native.stdout','native.stderr','wrapper.stderr','identity.stdout','identity.stderr','environment.json')}
        save(args.output/'human-observation.json',dict(task='P3-WSI-08',session_id=r['session_id'],native_sha256=r['logs']['native.stdout'],SDK_source_fingerprint=fp,device_uuid=selected['device_uuid'],observer=None,display_route=r['display_route'],environment_description=None,checks={k:None for k in CHECKS},direct_user_statements=[],notes=None))
        r['status']='smoke-passed' if smoke else 'ready-for-human-review'
    except (OSError,ValueError,KeyError,subprocess.SubprocessError) as e:r.update(status='failed',error=str(e))
    r['inputs_unchanged']=fp==sdk_fingerprint(ROOT) and all(pathlib.Path(n).is_file() and digest(pathlib.Path(n).read_bytes())==h for n,h in frozen.items());write()
    if r['status']!='failed' and r['inputs_unchanged']:
        try:result=check(args.output)
        except (OSError,ValueError,KeyError) as e:r.update(status='failed',error=str(e));write();print(r['error']);return 1
        print(json.dumps(result,indent=2));return 0
    print(r.get('error','Session inputs changed'));return 1


if __name__=='__main__':sys.exit(main())
