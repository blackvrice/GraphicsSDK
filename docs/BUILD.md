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

SDK 코어를 포함하지 않는 경쟁 SDK 전용 suite는
`cmake -S benchmarks -B out/bench-independent -A x64 -DGFX_BENCH_WITH_SDK=OFF`로 구성한다.
이 설정은 GraphicsSDK 소스/라이브러리 target을 사용하지 않는다.

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

현재 자동화는 Windows SDK 및 고정 competitor 빌드와 CTest/Python/외부 소비자 검증을
지원한다. GPU 성능 측정 중 다른 빌드나 GPU 부하를 실행하지 않는다.
