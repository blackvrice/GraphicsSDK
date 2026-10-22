"""Record normal Intel/hardware GBV runs with observed environment controls.

Comparison checks recorded fields, not all host state or a crash cause. Never
installs a driver, changes system settings, or accepts the memory stress gate.
"""
import argparse
import datetime
import hashlib
import json
import pathlib
import re
import subprocess
import sys

from audit_licenses import ROOT, sdk_fingerprint
from analyze_memory_stress import read_records, require, unique_object
from run_memory_stress import adapter_argument, run as run_stress, validate_native

STRESS = ROOT/'out/core/Release/gfx_memory_stress.exe'
ENUMERATOR = ROOT/'out/presentation/Release/gfx_presentation.exe'
ROLES = {'stress': STRESS, 'enumerator': ENUMERATOR, 'core_library': ROOT/'out/core/Release/GraphicsSDKCore.lib',
         'policy': ROOT/'tools/memory_stress_policy.json', 'native_source': ROOT/'tools/MemoryStress.cpp',
         'runner': ROOT/'tools/run_memory_stress.py', 'analyzer': ROOT/'tools/analyze_memory_stress.py',
         'environment_tool': pathlib.Path(__file__).resolve(), 'audit_tool': ROOT/'tools/audit_licenses.py'}
RUNTIMES = {'d3d12.dll', 'd3d12core.dll', 'd3d12sdklayers.dll', 'd3d10warp.dll', 'dxgi.dll'}

# Fixed read-only queries; no policy bypass, host name, SID, serial, or system UUID.
QUERY = r'''
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = New-Object System.Text.UTF8Encoding($false)
$os = Get-CimInstance Win32_OperatingSystem
$cpu = @(Get-CimInstance Win32_Processor | ForEach-Object {
  @{name=$_.Name; manufacturer=$_.Manufacturer; cores=$_.NumberOfCores; threads=$_.NumberOfLogicalProcessors}
})
$bios = @(Get-CimInstance Win32_BIOS | ForEach-Object {
  @{manufacturer=$_.Manufacturer; version=$_.SMBIOSBIOSVersion; release=$_.ReleaseDate.ToUniversalTime().ToString('o')}
})
$gpu = @(Get-CimInstance Win32_VideoController | ForEach-Object {
  @{name=$_.Name; pnp=$_.PNPDeviceID; version=$_.DriverVersion; date=$_.DriverDate.ToUniversalTime().ToString('o')}
})
$runtime = @{}
foreach ($name in @('d3d12.dll','d3d12core.dll','d3d12sdklayers.dll','d3d10warp.dll','dxgi.dll')) {
  $path = Join-Path $env:SystemRoot ('System32\'+$name)
  $stream = [System.IO.File]::OpenRead($path)
  $hasher = [System.Security.Cryptography.SHA256]::Create()
  try { $hash = [System.BitConverter]::ToString($hasher.ComputeHash($stream)).Replace('-','').ToLowerInvariant() }
  finally { $stream.Dispose(); $hasher.Dispose() }
  $runtime[$name] = @{path=$path; version=(Get-Item -LiteralPath $path).VersionInfo.FileVersion; sha256=$hash}
}
$key = [Microsoft.Win32.Registry]::LocalMachine.OpenSubKey('HARDWARE\DESCRIPTION\System\CentralProcessor\0')
try { $microcode = @($key.GetValue('Update Revision')) } finally { $key.Close() }
@{os=@{version=$os.Version; build=$os.BuildNumber}; boot=$os.LastBootUpTime.ToUniversalTime().ToString('o'); cpu=$cpu; bios=$bios; gpu=$gpu; microcode=$microcode; runtime=$runtime} | ConvertTo-Json -Depth 8 -Compress
'''


def sha(data):
    return hashlib.sha256(data).hexdigest()


def load(path):
    return json.loads(path.read_text(encoding='utf-8'), object_pairs_hook=unique_object)


def save(path, data):
    path.write_text(json.dumps(data,indent=2)+'\n',encoding='utf-8')


def file_record(path):
    data=path.read_bytes()
    return dict(path=str(path.resolve()),sha256=sha(data))


