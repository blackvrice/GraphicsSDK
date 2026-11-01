"""Join replayed Linux correctness evidence; never collect timings or execute archived code."""
import argparse
import hashlib
import json
import math
import pathlib
import re
import tempfile
import zipfile

import linux_comparison as q
import linux_llgl_probe as p
import linux_workload_matrix as matrix
import linux_llgl_upload as upload
import linux_llgl_draw as draw
import linux_llgl_pso as pso
import linux_llgl_allocation as allocation
from linux_comparison import ROOT, require, sha, save

MODULES=dict(matrix=matrix,upload=upload,draw=draw,pso=pso,allocation=allocation)
ORDER=('upload','draw','pso','allocation','matrix')
CASES=dict(upload=('odd','upload1080','upload4k','upload4k16f'),draw=('draw1','draw1000','instanced1000'),
           pso=('pso1','pso8'),allocation=('buffer1','buffer1000','texture1','texture1000'))
FILES={'receipt.json','report.json','report.md','handoff.json','inventory.json','evidence.zip','collector-source.txt'}
MAX_BYTES=512*1024*1024


def read(path):return matrix.strict_json(path.read_bytes())


def handoff():
    # These are data describing existing manual workflows, not commands dispatched by this tool.
    return dict(schema_version=1,executed=False,physical_GPU_accepted=False,performance_accepted=False,
        steps=[
            dict(task='P3-LNX-15',kind='environment_required',workflow='.github/workflows/linux-hardware-acceptance.yml',
                 inputs=['icd','device_uuids'],runner=['self-hosted','Linux','X64','graphicssdk-gpu']),
            dict(task='P3-BEN-07',kind='environment_required',workflow='.github/workflows/linux-comparison-physical.yml',
                 inputs=['icd','device_uuid'],runner=['self-hosted','Linux','X64','graphicssdk-gpu']),
            dict(task='P3-BEN-08',kind='implementation_and_environment_required',
                 remaining='Qualify LLGL consumers on a real Linux GPU; current LLGL runners reject physical mode.'),
            dict(tasks=['P3-LBEN-01','P3-LBEN-02','P3-LBEN-03','P3-LBEN-04'],kind='implementation_and_environment_required',
                 remaining='Prepare Linux three-SDK timed campaigns, then collect fresh repetitions on the same physical GPU/driver; software diagnostic rows are excluded.'),
            dict(task='P3-WSI-08',kind='human_observation_required',
                 remaining='Actual Linux desktop presentation and human window-operation acceptance.'),
            dict(tasks=['P3-GATE-01','P3-GATE-02','P3-GATE-03'],kind='acceptance_required',
                 remaining='Required physical/display/comparison evidence and final review remain incomplete.')])


def merge_records(groups):
    records={}
    for group in groups:
        for row in group:
            path=row['path'];value={k:row[k] for k in ('bytes','sha256')}
            require(type(path) is str and pathlib.PurePosixPath(path).is_absolute() and type(value['bytes']) is int
                    and value['bytes']>0 and type(value['sha256']) is str and re.fullmatch('[0-9a-f]{64}',value['sha256']), 'Invalid file binding')
            require(path not in records or p.same(records[path],value),'Conflicting frozen/runtime file binding: '+path)
            records[path]=value
    return [dict(path=path,**records[path]) for path in sorted(records)]


