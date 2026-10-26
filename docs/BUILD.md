# Build

Windows 검증 환경은 Visual Studio 2022 17.14, MSVC 19.44, Windows SDK
10.0.26100.0, x64, CMake 3.25 이상이다. C++23 `std::expected` 지원이 필요하다.

장시간 메모리 `P2-ACC-01`은 2026-10-25 사용자 지시로 재개했다. 현재 SDK의 NVIDIA GPU 15분과
WARP 5분은 통과했지만 full GBV는 warmup 중 기존 512MiB 증가 guard로 중단돼 전체 수용은 미완료다.
[재개 실행·원래 기준·원본 근거](PHASE2_MEMORY_RESUMPTION_REPORT.md)를 따른다. Intel AV 조사 `P2-ACC-02`도
사용자 지시로 [별도 재개](PHASE2_INTEL_CRASH_RESUMPTION_REPORT.md)했으며 원인은 아직 미확정이다.

```powershell
cmake --preset windows
cmake --build --preset windows-release
ctest --preset windows-release
cmake --install out/core --config Release --prefix out/install
cmake -S tests/consumer -B out/consumer -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=D:/source/GraphicsSDK/out/install
cmake --build out/consumer --config Release
./out/consumer/Release/consumer.exe
./out/core/Release/gfx_triangle.exe out/triangle.ppm
./out/core/Release/gfx_indexed_quads.exe out/indexed-quads.ppm
```

SDK 자체에는 다운로드가 없다. D3D12와 Vulkan을 모두 끈 빌드는 GPU backend를 제공하지 않으며
Unsupported 반환과 portable 헤더/코어 계약만 검증한다. Linux Vulkan의 native compile·CPU 검사는
[Ubuntu CI에서 검증](PHASE3_LINUX_COMPILE_REPORT.md)했다. Linux 물리 GPU/WSI와 macOS native build/runtime은
미검증이다. `portable-core` preset은 Ninja 및 C++23 toolchain이 필요하다.
현재 host의 Linux 환경 가용성은 [P3-LNX-01 조사](PHASE3_LINUX_ENVIRONMENT.md)에
기록한다. 로컬 WSL 미설치 관측은 보존하며, 후속 실제 Linux compile 근거는 별도 CI 보고서에 연결한다.
Linux 전용 `linux-vulkan` / `linux-vulkan-validation` preset의 backend flag·외부 헤더·
분리된 build/install 경로와 재현 명령은 [P3-LNX-02 설정](PHASE3_LINUX_PRESETS.md)을 따른다.
실제 Linux configure/compile과 두 preset의 CPU 22개 검사는 `P3-LNX-03`에서 완료했다.
Linux software ICD의 loader·장치·선택 오류 실행은 [P3-LNX-04](PHASE3_LINUX_DEVICE_REPORT.md)에
기록한다. [P3-LNX-05](PHASE3_LINUX_TRANSFER_REPORT.md)에서는 같은 software ICD의 버퍼·queue/fence·수명 suite를
in-tree와 설치 소비자에서 통과했다. [P3-LNX-06](PHASE3_LINUX_TEXTURE_REPORT.md)에서는 software ICD의
RGBA8/RGBA16F 텍스처 전송·전체 texel/padding·수명 검사도 두 경로에서 통과했다.
[P3-LNX-07](PHASE3_LINUX_DRAW_REPORT.md)에서는 컬러 pass·기본 draw·blend/scissor·instancing·MRT 참조도
software ICD의 두 경로에서 통과했다. [P3-LNX-08](PHASE3_LINUX_INPUT_REPORT.md)에서는 vertex/index/instance 입력과
배열·sampler·resource snapshot도 두 경로에서 검증했다. [P3-LNX-09](PHASE3_LINUX_DEPTH_REPORT.md)에서는
D32·depth-only/read-only·비교/쓰기 mask·depth/MRT의 색·깊이 참조도 통과했다.
[P3-LNX-10](PHASE3_LINUX_TIMESTAMP_REPORT.md)에서는 native queue metadata·실제 timestamp elapsed/availability·수명과
CPU wrap 변환을 구분해 통과했다. [P3-LNX-11](PHASE3_LINUX_VALIDATION_REPORT.md)에서는 고정 VVL 1.3.290으로
정상 8개 suite·설치 소비자와 native 오류 controls를 통과했다. [P3-LNX-12](PHASE3_LINUX_PACKAGE_REPORT.md)에서는
설치 경로를 옮긴 뒤 저장소 밖 소비자 9개·실제 GNU 입력·고지·20개 파일의 검증용 SDK ZIP을 감사했다.
고지는 전용 도구의 설치 후 패키징 단계에서 추가한다. [P3-LNX-14](PHASE3_PYTHON_CI_REPORT.md)에서는
새 Windows/Linux CI의 전체 Python 640개씩을 오류·실패·건너뜀 0으로 통과했다. 과거 source ZIP 검증과
현재 checkout identity를 분리하고 Windows 경로 별칭을 수정했다. 물리 GPU(P3-LNX-15)는 별도 잔여다.
[P3-LNX-15 실행 준비](PHASE3_LINUX_HARDWARE_READINESS.md)에서는 하드웨어 전용 도구·수동 CI와 실제
software 장치 거부를 검증했다. 현재 전체 Python은 두 플랫폼 각각 654개가 통과했으며,
실제 Linux GPU 접속 대상이 없어 물리 GPU 정상 suite·validation 수용은 미완료다.