def validate_snapshot(data):
    require(isinstance(data,dict) and type(data.get('schema')) is int and data['schema']==1, 'Invalid environment schema')
    controls=data.get('controls');driver=data.get('selected_driver');observed=data.get('observations')
    require(isinstance(controls,dict) and isinstance(driver,dict) and isinstance(observed,dict), 'Missing environment controls')
    require(set(controls)=={'SDK_source_fingerprint','files','runtime','os','cpu','bios','microcode','adapter','other_gpu_drivers'}, 'Incomplete/unknown controls')
    require(re.fullmatch('[0-9a-f]{64}',controls['SDK_source_fingerprint'] or ''), 'Invalid SDK fingerprint')
    require(isinstance(controls['files'],dict) and set(controls['files'])==set(ROLES), 'Missing frozen file roles')
    require(isinstance(controls['runtime'],dict) and set(controls['runtime'])==RUNTIMES, 'Missing runtime identities')
    for value in [*controls['files'].values(),*controls['runtime'].values()]:
        require(isinstance(value,dict) and isinstance(value.get('path'),str) and value['path']
                and re.fullmatch('[0-9a-f]{64}',value.get('sha256','')), 'Invalid file identity')
    require(all(isinstance(v.get('version'),str) and v['version'] for v in controls['runtime'].values()), 'Missing runtime file version')
    require(isinstance(controls['os'],dict) and set(controls['os'])=={'version','build'}
            and all(isinstance(v,str) and v for v in controls['os'].values()), 'Missing OS version')
    for key in ('cpu','bios','microcode'):
        require(isinstance(controls[key],list) and controls[key], 'Missing '+key)
    require(all(isinstance(c,dict) and set(c)=={'name','manufacturer','cores','threads'}
                and all(isinstance(c[k],str) and c[k] for k in ('name','manufacturer'))
                and all(type(c[k]) is int and c[k]>0 for k in ('cores','threads')) for c in controls['cpu']), 'Invalid CPU metadata')
    require(all(isinstance(b,dict) and set(b)=={'manufacturer','version','release'} and all(isinstance(v,str) and v for v in b.values()) for b in controls['bios']), 'Invalid BIOS metadata')
    require(1<=len(controls['microcode'])<=32 and all(type(v) is int and 0<=v<=255 for v in controls['microcode']), 'Invalid microcode bytes')
    require(isinstance(controls['other_gpu_drivers'],list), 'Missing other GPU driver controls')
    require(all(isinstance(d,dict) and set(d)=={'name','pnp_sha256','version','date'}
                and all(isinstance(v,str) and v for v in d.values()) and re.fullmatch('[0-9a-f]{64}',d['pnp_sha256']) for d in controls['other_gpu_drivers']), 'Invalid other GPU driver identity')
    adapter=controls['adapter']
    require(isinstance(adapter,dict) and set(adapter)=={'name','vendor_id','device_id','pnp_sha256'}
            and isinstance(adapter['name'],str) and adapter['name']
            and all(type(adapter[k]) is int and 0<=adapter[k]<=65535 for k in ('vendor_id','device_id'))
            and re.fullmatch('[0-9a-f]{64}',adapter['pnp_sha256'] or ''), 'Missing stable selected adapter identity')
    require(set(driver)=={'version','date'} and all(isinstance(v,str) and v for v in driver.values()), 'Missing selected driver version/date')
    require(set(observed)=={'utc','boot_utc','adapter_id'} and isinstance(observed['boot_utc'],str) and observed['boot_utc']
            and isinstance(observed['utc'],str) and observed['utc']
            and type(observed['adapter_id']) is int and 0<=observed['adapter_id']<2**64, 'Invalid ephemeral observations')


