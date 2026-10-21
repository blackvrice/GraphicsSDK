# Windows CPU 및 메모리 관측

이 보고서는 `a2b87f9`의 역사적 측정이다. 이후 LLGL은 [공개 buffer-to-texture 경로와
새 자료](LLGL_UPLOAD_BENCHMARK_REPORT.md)로 업로드를 복구했다. 아래의 실패·차단 기록은
당시 원본을 설명하며 과거 측정을 재분류하거나 수정하지 않는다.

2026-10-21. 다음 Phase 1 항목 중 CPU 사용률·CPU 최대 메모리와 GPU 메모리 체크포인트
관측을 구현했다. 선택형 `--telemetry`를 사용하며 기본 실행에는 OS 관측 API를 호출하지
않는다. 캐시 cold/warm·PSO hit/miss 통제와 연속 GPU 메모리 peak 검증은 남아 있다.

## 관측 계약

새 결과의 workload contract revision은 4이며 `contract.telemetry`에 CPU·메모리의 API,
구간과 observer 비용을 저장한다. 같은 workload여도 기존 비계측 결과와 자동 비교하지
않는다. draw GPU timestamp를 함께 켜면 기존 query interval과 query resolve 구분도 유지한다.
일반 draw는 여전히 `gpu_ms=null`이다.

CPU는 warmup 뒤 첫 표본의 idle 직전부터 마지막 표본 관측 종료까지의 전체 프로세스
kernel+user 시간이다. 모든 프로세스 스레드를 합산한 `GetProcessTimes` 차이를 ms로
변환하고 steady-clock batch wall time으로 나눈다. 100%는 논리 CPU 하나를 사용한
시간이며 여러 스레드가 실행되면 100%를 넘을 수 있다. 머신 전체 비율이나 표본별 CPU
사용률이 아니다. 초기 device/shader/resource 준비, warmup, 마지막 correctness readback,
JSON 출력과 장치 teardown은 CPU 구간 밖이다. idle/wait, 결과 문자열 저장과 관측 API
비용은 구간 안이다. FILETIME 표현 단위는 100ns이나 실제 CPU accounting 해상도를
보장하지 않는다. 짧은 batch의 0%나 큰 회차 차이는 정밀한 차이로 해석하지 않는다.

`GetProcessMemoryInfo`의 working set/private commit 현재값을 체크포인트에 기록한다.
`PeakWorkingSetSize`와 `PeakPagefileUsage`는 프로세스 시작부터 해당 관측 시점까지의
최댓값이므로 초기화·shader compile·warmup도 포함한다. 해당 최대값을 measured batch만의
할당량이나 physical RAM의 독점 사용량으로 부르지 않는다. harness, runtime, driver의
프로세스 내 메모리도 포함하며 자원 바이트 수에서 이를 빼서 SDK 전용 사용량을 추정하지 않는다.

GPU는 선택된 GPU 이름이 유일한 DXGI adapter인지 확인하고 LUID를 원래 workload와
대조한다. 같은 프로세스에서 `IDXGIAdapter3::QueryVideoMemoryInfo`로 node 0의
local/nonlocal `CurrentUsage`와 `Budget`을 별도로 읽는다. 관측을 위해 장치를 새로 만들거나
GPU 명령을 제출하지 않는다. budget은 OS 목표이며 사용량이 아니다. 이 자료는 전체 머신
VRAM, 리소스 논리 크기, 실제 residency나 다른 GPU node의 사용량을 나타내지 않는다.
local/nonlocal은 순차 조회이므로 원자적인 합계 peak도 만들지 않는다.

체크포인트는 measured batch 시작 baseline과 각 표본 끝이다. buffers/textures는 모든
자원이 살아 있는 생성 완료·해제 직전에도 읽는다. allocation/release API timer에서는
그 조회를 제외하지만 allocation frame timer와 batch CPU/wall에는 포함한다. 나머지
작업의 끝 체크포인트는 기존 frame timer 밖이다. 관측 때문에 pacing과 후속 표본의 cache
상태가 바뀔 수 있으므로 기존 비계측 수치를 대체하지 않는다.