현재 core CTest는 34개다. GPU validation과 WARP를 포함하고, vertex/binding/render-pass/adapter/budget의
portable 구조 검증은 D3D12 OFF 빌드에서도 실행된다(총 6개 CPU CTest).
기존 D3D12 전용 SDK ZIP은 `out/GraphicsSDK-0.0.1-dev-windows-x64-copy-atomicity.zip`이다.
기존 ZIP과 측정 원본을 덮어쓰지 않았다. [복구 구현·검증 보고서](PHASE2_RECOVERY_REPORT.md)를 따른다.
`gfx_recovery_tests --gpu/--validation/--warp`는 각 독립 process에서 opt-in native RemoveDevice를
실행한다. 지원하지 않는 adapter/interface는 SKIP이며 완료 증거로 취급하지 않는다.
설치 후 `out/consumer/Release/recovery_consumer.exe` 및 `--warp`로 재구축·이미지 검증을 실행할 수 있다.
이 두 검증 실행 파일은 의도적으로 자기 process의 native 장치를 제거한다.

새 Vulkan 전송 구성은 [Vulkan 빌드·계약](VULKAN.md)을 따른다. 독립된
`out/vulkan-transfer`, `out/vulkan-install`, `out/vulkan-consumer`를 사용하며,
35개 CTest 중 하나가 실제 NVIDIA/Intel Vulkan 전송을 검사한다. D3D12 OFF/Vulkan ON
구성은 7개, 두 backend OFF 구성은 6개 CTest를 Windows에서 검증한다.
새 SDK는 `out/GraphicsSDK-0.0.1-dev-windows-x64-vulkan-transfer.zip`이며, 공개 헤더와
static library/CMake package, MIT/Apache-2.0 고지로 구성된다. Vulkan 헤더·loader·driver는
묶지 않는다. Linux 물리 GPU 실행은 미검증이며 software ICD 장치/전송/draw/입력·바인딩/depth와 native compile/CPU 근거는 위 CI 보고서를 따른다.
Vulkan core/synchronization layer 실행은 새 [고정 로컬 레이어 검증](PHASE3_VULKAN_VALIDATION_REPORT.md)에
기록한다. `GFX_VULKAN_VALIDATION_TESTS=ON`은 필요한 레이어가 없으면 실패하며 skip하지 않는다.
이 검증 구성의 별도 SDK ZIP은 `out/GraphicsSDK-0.0.1-dev-windows-x64-vulkan-validation.zip`이다.

이전 [색상 텍스처 확장](PHASE3_VULKAN_TEXTURE_REPORT.md)은 이전 빌드를 보존한 채
`out/vulkan-textures`, `out/vulkan-textures-install`, `out/vulkan-textures-consumer`를
사용한다. Required validation 구성은 통합 39개, Vulkan-only 11개 CTest이며
설치 소비자에 `vulkan_texture_consumer [--validation]`을 추가한다.
새 SDK ZIP은 `out/GraphicsSDK-0.0.1-dev-windows-x64-vulkan-textures.zip`이다.

이전 [색상 렌더 패스](PHASE3_VULKAN_COLOR_PASS_REPORT.md)는 `out/vulkan-color-pass`,
`out/vulkan-color-pass-install`, `out/vulkan-color-pass-consumer`에서 별도로 빌드한다.
Required validation은 통합 41개/Vulkan-only 13개를 등록한다. 최초 통합 실행의 기존
D3D12 메모리 validation 충돌을 보존했으며, 사용자 보류 범위의 메모리 검사 3개를
최종 실행에서 명시적으로 제외한다(통합 선택 38개). 설치 소비자는
`vulkan_render_consumer [--validation]`을 추가한다. 새 SDK는
`out/GraphicsSDK-0.0.1-dev-windows-x64-vulkan-color-pass.zip`이다.

이전 [SPIR-V draw](PHASE3_VULKAN_DRAW_REPORT.md)는 `out/vulkan-draw`,
`out/vulkan-draw-install`, `out/vulkan-draw-consumer`를 사용한다. Required validation
등록은 통합 44개/Vulkan-only 16개이며 통합에서는 보류 메모리 3개를 제외한 41개를
선택 실행한다. 두 backend OFF는 CPU 7개다. 설치 소비자는 `vulkan_draw_consumer
[--validation]`을 추가한다. ShaderBinary format 필드 추가에 따라 모든 소비자를 재빌드한다.
새 SDK는 `out/GraphicsSDK-0.0.1-dev-windows-x64-vulkan-draw.zip`이다.

이전 [정점·인덱스 입력](PHASE3_VULKAN_VERTEX_REPORT.md)은 `out/vulkan-vertex`,
`out/vulkan-vertex-install`, `out/vulkan-vertex-consumer`를 사용한다. Required validation
등록은 통합 47개/Vulkan-only 19개이며 통합은 보류 메모리 3개를 제외한 44개를 실행한다.
두 backend OFF는 CPU 8개다. 설치 소비자에 `vulkan_vertex_consumer [--validation]`을
추가하며 실제 SDK library와 모든 소비자를 재빌드했다. 공개 API 형태/ABI 변경은 없다.
새 SDK는 `out/GraphicsSDK-0.0.1-dev-windows-x64-vulkan-vertex.zip`이다.
재현 명령과 child 전용 layer 환경은 해당 보고서의 frozen verification script를 따른다.

이전 [리소스 바인딩](PHASE3_VULKAN_BINDINGS_REPORT.md)은 `out/vulkan-bindings`,
`out/vulkan-bindings-install`, `out/vulkan-bindings-consumer`를 사용한다. Required validation은
통합 50개/Vulkan-only 22개를 등록하며 통합은 보류 메모리 3개를 제외한 47개를 실행한다.
두 backend OFF는 CPU 9개다. `vulkan_binding_consumer [--validation]`을 추가했고 SDK와
해당 소비자를 모두 재빌드했다. 공개 API 형태/ABI 변경은 없다.
SDK ZIP은 `out/GraphicsSDK-0.0.1-dev-windows-x64-vulkan-bindings.zip`이다.

이전 [D32 깊이 패스](PHASE3_VULKAN_DEPTH_REPORT.md)는 `out/vulkan-depth`,
`out/vulkan-depth-install`, `out/vulkan-depth-consumer`를 사용한다. Required validation은
통합 52개/Vulkan-only 24개를 등록하고 통합은 보류 메모리 3개를 제외한 49개를 실행한다.
두 backend OFF는 CPU 9개다. `vulkan_depth_consumer [--validation]`을 추가했고 SDK와 모든
해당 소비자를 재빌드했다. 공개 API 형태/ABI 변경은 없다.
SDK ZIP은 `out/GraphicsSDK-0.0.1-dev-windows-x64-vulkan-depth.zip`이다.

