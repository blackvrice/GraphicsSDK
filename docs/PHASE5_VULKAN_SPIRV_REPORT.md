# P5-SHD-03 DXC로 생성한 SPIR-V의 Vulkan 실행

Linux x64의 오프라인 도구는 고정 DXC로 HLSL 2021 정점·픽셀 셰이더를
`vs_6_0`·`ps_6_0`으로 컴파일한다. DXC의 `-spirv -fspv-target-env=vulkan1.1`로
SPIR-V 1.3을 선택하고, `-fspv-entrypoint-name=main`으로 Vulkan SDK 진입점 규약을 맞춘다.
HLSL의 원래 진입점 이름은 `--entry`로 전달한다.
[DXC SPIR-V 옵션](https://github.com/microsoft/DirectXShaderCompiler/blob/b106a961d09221b3c5bdb37be45b679257da08b8/docs/SPIR-V.rst)

출력은 고정 소스에서 빌드한 `spirv-val --target-env vulkan1.1`과 버전·단계·진입점
검사를 통과한 뒤 공개된다. Vulkan SDK는 이 바이트를 `ShaderFormat::SPIRV`로 받고,
기존 단계·리소스·색상 출력 레이아웃 검사를 적용한다. 일반 HLSL 기능의 런타임 호환성은
Vulkan SDK가 지원하는 SPIR-V 인터페이스 범위를 따른다.
[Vulkan 인터페이스 계약](VULKAN.md) · [도구 버전 및 라이선스](PHASE5_SHADER_TOOLCHAIN_REPORT.md)

## 사용

Linux x64에서 Python, Git, CMake, Ninja와 C++ 컴파일러를 준비한다.
아래 명령은 새 출력 폴더에 검증된 `shader.spv`와 컴파일·검증 로그를 남긴다.
이미 받은 공식 DXC 아카이브가 있으면 `--dxc-archive`로 지정할 수 있다.
검증 프로필은 저장소의 단일 파일 HLSL을 기준으로 한다. 사용자 입력에 include가 있다면
해당 소스의 관리와 재현은 호출자가 담당한다.

```sh
python3 tools/vulkan_spirv.py compile \
  --source tests/fixtures/shader_toolchain/triangle.hlsl \
  --entry vertexMain --stage vert --output out/vertex-spv
```

런타임 검사는 고정 Vulkan-Headers로 SDK를 빌드·설치하고 별도 C++ 프로젝트가
`find_package`로 설치본을 사용한다. lavapipe ICD를 명시하고 실제로 로드된 공유
라이브러리를 기록한다. 이 실행은 외부 `spirv-val`과 SDK 입력 검사로 셰이더를 확인한다.
Mesa 패키지에 따라 ICD 파일명에
`x86_64`가 포함될 수 있다.

```sh
python3 tools/vulkan_spirv.py run --output out/vulkan-dxc-spirv \
  --icd /usr/share/vulkan/icd.d/lvp_icd.json
python3 tools/vulkan_spirv.py check --output out/vulkan-dxc-spirv
```

같은 모듈을 macOS MSL로 변환하는 절차는 [Metal 변환과 실행 보고서](PHASE5_METAL_SPIRV_REPORT.md)에 있다.

대조군은 잘못된 HLSL, `OpStore`에 다른 타입의 유효한 ID를 넣은 모듈, 외부 검증은 통과했으나
진입점 이름이 다른 모듈, 단계 불일치, 잘린 헤더를 구분한다. 정상 실행은 13×7 RGBA8
전체 픽셀과 행 패딩을 확인하고 D3D12 WARP의 보관된 DXIL·DXBC 이미지 둘과 바이트 단위로 비교한다.

## 보관한 결과

소스 커밋 `0d3149de22084c7235ee4d41d3e67d7668d1a421`의 Linux x64 CI에서
lavapipe가 설치 SDK로 렌더링한 364바이트 전체 이미지가 D3D12 DXIL·DXBC와 일치했다.
정점·픽셀 모듈은 고정 `spirv-val`을 통과했고, 타입이 다른 `OpStore` 대조군은
`OpStore Pointer`와 `Object` 타입 불일치 진단과 함께 거부됐다.
실행 범위는 Vulkan 검증 레이어를 끈 lavapipe 소프트웨어 ICD다.
[Vulkan CI](https://github.com/blackvrice/GraphicsSDK/actions/runs/41846592012) ·
[D3D12 결과](PHASE5_D3D12_DXIL_REPORT.md)

```sh
python3 benchmarks/reports/validation/2026-11-04-vulkan-spirv/verify.py
```
