# Shader pipeline

Metal은 별도 `ShaderFormat::MSL`의 reviewed UTF-8 source를 native compiler에 전달한다.
고정 entry는 `gfx_vertex`/`gfx_fragment`이며 pipeline 생성에서 active attribute와
argument reflection을 layout과 대조한다. 입력/binding profile과 전체 draw 근거는
[Metal graphics 보고서](PHASE4_METAL_GRAPHICS_REPORT.md)를 따른다.

Vulkan은 별도의 `ShaderFormat::SPIRV` 입력으로 외부 검증된 SPIR-V 1.3/Vulkan 1.1
`main` shader를 받는다. 초기 path는 standalone Position·VertexIndex/InstanceIndex,
Float/UNorm vertex inputs, matching float varyings와 float4 color outputs를 지원하며
별도 sampled image/immutable sampler/float4 Uniform Block binding을 지원한다.
Vulkan location은 attributes 배열 순서로, resource set 0의 binding은 layout entry 순서로,
sampler binding은 32+sampler index로 정한다. Uniform Block은 연속 float4 member만 받는다.
Vulkan depth-only layout은 fragment bytes를 생략할 수 있으며 VS Position의 Z로 D32 깊이를
공급한다. Color layout은 fragment module과 모든 color output을 요구한다. Fragment depth export는
지원하지 않는다. Depth 비교/쓰기/read-only pass는 [깊이 보고서](PHASE3_VULKAN_DEPTH_REPORT.md)를 따른다.
SDK의 framing/interface 검사는
전체 semantic validator가 아니다. 자체 fixture는 고정 로컬 SPIRV-Tools로 검증하며
이 외부 도구는 SDK에 링크·배포하지 않는다. [범위와 계약](VULKAN.md)을 따른다.
기본 `ShaderFormat`은 DXBC이며 `compileHlsl` 결과도 DXBC다. SPIR-V의 리소스는
`reflectShader`로, 진입점·단계·입출력 위치까지 포함한 인터페이스는 `reflectSpirvInterface`로
읽을 수 있다. [SPIR-V reflection 및 바인딩 검증](PHASE5_SPIRV_REFLECTION_REPORT.md)

현재 동작하는 bootstrap 경로는 Windows `D3DCompile` → SM5.1 DXBC → D3D12다.
entry point와 stage를 명시하고 최적화 level 3 및 warnings-as-errors를 사용한다.
잘못된 소스는 compiler 진단과 HRESULT를 Result에 담는다. include 파일 해석은 현재
제공하지 않는다. `D3DReflect`로 이름/register/count/space, 지원 resource 종류와 cbuffer의
최소 byte 크기를 읽고 실제 bytecode stage도 검증한다.

Windows x64의 별도 `compileHlslDxil` 경로는 HLSL 2021을 `vs_6_0` 또는 `ps_6_0` DXIL로
컴파일한다. D3D12는 Shader Model 6.0을 확인하고 DXC reflection으로 DXIL의 단계, 리소스
바인딩과 색상 출력을 검사한다. 빌드 설정과 애플리케이션 로컬 DXC DLL 준비 절차는
[D3D12 DXIL 보고서](PHASE5_D3D12_DXIL_REPORT.md)에 있다.

Phase 5 오프라인 검사는 고정 DXC로 SM 6.0 DXIL과 Vulkan 1.1용 SPIR-V 1.3을 생성하고,
SPIRV-Cross로 macOS MSL 2.1 소스를 생성한다. 각 경로의 버전, 지원 프로필, 라이선스와
재현 절차는 [도구 계약 보고서](PHASE5_SHADER_TOOLCHAIN_REPORT.md)에 있다.
Vulkan용 오프라인 컴파일 명령은 DXC로 SPIR-V 1.3을 만들고 `spirv-val`을 통과한 모듈을
`ShaderFormat::SPIRV`로 사용할 수 있게 한다. [Vulkan SPIR-V 실행 및 재현](PHASE5_VULKAN_SPIRV_REPORT.md)
리소스 바인딩 없는 Vulkan 모듈은 고정 SPIRV-Cross로 MSL 2.1로 변환한 뒤 Metal SDK에
소스로 전달할 수 있다. [Metal 변환과 실행 보고서](PHASE5_METAL_SPIRV_REPORT.md)
오프라인 DXC 명령은 선택적 컴파일 캐시를 지원한다. 설치 SDK는 같은 장치에서 그래픽스
파이프라인 객체를 재사용하는 별도 API를 제공한다.
[컴파일 캐시 계약](PHASE5_SHADER_CACHE_REPORT.md) · [파이프라인 캐시 계약](PHASE5_PIPELINE_CACHE_REPORT.md)

pipeline은 명시적 vertex layout과 graphics binding layout을 받는다. Texture2D/constant buffer의
bounded array, register space, VS/PS/All visibility 및 scalar static sampler를 지원한다.
D3D12 reflection으로 layout의 종류·범위·공간·단계를 대조하고 지원하지 않는 shader resource는
native PSO 생성 전에 Unsupported로 거부한다. 임의 reflection에서 layout을 자동 생성하지 않는다.
기존 sampledTexture 옵션은 fragment t0/s0 space0의 shorthand이며 explicit layout과 함께 쓰지 않는다.
전체 binding snapshot마다 명령 목록별 256개 shader-visible descriptor 중 새 영역을 사용하여
이미 기록한 draw의 descriptor를 덮어쓰지 않는다. 한도 초과는 실패한다.
static sampler array/comparison sampler, UAV/storage, cube/3D/MSAA/integer texture, unbounded array는
지원하지 않는다. [공개 계약](PUBLIC_API.md)과 [검증 범위](PHASE2_BINDINGS_REPORT.md)를 따른다.

Depth/MRT pipeline은 fragment output signature도 대조한다. layout의 모든 color slot에
floating-point SV_Target 출력을 요구하며 범위 초과/누락/integer 출력은 거부한다.
depth-only layout은 PS bytecode를 생략할 수 있고 VS의 SV_Position으로 깊이를 공급한다.
fragment depth/coverage/stencil export는 현재 Unsupported다. [Depth/MRT 범위](PHASE2_DEPTH_MRT_REPORT.md)를 따른다.

공통 draw 벤치마크는 같은 source를 한 번 compile하여 동일 bytecode를 세 SDK에
전달한다. compile 시간은 draw 측정 밖이다. 결과에 bytecode FNV-1a hash를 기록한다.
이 hash는 재현성 식별자이며 보안용 digest가 아니다.

오프라인 컴파일 캐시 키에는 원본 소스·전처리 결과, 고정 도구 식별자와 대상·진입점·프로필·
컴파일 인자를 포함한다. D 벤치마크에서는 컴파일 캐시의 cold/warm 상태와 드라이버 PSO
캐시 상태를 별도로 다룬다.