이전 [GPU 타임스탬프](PHASE3_VULKAN_TIMESTAMP_REPORT.md)는 `out/vulkan-timestamps`,
`out/vulkan-timestamps-install`, `out/vulkan-timestamps-consumer`를 사용한다. Required validation은
통합 55개/Vulkan-only 27개를 등록하고 통합은 보류 메모리 3개를 제외한 52개를 실행한다.
두 backend OFF는 CPU 10개다. `vulkan_timestamp_consumer [--validation]`을 추가했고 SDK와
해당 소비자를 재빌드했다. 공개 API 형태/ABI 변경은 없다.
SDK ZIP은 `out/GraphicsSDK-0.0.1-dev-windows-x64-vulkan-timestamps.zip`이다.

```powershell
cmake -S tests/vulkan_consumer -B out/vulkan-consumer -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=D:/source/GraphicsSDK/out/vulkan-install
cmake --build out/vulkan-consumer --config Release --parallel 1
./out/vulkan-consumer/Release/vulkan_consumer.exe
```

실제 창 표시 예제는 설치 SDK를 소비하는 별도 프로젝트다.

```powershell
cmake -S examples/presentation -B out/presentation -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=D:/source/GraphicsSDK/out/install
cmake --build out/presentation --config Release --parallel 1
./out/presentation/Release/gfx_presentation.exe
ctest --test-dir out/presentation -C Release --output-on-failure
```

기본은 하드웨어/VSync ON이며 Esc 또는 창 닫기로 종료한다. `--mode validation`,
`--mode warp`, `--no-vsync`, `--fps 1..240`을 선택할 수 있다. 기본 속도 상한은 60fps다.
자동 CTest 6개는 실제 창을 잠시 띄워
크기 변경·최소화·복원·native 제거/복구를 수행한다. 육안 승인은 별도다.
[상세 실행·검증 범위](../examples/presentation/README.md)를 따른다.
별도 CPU 정책 CTest 1개를 합쳐 총 7개이며, 높이 1픽셀 창과 대기 중 메시지·닫기 검증은
[창 크기·속도 조절 보고서](PHASE2_PRESENTATION_PACING_REPORT.md)에 보존한다.

`gfx_presentation --list-adapters`는 창을 만들기 전에 후보 ID와 메타데이터를 출력한다.
출력된 decimal ID를 `--adapter ID`로 지정한다. 하드웨어는 `--mode gpu` 또는
`--mode validation`, WARP는 `--mode warp`와 조합한다. ID가 없거나 software 종류가
다르면 오류이며 다른 GPU로 바꾸지 않는다. ID는 재부팅 후 재조회한다.
`out/consumer/Release/adapter_consumer.exe` 및 `--validation`은 설치 API만으로 전체 후보의
선택·이미지·native RemoveDevice 복구를 검증한다. [어댑터 보고서](PHASE2_ADAPTER_REPORT.md)를 따른다.

`out/consumer/Release/budget_consumer.exe` 및 `--validation`은 설치 public API만으로
전체 후보의 local/nonlocal memory budget과 같은 ID의 손실·복구를 검사한다.
`gfx_budget_tests --gpu/--validation`은 독립 DXGI bracket의 네 필드와 64MiB 보유·해제 대조도
검사한다. 새 Device virtual API를 소비하는 앱은 재빌드한다.
[메모리 예산 보고서](PHASE2_MEMORY_BUDGET_REPORT.md)를 따른다.

`gfx_fence_wait_tests --gpu/--validation`은 각 실제 후보의 native queue를 두 CPU gate로
차단해 zero-poll/반복 timeout/더 이른 펜스 재시도를 결정적으로 검사한다. 공개 SDK의 완료
timeout 검증·readback·스레드·손실/복구도 함께 확인한다. 테스트 종료 때 gate를 해제하며,
실제 TDR·드라이버 재시작이나 시스템 정책 변경은 하지 않는다.
[펜스 대기 보고서](PHASE2_FENCE_WAIT_REPORT.md)를 따른다.

## 경쟁 벤치마크

```powershell
./tools/bootstrap_competitors.ps1
python tools/run_benchmarks.py --sdks llgl sdl graphicssdk --scenarios draw instanced --count 10000 --runs 3 --samples 240 --warmup 30 --output benchmarks/results/my-draw-run
python tools/run_benchmarks.py --sdks llgl sdl graphicssdk --scenarios upload1080 upload4k upload4k16f --count 1 --runs 3 --samples 120 --warmup 10 --output benchmarks/results/my-upload-run
python tools/run_benchmarks.py --sdks llgl sdl graphicssdk --scenarios buffers textures --count 1000 --runs 3 --samples 60 --warmup 5 --output benchmarks/results/my-allocation-run
python -m unittest discover -s tests -p "test_*.py" -v
```

Python 3 표준 라이브러리만 필요하다. 이 장비에서는 PATH의 python.exe가 Windows
Store alias여서 Codex bundled Python의 절대 경로를 사용했다. 일반 설치에서는 실제
Python interpreter로 바꾼다. 모든 결과 디렉터리는 새 경로여야 한다.
LLGL/SDL source는 고정 SHA로 `.deps`에 받아 빌드하고 수정하지 않는다.
Sandbox는 bootstrap 대상이 아니다.

고정 standalone Diligent Core를 추가하려면 다음 별도 빌드 구성을 사용한다.
기존 `out/bench`와 과거 결과를 보존한다.

