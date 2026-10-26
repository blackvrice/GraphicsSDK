"""Observe native XCB prerequisites; never claim SDK presentation acceptance."""
import argparse
import hashlib
import json
import os
import pathlib
import platform
import subprocess
import sys
from audit_licenses import ROOT, sdk_fingerprint


def require(ok, message):
    if not ok:
        raise ValueError(message)


def validate_rows(rows, fingerprint):
    require(rows and rows[-1].get('event') == 'result', 'Incomplete scope observation')
    sessions = [r for r in rows if r.get('event') == 'session']
    adapters = [r for r in rows if r.get('event') == 'adapter']
    queues = [r for r in rows if r.get('event') == 'queue']
    result = rows[-1]
    require(len(sessions) == 1 and sessions[0]['system'] == 'xcb' and sessions[0]['native_surface'] is True
            and (sessions[0]['width'], sessions[0]['height']) == (64,48)
            and sessions[0]['wayland_session_tested'] is False, 'Wrong display session')
    require(result['source'] == fingerprint and len(adapters) == result['adapters'] > 0, 'Wrong source or adapter count')
    require(len({a['uuid'] for a in adapters}) == len(adapters), 'Duplicate adapter')
    for a in adapters:
        require(len(a['uuid']) == 32 and int(a['uuid'],16) != 0 and a['uuid'] == a['uuid'].lower(), 'Invalid native UUID')
        families = [q for q in queues if q['uuid'] == a['uuid']]
        require(families and len({q['family'] for q in families}) == len(families), 'Missing or duplicate queue')
        graphics = sorted(q['family'] for q in families if q['graphics'] is True)
        require(graphics and a['selected_family'] == graphics[0], 'Selected queue differs from SDK policy')
        q = next(q for q in families if q['family'] == a['selected_family'])
        require(a['selected_present'] is (q['surface_present'] and q['visual_present']), 'Surface/visual query mismatch')
        supported = all(a[k] is True for k in ('selected_present','swapchain_extension','maintenance_extension','maintenance_feature','rgba8','fifo'))
        supported = supported and (a['usage'] & 19) == 19 and sessions[0]['surface_maintenance1'] is True and sessions[0]['surface_capabilities2'] is True
        require(a['policy_ready'] is supported and a['native_device_created'] is supported, 'Unsupported policy or missing native creation')
        require(a['current_width'] == 64 and a['current_height'] == 48 and a['min_images'] > 0
                and (a['max_images'] == 0 or a['max_images'] >= a['min_images']), 'Wrong surface limits')
    require(all(q['uuid'] in {a['uuid'] for a in adapters} for q in queues), 'Unknown queue adapter')
    require(result['software_adapters'] == sum(a['device_type'] == 4 for a in adapters)
            and result['policy_ready_adapters'] == sum(a['policy_ready'] for a in adapters), 'Mismatched native totals')
    require(all(result[k] is False for k in ('sdk_linux_wsi','swapchain_created','presented','human_display_accepted')), 'Scope probe is not presentation acceptance')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=pathlib.Path,required=True)
    parser.add_argument('--binary',type=pathlib.Path,required=True)
    parser.add_argument('--sdk-library',type=pathlib.Path,required=True)
    parser.add_argument('--icd',type=pathlib.Path,required=True)
    args = parser.parse_args()
    require(platform.system() == 'Linux', 'Actual Linux host required')
    args.output.mkdir(parents=True,exist_ok=False)
    settings = args.output/'empty-settings';settings.mkdir()
    inputs = [args.binary,args.sdk_library,args.icd,pathlib.Path(__file__),ROOT/'tests/linux_wsi_scope/Probe.cpp',ROOT/'tests/linux_wsi_scope/CMakeLists.txt']
    frozen = {str(p.resolve()):hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}
    fp = sdk_fingerprint(ROOT)
    require(fp.encode() in args.binary.read_bytes(), 'Stale installed SDK consumer')
    state = dict(task='P3-WSI-01',SDK_source_fingerprint=fp,source_commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),status='running',frozen_inputs=frozen,cases=[])
    def save():
        (args.output/'receipt.json').write_text(json.dumps(state,indent=2)+'\n',encoding='utf-8')
    save()
    try:
        env = {k:v for k,v in os.environ.items() if not k.upper().startswith(('VK_','VVL_'))}
        env.update(VK_DRIVER_FILES=str(args.icd.resolve()),VK_LOADER_LAYERS_DISABLE='~implicit~',VK_LAYER_PATH=str(settings.resolve()),VK_LAYER_SETTINGS_PATH=str(settings.resolve()),LP_NUM_THREADS='2')
        for name,command,expected in [('xcb',['xvfb-run','-a','-s','-screen 0 800x600x24',str(args.binary.resolve())],0),('no-display',[str(args.binary.resolve())],2)]:
            child_env={**env}
            if name=='no-display':child_env['DISPLAY']=':65533'
            child=subprocess.run(command,cwd=ROOT,env=child_env,stdin=subprocess.DEVNULL,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=90)
            (args.output/(name+'.stdout')).write_bytes(child.stdout);(args.output/(name+'.stderr')).write_bytes(child.stderr)
            case=dict(name=name,command=command,returncode=child.returncode,environment={k:v for k,v in child_env.items() if k.startswith(('VK_','DISPLAY'))});state['cases'].append(case);save()
            require(child.returncode==expected,'Unexpected native exit: '+name)
            rows=[json.loads(l) for l in child.stdout.decode().splitlines()]
            if name=='xcb':
                case['result']=validate_rows(rows,fp)
                require(case['result']['adapters']==case['result']['software_adapters']==case['result']['policy_ready_adapters'], 'CI requires observed ready software candidates')
            else:require(len(rows)==1 and rows[0]['event']=='display_unavailable' and rows[0]['xcb_error']!=0,'Missing display negative control')
            save()
        packages=subprocess.run(['dpkg-query','-W','-f=${Package} ${Version}\n','libvulkan1','mesa-vulkan-drivers','libxcb1','xvfb','xauth'],stdout=subprocess.PIPE,stderr=subprocess.PIPE,check=True)
        (args.output/'packages.txt').write_bytes(packages.stdout)
        libraries=[pathlib.Path('/usr/lib/x86_64-linux-gnu')/n for n in ('libvulkan.so.1','libvulkan_lvp.so','libxcb.so.1')]
        state['runtime_files']=[dict(path=str(p.resolve()),bytes=p.stat().st_size,sha256=hashlib.sha256(p.read_bytes()).hexdigest(),redistributed=False) for p in libraries]
        state['status']='passed'
    except (ValueError,KeyError,OSError,subprocess.SubprocessError) as error:
        state.update(status='failed',error=str(error))
    state['inputs_unchanged']=sdk_fingerprint(ROOT)==fp and all(pathlib.Path(n).exists() and hashlib.sha256(pathlib.Path(n).read_bytes()).hexdigest()==h for n,h in frozen.items())
    save()
    print(state['status'],flush=True)
    return 0 if state['status']=='passed' and state['inputs_unchanged'] else 1


if __name__=='__main__':
    sys.exit(main())
