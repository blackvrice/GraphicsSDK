# The Forge D3D12 비교 추가

측정일: 2026-10-21. 고정 The Forge 소스에 선택형 Windows D3D12 벤치마크 프로파일을 추가했다.
8개 작업 × 4개 SDK × 3개 독립 프로세스 = 96개 프로세스, 표본 9,360개다. 같은 실행 파일과 계약으로 LLGL·SDL3·The Forge·GraphicsSDK를 비교했다. 전체 SDK 성능 순위를 뜻하지 않는다.

## 환경과 측정 조건

Intel(R) Core(TM) i7-14700K, RAM 68,473,667,584 bytes, NVIDIA GeForce GTX 1660 SUPER, LUID `0:95995`, driver `32.0.16.1088`.
Windows 10.0.26220, MSVC 194435228, x64 Release `/O2 /MD`, LTO 없음, validation OFF, headless.
모든 API와 loader 처리는 단일 CPU 스레드에서 실행한다. 각 작업의 GPU 완료를 기다려 frames-in-flight는 1이다. Forge draw는 direct queue, upload는 SDK loader의 graphics queue를 사용하고 해당 제출 fence를 기다린다.
Draw/instancing은 warmup 20 + 120회/프로세스, upload와 PSO는 warmup 10 + 120회, 할당은 warmup 5 + 30회다. 순서는 고정 seed로 섞었으며 측정 중 빌드·별도 GPU 테스트를 실행하지 않았다.
전용 장비 격리, background activity, GPU power/clock, driver shader/PSO cache cold/warm 상태는 통제하지 않았다. P50/P95/P99/MAD와 회차별 median을 보존하고 이상치를 삭제하지 않는다. GPU timestamp는 모두 null이다.

## 같은 계약의 frame P50

단위 ms. 할당은 1,000개 생성·release·idle의 batch 비용이다. [전체 percentile CSV](../benchmarks/reports/2026-10-21-forge/frame-percentiles.csv)에 P95/P99/MAD와 각 회차 median이 있다.

| 작업 | Count | LLGL | SDL3 | The Forge | GraphicsSDK |
|---|---:|---:|---:|---:|---:|
| buffers | 1000 | 240.499600 | 236.457600 | 8.077900 | 234.160650 |
| textures | 1000 | 213.268450 | 224.496100 | 6.193150 | 219.601100 |
| draw | 10000 | 0.509050 | 0.518000 | 0.468800 | 0.538500 |
| instanced | 10000 | 0.415500 | 0.413000 | 0.416500 | 0.413450 |
| upload1080 | 1 | 1.917750 | 1.137550 | 1.123250 | 1.140350 |
| upload4k | 1 | 8.268200 | 5.536950 | 5.669350 | 5.437300 |
| upload4k16f | 1 | 15.767450 | 12.487350 | 11.558800 | 11.660050 |
| pipeline | 1 | 0.003100 | 0.003800 | 0.003900 | 0.004200 |

64 KiB GPU-only vertex buffer 및 64×64 RGBA8 sampled texture는 초기 데이터 없이 생성·해제한다. Forge는 D3D12MA를 사용하며 각 buffer는 독립 native resource다. 내부 heap pool/cache 비용은 허용하지만 bgfx logical dynamic-buffer pool과 동일한 계약이라는 뜻은 아니다.
Draw는 동일 SM5.1 DXBC, vertex-ID triangle, 64×64 RGBA8, blend/depth/cull OFF다. 개별 10,000 draws와 3 vertices × 10,000 instances의 한 draw를 구분한다. Clear는 파란색, triangle은 빨간색이다.
Upload는 재사용 CPU source에서 persistent 1080p RGBA8 / 4K RGBA8 / 4K RGBA16F texture 전체를 갱신한다. Forge의 공개 begin/endUpdateResource와 flushResourceUpdates를 사용하고 mapped staging에 필요한 한 번의 memcpy를 수행한 뒤 fence 완료까지 측정한다. graphics/copy engine 각각 128 MiB × 1 slot의 staging pool을 측정 전에 준비한다.
PSO는 같은 shader binary와 render state로 addPipeline/removePipeline을 반복한다. native PSO가 실제로 생성됐는지 확인하며 shader compile, root signature bootstrap과 readback은 타이머 밖이다. driver cache를 초기화하지 않았고 각 재생성 PSO를 다시 그리는 workload도 아니다.
record/submit API 경계가 SDK마다 달라 부분 타이머를 raw D3D12 API 비용으로 동일시하지 않는다. 표는 GPU 완료까지의 end_to_end frame이며 pipeline_creation_ms 등 세부 값은 원본과 비교 CSV에 있다.

## 고정 빌드와 명시적인 호환 설정

