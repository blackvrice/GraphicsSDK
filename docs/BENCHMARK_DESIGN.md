# 재현 가능한 벤치마크 절차

## 비교 계약

Low-level API와 end-to-end는 별도 범주다. backend, 물리 GPU의 LUID, driver, CPU,
OS, RAM, compiler, Release 최적화, validation 설정 및 workload 계약이 모두 같은
자료만 비교한다. 다른 API/GPU의 숫자로 전체 SDK 순위를 만들지 않는다.
소프트웨어 renderer는 기능 검증으로 분리한다. 지원하지 않는 측정치는 null이며 0이 아니다.

공통 runner는 독립 프로세스를 여러 번 실행하고 SDK/시나리오 순서를 고정 seed로
섞는다. warmup은 각 프로세스에서 별도로 수행한다. 원자료를 보존하고 출력 디렉터리를
덮어쓰지 않는다. `sources.lock.json`의 SHA와 실제 checkout 및 변경 여부를 확인한다.
현재 첫 구현의 C는 GPU 명령을 실행하지 않는 자원 생성/해제 API 측정이다.
GPU 실행 시간은 없으며, 동기화 대기 wall time을 GPU timestamp로 부르지 않는다.

## 최초 실행 범위: C

버퍼 1,000개, 각 65,536 bytes, GPU default memory, vertex 용도.
텍스처 1,000개, 64×64 RGBA8 UNorm, mip 1, sample 1, sampled 용도.
초기 데이터와 초기 clear를 요청하지 않는다. 매 표본은 idle → 생성 → release API → idle.
LLGL NoInitialData를 명시하고 SDL의 cycling을 사용하지 않는다.
SDK 내부 메모리 풀과 deferred release 정책의 차이는 비용에 포함된다.
이것은 실제 물리 GPU allocation 수나 GPU 사용 중 해제 측정과 동일하지 않다.
현재 메모리 사용량·지연 해제·reuse·1000개 외 count sweep은 각각 추가 검증 대상이다.

## 추가 구현된 A / B / D 부분 집합

A는 같은 D3DCompiler SM5.1 bytecode로 64×64 RGBA8 target을 clear하고 동일한 triangle을
1k/10k/100k회 그리거나 같은 수의 instance를 한 draw로 그린다. CPU record/submit/wait를
분리하고 전후 이미지의 중앙/모서리 pixel을 검증한다. 공통 비교에는 GPU timestamp
명령을 넣지 않는다. SDK별 API 경계 차이로 low-level 오버헤드 순위로 해석하지 않는다.

B는 준비된 CPU payload를 반복 업로드하여 GPU 완료까지 측정한다. 1080p/4K RGBA8,
4K RGBA16F이고 한 texture와 한 frame-in-flight다. CPU source는 재사용한다. SDL/Core는
재사용 upload buffer에 memcpy하고 LLGL은 WriteTexture 내부 staging 정책을 사용한다.
내부 추가 복사/할당 차이는 end-to-end 비용에 포함한다. 같은 유효 byte 수와 256-byte
aligned row 조건을 사용하며 전후 전체 payload readback이 일치해야 한다.

D는 shader compile을 제외한 반복 PSO 생성·해제 부분만 구현했다. 같은 bytecode와
render state를 사용하고 SDK 내부 caching을 허용한다. native driver cache를 지우지
않으므로 cold/warm/cache hit-miss 통제 실험은 아니다. create API 구간이 주요 metric이며
release API와 전체 batch wall도 기록한다. Root signature 생성 포함 여부는 SDK 정책이다.

기본 10 warmup, 3개 프로세스 회차 × 120개 표본. 할당 비용처럼 실행 시간이 긴
작업은 우선 소규모 실행 후 분산과 실행 비용을 보고 표본 수를 조정하고 명시한다.
현재 공개 실행은 한 CPU 스레드, 한 SDK 장치, headless, VSync 비해당이다.

## A–H 구현 및 검증 순서

| 항목 | 동일 workload 규칙 | 필요한 측정/정확성 확인 |
|---|---|---|
| A draw | 1k/10k/100k draw 및 동일 instance 수, 같은 삼각형·shader·해상도·scissor·PSO | record, submit, wait, GPU timestamps 분리; 이미지 readback |
| B upload | 1080p/4K RGBA8 및 4K RGBA16F, 동일 바이트, single/multiple/continuous | row pitch, memcpy 포함 경계 명시, fence 완료, 바이트 readback |
| C allocation | 위 계약, 생성/파괴·재사용·in-flight 해제 분리 | 생성·release API·완료 대기와 CPU/GPU 메모리 분리 |
| D shader/PSO | 동일 bytecode/profile/flags, 캐시 cold/warm와 PSO 생성 분리 | compile cache 정책, shader hash, 오류 포함 |
| E 2D | 10k/100k sprite, 같은 정렬/배칭/atlas/blend/clip/layer | 동일 이미지, 실제 draw 수, CPU/GPU 및 메모리 |
| F 3D | 같은 mesh/instance/depth/MRT/postprocess | 동일 이미지와 실행 명령 조건 |
| G compute | 같은 blur/색 변환/reduction/buffer 크기와 workgroup | CPU 참조 결과와 GPU readback, barrier와 dispatch 분리 |
| H video | 같은 upload/필터/레이어/color contract, decode/encode 제외 | latency/copy 수/bytes/drops; 외부 texture는 실제 지원 조건만 |

