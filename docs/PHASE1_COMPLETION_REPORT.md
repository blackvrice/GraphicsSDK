# Phase 1 확장 구현 및 완료 검증

2026-10-21. **Phase 1 전체는 아직 완료되지 않았다.** 독립 LLGL D3D12 하네스에 남은
workload baseline을 구현하고 실제 GPU 검증·반복 측정을 수행했다. 완료 게이트는 다음
두 조건 때문에 실패를 유지한다.

1. 확장 E/F/G/H의 동일 작업을 비교할 추가 SDK adapter가 없다. 현재 확장 하네스는
   LLGL만 구현했고 SDL3/GraphicsSDK/Diligent/bgfx/Forge는 `adapter_not_implemented_in_this_harness`다.
   SDK 자체의 기능 미지원으로 일반화하지 않는다. GraphicsSDK의 compute·depth·MRT 등은
   Phase 2 선행 API 개발이 필요하다. 기존 A–D의 공통 비교 증거는 별도로 유지한다.
2. 연속·transient GPU 메모리 peak 증거를 확보하지 못했다. WPR GPU profile 시작이
   `0xc5585011` / `Failed to enable the policy to profile system performance`로 실패했다.
   준비한 프로젝트 capture script의 관리자 실행도 Windows가 작업 취소로 보고해 실행되지
   않았다. 시스템 정책/보안 설정을 바꾸지 않았으며 전후 WPR 미기록 상태를 확인했다.

[기계 판정](../benchmarks/reports/2026-10-21-phase1-final/acceptance.json)의
`phase1_complete=false`를 유지한다. 단순 ETL 경로, 실행 성공 또는 체크포인트 최댓값을
연속 peak 증거로 승인하지 않는다. event loss=0, allocation lifetime 정합성, transient
positive control 및 raw ETL/프로세스별 event 해시가 필요한 acceptance 입력을 정의했다.
이 판정기는 제출된 검증 상태를 조합하는 gate이며 ETL을 자동 분석하는 도구가 아니다.

## 새로 구현한 22개 workload

| 영역 | 시나리오 | 실제 GPU 작업과 참조 |
|---|---|---|
| D cache | shader_cold / shader_warm | in-process bytecode map을 매번 비우거나 미리 채운 후 lookup/compile/insert/copy. 새 D3DCompile 결과와 전체 bytes 비교 |
| D cache | pso_miss / pso_hit | 같은 shaders/pass/상태의 application PSO map. miss는 이전 객체 해제·public 생성·insert, hit는 실제 map lookup. 선택한 PSO로 GPU 이미지 검증 |
| D binding | switch_binding | 서로 다른 두 PSO·두 texture를 256번 교대, scissor별 전체 CPU 이미지 참조 |
| B upload | multi_upload | 다른 bytes를 가진 persistent 1080p RGBA8 texture 4개, public WriteBuffer와 batched texture copies. 4개 전체 readback |
| C reuse | resource_reuse | persistent 64KiB source/destination, 64회 public buffer copy, 완료 후 전체 byte 참조 |
| C lifetime | resource_inflight | transient buffer 4개와 별도 command/fence 4개. 각각 완료까지 자원 유지 후 해제, 최종 destination 전체 참조. driver 내부 지연 해제량 측정은 아님 |
| E sprite | sprites_10000 / sprites_100000 | fixed-order instanced one-pixel sprite, 2×2 atlas, alpha=128/255, 겹치는 layer, 48×48 scissor. 전체 64×64 CPU blend 참조 |
| F mesh | mesh_draw / mesh_instanced | vertex-buffer quad 2,000개를 1,000 draws 또는 한 instanced draw로 처리. near/far depth less/write, MRT 2개, invert 후처리. 두 attachment와 후처리 전체 참조 |
| G compute | compute_blur | uint32 256×256 clamp 3×3 평균, 모든 원소 CPU 참조 |
| G compute | compute_color | RGB packed uint의 명시적 정수 luma 변환, 모든 원소 CPU 참조 |
| G compute | compute_reduce | group 64, shared memory 및 barrier reduction, group sum과 untouched output 전체 참조 |
| G compute | compute_buffer | uint32 multiply/add, 모든 원소 CPU 참조 |
| G transition | compute_graphics | 위 compute output을 같은 command list에서 graphics shader가 읽어 렌더링. UAV→SRV 전환과 buffer/image 참조 |
| H video | video_1080 / video_4k | NV12 8-bit Y/UV plane 업로드, GPU BT.709 limited-range RGB 변환, 전체 CPU 참조 |
| H video | video_p010 | 4K P010 10-bit high-bits plane 업로드·정수 unpack·BT.709 limited 변환, RGBA8 출력 전체 CPU 참조. HDR tone mapping은 아님 |
| H video | video_layers | 서로 다른 두 NV12 영상의 Y/UV planeset 4개를 매번 업로드하고 두 GPU draw로 alpha 합성, 전체 CPU 참조 |
| H video | video_filter | plane 업로드·색 변환 후 3×3 blur+invert를 융합한 후처리 pass. 중간 RGBA8 quantization을 포함한 전체 CPU 참조 |

