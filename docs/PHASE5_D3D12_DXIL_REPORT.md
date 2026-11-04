# P5-SHD-02 D3D12 DXIL

Windows x64의 `compileHlslDxil(source, entry, stage)`는 고정 DXC로 HLSL 2021을
SM 6.0 DXIL로 컴파일하고 `ShaderFormat::DXIL`을 반환한다. 정점·픽셀 단계는 각각
`vs_6_0`·`ps_6_0`를 사용한다. 최적화 level 3, strict mode, warnings-as-errors를 적용한다.
컴파일 실패 시 DXC의 파일명·오류 위치·진단과 HRESULT가 `Result`에 포함된다.
입력은 단일 UTF-8 소스이며 외부 include는 지원 범위에서 제외한다.
`compileHlsl`은 기존 SM5.1 DXBC 경로다.

D3D12 파이프라인은 DXIL 사용 시 장치의 Shader Model 6.0 지원을 확인한다. DXIL은
`IDxcUtils::CreateReflection`으로, DXBC는 `D3DReflect`로 reflection을 읽고 공통 코드가
셰이더 단계, 리소스의 레지스터·공간·범위와 색상 첨부 출력을 확인한다.
[Microsoft DXIL reflection API](https://learn.microsoft.com/en-us/windows/win32/api/dxcapi/nf-dxcapi-idxcutils-createreflection)

## 빌드와 실행

공식 DXC ZIP의 버전과 SHA-256은 [도구 계약 보고서](PHASE5_SHADER_TOOLCHAIN_REPORT.md)에 있다.
SDK를 구성할 때 이 배포본의 `dxcapi.h`가 있는 `inc` 디렉터리를 지정한다.
실행 파일 옆에 같은 배포본 `bin/x64`의 `dxcompiler.dll`과 `dxil.dll`을 둔다.
SDK 설치 파일에는 DXC 바이너리와 헤더가 포함되지 않는다. DXC를 활성화하지 않은 SDK나
앱 로컬 DXC를 찾을 수 없는 프로세스에서 DXIL 요청은 `Unsupported`를 반환한다.

```powershell
cmake -S . -B out/dxil-sdk -G "Visual Studio 17 2022" -A x64 -DGFX_ENABLE_D3D12=ON "-DGFX_DXC_INCLUDE_DIR=$PWD/out/pinned-dxc/inc"
cmake --build out/dxil-sdk --config Release
```

설치 소비자 검사는 고정 ZIP을 받아 해시를 검증한 뒤 SDK를 빌드·설치한다.
별도 C++ 프로젝트가 설치된 `GraphicsSDK::Core`를 링크하고 WARP에서 DXIL·DXBC를 실행한다.
이미 받은 공식 ZIP은 `--dxc-archive`로 지정할 수 있다.

```powershell
python tools/d3d12_dxil.py run --output out/d3d12-dxil
python tools/d3d12_dxil.py check --output out/d3d12-dxil
```

독립 참조는 13×7 RGBA8 이미지 두 장과 readback 행의 패딩을 검증한다. 대조군은 앱 로컬 DLL
부재, 잘못된 HLSL, include, 손상된 DXIL, 단계 불일치, 누락된 상수 버퍼 및 잘못된
`SV_Target`을 검사한다. 컴파일된 정점·픽셀 파일은 Microsoft `dxv`로 각각 검증한다.
실행 검증 범위는 Windows x64의 D3D12 WARP 소프트웨어 렌더러다.

## 보관한 결과

소스 커밋 `6f0159a81a410898ba277e8b2621dbe29b9584bb`의 GitHub Windows x64 러너에서
Microsoft Basic Render Driver가 두 파이프라인을 실행했다. 각각 364바이트인 RGBA8 이미지가
참조와 일치했다. 진단에는 `GraphicsSDK.hlsl:1:39`와 `missingSymbol`이 기록됐고,
별도로 실행한 `dxv`는 두 DXIL 모듈의 검증에 성공했다.
[Windows WARP CI](https://github.com/blackvrice/GraphicsSDK/actions/runs/41842406398)

원본 이미지, 셰이더, 실행 로그와 파일별 SHA-256을 보관했다.
[보관 목록](../benchmarks/reports/validation/2026-11-04-d3d12-dxil/summary.json)

```sh
python benchmarks/reports/validation/2026-11-04-d3d12-dxil/verify.py
```