표에 있는 명세는 구현 완료 표시가 아니다. 구현된 adapter 및 측정 범위는
[ROADMAP](ROADMAP.md), [BENCHMARK_REPORT](BENCHMARK_REPORT.md)에 명시한다.

## 통계와 회귀

P50는 median, P95/P99는 정렬된 원자료의 `(N-1)*q` 인덱스를 선형 보간한다.
회차별 median, 전체 min/max/MAD와 3 MAD 초과 표본 수를 함께 저장한다.
이상치를 자동 삭제하지 않는다. 작은 MAD 또는 적은 표본에서는 꼬리 추정이 불안정하다.
프레임이 아닌 allocation 표본의 `frame_ms`는 전체 batch wall time을 뜻한다.

회귀 비용 변화율은 `100*(new_p50/baseline_p50-1)`이다. 비용이 낮을수록 좋다.
경고는 양쪽 최소 3회차이며 모든 새 회차 median이 기존 최대 회차 median보다
`max(기준 median의 5%, 3*(기존 MAD+새 MAD))` 이상 느릴 때만 발생한다.
이는 보수적인 휴리스틱이며 통계적 유의성 증명이나 성능 우위 판정이 아니다.
공유 GPU의 noisy 결과로 CI를 실패시키지 않는다. 신뢰 구간/전용 장비 반복은 추후 추가한다.

## 프로파일러와 환경

PIX/RenderDoc/Nsight/RGP/Xcode/Tracy를 사용한 프로파일 캡처는 수행 시 별도 기록한다.
현재 runner는 `steady_clock`, DXGI adapter identity/driver, Win32 RAM/OS/CPU 조회를 사용한다.
GPU timestamp 미지원 adapter에서는 null을 기록한다. 프로파일러 도구를 실행하지 않은
상태에서 측정 병목을 GPU 원인으로 확정하지 않는다.

## The Forge 선택형 프로파일

공통 8개 작업을 동일한 shader/payload/상태/완료 계약으로 실행한다. native readback과
빈 root signature bootstrap은 측정 밖이며 모든 측정 연산은 Forge 공개 API를 사용한다.
resource loader는 single-thread, staging은 graphics/copy engine 각각 128 MiB × 1 slot이다.
대기는 해당 제출 fence를 사용하며 GPU timestamp로 보고하지 않는다. 원본 소스는 그대로
두고 외부 application config와 MSVC/Windows SDK 호환 심볼 설정을 명시한다.
구형 NVAPI는 제외한다. [실행 범위·호환 설정·readback 제한과 실제 측정](FORGE_BENCHMARK_REPORT.md)을
따르며 vendor extension, 공식 전체 sample build, 다른 플랫폼까지 일반화하지 않는다.

## GPU timestamp draw interval

`draw_gpu`와 `instanced_gpu`는 category `gpu_timestamp`, contract revision 3을 사용한다.
기존 query 없는 end_to_end와 결과를 섞지 않는다. clear·viewport·pipeline API 설정 뒤
첫 draw 직전에 시작하고 마지막 draw 직후 pass 종료 전에 끝낸다. 64×64 RGBA8,
동일 DXBC, 3 vertices × count, blend/depth/cull OFF이며 GPU 완료 후 query를 재사용한다.

GraphicsSDK/LLGL/Forge는 resolve가 interval 밖이라 공통 비교가 가능하다. LLGL 결과는
nanoseconds, Forge와 Diligent는 counter/frequency, GraphicsSDK는 공개 milliseconds다.
모두 ms로 변환하며 CPU wait로 대체하지 않는다. API 내부 deferred native state 및
queue stall까지 순수 shader 실행과 분리했다는 뜻은 아니다.

Diligent의 `BeginQuery(DURATION)`는 시작 timestamp와 `ResolveQueryData`를 함께 기록한다.
이 시작 resolve는 interval 안이므로 `query_resolve`를 다르게 저장하여 직접 비교를
거부한다. SDL3에는 대응 API가 없고 bgfx view timer는 범위가 달라 해당 시나리오를 거부한다.
비교 범위를 맞추기 위해 native query를 대신 삽입하지 않는다. upload/copy/compute 구간은
추후 확장 대상이다. [실측과 초기 오분류 자료의 제외 사유](GPU_TIMESTAMP_BENCHMARK_REPORT.md)를 따른다.