캐시 key에는 shader source/entry/profile/flags 또는 고정 PSO 상태가 포함된다. native
출력의 실제 hit/miss counter는 pre-reference 실행 1회+warmup 3회+표본 수를 포함하며
runner가 정확한 수를 확인한다. 최종 cache 시나리오는 프로세스당 124 hit 또는 124 miss다.
shader 작업은 CPU binary reference이며 GPU readback은 null로 구분한다.

**통제한 캐시는 application cache다.** driver/OS cache를 지우거나 하드웨어 내부 PSO cache
hit 여부를 측정하지 않는다. `shader_warm`은 cache lookup+byte copy이며 재컴파일 시간이
아니다. `pso_hit`은 native PSO 재생성이 없는 application handle lookup이다. 이를 native
driver의 cold/warm compile 시간이나 GraphicsSDK에 구현된 cache로 부르지 않는다.
Microsoft의 [shader cache 종류](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ne-d3d12-d3d12_shader_cache_kind_flags)도
driver-managed 동작이 hint일 수 있음을 구분한다. 전역 cache 삭제나 developer-mode
[ShaderCacheControl](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device9-shadercachecontrol)은 실행하지 않았다.

## 구간과 메모리

GPU가 실행되는 E/F/G/H, binding, multi-upload는 LLGL public TimeElapsed query를 실제로
기록한다. 시작/끝은 명시적 command buffer 전체를 감싼다. clear/binding/copy/draw/dispatch를
포함하고, 먼저 실행되는 `WriteBuffer`의 내부 staging 제출은 포함하지 않는다. LLGL public
결과인 integer nanoseconds를 ms로 변환하며 완료 query가 없으면 실패한다. resolve/readback은
interval 밖이다. 기존 draw-only timestamp와 scope/category가 달라 자동 비교하지 않는다.

CPU frame은 public buffer update·추가 staging copies/대기·record·submit·완료·query 수집을
포함한다. 독립 pre/post CPU 참조와 전체 readback은 frame timer 밖이다. allocation/reuse의
여러 제출을 하나의 GPU interval로 가장하지 않으므로 `gpu_ms=null`이다. shader/PSO cache
작업도 GPU 구간은 null이며 검증용 render는 timer 밖이다.

새 하네스의 모든 실행은 CPU process-batch 사용률과 메모리 체크포인트 관측을 켠다.
memory observer와 query가 없는 기존 end-to-end 원본을 대체하지 않는다. CPU lifetime peak는
device/shader/reference 준비를 포함한 process 범위이고, GPU local/nonlocal 최대값은 여전히
체크포인트 하한이다. 짧은 cache batch의 CPU accounting 0은 비용 0을 뜻하지 않는다.
[기존 관측 정의](CPU_MEMORY_BENCHMARK_REPORT.md)를 유지한다.
compute/draw의 짧은 batch도 CPU accounting 해상도 영향을 크게 받는다. 0%나 100%를
넘는 수치만으로 비용 없음·worker 병렬성·SDK 간 CPU 우위를 판단하지 않는다.

