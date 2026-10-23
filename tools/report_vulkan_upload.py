"""Revalidate raw Vulkan upload results and write a narrowly scoped baseline report."""
import argparse
import csv
import json
import pathlib
import run_vulkan_upload as u

def review(path):
    x=json.loads((path/'results.json').read_text());commands=json.loads((path/'commands.json').read_text())
    u.require(len(commands)==2+16+12*x['repetitions']+10 and all(c['returncode']==c['expected'] for c in commands),'Incomplete process matrix/controls')
    u.require(x['api']=='vulkan' and x['category']=='end_to_end' and x['contract']==u.CONTRACT and x['validation_timings_excluded'] is True,'Different comparison contract')
    u.require(x['source_fingerprint']==u.fingerprint(),'Report executable sources changed')
    u.require(x['samples_removed']==0 and x['GPU_timestamp_measurement'] is False and x['overall_SDK_ranking'] is False and x['identity_native_handle_verified'] is False,'Unreviewed scope claim')
    names=set();grouped={};correctness=set();images=verified=0
    by_name={c['name']:c for c in commands};u.require(len(by_name)==len(commands),'Duplicate command record')
    for run in x['runs']:
        name=run['name'];r=run['result'];u.require(name not in names,'Duplicate process record');names.add(name)
        raw=json.loads((path/(name+'.stdout')).read_bytes());u.require(raw==r,'Raw stdout differs from normalized result')
        validation=run['kind']=='correctness';u.require(run['kind'] in ('correctness','measurement'),'Unknown run category')
        u.require(r['sdk'] in ('graphicssdk','sdl') and r['preference'] in ('high','low'),'Unknown SDK/device preference')
        w,n=(1,3) if validation else (x['warmup'],x['samples_per_process'])
        command=by_name[name]
        u.require(command['argv'][1:]==[r['sdk'],r['scenario'],r['preference'],str(w),str(n),str(int(validation)),r['device_name']] and command['validation']==validation and command['expected']==0,'Command/result attribution differs')
        u.require(command['seconds']>=sum(r['samples_ms'])/1000,'Sample intervals exceed enclosing process duration')
        u.validate_result(r,r['sdk'],r['scenario'],r['preference'],w,n,validation,x['probes'][r['preference']],x['source_fingerprint'],x['SDK_source_fingerprint'],x['SDL_commit'])
        if validation:
            key=(r['preference'],r['sdk'],r['scenario']);u.require(key not in correctness,'Duplicate correctness cell');correctness.add(key)
        if not validation:grouped.setdefault((r['preference'],r['scenario'],r['sdk']),[]).append(r)
        images+=r['verified_images'];verified+=r['verified_bytes']
    u.require(len(x['runs'])==16+12*x['repetitions'] and images==x['verified_images'] and verified==x['verified_bytes'],'Wrong verification totals')
    u.require(len(grouped)==12 and len(x['groups'])==6,'Incomplete comparison cells')
    u.require(correctness=={(p,s,c) for p in ('high','low') for s in ('graphicssdk','sdl') for c in u.SHAPES},'Incomplete required-validation matrix')
    u.require(len({(r['preference'],r['scenario']) for r in x['groups']})==6,'Duplicate summary row')
    for row in x['groups']:
        for sdk in ('graphicssdk','sdl'):
            data=grouped[(row['preference'],row['scenario'],sdk)];u.require(len(data)==x['repetitions'],'Insufficient independent process runs')
            u.require(u.summarize(data)==row['statistics'][sdk],'Changed summary statistics')
        u.require(row['GraphicsSDK_over_SDL_p50_ratio']==row['statistics']['graphicssdk']['p50_ms']/row['statistics']['sdl']['p50_ms'],'Changed ratio')
    return x

