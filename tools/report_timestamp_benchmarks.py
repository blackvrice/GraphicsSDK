"""Report matching draw-only D3D12 GPU intervals; preserve original observations."""
import csv
import hashlib
import json
import pathlib
import zipfile
from compare_results import compare
from run_benchmarks import GPU_SCOPE, comparable, diligent_fingerprint, forge_fingerprint, timestamp_contract

ROOT=pathlib.Path(__file__).resolve().parents[1]
COUNTS=(1000,10000,100000)
PROFILES={'forge':('llgl','the_forge','graphicssdk'),'diligent':('diligent_core','graphicssdk')}


def record(path): return {'path':path.relative_to(ROOT).as_posix(),'sha256':hashlib.sha256(path.read_bytes()).hexdigest()}


def main():
    rows=[]; groups={}; campaigns=[]
    fingerprints={'forge':forge_fingerprint(ROOT),'diligent':diligent_fingerprint(ROOT)}
    for profile,sdks in PROFILES.items():
        group=[]
        for count in COUNTS:
            suffix='diligent-resolve' if profile=='diligent' else profile
            campaign=f'2026-10-21-timestamps-{suffix}-{count}'; campaigns.append(campaign)
            summary=json.loads((ROOT/'benchmarks/results'/campaign/'summary.json').read_text(encoding='utf-8'))
            if len(summary)!=len(sdks)*2 or {(r['sdk'],r['scenario'],r['count']) for r in summary}!={(sdk,scenario,count) for sdk in sdks for scenario in ('draw_gpu','instanced_gpu')}:
                raise ValueError('Missing/duplicate SDK or scenario/count')
            for scenario in ('draw_gpu','instanced_gpu'):
                matching=[r for r in summary if r['scenario']==scenario]
                if profile=='forge' and not all(comparable(matching[0],r) for r in matching): raise ValueError('Different physical environment/workload interval')
                if profile=='diligent' and comparable(matching[0],matching[1]): raise ValueError('Diligent start resolve must remain a separate interval')
            for r in summary:
                if r['validation'] or r['category']!='gpu_timestamp' or r['contract']['timestamp_scope']!=GPU_SCOPE or not r['readback_verified']:
                    raise ValueError('Wrong timestamp/correctness contract')
                if r['metrics']['gpu_ms']['sample_count']!=360 or len(r['metrics']['gpu_ms']['run_p50'])!=3 or r['warmup']!=20:
                    raise ValueError('Unexpected sample/run/warmup count')
                if r['sdk'] in ('the_forge','diligent_core') and r['sdk_build_source_sha256']!=fingerprints[profile]:
                    raise ValueError('Measured adapter differs from current source identity')
                if any(r['contract'].get(key)!=value for key,value in timestamp_contract(r['sdk']).items()):
                    raise ValueError('Query resolve policy differs from the pinned implementation')
            group+=summary
        if len({r['executable_sha256'] for r in group})!=1: raise ValueError('One profile used multiple executables')
        groups[profile]=group; rows+=group
    if len({r['sdk_build_source_sha256'] for r in rows if r['sdk']=='graphicssdk'})!=1:
        raise ValueError('Core implementation differs between profiles')
    for r in rows:
        if any(r['environment'][key]!=rows[0]['environment'][key] for key in ('cpu','ram_bytes','os','luid','driver','software_renderer','compiler')):
            raise ValueError('Physical environment differs across profiles')
    # Hash all included source bytes, including scripts/tests. Support-tool changes
    # create a new archive rather than overwriting an earlier measured snapshot.
    paths=set(p for pattern in ('*.cpp','*.hpp','*.h','*.cmake') for p in (ROOT/'benchmarks').rglob(pattern))
    paths.update((ROOT/'src').rglob('*.cpp')); paths.update((ROOT/'include').rglob('*.hpp'))
    paths.update((ROOT/'tools').glob('*.py')); paths.update((ROOT/'tools').glob('*.ps1')); paths.update((ROOT/'tests').glob('*.py'))
    paths.update((ROOT/'benchmarks/competitors').glob('*.lock.json'))
    paths.update(p for p in (ROOT/'cmake').rglob('*') if p.is_file())
    paths.update(ROOT/name for name in ('CMakeLists.txt','benchmarks/CMakeLists.txt','CMakePresets.json','LICENSE'))
    source_hash=hashlib.sha256(''.join(f"{p.relative_to(ROOT).as_posix()}|{hashlib.sha256(p.read_bytes()).hexdigest()}\n" for p in sorted(paths)).encode()).hexdigest()
    archive=ROOT/'benchmarks/reports/sources'/f'timestamps-{source_hash}.zip'
    if archive.exists():
        with zipfile.ZipFile(archive) as saved:
            if set(saved.namelist())!={p.relative_to(ROOT).as_posix() for p in paths} or any(saved.read(p.relative_to(ROOT).as_posix())!=p.read_bytes() for p in paths):
                raise ValueError('Existing archive differs; preserve it')
    else:
        with zipfile.ZipFile(archive,'x',compression=zipfile.ZIP_DEFLATED) as saved:
            for path in sorted(paths): saved.write(path,path.relative_to(ROOT).as_posix())
    output=ROOT/'benchmarks/reports/2026-10-21-timestamps'; output.mkdir(exist_ok=True)
    differences=[]
    for profile,group in groups.items():
        if profile!='forge': continue
        for sdk in PROFILES[profile][:-1]:
            differences.extend({'profile':profile,**r} for r in compare(group,group,sdk,'graphicssdk'))
    with (output/'comparison.csv').open('w',newline='',encoding='utf-8') as file:
        writer=csv.DictWriter(file,fieldnames=list(differences[0]));writer.writeheader();writer.writerows(differences)
    with (output/'gpu-percentiles.csv').open('w',newline='',encoding='utf-8') as file:
        writer=csv.writer(file);writer.writerow(['profile','sdk','scenario','count','gpu_p50_ms','gpu_p95_ms','gpu_p99_ms','gpu_mad_ms','gpu_run_p50_ms','frame_p50_ms','query_read_p50_ms'])
        for profile,group in groups.items():
            for r in sorted(group,key=lambda r:(r['count'],r['scenario'],r['sdk'])):
                v=r['metrics']['gpu_ms']; writer.writerow([profile,r['sdk'],r['scenario'],r['count'],*[v[k] for k in ('p50','p95','p99','mad')],json.dumps(v['run_p50']),r['metrics']['frame_ms']['p50'],r['metrics']['query_read_ms']['p50']])
    evidence={'campaigns':campaigns,'profiles':{profile:{'executable_sha256':group[0]['executable_sha256'],'build_fingerprint':fingerprints[profile]} for profile,group in groups.items()},
              'source_archive':record(archive),'source_identity':source_hash,'summary_files':[record(ROOT/'benchmarks/results'/campaign/'summary.json') for campaign in campaigns]}
    (output/'evidence.json').write_text(json.dumps(evidence,indent=2)+'\n',encoding='utf-8')
    env=rows[0]['environment']; samples=sum(r['metrics']['gpu_ms']['sample_count'] for r in rows)
    lines=['# D3D12 draw GPU timestamp 비교','',
           f'측정일: 2026-10-21. 최종 90개 독립 프로세스, {samples:,}개 GPU interval 표본, 30개 summary를 기록했다. 공통 비교는 GraphicsSDK·LLGL·The Forge의 54개 프로세스/6,480개 표본이다. Diligent의 36개 프로세스/4,320개 표본은 다른 query resolve 계약으로 따로 기록한다.',
           'GraphicsSDK·LLGL·The Forge는 Forge 프로파일의 같은 실행 파일에서 비교한다. Diligent 프로파일도 GraphicsSDK를 대조 관측으로 실행하지만 Diligent duration query는 시작 resolve 비용이 interval 안에 있어 SDK 간 비용 비율을 계산하지 않는다. 두 프로파일은 별도 바이너리이며 GraphicsSDK 코어 source fingerprint는 같다. 전체 SDK 순위로 일반화하지 않는다.','',
           '## GPU interval 계약','',
           '기존 draw/instanced는 query를 기록하지 않고 gpu_ms=null을 유지한다. 새 draw_gpu/instanced_gpu만 category=gpu_timestamp, contract revision 3으로 저장한다. 기존 end_to_end와 interval/단위가 다른 결과는 자동 비교에서 거부된다.',
           '공통 3개 SDK의 시작 marker는 clear·viewport·pipeline API 설정 뒤 첫 draw 직전, 끝 marker는 마지막 draw 직후 render pass 종료 전이다. query resolve/readback은 interval 밖이다. SDK가 draw 안에서 지연 적용하는 native state나 GPU queue stall은 interval에 남을 수 있어 순수 셰이더 ALU 시간이나 CPU API overhead로 해석하지 않는다.',
           '모든 측정은 같은 SM5.1 DXBC, 64×64 RGBA8, vertex-ID triangle, blend/depth/cull OFF, single graphics/direct queue의 한 완료 interval이다. 개별 N draws와 3 vertices × N instances의 한 draw를 구분한다. CPU 제출 뒤 fence/idle 완료를 기다리고 query 데이터를 읽은 뒤 다음 표본에서 재사용한다.',
           'GraphicsSDK writeTimestamp/resolveTimestamps/elapsedGpuMilliseconds, LLGL TimeElapsed/QueryResult, Diligent QUERY_TYPE_DURATION/GetData, Forge QueryPool begin/end/resolve/getQueryData의 공개 API를 사용한다. GPU 구간 안에 native query를 우회 삽입하지 않는다.',
           '고정 Diligent의 DeviceContextD3D12Impl::BeginQuery(DURATION)는 QueryManagerD3D12::EndQuery를 호출하고 그 함수는 시작 timestamp 직후 ResolveQueryData도 기록한다. 따라서 Diligent interval에는 시작 query resolve가 들어간다. contract.query_resolve에 start_marker_resolve_inside_interval_end_marker_resolve_after를 기록하고 공통 deferred resolve 계약과의 자동 비교를 거부한다. 공개 API로 이 resolve를 끝 marker 뒤로 미루는 경로는 확인되지 않았다.',
           'LLGL 공개 결과는 integer nanoseconds여서 1e6으로 나눈다. Diligent는 Duration/Frequency, Forge는 (end-begin)/queue frequency를 초→ms 변환하며 GraphicsSDK는 공개 ms 결과를 사용한다. 준비되지 않은 결과, 0/비유한 frequency, 역순 counter, 음수·NaN·무한 duration은 실패한다. 같은 tick의 0 interval은 해상도 한계로 허용한다.',
           'D3D12 timestamp는 이전 GPU 작업 완료 시점을 기록하며 queue frequency는 ticks/sec다. [Microsoft timing 문서](https://learn.microsoft.com/en-us/windows/win32/direct3d12/timing)를 따른다. CPU wall time이나 fence wait를 GPU 시간으로 대신하지 않는다.',
           'SDL3 공개 GPU API에는 대응 query가 없고 bgfx view 전체 timer는 clear 등을 포함해 구간이 다르므로 새 시나리오를 명시적으로 거부한다. runner는 결과 폴더를 만들기 전에 거부한다. upload/copy·compute 구간의 timestamp 비교는 이번 범위에 포함되지 않는다.','',
           '## 환경과 표본','',
           f"{env['cpu']}, RAM {env['ram_bytes']:,} bytes, {env['gpu']}, LUID `{env['luid']}`, driver `{env['driver']}`.",
           f"{env['os']}, {env['compiler']}, x64 Release /O2 /MD, no LTO, validation OFF, headless, 단일 API 스레드, frames-in-flight 1.",
           '각 count 1,000/10,000/100,000과 draw/instanced 조합마다 warmup 20 + 120회 × 3개 독립 프로세스다. 고정 seed로 순서를 섞고 이상치를 삭제하지 않았다. 측정 중 다른 빌드·GPU 테스트를 실행하지 않았다. GPU power/clock, background activity 및 driver cache cold/warm은 통제하지 않았다.','',
           '## GPU P50 (ms)','',
           '[전체 P95/P99/MAD·회차별 median 및 CPU 보조 metric](../benchmarks/reports/2026-10-21-timestamps/gpu-percentiles.csv)을 함께 확인한다. 첫 표만 동일 interval 비교이고 Diligent 표는 시작 resolve를 포함한 별도 관측이다.','']
    for profile,sdks in PROFILES.items():
        if profile=='diligent': sdks=('diligent_core',)
        lines += [f'### {profile} 프로파일','', '| 작업 | Count | '+' | '.join(sdks)+' |','|---|---:|'+'---:|'*len(sdks)]
        group=groups[profile]
        for count in COUNTS:
            for scenario in ('draw_gpu','instanced_gpu'):
                values={r['sdk']:r['metrics']['gpu_ms']['p50'] for r in group if r['count']==count and r['scenario']==scenario}
                lines.append(f'| {scenario} | {count} | '+' | '.join(f'{values[sdk]:.6f}' for sdk in sdks)+' |')
        lines.append('')
    lines += ['## 완료 대기와 결과 수집 비용','',
              'record_ms/submit_ms/wait_ms는 CPU wall 구간이며 gpu_ms와 단위만 같다. query_read_ms는 최초 GPU 완료 뒤 결과 수집 비용이다. LLGL QueryResult는 별도 내부 resolve command list를 제출·대기하므로 이 비용이 클 수 있다. 이 작업은 GPU interval 밖이며 timestamp-enabled frame_ms에는 포함된다. 이를 draw shader 시간에 더하지 않는다.',
              'SDK별 API 경계와 query resolve 위치가 달라 instrumented frame/부분 CPU metric을 bare native API 성능 순위로 보지 않는다. GPU timestamp 자체도 GPU 파이프라인과 clock 상태를 바꿀 수 있으므로 이전 query 없는 frame 측정을 대체하지 않는다.','',
              '## 검증과 제한','',
              '최종 Forge CTest 17개, Diligent CTest 7개 및 기존 SDK CTest 4개가 통과했다. timestamp 변환 CPU 테스트는 12개 조건을 검사했고 Python 통계/계약/감사 테스트는 34개 통과했다. unsupported 요청의 사전 거부, scope/category 분리, 완료 query 누락·비유한 값 거부, GPU percentile 보존 및 Diligent의 시작 resolve 계약 분리를 확인했다.',
              '각 GPU timestamp 작업은 실제 native debug layer에서 10,000 draw/instance를 여러 표본 반복하고 ERROR/CORRUPTION을 실패 처리한다. GraphicsSDK·LLGL·Diligent의 종료 시점 검사도 보강했다. Forge GPU-based validation이나 장기 stress 검증은 아니다. 기존 SDK GBV 테스트는 별도로 통과했다.',
              '모든 성능 프로세스는 측정 전후 빨간 중심·파란 모서리의 실제 이미지 readback을 확인한다. Forge의 untimed native copy probe 및 MSVC/no-NVAPI 호환 구성은 [이전 Forge 보고서](FORGE_BENCHMARK_REPORT.md)의 제한을 유지한다.',
              '초기 검증에서 GraphicsSDK root signature 생성이 1회 실패했다. timestamp 기록 전 초기화 단계였으며 당시 native code가 남지 않아 원인은 확정하지 못했다. 재실행, 최종 전체 검증, 일반/timestamp draw의 추가 독립 초기화 10회와 debugger capture에서는 재현되지 않았다. 진단에 native code를 추가했지만 해당 초기화 문제가 수정됐다고 주장하지 않는다. 최초 실패 로그를 보존했고 성능 runner에는 자동 재시도가 없다.',
              '[검증 결과와 로그 해시](../benchmarks/reports/validation/2026-10-21-timestamps/final-summary.json)에 초기 실패·최종 성공·debug capture를 함께 남겼다. capture의 optimized-clear-value warning은 보존했으며 clear는 timestamp interval 밖이다.',
              '초기 timestamps-diligent-1000/10000/100000 자료는 start resolve를 interval 밖으로 잘못 분류한 계약을 포함한다. 이 원본 36개 프로세스/4,320개 표본과 최초 소스 ZIP은 탐색 자료로 보존하고 최종 표·비교에서 제외한다. 올바른 정책으로 timestamps-diligent-resolve-*를 새로 측정했으며 원본 metadata를 고쳐 쓰지 않았다. [제외 목록](../benchmarks/reports/2026-10-21-timestamps-exploratory/exclusions.json)에 파일 hash와 사유를 남겼다.',
              '새 compiler/linker inventory는 Forge 1,103개 입력/39개 구성요소와 Diligent 1,350개 입력/26개 구성요소다. SDK ZIP은 그대로이며 기존 SDK package --check가 통과했다. 라이선스 종류와 Forge의 비활성 PIX header 재배포 조건 미해결 사항은 기존 정책을 유지한다. inventory는 법적 배포 승인이 아니다.','',
              '## 재현 자료','',
              *[f'- [{campaign} 원본](../benchmarks/results/{campaign}/summary.json)' for campaign in campaigns],
              '- [SDK pair별 세부 비교 CSV](../benchmarks/reports/2026-10-21-timestamps/comparison.csv)',
              '- [빌드·실행 파일·소스 archive·결과 해시](../benchmarks/reports/2026-10-21-timestamps/evidence.json)',
              f'- [측정한 자체 소스 snapshot](../{archive.relative_to(ROOT).as_posix()})',
              '- [Forge timestamp inventory](../licenses/generated/windows-benchmarks-timestamps-forge-release/inventory.json)',
              '- [Diligent timestamp inventory](../licenses/generated/windows-benchmarks-timestamps-diligent-release/inventory.json)','',
              '다음 로드맵 항목은 CPU 사용률·최대 CPU/GPU 메모리 관측 및 shader/PSO cache 통제다. 전체 A–H, Vulkan/Metal 및 장기 안정성 검증은 남아 있다.']
    (ROOT/'docs/GPU_TIMESTAMP_BENCHMARK_REPORT.md').write_text('\n'.join(lines)+'\n',encoding='utf-8')
    print(f'PASS: 90 processes, {samples} GPU intervals, 30 summaries; matched comparisons kept within each binary profile')


if __name__=='__main__': main()
