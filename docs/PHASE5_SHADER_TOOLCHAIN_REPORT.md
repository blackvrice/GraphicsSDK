# P5-SHD-01 오프라인 셰이더 도구 계약

검사는 저장소의 단일 HLSL 2021 파일에 있는 정점·픽셀 진입점을 각각 컴파일한다.
`vs_6_0`과 `ps_6_0`에 `-HV 2021 -O3 -WX -Ges`를 사용한다. 검증 입력은 외부
include·define·리소스 바인딩이 없는 전체 화면 삼각형이며, 이 프로필에서 검증한 범위는
아래 표와 같다.

| 출력 | 컴파일·검증 환경 | 프로필과 진입점 | 검증 |
| --- | --- | --- | --- |
| DXIL | Windows x64, Linux x64 | SM 6.0 / DXIL 1.0, `vertexMain`·`pixelMain` | DXC, Microsoft `dxv`, DXIL 컨테이너 프로그램 파트 확인 |
| SPIR-V | Linux x64 | Vulkan 1.1 / SPIR-V 1.3, `vertexMain`·`pixelMain` | DXC `-fspv-target-env=vulkan1.1`, `spirv-val --target-env vulkan1.1`, 바이너리 헤더 확인 |
| MSL 소스 | Linux x64 | macOS / MSL 2.1, `gfx_vertex`·`gfx_fragment` | 검증된 SPIR-V를 SPIRV-Cross `--msl-version 20100`으로 변환하고 단계·진입점 확인 |

컴파일러는 선언되지 않은 식별자를 진단하고 DXIL 출력을 생성하지 않아야 한다.
Linux에서는 같은 SPIR-V 1.3 모듈을 Vulkan 1.0 대상으로 검증해 버전 진단과 실패를 확인한다.
이 검사 범위는 파일 생성과 검증이다. D3D12 참조 렌더링은
[P5-SHD-02 보고서](PHASE5_D3D12_DXIL_REPORT.md)에 있다. Vulkan 참조 렌더링은
[P5-SHD-03 보고서](PHASE5_VULKAN_SPIRV_REPORT.md)에 있다. Apple MSL 컴파일과 Metal 실행은
`P5-SHD-04`에서 다룬다.

## 고정 버전과 라이선스

| 도구 | 버전·소스 | 라이선스 기록 |
| --- | --- | --- |
| DXC·DXIL 검증기 | 공식 `v1.8.2505.1`의 x64 ZIP·tar.gz. Windows 보고값 `1.8.2505.32 (b106a961d)`, Linux 보고값 `1.9(dev;4950-b106a961)` | 운영체제별 배포본의 Microsoft 사용 조건, LLVM/NCSA 고지, HLSL 헤더 고지, Windows MIT 고지. [Microsoft 배포본](https://github.com/microsoft/DirectXShaderCompiler/releases/tag/v1.8.2505.1) |
| SPIRV-Cross | `vulkan-sdk-1.4.321.0`의 `d8e3e2b141b8c8a167b2e3984736a6baacff316c` | Apache-2.0. [고정 소스](https://github.com/KhronosGroup/SPIRV-Cross/tree/d8e3e2b141b8c8a167b2e3984736a6baacff316c) |
| SPIRV-Tools·SPIRV-Headers | `spirv-val` 보고값 v2024.3. 기존 Vulkan 검증 입력과 같은 `0cfe9e7219148716dfd30b37f4d21753f098707a`·`2acb319af38d43be3ea76bfabf3998e5281d8d12` | Tools Apache-2.0, Headers MIT. [Tools 고지](https://github.com/KhronosGroup/SPIRV-Tools/blob/0cfe9e7219148716dfd30b37f4d21753f098707a/LICENSE), [Headers 고지](https://github.com/KhronosGroup/SPIRV-Headers/blob/2acb319af38d43be3ea76bfabf3998e5281d8d12/LICENSE) |

DXC 아카이브는 압축 해제 전에 SHA-256을 검사한다. Khronos 도구는 고정 소스에서
CLI를 빌드한다. 검증 결과에는 명령, 종료 코드, 진단, 도구 바이너리 해시와 원본 고지를 담는다.
도구는 오프라인 개발 의존성으로만 사용하며 SDK 빌드와 설치 패키지는 이 도구들을 링크하거나 배포하지 않는다.
[도구 잠금 파일](../tools/shader-toolchain.lock.json)

## 실행

Windows x64에서는 Python과 Git을, Linux x64에서는 C++ 컴파일러·CMake·Ninja도 사용한다.
다운로드와 빌드는 새 임시 디렉터리에서 수행한다. 이미 받은 공식 DXC 배포본이 있으면
`--dxc-archive`로 지정해 같은 해시 검사에 사용할 수 있다.

```sh
python tools/shader_toolchain.py run --output out/shader-toolchain --jobs 3
python tools/shader_toolchain.py check --output out/shader-toolchain
```

`check`는 기록된 파일 전체와 셰이더 출력의 바이트를 다른 운영체제에서도 검증한다.

## 보관한 결과

소스 커밋 `9fa879f1518bf51433ae508f587f69495e861f4a`에서 Windows x64의 DXIL 2개와
Linux x64의 DXIL·SPIR-V·MSL 각 2개를 검증했다. 두 CI의 원본 receipt와 출력,
검증 실패 대조군, 고지 및 해시를 보관했다. 도구 설치 없이 두 결과를 다시 검사할 수 있다.
[CI 실행](https://github.com/blackvrice/GraphicsSDK/actions/runs/41839597216),
[보관 목록](../benchmarks/reports/validation/2026-11-04-shader-toolchain/summary.json)

```sh
python benchmarks/reports/validation/2026-11-04-shader-toolchain/verify.py
```