GPU의 `observed_max_*`는 이 체크포인트의 최댓값이다. transient staging, 내부 PSO 생성,
GPU 작업 중간의 메모리 증가와 비동기 accounting을 놓칠 수 있는 하한 관측이다. 프로세스
수명 CPU peak와 같은 종류의 보장된 최대값이 아니다. 장기 누수·in-flight peak 증명도 아니다.
조회 실패·GPU 식별 불일치·누락·역행하는 CPU counter·비유한 수치는 실패로 처리한다.
지원되는 실제 0은 보존하고, 실패한 관측을 0으로 대체하거나 자동 재시도하지 않는다.

## 실제 자료

같은 Intel i7-14700K / GTX 1660 SUPER, LUID `0:95995`, driver `32.0.16.1088`,
Windows `10.0.26220`, MSVC `194435228`, x64 Release /O2 /MD, no LTO다.
GraphicsSDK·LLGL·SDL3는 하나의 실행 파일로 실행한다. validation OFF, headless,
단일 API 스레드, frames-in-flight 1이며 다른 SDK 내부 스레드 사용은 CPU 합계에 포함한다.
SDK 고정 SHA와 깨끗한 checkout, shader/payload 계약은 기존 runner가 확인한다.

- allocation: SDK 3개 × buffers/textures × 프로세스 3회, count 1,000, warmup 3 + 표본 20회.
- draw: SDK 3개 × draw/instanced × 프로세스 3회, count 100,000, warmup 20 + 표본 120회.
- upload: SDL3/GraphicsSDK × 1080p RGBA8·4K RGBA8·4K RGBA16F × 프로세스 3회, warmup 20 + 표본 120회.
- pipeline: SDK 3개 × 프로세스 3회, count 1, warmup 20 + 표본 120회.

합계 63개 독립 프로세스, workload 표본 5,760개다. 프로세스별 CPU 관측은 63개이며
프레임 표본 5,760개를 독립 CPU 관측으로 부풀리지 않는다. CPU %는 회차 median과 각
회차값을, 메모리는 회차별 peak와 그 범위를 보존한다. 아래 표의 MiB는 1,048,576 bytes다.
3개 프로세스만으로 process peak의 P95/P99나 전체 SDK 성능 순위를 주장하지 않는다.
GPU power/clock, background activity와 shader/driver cache는 통제하지 않았다.

실제 batch wall 범위는 allocation 4.54–5.81초, draw 0.328–0.473초, upload
0.130–1.463초였다. pipeline batch는 0.646–0.969ms여서 CPU accounting counter가
모든 회차에서 0으로 관측됐다. 표의 pipeline 0%는 CPU 비용이 없다는 뜻이 아니며 이
batch 길이에서는 CPU 사용률 비교가 불가능하다. 더 긴 CPU 관측 구간과 controlled-cache
실험이 필요하다. 작은 draw/instanced CPU %도 counter 양자화 영향을 함께 고려한다.

- [관측 표](../benchmarks/reports/2026-10-21-telemetry/TABLE.md)
- [CPU 회차값·메모리 CSV](../benchmarks/reports/2026-10-21-telemetry/process-metrics.csv)
- [실행 파일·원본·소스 ZIP 해시](../benchmarks/reports/2026-10-21-telemetry/evidence.json)
- [원본 allocation](../benchmarks/results/2026-10-21-telemetry-allocation/summary.json)
- [원본 draw](../benchmarks/results/2026-10-21-telemetry-draw/summary.json)
- [원본 upload](../benchmarks/results/2026-10-21-telemetry-upload/summary.json)
- [원본 pipeline](../benchmarks/results/2026-10-21-telemetry-pipeline/summary.json)

각 폴더의 `.stdout`은 child 출력이며 `.json`에는 검증한 환경·계약과 체크포인트가 함께 있다.
`process-metrics.csv`는 batch별 관측 단위이고 `summary.csv`는 기존 workload 시간 단위다.
요약은 모든 회차를 집계하며 첫 회차의 raw telemetry를 전체 요약으로 복사하지 않는다.