```powershell
./tools/bootstrap_competitors.ps1 -Diligent
python tools/run_benchmarks.py --exe out/bench-diligent/Release/gfx_bench.exe --sdks llgl sdl graphicssdk diligent_core --scenarios draw instanced --count 10000 --runs 3 --samples 120 --warmup 20 --output benchmarks/results/my-diligent-draw
python tools/run_benchmarks.py --exe out/bench-diligent/Release/gfx_bench.exe --sdks diligent_core --scenarios buffers textures draw instanced upload1080 upload4k upload4k16f pipeline --count 1 --runs 1 --samples 2 --warmup 1 --validation --output out/my-diligent-validation
python tools/audit_licenses.py --profile benchmarks-diligent --output out/my-diligent-license-audit
```

이 profile은 `out/bench-diligent` 고정 레이아웃을 사용한다. Core/xxHash/Abseil/NVAPI
커밋은 `benchmarks/competitors/diligent-build.lock.json`에 기록했다. upstream 기본 LTO와
AVX2 추가 옵션을 끄고 `/O2 /MD`를 사용한다. 코어 기본 빌드에는 다운로드가 추가되지 않는다.
DiligentEngine superproject와 commercial Metal은 이 구성에 포함되지 않는다.

bgfx는 별도 `-Bgfx` profile을 사용한다. `-Diligent`와 동시에 지정하지 않는다.

```powershell
./tools/bootstrap_competitors.ps1 -Bgfx
python tools/run_benchmarks.py --exe out/bench-bgfx/Release/gfx_bench.exe --sdks llgl sdl graphicssdk bgfx --scenarios draw instanced --count 10000 --runs 3 --samples 120 --warmup 20 --output benchmarks/results/my-bgfx-draw
python tools/run_benchmarks.py --exe out/bench-bgfx/Release/gfx_bench.exe --sdks bgfx --scenarios buffers textures draw instanced upload1080 upload4k upload4k16f --count 1 --runs 1 --samples 2 --warmup 1 --validation --output out/my-bgfx-validation
ctest --test-dir out/bench-bgfx -C Release --output-on-failure
python tools/audit_licenses.py --profile benchmarks-bgfx --output out/my-bgfx-license-audit
python tools/report_bgfx_benchmarks.py
```

이 profile은 `.deps/bgfx`, `.deps/bx`, `.deps/bimg`와 `out/bench-bgfx` 레이아웃을 사용한다.
경쟁 소스는 고정 pin 그대로 두고 공식 amalgamated build를 CMake로 구성한다. 별도
shaderc 실행 파일 없이 reflection으로 검증한 공통 DXBC를 bgfx container v13으로 감싼다.
지원 범위는 resource-free vertex-ID VS/PS뿐이다. 일반 bgfx shader compiler 대체 API가 아니다.
위 보고서 생성 명령은 고정된 2026-10-21 campaign을 읽는다. 새 출력 디렉터리의 측정은
기존 runner/compare 도구를 사용하고 원본 campaign을 덮어쓰지 않는다.

bgfx `pipeline`은 실행 전에 거부한다. `buffers`는 dynamic vertex buffer pool의 별도
resource policy이므로 기본 GPU resource 할당과 자동 비교하지 않는다. bgfx source와
컴파일한 static library의 fingerprint, 실제 compiler flags와 전이 고지를 검사한다.

SDK 코어를 포함하지 않는 경쟁 SDK 전용 suite는
`cmake -S benchmarks -B out/bench-independent -A x64 -DGFX_BENCH_WITH_SDK=OFF`로 구성한다.
이 설정은 GraphicsSDK 소스/라이브러리 target을 사용하지 않는다.

The Forge는 별도 `-Forge` profile을 사용한다. `-Diligent`, `-Bgfx`와 동시에 지정하지 않는다.

```powershell
./tools/bootstrap_competitors.ps1 -Forge
ctest --test-dir out/bench-forge -C Release --output-on-failure
python tools/run_benchmarks.py --exe out/bench-forge/Release/gfx_bench.exe --sdks llgl sdl the_forge graphicssdk --scenarios draw instanced --count 10000 --runs 3 --samples 120 --warmup 20 --output benchmarks/results/my-forge-draw
python tools/audit_licenses.py --profile benchmarks-forge --output out/my-forge-license-audit
python tools/report_forge_benchmarks.py
```

`.deps/TheForge`의 고정 원본과 `out/bench-forge` 레이아웃을 사용한다. 원본의 외부 config
hook에 정확히 MSVC 194435228 허용을 추가하고, 중복 GUID 3개의 심볼명만 바꾼다.
구형 NVAPI를 제외하며 Forge helper의 기존 fallback 경로를 선택한다. 생성 config의
정확한 내용, 실제 compiler flags·header/library 입력·runtime/GPU config copy를 검사한다.
CTest 10개는 실제 D3D12 hardware/debug layer가 필요하다. GPU 부하 테스트는 성능 측정과
동시에 실행하지 않는다. 자체 SDK 기본 빌드에는 Forge나 추가 DLL이 포함되지 않는다.
보고서 생성 명령은 고정 2026-10-21 campaign을 읽고 소스 ZIP을 보존한다. 새 측정에는
기존 runner와 새 출력 디렉터리를 사용한다. [범위와 native readback 제한](FORGE_BENCHMARK_REPORT.md)을 따른다.

보고서와 PNG를 재생성하려면 선택적으로 `python -m pip install --target .deps/python matplotlib`를
실행한 뒤 `python tools/report_benchmarks.py`를 사용한다. 이는 SDK 빌드 의존성이 아니다.
전후 비교는 `tools/compare_results.py baseline/summary.json candidate/summary.json --output 새경로`로 생성한다.

## 의존성 고지 및 패키지 검사

```powershell
python tools/audit_licenses.py --profile sdk --output out/sdk-license-audit
python tools/audit_licenses.py --profile benchmarks --output out/benchmark-license-audit
python tools/audit_licenses.py --profile benchmarks --output out/benchmark-license-audit --check
```

