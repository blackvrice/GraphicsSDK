# Phase 2 graphics 리소스 바인딩

2026-10-21. 순서상 다음 항목인 graphics binding layout과 correctness 범위를 구현했다.
다중 Texture2D/constant buffer, bounded arrays, register space와 stage visibility를 명시적으로
선언하고 전체 binding snapshot을 기록한다. **Phase 2 전체 완료는 아니다.** 다음 항목은 depth/MRT다.

## 구현과 계약

- `BindingLayout`, `validateBindingLayout`, `ResourceBindings`, `CommandList::bindResources`.
- Texture2D/ConstantBuffer 배열, slot/count/space 및 Vertex/Fragment/All visibility.
- 동일 register의 VS/PS 분리와 서로 다른 register space 지원. 겹치는 범위는 사전 거부.
- scalar static sampler의 Point/Linear, Clamp/Repeat 조합.
- typed shader reflection, 실제 bytecode stage 검사, shader 요구 cbuffer 크기 및 layout 대조.
- ConstantBuffer 논리 상태와 256-byte 정렬 view. Device/Upload, view 최대 64KiB.
- 전체 set 검증 뒤 descriptor 할당. 실패한 rebind는 이전 binding과 descriptor 용량을 보존.
- 기록된 draw마다 이전 descriptor 영역과 자원 수명 보존. draw 시 상태 재검사, submit version 검사.
- 기존 `sampledTexture`/`bindTexture` 경로 유지. explicit layout과 shorthand의 혼용은 거부.

구조 검증은 backend 없이 실행한다. 최대 32 entries/256 descriptors, static samplers 16개,
stage당 textures 128개/CBV 14개의 보수적인 profile이다. register 상한과 전체 지원 계약은
[공개 API](PUBLIC_API.md)에 정의했다. 자원 바이트를 snapshot으로 복사하지는 않는다.
shader의 동적 array index가 범위 안에 드는 것은 호출자 계약이다.

native root table의 range/space/visibility 구성은 Microsoft의
[root signature 계약](https://learn.microsoft.com/en-us/windows/win32/direct3d12/creating-a-root-signature),
[HLSL register/array 계약](https://learn.microsoft.com/en-us/windows/win32/direct3d12/resource-binding-in-hlsl),
[descriptor/CBV 계약](https://learn.microsoft.com/en-us/windows/win32/direct3d12/creating-descriptors)을 따른다.
첫 탐색에서 static sampler 두 개로 sampler array를 표현하면 native PSO 호환성 검사가
실패함을 확인했다. 최종 API는 scalar static sampler를 제공하며 shader sampler array와
comparison sampler를 Unsupported로 사전 거부하는 회귀 검사를 포함한다.

## 실제 검증

| 검사 | 결과 |
|---|---|
| Core CTest | 12/12, skip 없음 |
| D3D12 OFF | Windows 별도 빌드 및 CPU CTest 3/3 |
| 하드웨어 / debug+GBV / WARP | 각 8개 전체 이미지 CPU 참조. debug+GBV ERROR/CORRUPTION 0 |
| 공통 benchmark correctness | 최신 SDK core를 연결해 72/72 |
| Python | 56/56 |
| 설치 consumer | 새 헤더·layout 함수·capability·pipeline·CBV binding·submit/wait 통과 |
| SDK 패키지/의존성 | 새 SDK ZIP, SDK와 공통 benchmark inventory 및 재검사 보존 |

새 binding executable은 하드웨어/WARP에서 각각 36,989 assertions, debug+GBV에서
36,990 assertions를 통과했다. portable layout executable의 부정 계약 검사는 19 assertions다.

각 GPU mode에서 Upload/Device 상수 버퍼 × RGBA8/RGBA16F sampled textures의 4가지 구성으로
64×32 화면 전체를 검사한다. VS/PS가 동일 t3/b3 space1을 각기 다른 자원으로 사용하고,
PS texture array 두 개와 다른 space의 t3, sampler 두 개를 동시에 읽는다. left/right draw 사이
다중 자원과 CBV offset을 바꾸고 입력 vector 순서도 뒤집는다. 앞선 draw의 descriptor가
덮어써지거나 layout index를 vector 위치로 해석하면 전체 적색/녹색 참조 검사가 실패한다.

추가 4개 16×16 구성은 constant-buffer array와 All visibility, Point/Clamp, Point/Repeat,
Linear/Clamp 및 Linear/Repeat 경계의 실제 샘플링을 검사한다. linear 결과만 채널별 tolerance 1을
허용하고 나머지는 byte 단위 일치다. GPU mode당 총 9,216 pixels, RGBA 36,864개 채널을 검사한다.

잘못된 enum/범위/중복/단계/공간, shader-layout 불일치, 누락·중복 array element, null/foreign
자원과 thread 위반, CBV 정렬/overflow/bounds/최소 shader 크기를 거부한다. 64KiB CBV도
수용한다. 6 descriptors/set에서 42회의 snapshot이 들어가고 다음 set은 원자적으로 거부된다.
실패한 rebind 뒤 실제 draw 결과와 state 변경 뒤 draw 거부도 확인한다. pipeline 설정 및
begin은 binding을 비운다. recording 후 상수 버퍼 Upload write는 submit을 무효화한다.
성공한 submit 뒤 resource/pipeline/command wrapper를 해제한 다음 fence를 기다려 전체
이미지를 읽어 native 수명 보존을 검사했다.

[원본 로그](../benchmarks/reports/validation/2026-10-21-phase2-bindings/)와
[해시·재현 목록](../benchmarks/reports/2026-10-21-phase2-bindings/evidence.json)을 보존한다.
새 shader/API의 설치 consumer는 라이브러리와 함께 다시 빌드했다. API/virtual 함수가 바뀐
0.0.1 개발 버전이며 과거 바이너리와의 ABI 호환을 보장하지 않는다.

## 산출물과 남은 범위

로컬 SDK ZIP은 `out/GraphicsSDK-0.0.1-dev-windows-x64-bindings.zip`이다. 최초 SDK ZIP,
vertex/index 단계 ZIP, 모든 과거 inventory·측정·소스 archive를 보존한다. 신규 외부 dependency와
competitor 변경은 없다. 새 compiler-read inventory와 notices는 각각
`licenses/generated/windows-sdk-bindings-release`,
`licenses/generated/windows-benchmarks-bindings-release`에 있다. 이 검사는 빌드 입력과 package
내용의 확인이며 포괄적인 재배포 법적 승인을 뜻하지 않는다.
SDK inventory는 자체 compiler-read 입력 17개/구성요소 1개, 공통 benchmark 확장 closure는
975개/22개다. 두 inventory와 SDK ZIP 검사 후 `--check`로 변경 없음도 확인했다.

새 성능 측정이나 우위 주장은 하지 않았다. 이전 성능 결과는 당시 source fingerprint에 속한다.
Vulkan/Metal/native Linux, UAV/storage/compute, dynamic sampler와 sampler array, comparison sampler,
unbounded resource array, cube/3D/MSAA/integer texture는 이번 구현 범위 밖이다.
depth/MRT, device-loss 복구, 장기 stress, interactive presentation acceptance와 remote CI도 남아 있다.