def compose(receipts,results,bindings):
    """Pure join of already replayed receipts; fixtures alone are not native evidence."""
    require(set(receipts)==set(results)==set(bindings)==set(MODULES),'All five replayed components required')
    m=receipts['matrix'];qualification=m['readiness']['qualification'];commit=m['source_commit']
    require(type(commit) is str and re.fullmatch('[0-9a-f]{40}',commit),'Invalid native revision')
    p.exact(qualification,dict(mode='software-diagnostic',physical_probe_qualified=False,P3_BEN_07_completed=False))
    require(type(qualification['selected']['device_type']) is int and qualification['selected']['device_type']==4,'Software CPU device required')
    probe=receipts['upload']['probe'];source_hashes={};coverage=[];components=[];last=0
    require(qualification['source_commit']==probe['source_commit']==commit,'Qualification/probe revision differs')
    for name in ORDER:
        r=receipts[name];result=results[name]
        require(r['source_commit']==commit and r['status']=='completed' and r['inputs_unchanged'] is True,'Mixed/incomplete native revisions')
        p.exact(result,dict(performance_samples=0,physical_GPU_accepted=False))
        p.exact(r,dict(performance_samples=0,performance_accepted=False,physical_GPU_accepted=False))
        if name!='matrix':
            require(p.same(r['probe'],probe) and p.same(r['probe']['qualification'],qualification),'Different LLGL build or common qualification')
            p.exact(r,dict(performance_samples=0,performance_accepted=False,physical_GPU_accepted=False))
            require(r['probe']['LLGL_commit']==q.read(ROOT/'benchmarks/competitors/sources.lock.json')['llgl']['commit'],'Different approved LLGL pin')
        for item in r['source_inputs']:
            path=item['path'];digest=item['sha256']
            require(path not in source_hashes or source_hashes[path]==digest,'Conflicting shared source bytes: '+path)
            source_hashes[path]=digest
        rows=r['runs'];require(rows and all(type(row[k]) in (int,float) and math.isfinite(row[k])
            for row in rows for k in ('started','finished')) and last<=rows[0]['started'],'Cross-component processes overlap or run out of order')
        last=rows[-1]['finished']
        normal=controls=rejected=0;totals=dict(verified_images=0,verified_buffers=0,verified_bytes=0)
        for row in rows:
            job=row['job'];kind=job.get('kind','normal')
            if kind!='normal':
                if kind in upload.TOKENS:controls+=1
                else:rejected+=1
                continue
            normal+=1;v=row['result'];profile=job['profile'] if name=='matrix' else name
            sdk=job['sdk'] if name=='matrix' else 'llgl'
            require(profile in CASES and job['scenario'] in CASES[profile] and sdk in ('graphicssdk','sdl','llgl')
                    and type(job['validation']) is bool,'Unexpected coverage dimension')
            require(v['contract']==m['contract']['profiles'][profile]['contract'],'Different workload contract across SDKs')
            counts={k:v.get(k,0) for k in totals}
            require(all(type(n) is int and n>=0 for n in counts.values()),'Invalid readback counters')
            for k in totals:totals[k]+=counts[k]
            coverage.append(dict(profile=profile,scenario=job['scenario'],sdk=sdk,validation=job['validation'],
                contract=v['contract'],**counts,readback_verified=True,
                identity_attribution=v.get('identity_attribution','actual_LLGL_native_handle'),
                native_draw_observed=sdk=='llgl' and profile in ('draw','pso'),
                performance_samples=0,physical_GPU_accepted=False))
        require(normal+controls+rejected==len(rows)==result['processes'],'Component process totals differ')
        components.append(dict(name=name,processes=len(rows),normal_processes=normal,core_sync_controls=controls,
            rejection_controls=rejected,**totals,receipt=bindings[name]))
    expected={(profile,scenario,sdk,validation) for profile,cases in CASES.items() for scenario in cases
              for sdk in ('graphicssdk','sdl','llgl') for validation in (False,True)}
    actual=[(row['profile'],row['scenario'],row['sdk'],row['validation']) for row in coverage]
    require(len(actual)==len(set(actual))==78 and set(actual)==expected,'Missing/duplicate three-SDK scenario/mode coverage')
    totals={k:sum(c[k] for c in components) for k in ('processes','normal_processes','core_sync_controls','rejection_controls','verified_images','verified_buffers','verified_bytes')}
    require((totals['processes'],totals['normal_processes'],totals['core_sync_controls'],totals['rejection_controls'])==(124,78,8,38),'Complete process schedule differs')
    require((totals['verified_images'],totals['verified_buffers'],totals['verified_bytes'])==(24240,24024,2918751264)
            and results['matrix']['diagnostic_rows']==156,'Complete readback/diagnostic totals differ')
    files=merge_records([receipts[name]['frozen_inputs']+receipts[name].get('runtime_inputs',[]) for name in ORDER])
    coverage.sort(key=lambda row:(row['profile'],row['scenario'],row['sdk'],row['validation']))
    return dict(schema_version=1,task='P3-LBEN-software-integration',status='software_correctness_replayed',native_source_commit=commit,
        selected=qualification['selected'],environment=qualification['environment'],SDK_source_fingerprint=qualification['SDK_source_fingerprint'],
        SDL_commit=qualification['SDL_commit'],LLGL_commit=probe['LLGL_commit'],LLGL_profile=p.PROFILE,
        qualification_sha256=m['readiness']['qualification_sha256'],components=components,coverage=coverage,totals=totals,
        shared_source_files=[dict(path=path,sha256=source_hashes[path]) for path in sorted(source_hashes)],frozen_file_union=files,
        diagnostic_CPU_rows_retained_in_raw_evidence=results['matrix']['diagnostic_rows'],diagnostic_timings_excluded=True,
        preparation_processes_counted_again=False,independent_full_hash_cross_SDK_comparison_claimed=False,SDL_native_handle_verified=False,
        performance_samples=0,performance_accepted=False,overall_SDK_ranking=False,physical_GPU_accepted=False,
        P3_BEN_07_completed=False,P3_BEN_08_completed=False,P3_LBEN_completed=False,Phase3_completed=False)


