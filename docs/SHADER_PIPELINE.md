# Shader pipeline

Vulkan은 별도의 `ShaderFormat::SPIRV` 입력으로 외부 검증된 SPIR-V 1.3/Vulkan 1.1
`main` shader를 받는다. 초기 path는 standalone Position·VertexIndex/InstanceIndex와
float4 color outputs, 무바인딩 triangle draw만 지원한다. SDK의 framing/interface 검사는
전체 semantic validator가 아니다. 자체 fixture는 고정 로컬 SPIRV-Tools로 검증하며
이 외부 도구는 SDK에 링크·배포하지 않는다. [범위와 계약](VULKAN.md)을 따른다.
기본 `ShaderFormat`은 DXBC이며 compileHlsl 결과도 DXBC다. Public reflectShader의
SPIRV 요청은 Unsupported다. ShaderBinary의 trailing format 필드 추가로 소비자를 재빌드해야 한다.

현재 동작하는 bootstrap 경로는 Windows `D3DCompile` → SM5.1 DXBC → D3D12다.
entry point와 stage를 명시하고 최적화 level 3 및 warnings-as-errors를 사용한다.
잘못된 소스는 compiler 진단과 HRESULT를 Result에 담는다. include 파일 해석은 현재
제공하지 않는다. `D3DReflect`로 이름/register/count/space, 지원 resource 종류와 cbuffer의
최소 byte 크기를 읽고 실제 bytecode stage도 검증한다.

목표 경로인 DXC → DXIL/SPIR-V → SPIRV-Cross → MSL은 아직 구현하지 않았다.
DXC 설치 유무와 이 경로의 동작 여부는 별도이며, 현재 bootstrap을 Phase 5 완료로
표시하지 않는다. compile cache 및 pipeline cache도 아직 없다.

pipeline은 명시적 vertex layout과 graphics binding layout을 받는다. Texture2D/constant buffer의
bounded array, register space, VS/PS/All visibility 및 scalar static sampler를 지원한다.
reflection으로 layout의 종류·범위·공간·단계를 대조하고 지원하지 않는 shader resource는
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

추후 compile cache key에는 source/include 전체 content, compiler version,
entry/stage/profile/flags/defines와 target을 포함한다. disk cache의 cold/warm 상태와
driver PSO cache 상태를 구분해 D 벤치마크에서 검증해야 한다.