## 선택형 CPU 및 메모리 관측

`--telemetry`는 contract revision 4와 `contract.telemetry`를 기록한다. 이전 비계측
자료와 자동 비교하지 않는다. 측정 batch의 GetProcessTimes kernel+user 시간/wall 비율은
논리 CPU 하나=100%이며 모든 process thread를 포함한다. warmup·초기화·post-readback은
구간 밖이고 idle·문자열 저장·관측 비용은 안이다. CPU 관측은 프로세스별 하나이며 frame
표본 수로 복제하지 않는다. `process_metrics`는 모든 회차값과 median/min/max를 보존한다.

CPU current working set/private commit과 process-lifetime peak는 구분한다. GPU는 같은
LUID의 node 0 local/nonlocal CurrentUsage·Budget을 baseline, 각 표본 끝, allocation의
해제 전 live 지점에서 읽는다. 최대 체크포인트는 연속 peak나 residency 증명이 아니다.
allocation/release API 시간에는 관측을 넣지 않지만 allocation frame 시간은 live 관측을
포함한다. 지원하지 않는 값은 실패이며 0으로 대체하지 않는다.
[정확한 구간·원본·제한](CPU_MEMORY_BENCHMARK_REPORT.md)을 따른다.

현재 pin의 LLGL full-region WriteTexture는 ReadTexture 뒤 COPY_DEST 전환 없이 copy를
기록하여 native validation에 실패한다. adapter는 public WriteBuffer→CopyTextureFromBuffer
경로를 사용하며 추가 GPU buffer copy·대기를 end-to-end 비용에 포함한다. 새 upload
계약 revision 5는 old path/이전 계약과 분리한다. 기존 LLGL upload 수치는 보존하지만
현재 correctness 기준으로 다시 승인하지 않는다. [새 검증·측정](LLGL_UPLOAD_BENCHMARK_REPORT.md)을 따른다.

## Phase 1 advanced baseline

`phase1_llgl` / `run_phase1.py`는 별도 category `phase1_extension`이다. LLGL public API로
application shader/PSO cache, 다중 upload/재사용/fence 수명/binding, E/F/G/H 작업을 실행하고
GPU 전체 readback 또는 compiler binary 전체 bytes의 pre/post CPU reference를 검사한다.
compile cache와 GPU rendering/compute는 분리하고, CPU shader 비교를 GPU readback으로
보고하지 않는다. application cache miss/hit counter를 실제 lookup/insert 분기에서 집계한다.

GPU query는 명시적 command buffer의 clear/copy/draw/dispatch를 포함하며 별도 public
WriteBuffer 내부 제출은 제외한다. CPU frame에는 staging·추가 copies/waits와 query 수집이
포함된다. 4-fence C 시나리오를 하나의 GPU interval로 가장하지 않는다.
서로 다른 category/scope나 driver cache와 application cache 비용을 섞지 않는다.

LLGL 이외 advanced adapter는 미구현이고 matching advanced cross-SDK 비교는 미완료다.
연속 GPU memory acceptance도 미확보이며 gate가 phase1_complete=false를 유지한다.
그 외 SDK의 기존 A–D 측정 범위는 별도로 남긴다. [구체적 dataset·원본·제한](PHASE1_COMPLETION_REPORT.md)을 따른다.

## Separate Vulkan B-upload profile

`benchmarks/vulkan_upload` and `run_vulkan_upload.py` use the installed Vulkan SDK and
the pinned SDL static build, without rebuilding either dependency. The profile requires
Vulkan on both sides, one CPU thread/frame in flight, one persistent sampled texture
and upload buffer, full overwrite and submit-fence completion. CPU memcpy and internal
recording/staging/allocation policies are included; setup, payload generation and full
readback/comparison after every iteration are outside the timer. That readback cadence
still affects cache/thermal state; this is not a streaming-throughput or low-level API
comparison. Changing finite RGBA16F data and odd-pitch correctness controls prevent
stale/repeated-content acceptance.

The initial bounded profile is 3 independent processes x 30 samples, with 5 warmups
each, shuffled at fixed seed. It differs from the earlier D3D12 120-sample default and
is never pooled with that API. Required-layer correctness timings are excluded from
performance, and performance requires no loaded validation DLL. The raw results retain
P50/P95/P99, MAD, min/max, process medians and every outlier. SDL reported names must
uniquely map to native Vulkan/DXGI identities; this is name attribution, not access to
SDL's native VkPhysicalDevice. Results with mismatched environment/driver/device/source
or incomplete readback are rejected. [Evidence and limits](PHASE3_VULKAN_UPLOAD_REPORT.md).