`--package out/GraphicsSDK-0.0.1-dev-windows-x64.zip`를 SDK profile에 추가하면 실제 ZIP의
필수 파일·LICENSE와 라이브러리·공개 헤더·CMake export를 검사한다. 검사에는 해당
Windows Release 빌드의 MSBuild 기록이 필요하다. 범위와 한계는
[라이선스 감사](DEPENDENCY_LICENSES.md) 및 [고지 목록](../licenses/README.md)을 따른다.

## GPU timestamp draw 측정

```powershell
./tools/bootstrap_competitors.ps1 -Forge
python tools/run_benchmarks.py --exe out/bench-forge/Release/gfx_bench.exe --sdks llgl the_forge graphicssdk --scenarios draw_gpu instanced_gpu --count 10000 --runs 3 --samples 120 --warmup 20 --output benchmarks/results/my-timestamp-draw
./tools/bootstrap_competitors.ps1 -Diligent
python tools/run_benchmarks.py --exe out/bench-diligent/Release/gfx_bench.exe --sdks diligent_core graphicssdk --scenarios draw_gpu instanced_gpu --count 10000 --runs 3 --samples 120 --warmup 20 --output benchmarks/results/my-diligent-duration
python tools/report_timestamp_benchmarks.py
```

일반 `draw`/`instanced`는 query를 기록하지 않는다. 새 `*_gpu`만 GPU marker를 기록한다.
위 두 bootstrap은 CPU 변환 계약 테스트와 GPU timestamp CTest도 빌드·실행한다.
수동 빌드에서는 `--target gfx_bench gpu_timestamp_contracts`를 사용한다.
bgfx/SDL의 새 시나리오는 대응 interval이 없어 실행 전 거부한다.

Diligent duration query는 시작 marker 직후 native resolve가 interval 안에 들어간다.
`contract.query_resolve`로 구분하여 GraphicsSDK/LLGL/Forge 공통 interval과 자동 비교하지
않는다. query 없는 기존 frame 측정과도 category/contract가 다르다. `query_read_ms`는
최초 GPU 완료 후 결과 수집 비용이며 `gpu_ms`와 별도로 읽는다.

보고서 생성 명령은 고정 2026-10-21 campaign을 읽는다. 과거 보고서 생성기는 해당
소스 snapshot에서 재생성하며, 현재 하네스 fingerprint가 달라졌다면 기존 측정 재사용을
거부한다. 새 결과는 항상 새 폴더로 저장한다. [계약·검증·실측 범위](GPU_TIMESTAMP_BENCHMARK_REPORT.md)를 따른다.

현재 자동화는 Windows SDK 및 고정 competitor 빌드와 CTest/Python/외부 소비자 검증을
지원한다. GPU 성능 측정 중 다른 빌드나 GPU 부하를 실행하지 않는다.

## CPU 사용률 및 메모리 관측

```powershell
./tools/bootstrap_competitors.ps1
ctest --test-dir out/bench -C Release --output-on-failure
python tools/run_benchmarks.py --sdks llgl sdl graphicssdk --scenarios buffers textures --count 1000 --runs 3 --samples 20 --warmup 3 --telemetry --output benchmarks/results/my-memory-allocation
python tools/run_benchmarks.py --sdks llgl sdl graphicssdk --scenarios draw instanced --count 100000 --runs 3 --samples 120 --warmup 20 --telemetry --output benchmarks/results/my-cpu-draw
python tools/run_benchmarks.py --sdks sdl graphicssdk --scenarios upload1080 upload4k upload4k16f --count 1 --runs 3 --samples 120 --warmup 20 --telemetry --output benchmarks/results/my-memory-upload
python tools/run_benchmarks.py --sdks llgl sdl graphicssdk --scenarios pipeline --count 1 --runs 3 --samples 120 --warmup 20 --telemetry --output benchmarks/results/my-memory-pipeline
python tools/report_telemetry_benchmarks.py
```

직접 CLI에도 마지막 인자로 `--telemetry`를 붙인다. 없으면 OS 관측을 호출하지 않는다.
수동 target 빌드에는 `telemetry_contracts`를 포함한다. 보고서 생성기는 고정된
2026-10-21-telemetry 캠페인 네 개를 검증하고 소스 ZIP을 보존한다. 새로운 출력 경로는
기존 폴더와 겹치지 않게 지정한다. 현재 공통 native CTest 22개는 hardware/debug device가 필요하다.

`process-metrics.csv`/`summary.json`의 `process_metrics`는 process batch CPU %, CPU lifetime
memory peak 및 GPU checkpoint 최댓값이다. `summary.csv`의 frame 시간과 단위·표본 수를
섞지 않는다. GPU 관측은 node 0 local/nonlocal의 순간 process usage이며 연속 peak가 아니다.
캐시 cold/warm·PSO hit/miss는 아직 통제하지 않는다.

LLGL upload1080/upload4k/upload4k16f는 public WriteBuffer→CopyTextureFromBuffer 경로로
지원한다. 예전 full-region WriteTexture 경로/model 및 stale adapter fingerprint는 거부한다.
이번 선택형 계측의 실제 빌드·측정 범위는 공통 세 SDK이며 Diligent/bgfx/Forge 재검증은
수행하지 않았다. [관측 보고서](CPU_MEMORY_BENCHMARK_REPORT.md)를 따른다.

## LLGL 공개 업로드 경로 재측정

```powershell
./tools/bootstrap_competitors.ps1
ctest --test-dir out/bench -C Release --output-on-failure
python tools/run_benchmarks.py --sdks llgl sdl graphicssdk --scenarios upload1080 upload4k upload4k16f --count 1 --runs 3 --samples 120 --warmup 20 --output benchmarks/results/my-upload-plain
python tools/run_benchmarks.py --sdks llgl sdl graphicssdk --scenarios upload1080 upload4k upload4k16f --count 1 --runs 3 --samples 120 --warmup 20 --telemetry --output benchmarks/results/my-upload-telemetry
python tools/report_llgl_upload.py
```