def collect_snapshot(adapter_id):
    files={name:file_record(path) for name,path in ROLES.items()}
    fp=sdk_fingerprint(ROOT)
    require(fp.encode() in STRESS.read_bytes(), 'Stale stress executable')
    result=subprocess.run([str(ENUMERATOR),'--list-adapters'],stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=30,check=True)
    rows=[json.loads(line,object_pairs_hook=unique_object) for line in result.stdout.decode('utf-8').splitlines()]
    require(rows and rows[-1].get('event')=='adapter_list' and rows[-1].get('sdk_fingerprint')==fp, 'Stale adapter enumerator')
    candidates=[r for r in rows if r.get('event')=='adapter']
    require(type(rows[-1].get('count')) is int and rows[-1]['count']==len(candidates) and len(rows)==len(candidates)+1, 'Incomplete adapter enumeration')
    selected=[r for r in candidates if r.get('adapter_id')==adapter_id and type(r.get('adapter_id')) is int]
    require(len(selected)==1 and selected[0].get('software') is False, 'Select a fresh unique hardware adapter ID')
    selected=selected[0]
    require(sum((r.get('vendor_id'),r.get('device_id'),r.get('name'))==(selected['vendor_id'],selected['device_id'],selected['name']) for r in candidates)==1,
            'Identical adapter models are ambiguous for CIM driver mapping')
    result=subprocess.run(['powershell.exe','-NoProfile','-NonInteractive','-Command',QUERY],stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=60)
    require(result.returncode==0,'Read-only environment query failed: '+result.stderr.decode('utf-8',errors='replace').strip())
    native=json.loads(result.stdout.decode('utf-8-sig'),object_pairs_hook=unique_object)
    matches=[];others=[]
    for item in native['gpu']:
        pnp=item.get('pnp')
        require(isinstance(pnp,str) and pnp, 'GPU PnP identity unavailable')
        pci=re.match(r'PCI\\VEN_([0-9A-F]{4})&DEV_([0-9A-F]{4})',pnp,re.IGNORECASE)
        if pci and (int(pci[1],16),int(pci[2],16),item['name'])==(selected['vendor_id'],selected['device_id'],selected['name']):
            matches.append(item)
        else:
            others.append(dict(name=item['name'],pnp_sha256=sha(pnp.upper().encode()),version=item['version'],date=item['date']))
    require(len(matches)==1,'No unique exact CIM/PCI/name match for selected adapter')
    item=matches[0]
    controls=dict(SDK_source_fingerprint=fp,files=files,runtime=native['runtime'],os=native['os'],cpu=native['cpu'],bios=native['bios'],
                  microcode=native['microcode'],adapter=dict(name=selected['name'],vendor_id=selected['vendor_id'],device_id=selected['device_id'],pnp_sha256=sha(item['pnp'].upper().encode())),
                  other_gpu_drivers=sorted(others,key=lambda v:(v['pnp_sha256'],v['name'])))
    data=dict(schema=1,controls=controls,selected_driver=dict(version=item['version'],date=item['date']),
              observations=dict(utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),boot_utc=native['boot'],adapter_id=adapter_id))
    validate_snapshot(data)
    require(all(file_record(path)==files[name] for name,path in ROLES.items()) and sdk_fingerprint(ROOT)==fp, 'Input changed during environment query')
    return data


def differences(left,right,prefix=''):
    if isinstance(left,dict) and isinstance(right,dict):
        result=[]
        for key in sorted(set(left)|set(right)):
            name=prefix+'.'+key if prefix else key
            if key not in left or key not in right:result.append(name)
            else:result.extend(differences(left[key],right[key],name))
        return result
    return [] if type(left) is type(right) and left==right else [prefix]


def compare_snapshots(before,after,change='none',within_run=False):
    require(change in ('none','selected-driver'), 'Unknown comparison change')
    validate_snapshot(before);validate_snapshot(after)
    changed=differences(before['controls'],after['controls'],'controls')
    driver_changed=before['selected_driver']!=after['selected_driver']
    boot_changed=before['observations']['boot_utc']!=after['observations']['boot_utc']
    if (change=='none' and driver_changed) or (change=='selected-driver' and not driver_changed):
        changed.append('selected_driver: expected '+('unchanged' if change=='none' else 'changed'))
    if within_run and boot_changed:changed.append('observations.boot_utc: changed within run')
    if before['observations']['adapter_id']!=after['observations']['adapter_id'] and (within_run or not boot_changed):
        changed.append('observations.adapter_id: changed without an allowed reboot')
    return dict(recorded_controls_match=not changed,differences=changed,expected_change=change,selected_driver_changed=driver_changed,
                reboot_observed=boot_changed,diagnostic_only=True,memory_acceptance=False,crash_cause_resolved=False,
                scope='Only recorded controls; BIOS settings, power/thermal state, CPU/RAM stability and loaded driver modules are not certified')


def settings(seconds,guard):
    return dict(mode='validation',instrumentation='normal-no-debugger',seconds=seconds or 300,warmup=1 if seconds else 30,
                interval=1 if seconds else 5,max_private_growth_mib=guard,short_diagnostic=seconds is not None)


