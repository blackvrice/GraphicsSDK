# P5-SHD-04 SPIR-V에서 Metal MSL로 변환

오프라인 도구는 Vulkan 1.1용 SPIR-V 1.3의 정점 또는 픽셀 모듈을 macOS MSL 2.1
소스로 변환한다. `spirv-val`로 의미 검증하고 고정 SPIRV-Cross의 reflection으로 변환
프로필을 검사한다. 입력 진입점은 `main`, 출력 진입점은 각각 `gfx_vertex`와
`gfx_fragment`다. 도구의 소스 커밋과 라이선스는 [도구 계약 보고서](PHASE5_SHADER_TOOLCHAIN_REPORT.md)에 있다.
[SPIRV-Cross](https://github.com/KhronosGroup/SPIRV-Cross/tree/d8e3e2b141b8c8a167b2e3984736a6baacff316c)

초기 프로필은 정점 속성과 사용자 정의 보간 값, 버퍼·텍스처·샘플러가 없는 정점·픽셀
셰이더를 대상으로 한다. 정점은 위치를 출력하며 정점·인스턴스 ID를 사용할 수 있다.
픽셀은 위치 0의 단일 `float4` 색상을
출력한다. descriptor를 Metal의 슬롯에 매핑하는 일반 변환은 이 프로필에서 다루지 않는다.
유효한 텍스처·샘플러 모듈로 이 제한의 거부 동작을 검증한다.

## 실행

Python, Git, CMake, Ninja, C++ 컴파일러가 있는 Linux 또는 macOS에서 아래 명령을 실행한다.
검증과 변환이 성공하면 새 출력 폴더에 `shader.metal`을 생성한다. Metal SDK를 사용하는
애플리케이션은 이 UTF-8 소스를 해당 단계의 `ShaderFormat::MSL`로 전달한다.

```sh
python3 tools/metal_spirv.py convert --module out/vertex-spv/shader.spv \
  --stage vert --output out/vertex-msl
```

macOS에서는 Xcode Metal 컴파일러의 `-std=macos-metal2.1`로 MSL을 AIR와 metallib로
만드는 검사도 수행한다.
설치 SDK를 링크한 별도 C++ 소비자는 같은 MSL 소스를 런타임에서 컴파일하고 Metal draw를
실행한다. 두 컴파일 경로 모두 잘못된 MSL에 대해 실제 진단을 확인한다.
AIR와 metallib는 오프라인 컴파일 확인에 사용하고 해시를 기록한다.
[Apple 오프라인 셰이더 컴파일](https://developer.apple.com/documentation/metal/building-a-shader-library-by-precompiling-source-files)

```sh
python3 tools/metal_spirv.py run --output out/metal-spirv
python3 tools/metal_spirv.py check --output out/metal-spirv
```

변환 입력은 [Vulkan에서 실행한 SPIR-V 원본](PHASE5_VULKAN_SPIRV_REPORT.md)이다.
런타임 검사에서는 13×7 RGBA8의 전체 픽셀을 Vulkan·D3D12 실행 결과와 바이트 단위로
비교한다. GPU 복사 결과의 행 패딩은 별도로 검사한다.

## 보관한 결과

소스 커밋 `bcb5b22dbf0949dde362d87e9d577f14398e7104`의 GitHub macOS ARM64와 Intel
러너에서 두 진입점을 각각 컴파일하고 AIR 두 개와 metallib 하나를 생성했다.
설치 SDK로 실행한 이미지의 픽셀 364바이트가 Vulkan과 D3D12 DXIL·DXBC 모두와
일치했고, 행 패딩 1,428바이트가 보존됐다. 유효한 리소스 포함 모듈은 변환이 거부됐고
잘못된 MSL은 오프라인과 SDK 런타임에서 모두 `missingSymbol`을 진단했다.
실행 범위는 GitHub 가상 Metal 장치다.
[macOS ARM64·Intel CI](https://github.com/blackvrice/GraphicsSDK/actions/runs/41849454696)

보관한 두 아키텍처의 원본 SPIR-V, 생성 MSL, 진단, 픽셀과 패딩을 다른 OS에서도
검증할 수 있다. [보관 목록](../benchmarks/reports/validation/2026-11-04-metal-spirv/summary.json)

```sh
python3 benchmarks/reports/validation/2026-11-04-metal-spirv/verify.py
```
