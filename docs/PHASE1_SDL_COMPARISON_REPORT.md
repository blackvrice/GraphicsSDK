# Phase 1 SDL3 E–H 추가 비교

2026-10-21. E–H 14개 workload의 SDL3 public GPU API adapter를 추가했다.
같은 workload의 LLGL/SDL3 CPU 완료 시간을 비교하며, 추가 SDK 비교 조건은 충족했다.
이 SDL3 캠페인 당시에는 연속/transient GPU 메모리 증거가 미완료였다.
이후 [GPU 추적 검증](PHASE1_GPU_MEMORY_REPORT.md)을 완료해 현재 Phase 1 foundation은 통과했다.
아래의 미완료 판정 링크는 이 캠페인 당시 상태로 보존한다.

- [비교 표와 모든 회차 중앙값](../benchmarks/reports/2026-10-21-phase1-sdl/TABLE.md)
- [통계·CPU·메모리 요약](../benchmarks/results/2026-10-21-phase1-sdl/summary.json)
- [원본·실행 파일·소스 ZIP 해시](../benchmarks/reports/2026-10-21-phase1-sdl/evidence.json)
- [완료 판정](../benchmarks/reports/2026-10-21-phase1-sdl/acceptance.json)
- [SDK별 구현 상태](../benchmarks/results/2026-10-21-phase1-sdl/support.json)

## 작업과 동일성

| 영역 | 시나리오 | 두 public adapter의 공통 작업과 검증 |
|---|---|---|
| E | sprites_10000 / sprites_100000 | 순서 고정 instanced one-pixel sprite, 2×2 atlas, alpha=128/255, overlap, 48×48 scissor, 전체 CPU blend 참조 |
| F | mesh_draw / mesh_instanced | float3 vertex-buffer quad 2,000개, 1,000 draws 또는 1 instanced draw, LESS depth/write, MRT2, invert postprocess, attachment 3개 전체 참조 |
| G | compute_blur / compute_color / compute_reduce / compute_buffer | uint32 256×256, group64, clamp 3×3 정수 평균 / 정수 luma / shared-memory tree reduction / multiply-add, 전체 원소와 untouched output 참조 |
| G | compute_graphics | 같은 compute 결과를 같은 command buffer의 graphics shader에서 읽어 64×64 이미지 생성, buffer와 이미지 전체 참조 |
| H | video_1080 / video_4k / video_p010 | 1080p/4K NV12 또는 4K P010의 Y/UV 매회 업로드, BT.709 limited RGB 변환, 전체 CPU 참조 |
| H | video_layers | 서로 다른 두 NV12 영상의 4개 plane 매회 업로드, 두 draw로 alpha 합성, 전체 CPU 참조 |
| H | video_filter | NV12 변환 후 RGBA8 중간 quantization을 포함한 3×3 blur+invert 후처리, 전체 CPU 참조 |

`benchmarks/phase1/Workloads.hpp`에 shader 연산, mesh vertices, atlas bytes, CPU 참조와
workload 계약을 공유한다. 공통 파일 SHA256을 비교 계약에 기록하고 동일성을 검사한다.
shader는 둘 다 D3DCompile SM5.1 /O3 /warnings-as-errors이며 측정 전에 컴파일한다.

비교 수준은 **public semantic workload의 end-to-end CPU 완료**다. native shader binary나
버퍼 view가 같다는 주장은 하지 않는다. LLGL public buffer는 stride4 structured uint32이고
SDL3 public storage buffer는 raw byte-address다. 공통 컴퓨트 연산의 LoadA/StoreB accessor만
이 두 ABI에 맞춘다. SDL compute UAV는 u0/space1, fragment SRV는 space2, mesh vertex input은
TEXCOORD0이며 LLGL은 기존 register와 POSITION을 사용한다. 이 차이는 원본의
`native_shader_abi`와 양쪽 공통 계약의 `shader_abi_by_sdk`에 남긴다.

정수 영상 texture는 두 SDK 모두 같은 R8/RG8 또는 R16/RG16 UINT 및 Texture.Load를
사용한다. SDL3는 정수 texture의 SAMPLER usage를 금지하므로 public read-only storage
texture로 bind한다. LLGL은 sampled SRV다. 두 경로 모두 색 변환 전에 정수 plane 값을
읽는다. RGBA atlas/후처리에는 SDL public sampler binding을 사용하나 shader는 Load만 한다.

업로드의 SDK 내부 staging·copy·대기 구조는 SDK가 관리하며 CPU frame에 모두 포함한다.
LLGL WriteBuffer의 별도 내부 제출과 SDL transfer map/copy-pass의 단일 제출을 동일한
native 명령 수로 가장하지 않는다. payload bytes는 유효 영상 데이터 기준이고 padded
row 메모리와 내부 copy 횟수를 뜻하지 않는다. one batch in flight, headless, vsync 없음이다.

## 시간과 관측

SDL3의 matching public timestamp API가 없어 양쪽 모두 `--cpu-only`로 실행했다.
LLGL도 이 모드에서는 query heap을 생성하거나 GPU query를 기록·회수하지 않는다.
`gpu_ms=null`, `gpu_scope=disabled`를 native 출력과 runner에서 강제한다. 기존 LLGL
query ON baseline을 이번 CPU 비교의 상대편으로 사용하지 않는다.

CPU frame은 public updates→record→submit→GPU 완료까지다. pre/post 전체 readback과 CPU
reference는 timer 밖이다. sample별 checkpoint도 frame 밖이지만 process CPU/wall batch에는
들어간다. GPU 메모리는 checkpoint 하한이며 transient 연속 peak가 아니다. 짧은 batch의
process CPU 사용률은 OS accounting 해상도에 크게 영향을 받는다.