def record(args):
    require(not args.output.exists(),'Output already exists')
    before=collect_snapshot(args.adapter)
    args.output.mkdir(parents=True,exist_ok=False)
    save(args.output/'environment-before.json',before)
    result=dict(schema=1,status='starting',diagnostic_only=True,memory_acceptance=False,settings=settings(args.diagnostic_seconds,args.max_private_growth_mib))
    save(args.output/'case.json',result)
    try:
        runner_args=argparse.Namespace(executable=STRESS,output=args.output/'workload',mode='validation',adapter=args.adapter,
            diagnostic_seconds=args.diagnostic_seconds,max_private_growth_mib=args.max_private_growth_mib)
        code=run_stress(runner_args);result['runner_exit_code']=code
        campaign=load(args.output/'workload/campaign.json');processes=campaign['processes']
        require(campaign.get('inputs_unchanged') is True and len(processes)==1, 'Invalid/frozen workload campaign')
        process=processes[0];native=process.get('returncode')
        require(type(native) is int and process.get('status') in ('exited','timed_out'), 'Missing native outcome')
        result.update(native_exit_code=native,outcome='timed_out' if process['status']=='timed_out' else
                      'completed' if native==0 else 'guard_aborted' if native==2 else 'native_failed')
        if native in (0,2) and process['status']=='exited':
            records=read_records(args.output/'workload/validation.jsonl')
            require(records and records[0].get('schema')==3,'Current runner requires schema 3 native receipts')
            reference=validate_native(records,before['controls']['SDK_source_fingerprint'],'validation',args.adapter,native)
            h=records[0];s=result['settings']
            require(all(h.get(k)==s[k] for k in ('mode','seconds','warmup','interval'))
                    and h.get('private_growth_limit_bytes')==s['max_private_growth_mib']*2**20, 'Workload settings mismatch')
            result['references']=reference
        result['native_acceptance_receipt']=load(args.output/'workload/acceptance.json')
        after=collect_snapshot(args.adapter);save(args.output/'environment-after.json',after)
        comparison=compare_snapshots(before,after,within_run=True);result['within_run_environment']=comparison
        require(comparison['recorded_controls_match'],'Observed environment changed during run')
        result['status']='recorded'
    except (OSError,ValueError,KeyError,TypeError,subprocess.SubprocessError) as error:
        result.update(status='rejected',error=str(error))
    finally:
        result['artifacts']=[dict(path=p.relative_to(args.output).as_posix(),sha256=sha(p.read_bytes())) for p in sorted(args.output.rglob('*')) if p.is_file() and p.name!='case.json']
        save(args.output/'case.json',result)
    if result['status']=='recorded':
        try:validated_case(args.output/'case.json')
        except (OSError,ValueError,KeyError,TypeError) as error:
            result.update(status='rejected',error=str(error));save(args.output/'case.json',result)
    print('Environment case '+result['status']+'; memory_acceptance=false',flush=True)
    return 0 if result['status']=='recorded' else 1


def validated_case(path):
    case=load(path)
    require(isinstance(case,dict) and type(case.get('schema')) is int and case['schema']==1 and case.get('status')=='recorded'
            and case.get('diagnostic_only') is True and case.get('memory_acceptance') is False, 'Not a recorded diagnostic case')
    base=path.parent.resolve();members=set()
    require(isinstance(case.get('artifacts'),list),'Missing artifact manifest')
    for item in case['artifacts']:
        require(isinstance(item,dict),'Invalid artifact record')
        relative=item.get('path');require(isinstance(relative,str) and relative not in members, 'Duplicate/invalid artifact')
        members.add(relative);p=(base/relative).resolve()
        require(p.is_relative_to(base) and sha(p.read_bytes())==item.get('sha256'), 'Changed/missing case artifact')
    require({'environment-before.json','environment-after.json','workload/campaign.json','workload/acceptance.json'}<=members,'Missing case receipts')
    before=load(base/'environment-before.json');after=load(base/'environment-after.json')
    require(compare_snapshots(before,after,within_run=True)['recorded_controls_match'],'Unstable within-run environment')
    s=case.get('settings',{})
    require(isinstance(s,dict) and set(s)==set(settings(1,512)) and type(s.get('short_diagnostic')) is bool
            and type(s.get('seconds')) is int and type(s.get('max_private_growth_mib')) is int
            and 1<=s['max_private_growth_mib']<=4096
            and (1<=s['seconds']<=120 if s['short_diagnostic'] else s['seconds']==300)
            and s==settings(s['seconds'] if s['short_diagnostic'] else None,s['max_private_growth_mib']), 'Invalid case settings')
    campaign=load(base/'workload/campaign.json');processes=campaign.get('processes',[])
    require(campaign.get('inputs_unchanged') is True and campaign.get('SDK_source_fingerprint')==before['controls']['SDK_source_fingerprint']
            and len(processes)==1 and type(case.get('native_exit_code')) is int
            and case['native_exit_code']==processes[0].get('returncode'), 'Case/native outcome mismatch')
    process=processes[0];native=case['native_exit_code']
    require(process.get('status') in ('exited','timed_out') and type(case.get('runner_exit_code')) is int
            and case['runner_exit_code'] in (0,1), 'Invalid native/runner status')
    outcome='timed_out' if process['status']=='timed_out' else 'completed' if native==0 else 'guard_aborted' if native==2 else 'native_failed'
    require(case.get('outcome')==outcome, 'Mislabelled native outcome')
    require(case.get('native_acceptance_receipt')==load(base/'workload/acceptance.json'), 'Changed acceptance receipt summary')
    expected={before['controls']['files'][role]['path']:before['controls']['files'][role]['sha256']
              for role in ('stress','policy','runner','native_source','analyzer','core_library')}
    require(campaign.get('frozen_inputs')==expected, 'Workload inputs not bound to environment')
    command=[before['controls']['files']['stress']['path'],'--mode','validation','--seconds',str(s['seconds']),
             '--warmup',str(s['warmup']),'--interval',str(s['interval']),'--max-private-growth-mib',str(s['max_private_growth_mib']),
             '--output',str((base/'workload/validation.jsonl').resolve()),'--adapter',str(before['observations']['adapter_id'])]
    require(process.get('command')==command, 'Native command/settings mismatch')
    if case['native_exit_code'] in (0,2) and processes[0].get('status')=='exited':
        require('workload/validation.jsonl' in members,'Missing native references')
        rows=read_records(base/'workload/validation.jsonl');h=rows[0]
        require(h.get('schema')==3,'Current case requires schema 3 native receipts')
        reference=validate_native(rows,before['controls']['SDK_source_fingerprint'],'validation',before['observations']['adapter_id'],case['native_exit_code'])
        require(reference==case.get('references') and all(h.get(k)==s[k] for k in ('mode','seconds','warmup','interval'))
                and h.get('private_growth_limit_bytes')==s['max_private_growth_mib']*2**20, 'Case/reference/settings mismatch')
    return case,before,after


