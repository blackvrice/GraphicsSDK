# LLGL 공개 업로드 경로 복구 및 재측정

2026-10-21. 고정 LLGL의 full-region `WriteTexture`는 앞선 `ReadTexture`가 남긴
COPY_SOURCE 상태를 COPY_DEST로 전환하지 않아 native validation에 실패했다.
[발견 당시 원본과 실패 로그](CPU_MEMORY_BENCHMARK_REPORT.md)는 그대로 보존했다.
이번에는 adapter가 다른 공개 API 경로를 사용하도록 바꾸고 새 측정을 수행했다.
vendor의 `WriteTexture` 자체를 수정했다는 의미가 아니다.

## 구현과 계약

destination texture, 중간 default GPU buffer, command buffer를 초기화 때 한 번 만들고
재사용한다. 각 업로드는 LLGL `WriteBuffer`로 CPU payload를 중간 buffer에 쓰고,
`CommandBuffer::CopyTextureFromBuffer`로 destination에 복사한 뒤 public queue idle을
기다린다. 고정 LLGL의 WriteBuffer 내부 staging→default buffer copy/완료 대기와 이후
texture copy/완료 대기를 모두 end-to-end 비용에 포함한다. 직접 mapped upload heap을
사용한다고 주장하지 않는다. native GPU copy나 resource-state 조작을 우회 삽입하지 않는다.

공개 `CopyTextureFromBuffer` 구현이 source/destination 상태 전환을 추적하므로 readback
뒤 재업로드와 연속 업로드가 가능하다. 원본 LLGL/SDL checkout은 pin과 일치하고 수정하지
않았다. 기존 native InfoQueue의 ERROR/CORRUPTION 검사는 완료·자원 해제·장치 종료까지
유지한다. SDK core, 공개 GraphicsSDK API 및 SDK ZIP은 바꾸지 않았다.

CPU payload는 tight rows이며 GPU row pitch는 256-byte aligned다. 1080p/4K RGBA8,
4K RGBA16F의 기존 폭은 이미 정렬되어 별도 host padding copy를 하지 않는다.
정렬되지 않은 폭은 미리 만든 host buffer에 row copy하고 padding을 0으로 채운다.
payload 크기는 GPU 변경 전에 검사한다. zero extent, row/layer 크기 overflow는 생성 전에
거부하며 이 benchmark 2D 경로의 layer 크기는 UINT32_MAX 이하로 제한한다.

새 `upload_model`은 `llgl_writebuffer_default_buffer_then_copytexturefrombuffer_v1`다.
runner는 LLGL pin·adapter·harness 소스 fingerprint를 실제 실행 결과와 대조하고 이전
WriteTexture/누락 model을 거부한다. 모든 새 upload 결과는 contract revision 5로
CPU payload→persistent texture→GPU 완료, 전체 pre/post readback, staging 비용 포함을
명시한다. SDK마다 staging 정책은 다르며 모델은 원자료에 기록한다. 비교 대상은 동일한
최종 작업의 end-to-end 비용이고 native copy 한 번의 overhead 순위가 아니다.

이전 revision 2/4 upload 결과와 자동 비교하지 않는다. telemetry ON/OFF도 별도 계약으로
유지하며 새 결과로 과거 오류 경로의 correctness를 소급 승인하지 않는다.

## 반복 측정

GraphicsSDK·LLGL·SDL3를 같은 실행 파일에서 실행했다. 각 1080p RGBA8·4K RGBA8·4K
RGBA16F에 count=1, warmup 20, 표본 120 × 독립 프로세스 3회다. 작업 순서는 기존 runner의
고정 seed로 섞으며 원본을 덮어쓰거나 이상치를 제거하지 않는다.

- 비계측: 27개 프로세스 / 3,240개 표본.
- CPU·메모리 체크포인트 계측: 27개 프로세스 / 3,240개 표본.
- 합계: 54개 프로세스 / 6,480개 작업 표본, 18개 summary.

환경은 Intel i7-14700K, RAM 68,473,667,584 bytes, GTX 1660 SUPER,
LUID `0:95995`, driver `32.0.16.1088`, Windows `10.0.26220`, MSVC `194435228`다.
x64 Release /O2 /MD, no LTO, validation OFF, headless, 한 API thread/한 frame-in-flight다.
GPU power/clock, background activity 및 driver/shader cache는 통제하지 않았다.

- [P50/P95/P99 및 회차 median 표](../benchmarks/reports/2026-10-21-llgl-upload/TABLE.md)
- [동일 profile 내 비교 CSV](../benchmarks/reports/2026-10-21-llgl-upload/comparison.csv)
- [비계측 원본](../benchmarks/results/2026-10-21-llgl-upload-plain/summary.json)
- [계측 원본·process_metrics](../benchmarks/results/2026-10-21-llgl-upload-telemetry/summary.json)
- [실행 파일·소스 ZIP·모든 원본 해시](../benchmarks/reports/2026-10-21-llgl-upload/evidence.json)

upload latency는 CPU wall time이며 GPU timestamp가 아니다. GPU time은 null로 유지한다.
CPU utilization은 batch 전체의 process CPU/wall이며 CPU memory peak는 process lifetime,
GPU memory는 node 0 local/nonlocal 체크포인트의 최댓값이다. 연속·transient peak나
실제 residency를 측정했다는 뜻이 아니다. 정확한 정의는 [계측 계약](CPU_MEMORY_BENCHMARK_REPORT.md)을 따른다.

## 검증과 범위

공통 CTest 22개와 기존 core CTest 4개가 통과했다. 전용 LLGL 회귀 테스트는
7×5, 257×3, 1920×1080, 3840×2160 각각 RGBA8/RGBA16F를 검사했다. 8개 조합에서
payload를 바꾸는 공개 업로드 72회, 전체 readback 96회, short payload 거부 뒤 원본 유지,
정렬·padding·zero/overflow 거부를 확인했다. 전체 13,156개 check가 native validation
종료 검사까지 통과했다. 동일 bytes를 반복해서 마지막 성공만 읽는 테스트가 아니다.

세 SDK의 세 upload 시나리오는 별도 native 실행 18개로 계측 OFF/ON을 검증했다.
비계측 warmup=2, 계측 warmup=0, 각각 8개 표본과 pre/post byte readback을 확인했다.
Python 테스트 43개는 model 누락/이전 경로 거부, source/pin 변경 감지와 revision/계측
계약 분리 및 기존 통계·감사를 확인했다.
[검증·artifact 해시](../benchmarks/reports/validation/2026-10-21-llgl-upload/summary.json)에
로그와 raw JSON을 보존한다. 이전 일회성 GraphicsSDK root signature 초기화 실패는
원인 미확정 상태를 유지하며 이번 작업으로 수정됐다고 주장하지 않는다.

새 [inventory](../licenses/generated/windows-benchmarks-llgl-upload-release/inventory.json)는
compiler-read 프로젝트 입력 969개/구성요소 22개를 기록하며 옆의 고지문을 유지한다.
기존 SDK ZIP 및 새 inventory의 `--check`도 통과했다. 새로운 외부 dependency, competitor
pin 변경이나 runtime bundle은 없다. inventory는 재배포 승인이 아니다.

선택형 Forge/Diligent/bgfx의 새 adapter 빌드·재측정, LLGL GPU-based validation,
장기 stress/누수, multi-texture/in-flight, 연속 GPU peak 및 캐시 hit/miss 통제는 수행하지
않았다. public copy 경로의 correctness와 이 제한된 반복 측정까지 완료로 표시한다.