def replay(folders,live=False):
    receipts={};results={};bindings={}
    for name,module in MODULES.items():
        folder=folders[name];results[name]=module.check(folder,live=live);receipts[name]=read(folder/'receipt.json')
        bindings[name]=dict(path=name+'/receipt.json',sha256=sha(folder/'receipt.json'))
    return compose(receipts,results,bindings)


def render(report):
    t=report['totals'];lines=['# Linux 세 SDK software correctness 통합', '',
        'Native source: `'+report['native_source_commit']+'`. 기존 원본의 재검사이며 성능 측정·물리 GPU 수용이 아니다.', '',
        f"13개 입력 × 3 SDK × 일반/필수 validation = 78개 정상 실행. workload·제어 {t['processes']}개 프로세스에는 core/sync 제어 8개·거부 제어 38개를 포함한다. 선행 qualification/probe/smoke는 이 합계에 다시 포함하지 않는다.", '',
        '| 구성 | 정상 | core/sync | 거부 | 이미지 | 버퍼 | bytes |','|---|---:|---:|---:|---:|---:|---:|']
    for c in report['components']:
        lines.append('| '+' | '.join(str(c[k]) for k in ('name','normal_processes','core_sync_controls','rejection_controls','verified_images','verified_buffers','verified_bytes'))+' |')
    lines+=['',f"전체 이미지 {t['verified_images']:,}개·버퍼 {t['verified_buffers']:,}개·{t['verified_bytes']:,} bytes.", '',
        '개별 validator를 먼저 재생하고 동일 qualification·LLGL build·source revision·공통 source/runtime hash와 전역 직렬 순서를 대조했다.',
        'SDK/SDL의 156개 CPU 진단 행은 원본에만 보존한다. 통계·순위·성능 표본은 생성하지 않는다.',
        'SDK/SDL 장치 귀속과 LLGL 실제 native handle·draw 관측의 차이를 coverage에 명시한다. 서로 다른 SDK의 payload hash를 직접 같다고 주장하지 않는다.', '',
        'LLGL의 실제 Linux GPU 모드와 세 SDK Linux 성능 캠페인은 별도 구현·환경 검증이 남아 있다. handoff.json은 미실행 준비 목록이며 워크플로를 자동 실행하지 않는다.',
        '실제 Linux GPU·사람의 표시 조작·최종 gate와 Phase 3는 미완료다. source ZIP 또는 native code를 실행하지 않는다.', '']
    return '\n'.join(lines)


def safe_name(name):
    value=pathlib.PurePosixPath(name)
    require(type(name) is str and value.as_posix()==name and len(value.parts)>1 and value.parts[0] in MODULES
        and not value.is_absolute() and all(part not in ('.','..','') for part in value.parts)
        and '\\' not in name and ':' not in name,'Unsafe evidence member')
    return value