수동 target 빌드는 `gfx_bench gpu_timestamp_contracts telemetry_contracts llgl_upload_contracts`를
포함한다. 전용 LLGL 테스트는 8개 형식·크기에서 바뀌는 bytes, readback 뒤 재업로드,
연속 upload, malformed payload 및 padded row를 검사한다. 공통 CTest는 22개다.
report 생성기는 고정 2026-10-21-llgl-upload-plain/telemetry 폴더를 검증한다.
새 upload 계약 revision 5와 계측 여부를 유지하며 이전 upload 자료와 자동 비교하지 않는다.
GPU 시간은 null이다. [범위·추가 copy 비용·원본](LLGL_UPLOAD_BENCHMARK_REPORT.md)을 따른다.

## Phase 1 확장 baseline과 완료 게이트

```powershell
./tools/bootstrap_competitors.ps1 -Jobs 1
ctest --test-dir out/bench -C Release --output-on-failure
python tools/run_phase1.py --samples 5 --runs 1 --validation --output out/my-phase1-native
python tools/run_phase1.py --samples 120 --runs 3 --output benchmarks/results/my-phase1-baselines
python tools/audit_licenses.py --profile benchmarks-phase1 --output out/my-phase1-inventory
python tools/run_phase1.py --compare-sdl --validation --samples 5 --runs 1 --output benchmarks/reports/validation/<new-stage>/native
python tools/run_phase1.py --compare-sdl --samples 120 --runs 3 --output benchmarks/results/<new-stage>
python tools/report_phase1_comparison.py --results benchmarks/results/<new-stage> --validation benchmarks/reports/validation/<new-stage> --inventory out/my-phase1-inventory --output benchmarks/reports/<new-stage>
python tools/phase1_acceptance.py benchmarks/reports/2026-10-21-phase1-gpu-memory-final/acceptance-evidence.json
```

새 `phase1_llgl` target은 public LLGL cache/resource/E/F/G/H baseline 22개다. 공통 전체
CTest는 SDL CPU profile 28개를 포함한 72개이고 source와 실제 exe의 fingerprint를 대조한다.
`--compare-sdl`은 E–H 14개에서 두 SDK의 GPU query를 모두 끈 public semantic workload
CPU 완료 구간을 비교한다. 명시적인 binding ABI 차이를 계약에 남긴다. SDL 단독 실행은
`--sdk sdl --cpu-only`를 사용한다. 미구현 SDK/profile은 원본 폴더 생성 전에 거부한다.
SDK 자체의 기술적 기능 미지원이라는 뜻은 아니다. [현재 상태](PHASE1_COMPLETION_REPORT.md)를 따른다.

이전 LLGL 전용 `report_phase1.py`는 역사 캠페인용이다. 현재 추가 비교에는 새 report
생성기를 사용하고 새 측정 폴더를 재사용하거나 과거 metadata를 수정하지 않는다.

현재 완료 게이트는 **exit 0**이다. shader/PSO application cache를 통제했으나 driver/OS
cache를 지우지 않았다. Windows D3D12 foundation 범위이며 SDK release 완료를 뜻하지 않는다.

GPU memory 도구를 빌드하고 사용자 승인된 별도 Windows 관리자 프로세스에서 전용 ETW
수집을 실행한다. 자기 session만 finally에서 종료하며 WPR/global recording과 시스템 정책을
바꾸지 않는다. ETL 수집 성공만으로 완료하지 않고 새 분석기로 손실·수명·positive control을 검사한다.

```powershell
cmake -S tools/gpu_trace -B out/gpu-trace -G "Visual Studio 17 2022" -A x64
cmake --build out/gpu-trace --config Release --parallel 1
ctest --test-dir out/gpu-trace -C Release --output-on-failure
./tools/capture_phase1_gpu_lifetime.ps1 -Output D:/source/GraphicsSDK/out/<new-capture>
python tools/analyze_gpu_memory_trace.py --capture out/<new-capture> --output benchmarks/reports/<new-memory-report>
python tools/audit_licenses.py --profile benchmarks-gpu-memory --output out/<new-memory-inventory>
```

이전 WPR 오류/관리자 취소 이력은 보존했다. 새 승인 후 전용 capture/TDH 해독·프로세스
수명 분석을 실제로 통과했다. [GPU 메모리 보고서](PHASE1_GPU_MEMORY_REPORT.md)의 범위와
raw ETL 로컬 보존 조건을 따른다. 이전 병렬 MSVC crash의 원인 미확정 이력은 그대로다.

## Phase 2 메모리 스트레스

Windows D3D12 테스트 빌드에 `gfx_memory_stress`를 포함한다. 일반 CTest는 mode별 2초 회귀 검사이며
장시간 gate를 충족하지 않는다. 별도 campaign은 hardware 900초, debug+GBV 300초, WARP 300초를
각각 30초 warmup 후 순차 실행한다. 전체 실행 동안 다른 GPU workload/benchmark/build를 겹치지 않는다.

```powershell
cmake --build out/core --config Release --parallel 1
ctest --test-dir out/core -C Release --output-on-failure
python tools/run_memory_stress.py --output out/<new-memory-campaign>
python tools/analyze_memory_stress.py out/<new-memory-campaign>/gpu.jsonl out/<new-memory-campaign>/validation.jsonl out/<new-memory-campaign>/warp.jsonl --fingerprint <SDK-buildFingerprint> --output out/<new-acceptance>.json
python tools/audit_licenses.py --profile sdk-stress --output out/<new-stress-inventory>
```

Runner는 새 폴더만 만들고 exe/library/SDK source/policy/tool source hash를 실행 전후 확인한다.
도구의 `--mode gpu|validation|warp --seconds N --warmup N --interval N --output <new.jsonl>`은
개별 실행을 지원한다. 기존 출력은 기본 거부하며 CTest만 명시적 `--replace 1`을 사용한다.
측정 중 5초 간격의 live/drained snapshot과 마지막 결과를 즉시 저장한다. 실패한 native 실행,
짧은 smoke, 누락된 footer/reference, 비정상 증가 및 기준 미달을 성공으로 바꾸지 않는다.
실패 출력은 새 경로에서 보존한 뒤 원인을 수정한다. Windows 관리자 권한이나 ETW는 필요 없다.