def compare_cases(before_path,after_path,change):
    initial={p:sha(p.read_bytes()) for p in (before_path,after_path)}
    a,before,_=validated_case(before_path);b,after,_=validated_case(after_path)
    result=compare_snapshots(before,after,change)
    workload=differences(a['settings'],b['settings'],'settings')
    result['differences']+=workload;result['recorded_controls_match']=not result['differences']
    result['outcomes']=[dict(outcome=c.get('outcome'),native_exit_code=c['native_exit_code'],references=c.get('references')) for c in (a,b)]
    require(all(sha(p.read_bytes())==h for p,h in initial.items()),'Case changed during comparison')
    for path,case in ((before_path,a),(after_path,b)):
        require(all(sha((path.parent/item['path']).read_bytes())==item['sha256'] for item in case['artifacts']), 'Artifact changed during comparison')
    return result


def main():
    p=argparse.ArgumentParser(description=__doc__);commands=p.add_subparsers(dest='command',required=True)
    r=commands.add_parser('record');r.add_argument('--adapter',type=adapter_argument,required=True);r.add_argument('--output',type=pathlib.Path,required=True)
    r.add_argument('--diagnostic-seconds',type=int);r.add_argument('--max-private-growth-mib',type=int,default=512)
    c=commands.add_parser('compare');c.add_argument('--before',type=pathlib.Path,required=True);c.add_argument('--after',type=pathlib.Path,required=True)
    c.add_argument('--change',choices=('none','selected-driver'),required=True);c.add_argument('--output',type=pathlib.Path,required=True)
    args=p.parse_args();args.output=args.output.resolve()
    try:
        require(not args.output.exists(),'Output already exists')
        if args.command=='record':
            require(args.diagnostic_seconds is None or 1<=args.diagnostic_seconds<=120,'Diagnostic duration must be 1..120 seconds')
            require(1<=args.max_private_growth_mib<=4096,'Growth guard must be 1..4096 MiB')
            return record(args)
        result=compare_cases(args.before.resolve(),args.after.resolve(),args.change)
        with args.output.open('x',encoding='utf-8') as f:json.dump(result,f,indent=2);f.write('\n')
        print('Recorded environment controls match='+str(result['recorded_controls_match'])+'; memory_acceptance=false')
        return 0 if result['recorded_controls_match'] else 1
    except (OSError,ValueError,KeyError,TypeError,subprocess.SubprocessError) as error:
        print('FAIL: '+str(error),file=sys.stderr);return 1


if __name__=='__main__':raise SystemExit(main())