def unpack(archive,inventory,destination):
    require(type(inventory) is list and 0<len(inventory)<5000,'Invalid evidence inventory')
    names=[item['path'] for item in inventory]
    require(len(names)==len(set(n.casefold() for n in names)) and sum(item['bytes'] for item in inventory)<=MAX_BYTES,'Duplicate or oversized evidence inventory')
    with zipfile.ZipFile(archive) as z:
        require(len(z.namelist())==len(names) and set(z.namelist())==set(names),'Evidence ZIP inventory drift')
        for item in inventory:
            name=item['path'];safe_name(name);info=z.getinfo(name)
            require(type(item['bytes']) is int and item['bytes']>=0 and info.file_size==item['bytes']
                and ((info.external_attr>>16)&0o170000) != 0o120000 and not info.is_dir(),'Invalid evidence member type/size')
            data=z.read(name);require(hashlib.sha256(data).hexdigest()==item['sha256'],'Evidence member hash drift')
            path=destination.joinpath(*pathlib.PurePosixPath(name).parts);path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(data)


def check(folder,live=False):
    require({path.name for path in folder.iterdir()}==FILES,'Report bundle inventory differs')
    r=read(folder/'receipt.json')
    p.exact(r,dict(schema_version=1,task='P3-LBEN-software-integration',native_code_executed_by_report=False,
                   performance_accepted=False,physical_GPU_accepted=False))
    require(all(sha(folder/name)==r['files'][name] for name in FILES-{'receipt.json'}),'Report bundle changed')
    require(set(r['files'])==FILES-{'receipt.json'} and sha(folder/'collector-source.txt')==r['collector_source_sha256'],'Collector source binding changed')
    (ROOT/'out').mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='lrr-',dir=ROOT/'out') as td:
        temp=pathlib.Path(td).resolve();unpack(folder/'evidence.zip',read(folder/'inventory.json'),temp)
        report=replay({name:temp/name for name in MODULES},live=live)
    require(p.same(report,read(folder/'report.json')) and report['native_source_commit']==r['native_source_commit'],'Replayed aggregate changed')
    require((folder/'report.md').read_text(encoding='utf-8')==render(report) and p.same(read(folder/'handoff.json'),handoff()),'Report text or handoff drift')
    return dict(native_source_commit=report['native_source_commit'],**report['totals'],coverage_rows=len(report['coverage']),
        performance_samples=0,physical_GPU_accepted=False,Phase3_completed=False)


def collect(folders,output,live=False):
    require(set(folders)==set(MODULES),'Five input folders required')
    folders={name:path.resolve() for name,path in folders.items()};out=output.resolve()
    require(out.is_relative_to(ROOT/'out') and not out.exists() and all(not out.is_relative_to(path) and not path.is_relative_to(out) for path in folders.values()),'Fresh independent output under out/ required')
    report=replay(folders,live);out.mkdir(parents=True);inventory=[]
    with zipfile.ZipFile(out/'evidence.zip','w',zipfile.ZIP_DEFLATED) as z:
        for name,folder in folders.items():
            for path in sorted(folder.rglob('*')):
                if not path.is_file():continue
                require(not path.is_symlink(),'Evidence symlink forbidden')
                key=name+'/'+path.relative_to(folder).as_posix();safe_name(key);data=path.read_bytes()
                inventory.append(dict(path=key,bytes=len(data),sha256=hashlib.sha256(data).hexdigest()));z.writestr(key,data)
    save(out/'inventory.json',inventory);save(out/'report.json',report);save(out/'handoff.json',handoff())
    (out/'report.md').write_text(render(report),encoding='utf-8',newline='\n')
    (out/'collector-source.txt').write_bytes(pathlib.Path(__file__).read_bytes())
    r=dict(schema_version=1,task='P3-LBEN-software-integration',collector_checkout_head=q.git(ROOT,'rev-parse','HEAD'),
        native_source_commit=report['native_source_commit'],collector_source_sha256=sha(out/'collector-source.txt'),
        live_inputs_verified=live,native_code_executed_by_report=False,performance_accepted=False,physical_GPU_accepted=False,
        files={name:sha(out/name) for name in sorted(FILES-{'receipt.json'})})
    save(out/'receipt.json',r);return check(out,live)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('stage',choices=('collect','check'))
    parser.add_argument('--output',required=True,type=pathlib.Path);parser.add_argument('--live',action='store_true')
    for name in MODULES:parser.add_argument('--'+name,type=pathlib.Path)
    args=parser.parse_args()
    if args.stage=='check':result=check(args.output,args.live)
    else:
        require(all(getattr(args,name) is not None for name in MODULES),'All five evidence folders required')
        result=collect({name:getattr(args,name) for name in MODULES},args.output,args.live)
    print(json.dumps(result,indent=2))
