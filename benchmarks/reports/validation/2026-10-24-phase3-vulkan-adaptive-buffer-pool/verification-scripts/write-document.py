import json
import pathlib
import re
root=pathlib.Path(__file__).resolve().parents[5];v=pathlib.Path(__file__).resolve().parent.parent
x=json.loads((root/'benchmarks/results/2026-10-24-vulkan-adaptive-buffer-pool/results.json').read_text(encoding='utf-8'))
native=(v/'native-tests.log').read_text(errors='replace')
count=re.search(r'pool_range_assertions=(\d+)',native).group(1)
def table(channel):
    s='| GPU | 작업 | 기존 SDK | 고정 64KiB | 적응형 후보 | SDL |\n|---|---|---:|---:|---:|---:|\n'
    for row in x['groups']:
        values=[row['statistics'][k][channel]['p50_ms'] for k in ('baseline','fixed','candidate','sdl')]
        s+='| '+row['device_name']+' | '+row['scenario']+' | '+' | '.join(f'{n:.5f}' for n in values)+' |\n'
    return s
comparisons=[]
for row in x['groups']:
    c=row['statistics'];a=c['candidate']['create_ms']['p50_ms'];b=c['baseline']['create_ms']['p50_ms'];f=c['fixed']['create_ms']['p50_ms']
    comparisons.append(f"- {row['device_name']} {row['scenario']}: 적응형 생성 P50 / 기존 {a/b:.3f}, / 고정 {a/f:.3f}.")