유효 CPU payload 기준 GiB/s와 headless completed batches/s를 원본의 `derived`에 기록한다.
H 작업은 16.666667ms batch budget 초과 횟수를 별도로 기록한다. 실제 presentation dropped
frames, driver 내부 copy 횟수, 외부 texture import 및 decode/encode는 측정하지 않아 null/
미구현으로 구분한다. 외부 GPU 명령으로 다른 SDK adapter를 대신 구현하지 않는다.

## 측정과 검증

최종 22개 workload × 독립 3회 × 120표본 = **66개 프로세스, 7,920개 표본**이다.
각 프로세스 warmup 3회이며 pre/post reference가 모두 통과해야 한다. 같은 i7-14700K,
GTX 1660 SUPER, LUID `0:95995`, driver `32.0.16.1088`, Windows `10.0.26220`,
MSVC `194435228`, x64 Release /O2 /MD, no LTO, headless에서 실행했다.
validation OFF 성능 자료와 validation ON의 별도 22개 프로세스를 분리했다.
fixed seed로 순서를 섞고 outlier를 삭제하지 않았다. clock/background/driver cache는 통제하지 않았다.

- [관측 표](../benchmarks/reports/2026-10-21-phase1-final/TABLE.md)
- [원본·모든 회차·통계·CPU/메모리](../benchmarks/results/2026-10-21-phase1-final/summary.json)
- [SDK별 구현 상태](../benchmarks/results/2026-10-21-phase1-final/support.json)
- [실행 파일·source ZIP·원본 해시](../benchmarks/reports/2026-10-21-phase1-final/evidence.json)
- [완료 조건 입력](../benchmarks/reports/2026-10-21-phase1-final/acceptance-evidence.json)
- [native/Python/WPR/빌드 로그](../benchmarks/reports/validation/2026-10-21-phase1-final/summary.json)

공통 CTest **44개**, 기존 core CTest **4개**, Python 테스트 **49개**가 통과했다.
44개는 기존 공통 22개+확장 22개다. 별도 runner native 22개는 finite GPU query,
정확한 draw/dispatch/payload 수, 실제 cache counter, CPU reference 종류와 process memory
schema를 검증한다. LLGL ERROR/CORRUPTION은 장치 종료까지 실패 처리한다. GBV/stress나
외부 영상 decoder acceptance로 일반화하지 않는다.

초기 병렬 빌드 1회에서 MSVC CL.exe가 `0xc0000005`로 종료됐고 최종 serial build와 전체
native 검증은 통과했다. 원인 미확정으로 실패 로그도 보존하며 수정됐다고 주장하지 않는다.
앞선 GraphicsSDK root signature 일회성 초기화 실패의 원인 미확정 상태도 유지한다.

초기 확장 측정의 video_layers는 NV12 영상+단색 overlay였고 shader binary 검증의 metadata가
GPU readback과 구분되지 않았다. 이 원본·첫 source ZIP·inventory는 [탐색 제외 목록](../benchmarks/reports/2026-10-21-phase1-exploratory/exclusions.json)에
해시로 보존한다. 최종 two-video/reference 계약으로 새 프로세스를 수집했으며 과거 값을
바꿔 쓰지 않았다. 최종 보고서에는 final 캠페인만 사용한다.

새 보수적 compiler-read inventory는 공통+확장 target의 프로젝트 입력 **970개/구성요소 22개**다.
새 외부 dependency/pin 변경은 없고 자체 SDK/API/SDK ZIP은 그대로다. source fingerprint,
runtime 복사본, 실제 linker/compiler 입력과 기존 SDK ZIP을 감사했다. inventory는 재배포
승인이 아니다. optional competitor 재빌드·원격 CI·Linux/Metal/Vulkan 실행은 수행하지 않았다.

## 남은 완료 조건

- 동일 E/F/G/H 계약을 실행하는 추가 SDK adapter와 실제 같은 환경의 비교 자료.
- 관리자 승인된 GPU allocation trace, event-loss 점검, process/lifetime 재구성 및 transient
  positive control에 근거한 연속 GPU peak acceptance.

`tools/phase1_acceptance.py`는 이 둘 중 하나라도 누락되면 exit 1을 반환한다. 현재 그 실패는
예상된 미완료 결과이며 native correctness 테스트 실패로 숨기거나 완료로 바꾸지 않는다.