def write_report(path,out,doc):
    x=review(path);u.require(not out.exists() and not doc.exists(),'Reports are exclusive snapshots')
    out.mkdir(parents=True)
    summary={k:v for k,v in x.items() if k!='runs'}
    summary.update(production_SDK_changed=False,public_API_changed=False,SDK_rebuilt=False,SDL_rebuilt=False,CTests=2,Python_tests=239,correctness_GPU_processes=16,measurement_GPU_processes=12*x['repetitions'],performance_samples=12*x['repetitions']*x['samples_per_process'],full_SDK_CTests_rerun=False,
                   prior_93_selected_CTests='Preserved historical evidence; not rerun for unchanged SDK',Linux_native_verified=False,Phase3_complete=False,memory_acceptance=False,Intel_AV_cause_resolved=False)
    (out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
    with (out/'metrics.csv').open('w',newline='') as f:
        writer=csv.writer(f);writer.writerow(['gpu','scenario','sdk','processes','samples','p50_ms','p95_ms','p99_ms','MAD_ms','min_ms','max_ms','process_medians_ms'])
        for row in x['groups']:
            for sdk,stats in row['statistics'].items():writer.writerow([row['device_name'],row['scenario'],sdk,*[stats[k] for k in ('processes','samples','p50_ms','p95_ms','p99_ms','MAD_ms','min_ms','max_ms')],json.dumps(stats['process_medians_ms'])])
    table='| GPU | 작업 | GraphicsSDK P50 ms | SDL P50 ms | SDK / SDL |\n|---|---|---:|---:|---:|\n'
    for row in x['groups']:
        a=row['statistics']['graphicssdk']['p50_ms'];b=row['statistics']['sdl']['p50_ms'];table+=f"| {row['device_name']} | {row['scenario']} | {a:.4f} | {b:.4f} | {a/b:.3f} |\n"
    text=f'''# Phase 3 — 동일 Vulkan 텍스처 업로드 비교

2026-10-23, Windows x64 / MSVC 19.44, GraphicsSDK와 고정 SDL3의 B-upload 부분 비교다.
SDK 소스·공개 API·ABI를 바꾸지 않고 이전 설치 SDK와 SDL static library를 재사용했다.
새 harness와 runner, fail-closed 결과 검사, 독립 프로세스 통계와 라이선스 감사를 구현했다.
Linux 환경이 현재 PC에 없어 Linux 실기기 검증은 수행하지 않았다. Phase 3 전체 완료가 아니다.

## 측정 계약

Vulkan / Release / CPU thread 1개 / headless / frame-in-flight 1개 / sampled texture 하나 /
재사용 upload buffer 하나다. 1080p RGBA8, 4K RGBA8, 4K RGBA16F의 동일 tight CPU payload를
기존 texture에 덮어쓰고 해당 submit fence가 완료될 때까지 wall time을 측정한다. memcpy,
API 내부 staging·recording·barrier·allocation·submit·wait 비용은 포함한다. SDK 기본 recording의
query-pool 생성 등 내부 정책도 그대로 포함한다. Shader compile·GPU timestamp·표시·장치/자원
초기 생성과 readback/비교는 측정 밖이다. Queue WaitIdle을 GPU 시간으로 보고하지 않는다.

매 반복 CPU payload를 바꾸고 warmup/측정 모두 전체 readback을 비교한다. RGBA16F payload는
유한 half 값만 쓴다. SDK staging pitch는 256-byte 정렬이고 측정 해상도의 tight row와 같다.
65×37 odd-pitch는 별도 validation correctness이며 성능 집계에 섞지 않는다. Readback은 측정
밖이지만 반복 사이의 cache/온도·메모리 상태에 영향을 주므로 이 검증 포함 실행 주기의
비교다. 연속 streaming 최대 처리량이나 순수 low-level API 오버헤드 순위가 아니다.

SDL 공개 Vulkan GPU API를 사용하며 [업로드](https://wiki.libsdl.org/SDL3/SDL_UploadToGPUTexture)와
[펜스 대기](https://wiki.libsdl.org/SDL3/SDL_WaitForGPUFences)의 GPU 완료 경계를 따른다.
Cycling은 false다. SDK도 동일 텍스처/업로드 메모리를 재사용하고 submit fence를 기다린다.
SDL pin은 `{x['SDL_commit']}`이고 vendor 소스는 수정하지 않았다. LLGL 등 다른 Vulkan
adapter와 draw/allocation/PSO 작업은 이 프로파일에서 아직 구현/측정하지 않았다.

## GPU 식별과 validation

SDL의 high/low-power 선호로 현재 NVIDIA/Intel을 발견하고, 보고된 이름이 native Vulkan
장치 및 DXGI에 각각 정확히 하나 대응해야 한다. Native UUID/driver UUID/LUID/vendor/device/
API·driver version, DXGI LUID/driver, CPU/OS/RAM/compiler를 저장하고 모든 비교 표본에서
같은 값을 요구한다. SDK는 해당 이름에 유일하게 대응하는 public adapter ID로 생성한다.
SDL 공개 API는 native VkPhysicalDevice를 제공하지 않으므로 **이름의 유일한 대응을 통한
귀속이며 SDL native handle 직접 증명이 아니다**. 중복/일반 이름은 거부한다. 다른 장치·
드라이버·API·범주·소스 fingerprint의 결과를 합치지 않는다. Driver version 표기는 raw native
값과 DXGI 문자열을 각각 보존한다. 미지원/불명 값으로 동등성을 합성하지 않는다.

상속 VK_/VVL_/SDL_ 설정을 child에서 제거하고 implicit layers를 비활성화한다. 정확성은
고정 Khronos DLL/manifest SHA, 명시적 layer 경로, synchronization/submit-time 설정을 쓰고
실제 로드한 DLL 경로를 확인한다. SDL의 validation 없는 fallback을 막기 위해 먼저 SDK의
required-layer 생성을 확인한다. SDL odd controls의 loader trace에서 layer 삽입도 확인한다.
SDK는 validationErrorCount, SDL은 error-priority log 및 captured stderr의 validation error/
sync hazard를 검사한다. SDL native callback ERROR 카운터를 제공한다고 주장하지 않는다.
검증을 끈 성능 프로세스는 validation DLL 미로드를 확인한다. 시스템 설정 변경은 없다.

## 결과

장치×SDK×측정 작업마다 {x['repetitions']}개 독립 프로세스, 프로세스마다 warmup {x['warmup']}회 /
측정 {x['samples_per_process']}개다. 실행 순서는 seed {x['seed']}로 섞었다. 전체 성능 표본은
{12*x['repetitions']*x['samples_per_process']:,}개이며 validation 시간은 전부 제외한다.
{x['samples_per_process']}개/회차는 첫 bounded baseline 선택으로 기존 D3D12 기본 120개와
같다고 주장하지 않는다. Outlier를 삭제하지 않고 P50/P95/P99, MAD, min/max와 회차별 median을
JSON/CSV에 보존했다. 적은 표본의 tail 추정과 공유 GPU 변동을 고려해야 한다.

{table}
비율은 이 장치·드라이버·업로드 계약에서만 해석한다. 1보다 크면 이번 GraphicsSDK wall
time이 길다. 전체 SDK 성능 우위나 원인에 관한 결론은 아니다. 프로파일러 캡처나 병목
원인 분석, 최적화는 이번에 수행하지 않았다. 다른 GPU와 D3D12 결과를 섞지 않는다.

정확성 GPU 프로세스 16개와 성능 GPU 프로세스 {12*x['repetitions']}개에서 총
**{x['verified_images']:,}개 전체 이미지 / {x['verified_bytes']:,} bytes**가 일치했다.
새 CTest probe 2/2와 Python 회귀 239/239가 통과했다. Required-layer 미설치, 예상 GPU 불일치,
잘못된 option controls도 실패를 확인한다. Python 31개 새 계약은 다른 API/driver/CPU/장치/
소스/validation 혼입, 불완전 readback, 잘못된 시간·통계·schema를 거부한다.

기존 SDK가 그대로이므로 이전 93개 선택 SDK CTest를 새로 실행했다고 주장하지 않는다.
이전 [출력 증거](PHASE3_VULKAN_PRESENTATION_REPORT.md)와 SDK ZIP 22개를 SHA로 보존한다.
새 실행은 memory_reclamation 테스트를 호출하지 않으며 memory policy·장시간 기준·Intel AV
보류를 바꾸거나 하드웨어 원인으로 확정하지 않는다. 이 짧은 Intel transfer 검증은 이전 충돌
규명이나 장시간 메모리 수용을 대체하지 않는다.

## 빌드·감사·재현

`benchmarks/vulkan_upload`는 이전 설치 SDK와 reviewed `out/sdl`을 find_package로 사용한다.
새 root는 `out/vulkan-upload-final`이며 serial Release로 이 harness만 빌드했다. 공개 SDK에
SDL/Vulkan vendor API를 추가하지 않는다. 신규 SDK binary ZIP은 만들지 않는다.

`audit_vulkan_upload.py`는 새 compiler/linker read log 1개씩과 실제 library hash를 검사한다.
이전 SDK inventory 및 SDL static library 전체 compiler closure를 hash로 확인해 재사용한다.
이는 보수적인 정적 library 입력 목록이며 실제 링크 symbol 도달성이나 전체 재빌드 주장이
아니다. SDL의 미사용 D3D/OpenGL 구성요소도 보수적 closure에 남는다. Vendor/license text는
기존 정책의 선택 조건을 유지하고 새 benchmark binary·vendor source/runtime을 배포하지 않는다.

[결과 JSON](../results/2026-10-23-vulkan-upload/results.json),
[요약](../benchmarks/reports/2026-10-23-phase3-vulkan-upload/summary.json),
[CSV](../benchmarks/reports/2026-10-23-phase3-vulkan-upload/metrics.csv),
[증거](../benchmarks/reports/2026-10-23-phase3-vulkan-upload/evidence.json),
[inventory](../licenses/generated/windows-benchmarks-vulkan-upload-release/inventory.json)를 참조한다.
원본 stdout/stderr와 환경·명령은 results 디렉터리에 보존한다. SHA 및 소스 ZIP 검증은
[동결 scripts](../benchmarks/reports/validation/2026-10-23-phase3-vulkan-upload/verification-scripts/run.py)를 따른다.
'''
    text=text.replace('../results/2026-10-23-vulkan-upload/','../benchmarks/results/2026-10-23-vulkan-upload/')
    doc.write_text(text,encoding='utf-8',newline='\r\n')
    print(f"PASS: six same-device comparison rows; {x['verified_images']} complete images; validation timings excluded")

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--input',type=pathlib.Path,required=True);p.add_argument('--output',type=pathlib.Path,required=True);p.add_argument('--document',type=pathlib.Path,required=True);a=p.parse_args();write_report(a.input,a.output,a.document)