text=f'''# Phase 3 — Vulkan 적응형 소형 버퍼 예약 후보

2026-10-24 Windows x64 / MSVC 19.44. 고정 64KiB 후보의 첫 버퍼 예약 비용을
줄이는 다음 정책을 실제 격리 SDK에 구현했다. **새 4KiB 버퍼 하나의 native 요청은
64KiB에서 4KiB로 감소**했다. 대량 생성의 allocation 절감도 유지됐다.
현재는 실험 후보이며 제품 기본값으로 채택하지 않았다. 실패 주입과 더 다양한
할당 패턴의 성능·정책 검증이 남아 있다.

## 구현과 범위

[실험 코드](../experiments/vulkan_adaptive_buffer_pool/README.md),
[검토 diff](../experiments/vulkan_adaptive_buffer_pool/candidate.patch),
[생성 도구](../tools/prepare_vulkan_adaptive_buffer_pool.py)를 추가했다.
기존 제품 fingerprint와 정확히 한 번 일치하는 patch anchor를 확인한 뒤, 새 `out/`
복사본에만 적용한다. 과거 고정 후보·결과·vendor tree는 보존했다.

Device-local 소형 buffer의 memory type/stride별 첫 block은 1 slot이다. 기존 block의
빈 slot을 우선 재사용하고, 모두 차면 가장 큰 **현재 생존 block**의 capacity를
두 배로 늘리되 block당 64KiB를 넘지 않는다. 4KiB class의 capacity는
1,2,4,8,16,16,... 이다. 각 VkBuffer의 독립 handle과 비중첩 offset을 유지한다.
64..4096-byte power-of-two class, size/alignment eligibility와 dedicated 요구,
host/image/큰 buffer의 dedicated 경로는 앞선 후보와 같다.
정렬·허용 memory type·범위는
[Vulkan bind 계약](https://docs.vulkan.org/refpages/latest/refpages/source/vkBindBufferMemory.html)을,
dedicated 판단은 [requirements](https://docs.vulkan.org/refpages/latest/refpages/source/VkMemoryDedicatedRequirements.html)를 따른다.

Slot lease는 native Allocation에 묶여 ended recording과 pending submission이 보호한다.
마지막 lease가 끝난 block은 즉시 free하고 빈 block을 cache하지 않는다. 현재 살아 있는
block만 성장 판단에 사용하므로 전부 비우면 다음 첫 자원은 다시 1 slot이다.
이미 큰 block에 생존 자원 하나가 남은 경우에는 그 block 전체가 유지된다. 자동
migration/compaction은 없다. GPU 검사에서 이 잔존 예약과 완전 해제 뒤 reset을 확인했다.
통계는 같은 block을 한 번만 더하고 resource 개수는 자원별로 유지한다.
Public API/ABI와 제품 SDK 소스는 변경하지 않았다.

Block allocation OOM에서 exact-size fallback을 시도하는 코드는 유지했다.
이번 자료는 OOM/bind failure/device loss 주입을 승인하지 않으며, 기존 device 연산의
thread 정책을 넘어서는 동시 allocator 안전성을 주장하지 않는다.

## 검증

새 source/build/install root로 D3D12+Vulkan 후보와 installed benchmark/trace 소비자를
MSVC 직렬 빌드했다. 통합 CTest는 처음부터 `-E '^memory_reclamation_'`를 사용해
보류한 3개를 제외했다. **57/57 CTests**가 통과했다(기존 54 + 후보 3).
D3D12 hardware/GBV/WARP 및 Vulkan NVIDIA/Intel 기능·validation 회귀를 포함하지만,
과거 다른 build의 전체 gate 재실행이나 장시간 메모리 승인을 뜻하지 않는다.

CPU range 검사는 **{count} assertions**로 각 stride의 모든 power-of-two capacity,
잘못된 capacity/growth, 비중첩·exhaustion·부분 반환·재사용·double-free를 확인했다.
GPU 검사는 validation OFF/ON으로 두 GPU를 실행했다. 1/2/3/4/7/8/15/16/31/32개
경계 batch, 1000개 생성·부분 교체, 큰 block의 마지막 생존 자원, 비운 뒤 첫 4KiB
예약, ended recording이 보유한 예전 데이터와 다른 값으로 쓴 신규 slot의 비별칭,
혼합 크기 copy/readback, 종료된 device 자식과 새 generation을 검사했다.
OFF **103**, ON **105 assertions**, 각 mode **164,742 bytes**가 일치했다.

Python **478/478**(신규 34), report 변조 거부 controls 8개, audit controls 2개가
통과했다. 누락 layer·다른 GPU·잘못된 CLI의 native negative controls 14개는
결과 없이 실패했다. 보류한 장시간 메모리 기준과 과거 Intel AV 원인 조사는 재개하지 않았다.

## 별도 네이티브 관측

기존 진단 main/layer를 그대로 사용해 기존/고정/적응형 × 두 GPU × buffer1/buffer1000의
**12개 validation trace 프로세스 / 252개 구간**을 실행했다. 구간은 target 생성,
helper/GPU 사용, helper 해제, target 해제, idle과 device teardown이다.
각 process의 warmup 1 + 검사 3회에서 모든 buffer를 GPU copy/full readback했다.
**24,024개 buffer / 98,402,304 bytes**가 일치했고 구간별 객체·바이트 conservation,
accounting/drop/implicit cleanup=0, 마지막 native live=0을 검사했다.

| 새 target batch | 기존 alloc/free | 고정 alloc/free | 적응형 alloc/free | 기존 요청 bytes | 고정 요청 bytes | 적응형 요청 bytes |
|---|---:|---:|---:|---:|---:|---:|
| 4KiB ×1 | 1/1 | 1/1 | 1/1 | 4096 | 65536 | 4096 |
| 4KiB ×1000 | 1000/1000 | 63/63 | 66/66 | 4096000 | 4128768 | 4124672 |

이는 target 구간 값이며 helper 할당은 분리돼 있다. 각 create/bind/destroy는 N회다.
적응형 1000개는 1007 slots, 추가 예약 28KiB(0.7%)다. 고정 후보보다 요청 바이트는
4KiB 적고 native allocation은 3회 많다. 이 정책은 희소 사용량과 호출 수의 절충이다.
Vulkan 요청 바이트와 free 호출을 관측한 것이며 OS commitment/residency, driver
내부 할당, 실제 VRAM 회수나 장시간 plateau를 측정한 자료가 아니다.

## 새 성능 비교

과거 timing을 재사용하지 않고 **기존 SDK / 고정 후보 / 적응형 후보 / pinned SDL**을
모두 다시 실행했다. GPU×작업×variant별 독립 process 3개, warmup 5 + 측정 30회,
seed 20261030으로 순서를 섞었다. 작업은 4KiB buffer1/buffer1000, headless,
CPU 기록 thread 하나/한 submission이다. 매 반복 모든 buffer에 다른 payload를
전달하고 full readback한다. Create와 public release는 별개 CPU API 구간이고,
helper 준비/정리·GPU 기록/submit/wait·readback·idle은 구간 밖이다.
SDL의 내부 pool/deferred disposal과 더 좁은 vertex usage 차이는 포함한다.

정확성 process 16개가 먼저 통과한 뒤 validation/trace DLL 없는 성능 process
48개를 순차 실행했다. GPU 회귀·trace와 성능을 겹치지 않았다.
총 **1440개 측정 반복 / 2880개 CPU interval**이다. Outlier 삭제는 없고,
P50/P95/P99/MAD/min/max와 각 process median을 원본 및 CSV에 보존했다.
아래 값은 batch당 P50, 단위 ms다.

생성:

{table('create_ms')}
공개 해제:

{table('public_release_ms')}
{chr(10).join(comparisons)}

첫 요청 크기의 문제를 해결했다고 해서 단일 buffer 지연의 무회귀를 승인하지 않는다.
위 결과는 이 campaign의 범위이며 driver 내부 원인·전체 SDK 순위·GPU 실행 시간이나
다른 크기/분포의 우위를 설명하지 않는다. 특히 큰 block의 일부만 남는 패턴에서는
앞서 설명한 잔존 예약이 있으므로 첫 자원 결과를 모든 희소 패턴으로 일반화하지 않는다.
성능·정확성 process에서 **872,872개 buffer / 3,575,283,712 bytes**를 확인했다.
실행 명령은 probe 2 + trace 12 + correctness 16 + measurement 48 + controls 14 = **92개**다.

## 결정과 재현

적응형 후보의 첫 버퍼 예약 제한과 batch 절감은 검증됐다. 제품 채택 전에는
fault-injected OOM/bind failure 정리·복구, 혼합 size/점유율·반복 churn의 성능과
정책 기준을 추가해야 한다. 이번 변경은 이 후보와 재현 가능한 증거를 보존하며
제품 기본값은 유지한다. Phase 3 전체 완료나 보류된 메모리/Intel AV 해결을 주장하지 않는다.

제품 `{x['product_SDK_source_fingerprint']}`, 고정 `{x['fixed_SDK_source_fingerprint']}`,
적응형 `{x['candidate_SDK_source_fingerprint']}`다. 공통 benchmark
`{x['benchmark_source_fingerprint']}`, trace `{x['trace_source_fingerprint']}`는 변경하지 않았다.
이전 증거와 SDK ZIP 23개를 SHA로 보존하고 새 SDK binary package는 만들지 않았다.
[의존성 inventory](../licenses/generated/windows-vulkan-adaptive-buffer-pool-candidate-release/inventory.json)는
805 conservative inputs / 21 components, 새 compiler read 35 / linker read 34개다.
후보와 설치 소비자의 실제 compiler/linker 입력 및 고정 후보까지의 보존 closure를
확인했다. 추가 third-party allocator·vendor 수정은 없으며 법적 승인/심볼 도달성 주장은 아니다.
Source ZIP은 own sources/fixtures/실험·patch/tools/문서/audit이며 vendor trees,
EXE/DLL/LIB/PDB/dump/ETL/VVL runtime은 포함하지 않는다.

실제 [build script](../benchmarks/reports/validation/2026-10-24-phase3-vulkan-adaptive-buffer-pool/verification-scripts/build.py),
[runner](../tools/run_vulkan_adaptive_buffer_pool.py),
[원본 결과](../benchmarks/results/2026-10-24-vulkan-adaptive-buffer-pool/results.json),
[요약](../benchmarks/reports/2026-10-24-phase3-vulkan-adaptive-buffer-pool/summary.json),
[CSV](../benchmarks/reports/2026-10-24-phase3-vulkan-adaptive-buffer-pool/metrics.csv),
[증거](../benchmarks/reports/2026-10-24-phase3-vulkan-adaptive-buffer-pool/evidence.json),
[검증 script](../benchmarks/reports/validation/2026-10-24-phase3-vulkan-adaptive-buffer-pool/verification-scripts/verify.py)를 따른다.
Runner/audit의 root는 현재 snapshot에 고정돼 있다. 재실행은 새 source/build/install/result
root로 분리하고 경로를 맞춘다. VVL은 자식 process 환경에만 적용하며 전역 설정은 바꾸지 않는다.
Linux native 검증, 더 넓은 경쟁 SDK 비교와 전체 RHI 발전 항목은 별도로 남아 있다.
'''
(root/'docs/PHASE3_VULKAN_ADAPTIVE_BUFFER_POOL_REPORT.md').write_bytes(text.replace('\n','\r\n').encode())
print('PASS wrote adaptive report from current four-variant data')