현재의 보존 campaign은 GBV 증가 기준을 실패해 runner/strict analyzer가 exit 1이다. runner는
실패 시 다음 mode를 시작하지 않는다. 보존된 WARP는 같은 바이너리로 위 개별 실행 명령을 사용한
별도 측정이며 supplemental manifest에 명령·PID·해시를 기록했다. 실패한 측정을 완료로 표시하지 않는다.
`report_memory_stress.py`는 실패를 포함한 **보고서 생성** 도구다. 생성 성공이 acceptance 성공을
뜻하지 않으며 `accepted=false`와 strict gate의 exit 1을 보존한다.

```powershell
cmake -S tools/memory_validation_probe -B out/memory-validation-probe -G "Visual Studio 17 2022" -A x64
cmake --build out/memory-validation-probe --config Release --parallel 1
python tools/memory_validation_probe/run.py --output out/<new-factor-probes>
python tools/report_memory_stress.py --campaign out/<complete-campaign> --output out/<new-report>
```

요인 검사는 30초짜리 세 render case와 의도적 메시지 주입 counter 계약이다. 15분/5분 gate를
대체하지 않는다. 진단 도구는 현재 `out/core/Release/GraphicsSDKCore.lib`에 연결하므로 SDK를 먼저
빌드한다. 이전 관측을 오류 집계 수정 후 source와 섞지 않도록 원래 source ZIP과 frozen input
사본을 보존했다. 이 자료의 보고서 재생성에는 `--observed-inputs <observed-inputs.json>`을 추가한다.

현재 [Vulkan Win32 출력](PHASE3_VULKAN_PRESENTATION_REPORT.md)은
`out/vulkan-presentation`, `-install`, `-consumer`, `-only`, `-portable`을 사용한다.
Required validation은 통합 57개/Vulkan-only 29개를 등록하고 통합은 보류 메모리 3개를
제외한 54개를 실행한다. 두 backend OFF는 CPU 10개다. 새 설치 소비자는
`vulkan_presentation_consumer [--validation]`이며 native Vulkan 헤더/import lib가 필요 없다.
Win32 SDK prerequisite User32와 maintenance1 완료 펜스 지원 driver가 필요하다.
SDK ZIP은 `out/GraphicsSDK-0.0.1-dev-windows-x64-vulkan-presentation.zip`이다.

Vulkan B-upload 비교는 SDK와 SDL을 다시 빌드하지 않고 별도 프로젝트를 빌드한다.

```powershell
cmake -S benchmarks/vulkan_upload -B out/vulkan-upload-final -G "Visual Studio 17 2022" -A x64
cmake --build out/vulkan-upload-final --config Release --parallel 1
ctest --test-dir out/vulkan-upload-final -C Release --output-on-failure
python tools/run_vulkan_upload.py --exe out/vulkan-upload-final/Release/vulkan_upload.exe --output benchmarks/results/your-new-vulkan-upload-run
```

The subsequent [upload-memory comparison](PHASE3_VULKAN_UPLOAD_MEMORY_REPORT.md)
uses the preserved baseline and a separate new install. Configure both
`benchmarks/vulkan_upload` and `benchmarks/vulkan_upload_profile` in separate fresh
build roots with `-DGFX_INSTALLED_SDK=<absolute-new-install-path>`; build Release
serially. `tools/compare_vulkan_upload.py` requires `--before`, `--after`,
`--profile-before`, `--profile-after` executable paths and a new `--output` directory.
After rejecting the candidate, replay against the frozen executables also needs
`--candidate-receipt benchmarks/reports/validation/2026-10-23-phase3-vulkan-upload-memory/cached-candidate/receipt.json`.
Its SDK regression build and installed-consumer commands are frozen in
[run.py](../benchmarks/reports/validation/2026-10-23-phase3-vulkan-upload-memory/verification-scripts/run.py).
Complete those GPU tests before the serial comparison. The rejected candidate was audited with
`tools/audit_vulkan.py --variant upload-memory` before production sources were restored.
Recheck frozen candidate sources/builds with `tools/audit_vulkan_upload_compare.py`;
the former command intentionally requires the candidate source tree, not current
production. Historical installs and packages are inputs,
so do not overwrite them. Linux execution remains unverified.

기본 dependency는 `out/vulkan-presentation-install`과 reviewed `out/sdl`이다. 기존 실행 폴더를
덮어쓰지 않으며 runner 결과 폴더는 새 경로여야 한다. Linux build/runtime 수용은 별도다.
[프로파일과 제한](PHASE3_VULKAN_UPLOAD_REPORT.md)을 따른다.

이 초기 runner는 high/low 선호가 서로 다른 두 hardware GPU를 선택하는 환경을 요구한다.
동일 장치로 해석되는 경우 합치거나 두 장치 비교를 합성하지 않고 실패한다.

The [Vulkan draw profile](PHASE3_VULKAN_DRAW_BENCHMARK_REPORT.md) builds standalone
from `benchmarks/vulkan_draw` against the frozen installed SDK and SDL static build.
Use a fresh build root and serial Release builds, then run its three CTests.
`tools/run_vulkan_draw.py --exe <Release/vulkan_draw.exe> --output <new-results>`
runs required-layer correctness before unvalidated measurements.
`tools/report_vulkan_draw.py --input <results> --output <new-report>` reconciles
raw output/commands and computes primary and CPU stage statistics.
The frozen license profile `tools/audit_vulkan_draw.py` checks this run's
`out/vulkan-draw-benchmark-vulkan11` compiler/linker reads, reused libraries and notices.
SDL's public Vulkan options request API 1.1; its default API 1.0 cannot accept the
SDK's supported SPIR-V 1.3 fixture version. Vendor and SDK sources remain unchanged.

