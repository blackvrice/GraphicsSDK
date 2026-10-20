# Diligent Core D3D12 비교 추가

측정일: 2026-10-20. 기존 LLGL/SDL3/GraphicsSDK와 standalone public Diligent Core를 같은 실행 파일로 다시 측정했다.
8개 시나리오 × 4개 SDK × 3개 독립 프로세스 = 96개 프로세스, 측정 표본 9,360개다.

## 실행 조건과 범위

Intel(R) Core(TM) i7-14700K, RAM 68,473,667,584 bytes, NVIDIA GeForce GTX 1660 SUPER, DXGI LUID `0:95937`, driver `32.0.16.1088`.
Windows 10.0.26220, MSVC 194435228, x64 Release `/O2 /MD`, LTO 없음. validation OFF, headless, single caller thread, 한 프레임 완료 후 다음 작업을 시작한다.
기본 Diligent의 `/GL`, `/arch:AVX2` 추가 옵션을 끄고 비동기 shader compile을 비활성화했다. GPU timestamp 값은 전부 null이다.

DiligentEngine superproject의 submodule 조합이 아닌 연구 시 고정한 standalone Core 커밋을 사용했다. commercial Metal은 사용하지 않는다.
[Core/xxHash/Abseil/NVAPI 고정 버전](../benchmarks/competitors/diligent-build.lock.json), [실제 의존성·고지](DEPENDENCY_LICENSES.md#diligent-core-추가-profile)를 참조한다.

## 측정한 frame 비용

단위 ms. 각 열은 전체 표본의 P50이다. P95/P99/MAD와 각 프로세스 P50은 [전체 percentile CSV](../benchmarks/reports/2026-10-20-diligent/frame-percentiles.csv)에 있다.

| 시나리오 | Count | LLGL | SDL3 | Diligent Core | GraphicsSDK |
|---|---:|---:|---:|---:|---:|
| buffers | 1000 | 319.444650 | 348.634700 | 488.562650 | 409.420600 |
| textures | 1000 | 323.698850 | 411.637650 | 477.606750 | 307.774950 |
| draw | 10000 | 0.519950 | 0.555800 | 0.513750 | 0.521150 |
| instanced | 10000 | 0.430800 | 0.420000 | 0.429050 | 0.422000 |
| upload1080 | 1 | 2.809550 | 1.528800 | 1.630400 | 1.550000 |
| upload4k | 1 | 11.248150 | 8.105600 | 8.456850 | 7.784800 |
| upload4k16f | 1 | 24.987150 | 17.221950 | 15.148400 | 16.397800 |
| pipeline | 1 | 0.003600 | 0.004100 | 0.010000 | 0.004800 |

Draw/instancing은 warmup 20 + 측정 120회/프로세스, 업로드/PSO는 warmup 10 + 측정 120회, 할당은 warmup 5 + 측정 30회다.
할당은 64 KiB vertex-capable default buffer 또는 64×64 RGBA8 texture를 1,000개 생성·해제하고 idle까지 기다린다.
Draw는 동일한 DXBC bytecode로 64×64 RGBA8에 vertex-ID triangle을 10,000번 그린다. Instancing은 3 vertices × 10,000 instances의 한 호출이다.
업로드는 persistent texture에 1080p RGBA8 / 4K RGBA8 / 4K RGBA16F 전체를 복사한다. PSO는 동일한 사전 컴파일 bytecode에서 반복 생성한다.

## 경계와 검증

Diligent는 immediate context에서 기록하고 `Flush()`로 제출한다. `WaitForIdle()`, `FinishFrame()`, `ReleaseStaleResources(true)`로 GPU 완료와 headless 임시 자원 회수를 처리한다.
각 SDK의 기록/제출/캐싱 경계는 다르므로 부분 타이머만으로 native API overhead를 단정하지 않는다. 비교 분류는 end_to_end다.
Draw frame은 기록·제출·완료 대기를 포함하고, allocation frame은 생성·release API·idle을 포함한다. PSO frame은 생성·release API가 있는 loop body이며 다음 반복 전 idle은 제외한다.
`release_api_ms`는 호출 비용이다. 모든 native allocation의 즉시 회수를 의미하지 않으며 SDK 내부 캐시·지연 회수를 허용한다. driver shader/PSO cache cold/warm 상태는 통제하지 않았다.
Draw/PSO는 측정 전후 빨간 중심·파란 clear 모서리를 확인하고, 업로드는 측정 전후 모든 byte를 readback 비교했다. 고정 셰이더 hash 및 workload/environment 계약 일치를 검사했다.
별도의 validation ON 실행은 8개 시나리오 모두 통과했고 Diligent native D3D12 info queue ERROR/CORRUPTION은 없었다. [실행 기록](../benchmarks/reports/validation/2026-10-20-diligent/summary.json)은 성능 표본과 분리했다.

같은 데스크톱에서 순서를 고정 seed로 섞어 실행했다. 전용 장비의 background activity·clock/power 상태는 통제하지 않았으며 결과는 이 장비·작업량의 관측값이다. 일반 SDK 우열이나 통계적 유의성을 주장하지 않는다.

## 보존한 증거

- [2026-10-20-diligent-draw 원본 JSON/CSV](../benchmarks/results/2026-10-20-diligent-draw/summary.json)
- [2026-10-20-diligent-upload-pipeline 원본 JSON/CSV](../benchmarks/results/2026-10-20-diligent-upload-pipeline/summary.json)
- [2026-10-20-diligent-allocation 원본 JSON/CSV](../benchmarks/results/2026-10-20-diligent-allocation/summary.json)
- [Diligent baseline ↔ GraphicsSDK 세부 비용 비교 CSV](../benchmarks/reports/2026-10-20-diligent/comparison.csv)
- [실행 파일·소스 archive·결과 hash](../benchmarks/reports/2026-10-20-diligent/evidence.json)
- [측정한 자체 어댑터·코어 소스 archive](../benchmarks/reports/sources/diligent-078c4113b068dd32d9dd38e0d846679e85abe516f093370505a1c58c0175e761.zip)

기존 측정 및 배포 ZIP은 보존했다. bgfx/The Forge 어댑터와 전체 A–H 비교, GPU timestamp 공통 비교, CPU/GPU 메모리·cache 통제는 남아 있다.