14개 workload × SDK 2개 × 독립 프로세스 3회 × 120표본 = **84개 프로세스/10,080표본**이다.
warmup은 프로세스당 3회다. fixed seed 20261021로 SDK·workload·회차 순서를 함께 섞었다.
자동 재시도나 outlier 삭제는 없다. validation ON은 별도 28개 프로세스×5표본이며 성능
캠페인은 validation OFF다. 같은 i7-14700K / GTX 1660 SUPER / driver 32.0.16.1088 /
Windows 10.0.26220 / MSVC 194435228 / x64 Release /O2 /MD /no LTO 환경을 검사한다.
GPU clock·background load·driver/OS cache는 통제하지 않았다. 표의 비율은 해당 작업·환경의
CPU cost 관측이며 전체 SDK 순위나 GPU 실행 속도 우위로 일반화하지 않는다.

## 검증·입력 보존

공통 CTest **72개**, Python **52개**가 통과했다. 72개는 기존 공통/LLGL 확장 44개와 새
CPU profile 28개다. 성능 원본은 모든 프로세스의 pre/post reference, work count, payload,
query OFF, environment, compiler, source/executable identity를 검사했다. 보고서는 누락·중복
회차, 다른 driver/계약/query, reference 누락, 잘못 집계된 summary를 거부한다.

SDL debug device와 public SDL log callback으로 전달되는 GPU ERROR를 종료까지 실패
처리한다. SDL assertion은 UI 대기 없이 abort한다. public 오류 반환 및 전체 readback을 함께
검사한다. LLGL은 기존 native InfoQueue 검사를 유지한다. SDL public API로 debug callback
등록 성공 여부를 직접 조회하지 않으므로 모든 플랫폼의 native diagnostic 가용성을 보증하지
않는다. SDL3 외부 native GPU 명령으로 작업을 대신하지 않는다.

초기 SDL 영상 테스트 5개는 정수 texture SAMPLER usage assertion으로 실패했다.
public storage-read binding으로 수정 후 5개와 전체 72개가 통과했다. 실패 로그도
[검증 폴더](../benchmarks/reports/validation/2026-10-21-phase1-sdl/)에 보존한다.

첫 보고서 export는 검증 후 상대 경로의 artifact 기록에서 실패했다. 경로를 repository
내 절대 경로로 해석하도록 고친 뒤 생성에 성공했다. 이전 부분 보고서와 source ZIP은
[탐색 제외 목록](../benchmarks/reports/2026-10-21-phase1-sdl-report-exploratory/exclusions.json)에
해시로 보존한다. 측정 원본은 바꾸지 않았고 최종 evidence는 수정된 도구의 source ZIP을 참조한다.

LLGL/SDL의 고정 commit과 vendor checkout은 변경하지 않았다. 새
[compiler-read inventory](../licenses/generated/windows-benchmarks-phase1-sdl-release/inventory.json)는
`gfx_bench`, `phase1_llgl`, `phase1_sdl`의 compiler/linker 입력 **972개/22개 구성요소**와
실제 artifact 해시를 포함한다. 신규 외부 패키지는 없고 기존 자체 SDK/API/배포 ZIP은
변경하지 않았다. SDK ZIP의 기존 감사 `--check`가 통과했다. inventory는 재배포 승인이 아니다.

이전 LLGL 22개 baseline은 원본·소스 ZIP·inventory 해시를 검증한 역사 자료로 유지한다.
shader/PSO application cache와 resource baseline의 반복 증거는 그 자료를 참조하며,
이번에는 SDL cache/resource/binding adapter를 구현하거나 두 SDK 간 cache 성능을 비교하지
않았다. GraphicsSDK/Diligent/bgfx/Forge advanced adapter는 여전히 미구현이다.

## 재현

새 출력 폴더를 사용한다. 이미 존재하는 측정/보고서는 덮어쓰지 않는다.

```powershell
cmake -S benchmarks -B out/bench
cmake --build out/bench --config Release --parallel 1
ctest --test-dir out/bench -C Release --output-on-failure
python tools/run_phase1.py --compare-sdl --validation --runs 1 --samples 5 --output benchmarks/reports/validation/<new-stage>/native
python tools/run_phase1.py --compare-sdl --runs 3 --samples 120 --output benchmarks/results/<new-stage>
python tools/audit_licenses.py --profile benchmarks-phase1 --output licenses/generated/<new-inventory>
python tools/report_phase1_comparison.py --results benchmarks/results/<new-stage> --validation benchmarks/reports/validation/<new-stage> --inventory licenses/generated/<new-inventory> --output benchmarks/reports/<new-stage>
```

## 남은 완료 조건

아래는 이 캠페인 당시의 이력이다. 이후 사용자가 새 관리자 실행을 승인했고,
[전용 ETW 수집·분석](PHASE1_GPU_MEMORY_REPORT.md)으로 해당 조건을 통과했다.

관리자 승인된 GPU allocation trace의 event loss=0, allocation lifetime 재구성, transient
positive control, raw ETL/프로세스별 event 해시를 갖춘 연속 GPU memory peak acceptance가
남아 있다. 이전 WPR 오류 `0xc5585011`과 관리자 실행 취소 후 권한 실행을 재시도하지 않았다.
체크포인트 최대값이나 trace 파일 경로만으로 완료 처리하지 않는다. 판정기는 이 조건을
미완료로 두며 `phase1_complete=false`를 유지한다. ETL 자동 분석기가 구현됐다는 뜻도 아니다.