The [Vulkan PSO profile](PHASE3_VULKAN_PSO_BENCHMARK_REPORT.md) builds separately from
`benchmarks/vulkan_pso` against the same frozen SDK/SDL installs. Use a fresh
build root, Release and `--parallel 1`, then run its three CTests.
`tools/run_vulkan_pso.py --exe <Release/vulkan_pso.exe> --output <new-results>`
runs required-layer correctness before measurements.
`tools/report_vulkan_pso.py --input <results> --output <new-report>` checks raw
commands/results and emits distinct creation/public-release statistics.
`tools/audit_vulkan_pso.py` audits the frozen `out/vulkan-pso-benchmark` root.
Shader assembly is not rerun; the same validated SPIR-V 1.3 and Vulkan 1.1
request are used for both SDKs. New result/report directories must not exist.

The [Vulkan allocation profile](PHASE3_VULKAN_ALLOCATION_BENCHMARK_REPORT.md) is an
isolated shaderless project using the frozen installed SDK and reviewed SDL build:

```powershell
cmake -S benchmarks/vulkan_allocation -B out/your-new-allocation-build -G "Visual Studio 17 2022" -A x64
cmake --build out/your-new-allocation-build --config Release --parallel 1
ctest --test-dir out/your-new-allocation-build -C Release --output-on-failure
python tools/run_vulkan_allocation.py --exe out/your-new-allocation-build/Release/vulkan_allocation.exe --output benchmarks/results/your-new-allocation-run
python tools/report_vulkan_allocation.py --input benchmarks/results/your-new-allocation-run --output benchmarks/reports/your-new-allocation-report
```

Use fresh build/result roots. The current audit profile uses out/vulkan-allocation-benchmark-final.
Run GPU validation and performance processes serially, separately from other GPU tests.

The [native allocation diagnostic](PHASE3_VULKAN_ALLOCATION_TRACE_REPORT.md) builds
an isolated executable, an own explicit layer DLL and two CPU controls. Use fresh roots:

```powershell
cmake -S benchmarks/vulkan_allocation_trace -B out/your-new-trace-build -G "Visual Studio 17 2022" -A x64
cmake --build out/your-new-trace-build --config Release --parallel 1
ctest --test-dir out/your-new-trace-build -C Release --output-on-failure
python tools/run_vulkan_allocation_trace.py --build out/your-new-trace-build --output benchmarks/results/your-new-trace-run
python tools/report_vulkan_allocation_trace.py --input benchmarks/results/your-new-trace-run --output benchmarks/reports/your-new-trace-report
```

The reviewed runner/audit profile uses out/vulkan-allocation-trace-verified. The runner
sets child-only VK_LAYER_PATH/VK_INSTANCE_LAYERS and expected DLL paths; it does not
install registry entries or change driver/system settings. Source SDK/SDL installs and
the pinned validation runtime are prerequisites. Diagnostic times are excluded.

The [isolated buffer-pool candidate](PHASE3_VULKAN_BUFFER_POOL_REPORT.md) is generated
by tools/prepare_vulkan_buffer_pool.py into a new out/ source root. It requires the
exact reviewed product fingerprint and unique patch anchors. The report links the
actual serial SDK/installed-consumer build, pinned VVL environment and CTest selection
excluding ^memory_reclamation_ from the start. Reproduction needs new source/build/
install/result roots and matching runner/audit paths. Never patch product/vendor files.

For the next isolated allocator candidate, use the
[adaptive study build script](../benchmarks/reports/validation/2026-10-24-phase3-vulkan-adaptive-buffer-pool/verification-scripts/build.py)
and [generator](../tools/prepare_vulkan_adaptive_buffer_pool.py). The recorded profile uses
new source/build/install/benchmark/trace roots, MSVC `--parallel 1`, child-only VVL settings,
and the deferred memory exclusion from the start. Existing product and fixed-candidate
builds are preserved. Reruns require new roots and matching runner/audit paths.

The [failure-study build script](../benchmarks/reports/validation/2026-10-24-phase3-vulkan-buffer-pool-fault/verification-scripts/build.py)
builds a new hardened copy, then links the same guarded fault client to prior and new
SDK installations. It uses MSVC serial builds, new roots, child-only VVL settings and
the deferred memory CTest exclusion from initial selection. Existing source/build/result
roots are preserved; new reruns require matching new runner/audit paths.

The [product ownership profile](VULKAN_CREATION_OWNERSHIP_REPORT.md) uses
new product and before/after consumer roots with MSVC serial builds and the deferred
memory exclusion from initial CTest selection. The recorded
[build script](../benchmarks/reports/validation/2026-10-24-vulkan-creation-ownership/verification-scripts/build.py)
builds the current product rather than a pool candidate. Historical experiment tools
pin the old SDK source identity: reproduce them from the older commit/source ZIP or
preserved original source, not by relaxing their guards against the new product.

The [loader failure consumer](VULKAN_LOADER_FAULT_REPORT.md) links the existing
before/after product installations without rebuilding or changing product sources.
Its final recorded build script is `2026-10-24-vulkan-loader-fault/verification-scripts/build-v4.py`
under benchmark validation reports; serial MSVC Release builds use fresh roots.
Pilot/failed build roots are retained separately from final evidence.

The [rebased pool experiment](VULKAN_POOL_CHURN_REPORT.md) uses new
`out/vulkan-pool-churn-source`, `-sdk`, `-install` and separate product/candidate
consumer roots. `prepare_vulkan_pool_churn.py` verifies the current product and
historical candidate before patching only the new copy. SDK and consumers build
serially with MSVC Release; the original product and old artifacts stay preserved.

The [rebased fault profile](VULKAN_POOL_REBASE_FAULT_REPORT.md) freshly builds the
unchanged historical fault project and a separate mixed-context consumer against
`out/vulkan-pool-churn-install`. Product/candidate SDKs are not rebuilt in this stage.
MSVC Release uses new roots and serial compilation; actual installed-library hashes
and linker reads distinguish these fresh runs from old candidate evidence.