## 발견한 LLGL 업로드 제한

첫 native CTest의 LLGL upload1080과 별도 upload4k 실행이 COPY_SOURCE 상태인 texture를
CopyTextureRegion destination으로 사용하는 오류로 실패했다. 계측을 끈 upload1080에서도
동일하게 재현했다. 고정 LLGL의 `D3D12RenderSystem::WriteTexture`는 full-region이면
`D3D12Texture::UpdateSubresource`로 바로 진입하고 COPY_DEST 전환 없이 UpdateSubresources를
호출한다. 앞선 `ReadTexture`는 texture를 COPY_SOURCE로 전환한다. 따라서 기존
pre-readback → 반복 full upload 경로는 현재 pin에서 native validation을 통과하지 못한다.

이 경로를 native 우회나 vendor patch로 바꾸지 않았다. 세 LLGL upload 요청은
runner에서 폴더 생성·실행 전에 거부하며 직접 CLI도 명시적으로 실패한다. 이번 upload
표에는 SDL3/GraphicsSDK만 포함한다. 과거 LLGL upload 자료는 역사적 관측으로 보존하지만
현재의 correctness acceptance나 성능 우위 근거로 재사용하지 않는다. 새 기준에서의
public upload 경로 수정 및 재측정은 별도 미완료 항목이다.

## 검증과 범위

공통 CTest 17개가 통과했다. CPU 산술 테스트는 100%·200%·0%, 큰 counter 차분과
역순/0/음수/NaN/무한 wall time 거부를 확인한다. 공통 native 실행 25개는 3개 SDK의
지원 workload, GPU timestamp 병행, warmup 0과 완료 체크포인트 계약을 검증했다.
이미지·upload byte readback은 기존 adapter 계약을 유지한다. native ERROR/CORRUPTION을
검사하는 LLGL·GraphicsSDK의 검사도 유지하며 SDL은 기존 debug-device 설정과 readback
검증 범위다. 선택형 Diligent/bgfx/Forge 계측 빌드·실행은 이번에 수행하지 않았다.
Python 계약·통계·감사 테스트 41개와 기존 코어 CTest 4개도 통과했다. 계측을 끈 공통
세 SDK의 별도 draw 실행은 `telemetry=null`과 기존 frame/GPU 계약을 확인했다.

[실패 및 최종 검증 자료](../benchmarks/reports/validation/2026-10-21-telemetry/summary.json)에
처음 LLGL 오류·비계측 재현과 최종 성공을 함께 남긴다. 최초 실패를 통과로 바꿔 쓰지 않는다.
기존 timestamp 단계의 일회성 root signature 초기화 실패는 원인이 미확정인 상태를 유지한다.

새 공통 benchmark inventory는 compiler-read 입력 968개/구성요소 22개를 기록한다.
Windows OS의 PSAPI/DXGI 헤더·시스템 DLL은 시스템 전제조건이며 새 외부 패키지를
번들하지 않는다. 코어 SDK/API 및 SDK ZIP은 바꾸지 않았고 기존 ZIP 감사도 통과했다.
inventory는 재배포에 대한 법적 승인이 아니다.

캐시 통제, 연속 GPU peak, 장기 stress·누수, 다른 플랫폼, linked multi-node 및 profiler
캡처는 수행하지 않았다. 전체 A–H 벤치마크 완성으로 표시하지 않는다.

## API 근거

- [GetProcessTimes](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getprocesstimes): 모든 process thread의 kernel/user 시간.
- [PROCESS_MEMORY_COUNTERS_EX](https://learn.microsoft.com/en-us/windows/win32/api/psapi/ns-psapi-process_memory_counters_ex): current working set, private commit, lifetime peak commit.
- [QueryVideoMemoryInfo](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_4/nf-dxgi1_4-idxgiadapter3-queryvideomemoryinfo): process usage, local/nonlocal 및 node 선택.
- [DXGI_QUERY_VIDEO_MEMORY_INFO](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_4/ns-dxgi1_4-dxgi_query_video_memory_info): budget과 application current usage 구분.
