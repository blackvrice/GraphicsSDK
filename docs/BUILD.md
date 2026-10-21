# Build

Windows 검증 환경은 Visual Studio 2022 17.14, MSVC 19.44, Windows SDK
10.0.26100.0, x64, CMake 3.25 이상이다. C++23 `std::expected` 지원이 필요하다.

```powershell
cmake --preset windows
cmake --build --preset windows-release
ctest --preset windows-release
cmake --install out/core --config Release --prefix out/install
cmake -S tests/consumer -B out/consumer -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=D:/source/GraphicsSDK/out/install
cmake --build out/consumer --config Release
./out/consumer/Release/consumer.exe
./out/core/Release/gfx_triangle.exe out/triangle.ppm
```

SDK 자체에는 다운로드가 없다. D3D12를 끈 빌드는 GPU backend를 제공하지 않으며
Unsupported 반환과 portable 헤더/코어 계약만 검증한다. Linux/macOS 실기기 빌드는
현재 미검증이다. `portable-core` preset은 Ninja 및 C++23 toolchain이 필요하다.

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