[고정 pin과 프로파일](../benchmarks/competitors/forge-build.lock.json)은 The Forge `cd5046893faba2dc7869243873bf01f02a6f0df9`다. 원본 tracked 파일을 수정하지 않고 C backend, D3D12MA, resource loader 및 필요한 Windows/utility 소스만 별도 CMake target으로 빌드한다.
상위 Config.h는 VS 2019만 허용한다. 공식 EXTERNAL_CONFIG_FILEPATH hook으로 원본 설정에서 정확히 MSVC 194435228을 추가한 파일을 생성한다. UI·font·script·profiler·shader reload 등 사용하지 않는 application 기능을 끄며 원본 copyright와 Apache 고지를 유지한다. 공식 VS 2019 sample build와 동일한 구성이라는 주장은 아니다.
최신 Windows SDK dxguid.lib와 중복되는 GUID 3개는 해당 C translation unit에서만 이름을 바꾼다. GUID 값과 API 호출은 그대로며 /FORCE:MULTIPLE을 쓰지 않는다.
동봉 구형 NVAPI의 공개 benchmark 제한 때문에 NVAPI를 제외한다. Forge helper의 기존 fallback branch를 먼저 include하고 platform macro를 즉시 복원한다. 실제 compiler/linker 입력에서 proprietary NVAPI header/library가 없음을 fail-closed 검사한다. NVIDIA 전용 확장 기능을 평가한 결과가 아니다.
일반 descriptor/FSL root 파일 대신 public Renderer의 user-filled graphics root signature field에 빈 native signature를 준비한다. bindings 없는 현재 workload로 한정한다. addShaderBinary는 공통 DXBC를 변경 없이 DXC blob으로 감싸며 런타임 shader 재컴파일을 하지 않는다.

## Correctness 및 의존성 증거

Forge CTest 10개는 8개 작업 및 10,000 draw/instance를 실제 GPU에서 실행했다. 네이티브 debug-layer ERROR/CORRUPTION, Forge error callback, device removal 및 teardown 오류는 실행 실패로 처리한다. Forge GPU-based validation·장기 leak stress를 수행했다는 의미는 아니다.
그리기는 측정 전후 빨간 중심·파란 모서리를 읽어 확인하고 upload는 모든 payload byte를 비교한다. 고정 loader copyResource의 transfer-queue readback이 최신 debug layer layout 검사에서 실패했으므로, 측정 밖의 probe만 Forge direct command list의 native CopyTextureRegion으로 수행한다. 장벽·할당·제출·완료 대기는 Forge API다. 이 native readback을 Forge 공개 copyResource의 성공 증거로 취급하지 않는다.
기존 SDK CTest 4개와 Python 계약/통계/감사 테스트 29개가 통과했다. [검증 원본](../benchmarks/reports/validation/2026-10-21-forge/summary.json)에 실제 결과와 log hash를 보존했다. 새 -Forge bootstrap으로 Release build 및 native tests가 통과했다.
[의존성 inventory](../licenses/generated/windows-benchmarks-forge-release/inventory.json)는 실제 compiler-read 입력 1,102개와 구성요소 39개를 기록하고 native libraries·runtime DLL·GPU 설정의 고정 해시도 검사한다. AGS MIT, DXC NCSA/MIT, Agility MIT headers, D3D12MA MIT, Sony BSD/Forge Apache, bstrlib/LZ4/Zstd/STB 등의 고지를 구분했다.
DXC dxcapi.h·import library·dxcompiler.dll은 [공식 v1.8.2405 배포본](https://github.com/microsoft/DirectXShaderCompiler/releases/tag/v1.8.2405)과 byte-identical임을 확인하고 고지문을 보완했다. [출처 증거](../licenses/provenance/forge-terms-origin.json)에 release와 파일 해시가 있다. dxil.dll, Agility runtime 및 PIX DLL은 복사하지 않는다.
PIX header는 compiler-read 목록에 보수적으로 기록한다. USE_PIX를 끄고 library/DLL은 링크·복사하지 않는다. 옛 NuGet licenseUrl은 현재 다른 페이지로 이동하며 동봉 snapshot에 primary terms가 없어 header 재배포 조건은 별도 검토 대상으로 남긴다. inventory_verified는 배포 승인 표시가 아니다.
기존 자체 SDK ZIP에는 경쟁 코드나 DLL을 추가하지 않았고 SDK package audit --check가 통과했다. 모든 원본 측정·과거 보고서·고지 inventory는 보존했다.

## 산출물

- [2026-10-21-forge-draw 원본 JSON/CSV](../benchmarks/results/2026-10-21-forge-draw/summary.json)
- [2026-10-21-forge-upload-pipeline 원본 JSON/CSV](../benchmarks/results/2026-10-21-forge-upload-pipeline/summary.json)
- [2026-10-21-forge-allocation 원본 JSON/CSV](../benchmarks/results/2026-10-21-forge-allocation/summary.json)
- [Forge ↔ GraphicsSDK 세부 비교](../benchmarks/reports/2026-10-21-forge/comparison.csv)
- [실행 파일·소스 snapshot·측정 해시](../benchmarks/reports/2026-10-21-forge/evidence.json)
- [측정에 사용한 자체 SDK·하네스 소스](../benchmarks/reports/sources/forge-f6b5645584bb2626f7b54f187f58d606a14b90c2332ea41810e1e9556ef9ae49.zip)
- [고지 원문](../licenses/generated/windows-benchmarks-forge-release/THIRD_PARTY_NOTICES.txt)

다음 미완료 항목은 양쪽에서 같은 기능을 제공하는 low-level GPU timestamp 비교다. CPU/GPU memory, CPU 사용률, cache 통제 및 전체 A–H, Vulkan/Metal은 여전히 미완료다.
