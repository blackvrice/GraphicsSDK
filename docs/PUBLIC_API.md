# Public API

진입점은 `<graphics/Graphics.hpp>`, namespace는 `gfx`, CMake target은
`GraphicsSDK::Core`다. Windows 및 D3D12 타입은 헤더에 노출되지 않는다.

```cpp
auto result = gfx::createDevice({gfx::Backend::D3D12, true});
if (!result) { /* result.error().message / code / nativeCode */ }
auto device = std::move(result.value());
auto texture = device->createTexture({1920,1080,gfx::Format::RGBA8UNorm});
```

Device는 장치 정보/기능, 자원/파이프라인/명령 목록/스왑체인 생성과 queue 접근을 제공한다.
queue는 submit 결과로 Fence를 반환하고 wait/waitIdle/completedValue를 제공한다.
GPU 비동기 완료를 명시적으로 기다린 뒤 readback을 읽는다.

`queue().wait(fence, timeout)`는 생성 thread 전용이다. D3D12의 유효 timeout은
0..4,294,967,294ms이며, 음수·INFINITE(4,294,967,295ms)·그보다 큰 값은 이미 완료된
펜스에서도 `InvalidArgument`다. 0ms는 완료 여부를 조회하고 미완료면 `Timeout`을 반환하며
이벤트 생성/등록이나 GPU 완료 대기를 하지 않는다. 완료된 제출의 소유권 정리는 수행할 수 있다.
미제출 값은 `InvalidArgument`, 다른 장치/세대의 펜스는 `WrongDevice`다.

제한 시간 초과는 제출 취소나 자원 해제가 아니다. 같은 펜스 또는 더 이른 펜스를 다시 기다릴
수 있으며 실제 완료 전까지 native 제출 소유권을 유지한다. 양수 대기는 generation에 이벤트
하나를 재사용하고 아직 미완료인 native 등록은 하나만 유지한다. 단일 증가 timeline의 다음
미완료 값을 등록하므로 중간 완료에서 깨어날 수 있지만 요청 값과 원래 deadline을 계속 확인한다.
native 호출/스케줄링 시간까지 포함하는 hard real-time 상한은 아니다.
건강한 같은 세대에서는 timeout을 검증하고, 관측된 손실/퇴역은 원래 원인의 `DeviceLost`로
반환한다. 다른 장치 펜스의 `WrongDevice` 검사는 기존과 같이 generation 상태 검사보다 먼저다.
[펜스 대기 수정과 검증 범위](PHASE2_FENCE_WAIT_REPORT.md)를 따른다.

Texture::footprint는 buffer-texture copy의 총 바이트, aligned rowPitch,
유효 rowBytes 및 높이를 반환한다. 행 padding을 이미지 데이터로 해석하지 않는다.
Buffer::write는 Upload, Buffer::read는 Readback 메모리에만 허용한다.

CommandList는 transition, copy, color/depth render pass, pipeline, resource/vertex/index binding,
scissor, draw/indexed draw/instancing, debug marker, 2개 timestamp 슬롯을 지원한다.
GPU timestamp read는 resolve 후 submit fence 완료 시에만 가능하다.

Vulkan `debugMarker(text)`는 debug-utils 확장과 함수가 있을 때 실제 label을 넣는다.
정상 recording에서 함수가 없으면 Unsupported이며 creating thread·recording·퇴역 검사를 먼저 한다.
지원 시 빈 문자열은 허용하고, NUL-free UTF-8과 UINT32_MAX 이하 길이를 요구한다. Substring·임시
문자열은 native 호출 동안 null-terminated 복사본으로 유지한다. Render pass 안팎에서 호출할 수 있다.
[실제 label·미지원·상태/수명 근거](PHASE3_VULKAN_MARKER_REPORT.md)를 따른다.

Vulkan도 두 slot을 지원한다. Slot 0은 TOP_OF_PIPE, 1은 BOTTOM_OF_PIPE이며 각 한 번 기록하고
pass 밖에서 resolve한다. 완료된 fence와 두 query availability를 확인한 후 native GPU 값을
valid-bit modular 차이와 timestampPeriod로 변환한다. Begin은 이전 결과를 비우고 성공한
read는 캐시한다. 역순 slot과 한 counter cycle 이상 지난 첫 read는 InvalidState다.
CPU 시간은 wrap 모호성의 보수적 상한 확인에만 사용하고 GPU 결과를 대신하지 않는다.
지원 여부·수명·범위는 [Vulkan 타임스탬프 계약](VULKAN.md)을 따른다.

transition/copy의 validation 거부는 명령 목록의 기존 상태를 유지하며 새 자원 참조나
observed-version snapshot을 추가하지 않는다. `copyBuffer`, `copyBufferToTexture`,
`copyTextureToBuffer`는 메모리/범위·양쪽의 논리 상태를 확인한 뒤에만 자원을 기록한다.
논리 상태는 앞서 같은 명령 목록에서 수락한 transition을 포함한다. Upload/Readback heap의
지원하지 않는 transition도 참조를 보유하기 전에 거부한다. 기존에 정상 사용한 자원과 그
version 검사는 유지하므로, 정상 기록 뒤의 upload 변경은 여전히 submit을 실패시킨다.
실패한 요청의 인자에만 등장한 자원은 caller 참조 해제 뒤 회수할 수 있다.
[복사 거부 원자성 보고서](PHASE2_COPY_ATOMICITY_REPORT.md)를 따른다.

ShaderBinary는 stage와 바이트 배열이다. compileHlsl/reflectShader는 Windows bootstrap
DXBC 경로이며 [SHADER_PIPELINE](SHADER_PIPELINE.md)의 한계를 따른다.
caps는 실제 구현 범위를 나타낸다. backendCompiled는 컴파일 여부이며 런타임 가용성을
보장하지 않는다. Result 실패를 확인하는 전체 예제는 `examples/Triangle.cpp`에 있다.

## 어댑터 조회와 명시적 선택

`enumerateAdapters(Backend::D3D12)`는 매 호출마다 새 DXGI 후보 목록을 반환한다.
하드웨어는 OS high-performance preference 순서이며 WARP가 가용하면 마지막에 포함한다.
`AdapterInfo`는 backend-scoped `AdapterId`, UTF-8 이름, software 여부, vendor/device ID,
dedicated video/shared system memory 용량을 제공한다. 용량은 현재 사용량·budget·peak가 아니다.
열거는 native Device를 생성하거나 feature-level probe·validation 설정 변경을 하지 않는다.
`createDevice`가 실제 D3D12 feature level 11.0 지원과 생성 성공을 검사한다. 미컴파일 backend의
열거는 Unsupported다.

```cpp
auto adapters = gfx::enumerateAdapters();
if (adapters && !adapters->empty()) {
    const auto& selected = adapters->front(); // Application chooses a candidate.
    gfx::DeviceDesc desc;
    desc.adapter = selected.id;
    desc.softwareAdapter = selected.softwareAdapter;
    auto device = gfx::createDevice(desc);
    if (device) { /* device.value()->caps().adapterId == selected.id */ }
}
```

`DeviceDesc::adapter`가 있으면 정확한 ID를 선택하며 다른 GPU로 자동 대체하지 않는다.
ID의 backend가 descriptor와 다르거나 softwareAdapter 값이 선택 대상과 다르면 InvalidArgument다.
해당 ID를 찾지 못하면 Unsupported이며 native 원인도 보존한다. 값 0도 명시적 ID다.
옵션이 없으면 기존 하드웨어 선호/WARP 정책을 유지한다. `caps().adapterId`는 실제 native
Device의 GetAdapterLuid 결과에서 만든 식별자이며 선택된 DXGI 어댑터와 일치하는지 검사한다.

D3D12 ID는 LUID의 비트 표현이다. 열거 index 또는 재부팅을 넘어 영속적인 하드웨어 serial이
아니다. topology 변경·재부팅 뒤 목록을 새로 조회한다. 명시적 ID가 있는 recover는 같은 ID만
재시도하며, 없어졌다고 다른 GPU를 선택하지 않는다. ID가 없는 D3D12 recover는 기존 선택 정책을
다시 적용한다. Vulkan recover는 최초 자동 선택도 실제 선택한 UUID 기반 ID로 고정한다.
외부 native owner가 제거된 D3D12 singleton을 보유하는 동안에는 복구가 DeviceLost로
실패할 수 있고 owner 해제 뒤 같은 ID로 재시도한다.

공개 구조체가 확장되었으므로 정적 SDK 소비자를 새 헤더·라이브러리로 함께 재빌드한다.
초기 0.0.1 API/ABI 동결은 아직 없다. 실제 장치 분리/TDR 승인은 별도이며
[선택·복구 검증 보고서](PHASE2_ADAPTER_REPORT.md)를 따른다.

## Vertex/index 입력

`GraphicsPipelineDesc::vertexLayout`에 buffer slot/stride/rate와 semantic/index/format/slot/offset을
선언한다. 빈 layout은 기존 `SV_VertexID` shader를 유지한다. `validateVertexLayout`은 GPU 없이
구조를 검사하며 pipeline 생성도 이를 호출한다. 32개 slot/attribute와 stride 1–2048 bytes를
지원한다. attribute가 stride 안에 들어가야 하고 float 입력의 offset/stride는 4-byte 정렬이다.
semantic은 대소문자 구분 없이 중복을 거부하고 ASCII 식별자와 별도 semanticIndex를 사용한다.
system-value `SV_` semantic은 layout에 선언하지 않는다.

`VertexFormat`은 Float/Float2/Float3/Float4/UNorm8x4다. `VertexInputRate::Instance`는 instance당
한 record를 소비한다. 같은 slot의 attributes는 한 stride/rate를 공유한다.

```cpp
gfx::GraphicsPipelineDesc desc{vertexShader, fragmentShader};
desc.vertexLayout = {
    {{0, 12, gfx::VertexInputRate::Vertex}},
    {{"POSITION", 0, gfx::VertexFormat::Float3, 0, 0}}
};
```

`bindVertexBuffer(slot, buffer, stride, offset=0, size=0)`과
`bindIndexBuffer(buffer, IndexFormat::UInt16/UInt32, offset=0, size=0)`는 recording 안에서
pass 전/중에 호출한다. size=0은 buffer의 나머지 bytes이며 view는 1..UINT32_MAX bytes다.
index view의 offset/size는 element 크기로 정렬한다. vertex stride는 현재 pipeline 선언과
맞아야 하고 실제 attribute extent와 base-address 정렬도 draw 전에 검사한다.
Readback memory는 input으로 사용할 수 없다. binding 실패는 앞선 정상 binding을 바꾸지 않는다.

버퍼는 `VertexBuffer`/`IndexBuffer` 상태로 명시적으로 transition한다. 한 버퍼를 동시에 두
역할로 쓰면 `VertexAndIndexBuffer`를 사용한다. Upload heap의 native state는 GENERIC_READ로
유지하면서 SDK의 논리 상태를 바꾼다. 다시 copy source로 쓰려면 CopySource로 transition한다.
Device memory는 실제 barrier를 기록한다. Texture에는 이 세 상태를 허용하지 않는다.

`draw(vertices, instances=1, firstVertex=0, firstInstance=0)`와
`drawIndexed(indices, instances=1, firstIndex=0, baseVertex=0, firstInstance=0)`를 지원한다.
vertex/instance의 직접 접근 범위와 index view의 요청 window를 검사한다. indexed draw의
**실제 index 값 + baseVertex가 유효한 vertex record를 가리키는 것은 호출자 계약**이다.
GPU buffer 내용을 CPU로 읽어서 검사하지 않는다. 유효한 음수 baseVertex도 지원한다.

pipeline 전환 뒤 stride/required slot을 다시 검사하고 draw 시점의 buffer state도 확인한다.
`begin()`은 입력 binding을 비운다. bound buffer는 제출 version 검사와 fence 수명 보존에
참여하므로 recording 후 Upload write는 submit을 거부하며, submit 후 wrapper/command를
해제해도 GPU 완료까지 native resource가 유지된다. caps.vertexInput/indexedDrawing은 D3D12에서
true다. 새 virtual 함수/descriptor는 0.0.1 개발 API 확장이므로 consumer는 새 헤더·라이브러리로
함께 재빌드한다. 실행 가능한 전체 예제는 `examples/IndexedQuads.cpp`다.

## Graphics resource binding

`GraphicsPipelineDesc::bindingLayout`은 Texture2D/ConstantBuffer entry와 scalar static sampler를
선언한다. entry의 slot/count/space는 HLSL `register(tN/bN, spaceM)`의 연속 범위이며
visibility는 Vertex/Fragment/All이다. 같은 종류·공간·단계에서 범위 중복은 거부한다.
VS 전용과 PS 전용 선언은 같은 register를 서로 다른 자원으로 사용할 수 있다.
`validateBindingLayout`은 backend 없이 동작하며 pipeline 생성은 shader reflection도 대조한다.
layout에 사용하지 않는 entry가 있어도 되지만 전체 entry의 모든 element를 채워야 한다.

```cpp
gfx::GraphicsPipelineDesc desc{vertexShader, fragmentShader};
desc.bindingLayout = {
    {{gfx::BindingType::Texture2D, 3, 2, 1, gfx::ShaderVisibility::Fragment},
     {gfx::BindingType::ConstantBuffer, 0, 1, 2, gfx::ShaderVisibility::All}},
    {{2, 1, gfx::ShaderVisibility::Fragment, gfx::SamplerFilter::Point,
      gfx::SamplerAddress::Repeat}}
};
// HLSL: Texture2D<float4> images[2] : register(t3, space1);
//       cbuffer Params : register(b0, space2) { float4 color; }
//       SamplerState sampleImage : register(s2, space1);
// Create pipeline, begin commands, transition textures and constants, set pipeline.
gfx::ResourceBindings bindings{
    {{0, 0, firstTexture}, {0, 1, secondTexture}},
    {{1, 0, constants, 256, 256}}
};
auto bound = commands->bindResources(bindings);
```

TextureBinding/ConstantBufferBinding의 `binding`은 register 번호가 아닌 **layout.entries의 index**다.
element는 해당 entry의 array index다. 모든 element를 정확히 한 번 제공해야 한다.
원소 순서는 자유다. snapshot 전체를 검증한 후 descriptor를 기록하므로 실패하면 이전 정상
binding을 보존하고 descriptor 용량을 소비하지 않는다. 부분 갱신은 제공하지 않는다.

현재 보수적인 graphics profile은 최대 32 entries, layout 전체 256 descriptors, static samplers
16개다. 각 stage에서 textures 128개와 CBV 14개까지 허용하며 공간 전체에 걸쳐 합산한다.
slot 범위는 t0..127, b0..13, s0..15, space는 0..0xffffffef다.
sampler는 Point/Linear 및 Clamp/Repeat이고 모든 UVW 축에 같은 address mode를 적용한다.
Texture2D는 현재 공개 texture의 RGBA8UNorm/RGBA16Float 형식이며 단일 mip/layer다.

Texture는 ShaderRead, 상수 버퍼는 ConstantBuffer로 transition한다. ShaderRead는 VS/PS 양쪽의
native read 상태다. ConstantBuffer는 Device/Upload에서 허용하며 offset/size가 256-byte 정렬이고
size는 256..65536 bytes다. buffer 범위와 shader가 요구하는 cbuffer 크기를 모두 검사한다.
D3D12 Upload의 native GENERIC_READ는 유지한다. 이 논리 상태를 vertex/index 상태와 동시에 사용하지 않는다.
backbuffer sampling과 Readback CBV는 거부한다. shader array index 범위 준수는 호출자 계약이다.

명령 목록의 descriptor 용량은 256개다. bindResources 호출마다 전체 layout 크기만큼 소비하고,
이전에 기록한 draw의 descriptor는 fence 완료까지 보존한다. 한도에 도달하면 새 명령 목록으로
나눈다. bindResources는 begin 이후 pipeline 설정 뒤 pass 전/중에 호출할 수 있다.
pipeline 설정과 begin은 바인딩을 비운다. draw는 자원 상태를 재확인하고, 사용한 자원은
제출 version 검사 및 fence 수명 보존에 참여한다. recording 뒤 Upload write는 submit을 거부한다.
snapshot은 descriptor와 자원 참조를 보존하며 buffer 내용의 복사본을 만들지는 않는다.

기존 `sampledTexture=true`와 `bindTexture`는 fragment t0/s0 space0, Linear/Clamp 하나를 유지한다.
이 shorthand와 explicit bindingLayout을 함께 지정하면 실패한다. bindResources도 legacy pipeline에
사용할 수 있다. 빈 layout의 resource-free draw는 별도 binding 호출 없이 사용할 수 있다.

caps.bindingLayouts는 D3D12와 opt-in Vulkan에서 true다. D3D12 shader stage spoof/missing/type/space/count/visibility
불일치는 pipeline 생성 시 거부한다. UAV/storage/compute, dynamic sampler 및 sampler array,
comparison sampler, unbounded array, cube/3D/MSAA/integer texture는 Unsupported다.
D3D12 지원 resource 종류는 reflectShader의 type, cbuffer 크기는 minimumBytes로 확인할 수 있다.
새 virtual 함수와 descriptor 변경이 있으므로 개발 버전 consumer는 헤더·라이브러리를 함께 재빌드한다.

Vulkan의 [별도 계약](VULKAN.md)은 set 0의 binding i를 layout entry i에, sampler binding
32+i를 static sampler i에 매핑한다. 외부 검증된 SPIR-V 1.3/Vulkan 1.1과 별도 sampled image,
연속 float4 Uniform Block만 지원하며 block minimum과 visibility/count를 검사한다.
Vulkan은 active pass에서 pipeline을 설정하고 각 새 pass와 setPipeline이 snapshot을 비운다.
미초기화 texture는 거부하고 native descriptor/상수 alignment 한도도 적용한다.
Recording당 resource descriptor 256개에 더해 snapshot 최대 256개이며 일반 SPIR-V reflection은
제공하지 않는다. 이번 Vulkan 구현 추가는 공개 API 형태나 ABI를 바꾸지 않는다.

## Depth/MRT와 render pass

`Format::D32Float`은 `TextureDesc::depthStencil=true`로 생성하는 단일 mip/layer 깊이 형식이다.
renderTarget은 false여야 한다. RGBA8UNorm/RGBA16Float은 color 형식이며 depthStencil을
설정하지 않는다. `validateTextureDescription`은 이 계약과 1..16384 extent를 GPU 없이 검사한다.
D32 buffer copy/upload/readback은 기존 footprint/rowPitch 계약을 사용한다. sampling은 지원하지 않는다.

기존 pipeline의 colorFormat은 단일 color layout이다. 선택형 `renderTargets`를 설정하면
colorFormat 대신 해당 layout을 사용한다. 최대 8개 colors를 서로 다른 color 형식으로 구성할 수 있고,
depth에는 D32Float 하나를 지정한다. colors가 비어 있고 depth가 있는 depth-only layout도 가능하다.
`DepthState`의 testEnabled/writeEnabled/compare는 pipeline에 고정된다. writeEnabled는
testEnabled가 필요하고 둘 중 하나를 켜면 depth 형식이 있어야 한다. CompareOp는 Never/Less/Equal/
LessEqual/Greater/NotEqual/GreaterEqual/Always다. `validateRenderTargets`가 구조를 검사한다.

```cpp
auto z = device->createTexture({width, height, gfx::Format::D32Float, false, true});
gfx::GraphicsPipelineDesc desc{vertexShader, twoOutputFragmentShader};
desc.renderTargets = gfx::RenderTargetLayout{
    {gfx::Format::RGBA8UNorm, gfx::Format::RGBA16Float}, gfx::Format::D32Float
};
desc.depthState = {true, true, gfx::CompareOp::Less};
// Create pipeline, begin commands and transition color0/color1 to RenderTarget, z to DepthWrite.
gfx::RenderPassDesc pass{
    {{color0, gfx::LoadOp::Clear, {0,0,0,1}},
     {color1, gfx::LoadOp::Clear, {0,0,0,1}}},
    {z.value(), gfx::LoadOp::Clear, 1.0f, false}
};
auto begun = commands->beginRenderPass(pass);
```

fragment shader는 모든 color slot에 대응하는 floating-point `SV_Target0..N-1` 출력을 제공해야 한다.
누락·범위 초과·integer 출력은 pipeline 생성 전에 거부한다. 현재 fragment depth/coverage/stencil
export는 Unsupported다. depth-only pipeline은 fragment bytecode를 생략할 수 있다.
깊이 값은 VS의 SV_Position에서 공급한다. `alphaBlend`는 모든 color target에 같은 blend를 적용한다.
개별 target별 독립 blend/mask 설정은 아직 없다.

`beginRenderPass(RenderPassDesc)`는 최소 color 하나 또는 depth 하나를 받는다. attachment는 같은
장치·같은 width/height이고 서로 중복되지 않아야 한다. color는 renderTarget usage와 RenderTarget
상태가 필요하다. depth는 depthStencil usage와 DepthWrite 상태가 필요하다.
DepthAttachment::readOnly=true면 DepthRead 상태와 Load를 사용하며 draw의 depth write를 거부한다.
Clear는 색/깊이를 초기화하고 Load는 기존 내용을 유지한다. 모든 attachment는 end 뒤에도 저장한다.
Load의 내용은 호출자가 이전 clear/draw/copy로 초기화해야 하며, 아직 기록하지 않은 내용을 자동
정의하지 않는다. clear color는 유한 값, depth clear는 유한한 0..1이어야 한다.

`validateRenderPass`는 attachment usage/크기/중복/load/clear를 검사한다. beginRenderPass는
추가로 device/state를 모두 확인한 다음에만 clear/bind를 기록하므로 실패한 요청이 일부 attachment를
변경하지 않는다. viewport/scissor는 pass 전체 extent로 초기화한다. pass 중 transition/copy/nested pass,
pass를 닫지 않은 command end는 거부한다. endRenderPass 뒤에도 pipeline/binding은 유지된다.
draw는 pipeline의 color count/순서별 format, depth 유무/format, read-only access를 확인한다.

기존 `beginRenderPass(texture, clear)`는 depth 없는 단일 color Clear pass의 shorthand다.
depth texture는 Common/CopySource/CopyDestination/DepthWrite/DepthRead 상태를 사용하고,
color texture·buffer에는 DepthWrite/DepthRead를 허용하지 않는다. D32의 ShaderRead/Present/
RenderTarget도 거부한다. 모든 attachment는 기존 submit version 검사와 fence retention에 참여한다.
caps.depthAttachment/multipleRenderTargets는 D3D12에서 true다. Vulkan은 실제 D32 attachment/transfer
조합과 native color attachment 한도를 조회해 표시하며 같은 깊이 비교·쓰기를 지원한다.
Vulkan depth-only pipeline도 fragment bytes를 생략할 수 있고 clip Position의 Z를 깊이로 사용한다.
깊이 ShaderRead/stencil/fragment depth export는 지원하지 않는다. 이번 추가는 공개 API 형태/ABI를
바꾸지 않는다. 기존의 새 virtual 함수와 descriptor를 적용하는
개발 버전 consumer는 다시 빌드해야 한다. Stencil, MSAA/resolve, discard/store 선택, subpass는 미지원이다.

## 장치 손실과 복구

`Device::checkStatus()`는 장치가 정상인지 확인한다. 손실을 관찰하면 DeviceLost와 nativeCode를
저장하고 이후 이 세대의 native 조작은 같은 원인으로 실패한다. begin/submit/wait, 자원 생성·CPU
접근 및 present/resize에서도 상태를 확인한다. native API 실패 역시 실제 제거 원인을 확인하고
손실을 저장한다. command recording은 매 draw마다 driver를 poll하지 않으며, 아직 관찰하지 않은
비동기 손실은 checkStatus/begin/submit/wait 등 상태 확인 지점에서 발견한다.

```cpp
auto status = device->checkStatus();
if (!status && status.error().code == gfx::ErrorCode::DeviceLost) {
    auto recovered = device->recover();
    if (recovered) {
        device = std::move(recovered.value());
        // Rebuild buffers/textures/pipelines/swapchains from application CPU data.
    }
}
```

`recover()`는 손실된 native 세대를 퇴역시키고 새 Device를 반환한다. D3D12는 같은 DeviceDesc
선택 정책을, Vulkan은 최초 실제 선택한 adapter ID와 software/validation 설정을 유지한다.
이전 CPU wrapper를 보관해도 native 자원·descriptor·PSO/root·명령·swapchain과 pending batch를
해제한다. 약한 참조 등록은 wrapper를 영구 소유하지 않으며, 퇴역 중에는 전체 snapshot을 먼저
강하게 고정해 다른 자원의 해제로 인한 수명 문제를 막는다. D3D12 lost device에서는 GPU 완료를
기다리지 않는다. Vulkan은 native device-idle 종료 후 child를 정리하며, 손실 장치의 wait는
손실 결과로 종료될 수 있다. instance/surface 등 정리 부모는 이전 wrapper 해제까지 남을 수 있다.
새 장치는 새 resource/fence identity를 사용한다. 예전 wrapper의 description/footprint/caps는 읽을
수 있지만 조작은 계속 DeviceLost이고 새 장치에서 사용하면 WrongDevice다.
shader bytecode와 애플리케이션 CPU asset data는 재사용할 수 있다. 자원 내용은 자동 복원하지 않는다.

정상 장치의 recover는 InvalidState이며 기존 자원을 버리지 않는다. 성공한 이전 세대의 두 번째
recover도 InvalidState다. 새 장치 생성에 실패하면 이전 세대는 손실·퇴역 상태로 남고 recover를
재시도할 수 있다. D3D12는 같은 process/adapter의 native device를 공유하므로 다른 SDK Device나
외부 native owner가 제거된 장치를 계속 보유하면 재생성을 막을 수 있다. 다른 owner를 정리한 뒤
재시도한다. recover는 해당 Device 세대만 정리한다. 다른 장치나 외부 라이브러리를 대신 폐기하지 않는다.
hardware 선택을 WARP로 자동 전환하지 않으며 adapter 연결 변화 때 같은 물리 adapter의 복귀를 보장하지 않는다.

`DeviceDesc::enableDeviceLossTesting`의 기본값은 false다. 명시적으로 켠 테스트 장치만
`removeForTesting()`을 호출할 수 있다. 이 함수는 성공한 호출의 Result<void>를 반환하면서
D3D12 runtime의 실제 RemoveDevice로 장치를 제거한다. 다음 checkStatus는 DeviceLost다.
driver TDR/무한 GPU 작업이나 시스템 정책 변경을 사용하지 않는다. 같은 process/adapter의 다른
native owner도 영향을 받을 수 있으므로 독립 테스트 process에서 사용한다. 기본 설정의 호출은
Unsupported다. caps.deviceRecovery는 복구 구현 여부, deviceLossInjection은 native injection
interface 가용성으로 opt-in 여부와 별개다. RemoveDevice 검증 환경은 Windows build 20348 이상이다.
세 API는 장치 생성 thread에서 호출한다. 새 virtual 함수와 descriptor 때문에 기존 consumer는 재빌드해야 한다.

`tests/consumer/recovery.cpp`는 설치된 SDK만으로 제거 전후 전체 이미지와 stale buffer 거부를 검증한다.
Native RemoveDevice와 hidden-window 재표시는 예상치 못한 driver TDR/물리 hot-unplug,
장시간 leak stress 또는 interactive presentation 검증을 대체하지 않는다.

Vulkan도 `caps.deviceRecovery=true`이며 관찰된 native 손실에서만 복구한다. 실제 선택한 ID가
사라지면 다른 GPU로 fallback하지 않는다. public `deviceLossInjection`은 false이고 정상
Vulkan device의 testing opt-in 생성·`removeForTesting()`는 Unsupported다. 설치 SDK의
`tests/vulkan_recovery` 소비자는 별도 합성 layer로 submit/poll/fence 손실과 OOM/손실 재생성 실패,
재시도, 이전 handle/fence 거부 및 CPU 입력 기반 전체 이미지·버퍼 재구축을 검증한다.
[Vulkan 복구 보고서](PHASE3_VULKAN_RECOVERY_REPORT.md)의 합성 제어와 실제 TDR 수용을 구분한다.

## 자원 보유 진단

`Device::resourceStatistics()`는 생성 thread에서 호출하는 진단용 snapshot이다. 완료된 제출과
만료된 weak registry 항목을 먼저 정리하지만 GPU 완료를 기다리지는 않는다. 잘못된 thread는
InvalidState, 손실되거나 퇴역한 세대는 저장된 원인의 DeviceLost를 반환한다.

- `trackedObjects`: 살아 있는 buffer/texture/pipeline/native command payload/swapchain payload의 수.
- `uniqueResources`: wrapper, 녹화된 command 또는 미완료 제출 batch가 보유한 고유 native resource 수.
  wrapper/command/batch의 중복 참조는 한 번만 센다. command의 timestamp readback도 포함한다.
- `resourceAllocationBytes`: 각 고유 resource의 D3D12 GetResourceAllocationInfo 크기의 합.
  현재 구현은 committed resources다. driver/OS cache, descriptor heap, PSO, 실제 residency나
  총 VRAM 사용량을 뜻하지 않는다. OS process memory와 DXGI 사용량은 별도 관측값이다.
- `pendingSubmissions`, `pendingObjectReferences`: 완료 정리 후 남은 제출 batch와 그 COM 참조 수.
  참조 수는 고유 객체 수가 아니며 resource 외 allocator/list/heap/query/PSO 등을 포함한다.
  Vulkan은 recording/resource use/pass/pipeline 보유 항목 수이며 descriptor set/pool은
  recording 내부에 포함된다. Timestamp query pool은 별도 보유 항목 하나로 센다.
  Vulkan query pool은 Buffer/Image allocation bytes에 포함하지 않는다.
  두 backend의 수치를 native 객체 수로 직접 비교하지 않는다.

자원 wrapper를 지워도 녹화되거나 제출된 command가 native resource를 보유할 수 있다.
관련 command와 자원 wrapper를 모두 해제하고 `queue().waitIdle()`을 마친 뒤 조회하면,
다른 장기 보유 자원이 없는 장치의 다섯 값은 모두 0이다. 실제 GPU가 이미 끝났으면 조회 시
미완료 batch가 0일 수도 있다. 이 API는 traversal/COM query/임시 메모리를 사용하므로 draw마다
호출하는 경량 성능 카운터로 취급하지 않는다. 새 virtual 함수로 개발 버전 consumer는 재빌드한다.

스트레스 도구의 생성·제출·해제 및 전체 readback 계약은
[메모리 검증 보고서](PHASE2_MEMORY_STRESS_REPORT.md)를 따른다.

`validationErrorCount()`는 이 Device의 unfiltered native 메시지 콜백 등록 이후 발생한
ERROR/CORRUPTION의 누적 snapshot이다. native text queue가 가득 차거나 storage/retrieval
filter가 설정되거나 외부 코드가 ClearStoredMessages를 호출해도 집계는 사라지지 않는다.
SDK가 대기열을 지우거나 무제한으로 늘리지는 않는다. validation=true 장치는
ID3D12InfoQueue1이 필요하며 미지원이면 생성이 Unsupported다. validation=false의 조회는
Unsupported, 손실 세대의 조회는 원인이 보존된 DeviceLost다. GPU 작업 검증에는 완료 대기 뒤
조회한다. callback은 다른 thread에서도 호출될 수 있어 원자 카운터만 갱신하며 D3D API를
호출하지 않는다. 퇴역·파괴는 진행 중인 callback의 종료까지 기다리는 native unregister를 수행한다.

같은 native 장치를 공유하는 SDK Device도 각자의 등록 시점부터 독립적으로 집계한다.
이는 native device 전체의 메시지를 관측하므로 다른 owner가 유발한 오류도 포함될 수 있다.
외부 native 인터페이스는 SDK 공개 API로 제공하지 않는다. 독립 진단 도구만 같은 process/adapter의
native singleton을 관측하여 intentional error/overflow/filter/clear/thread/owner 수명 계약을 검사한다.

## OS 메모리 사용량·예산 snapshot

```cpp
auto snapshot = device->queryMemoryBudget(gfx::MemorySegment::Local);
if (snapshot) {
    const auto& b = *snapshot;
    // This is a current OS process/adapter observation. It can change immediately.
    auto overBudget = b.usageBytes > b.budgetBytes;
    (void)overBudget;
} else {
    // Preserve Unsupported/native failures; a failed query is not a zero reading.
}
```

`MemorySegment::Local`과 `NonLocal`을 각각 조회한다. D3D12 구현은 실제 device의 LUID로
DXGI adapter를 찾아 node 0을 조회한다. 반환 `MemoryBudget`에는 `adapterId`, `segment`와
네 가지 byte 값 `budgetBytes`, `usageBytes`, `availableForReservationBytes`, `reservationBytes`가
있다. mapping은 [DXGI 계약](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_4/ns-dxgi1_4-dxgi_query_video_memory_info)을
따른다. budget은 OS가 정한 process 목표치이며 usage가 budget보다 커질 수 있다.
available-for-reservation은 budget−usage나 빈 VRAM이 아니고, reservation은 OS working-set
힌트다. 이 API는 예약량·자원·residency 정책을 바꾸지 않는다.

사용량은 이 process가 해당 adapter에서 보유한 다른 device/native owner도 포함할 수 있다.
`resourceStatistics()`의 SDK 보유 allocation 합, 물리 VRAM 총량, free memory, process private
commit 또는 연속 peak와 다르다. UMA/WARP/nonlocal의 성공한 0 값도 그대로 반환하며, 실패를
0으로 합성하지 않는다. query/interface가 미지원이면 `Unsupported`와 native code를 반환한다.
그 밖의 native 실패도 보존한다. 두 segment의 호출은 서로 다른 시점의 관측이다.

creating thread 전용이다. query는 GPU 완료 대기, fence signal, SDK 객체 회수나 GPU 자원 할당을
요청하지 않는다. 상태를 확인한 뒤 segment를 검증하므로 잘못된 enum은 건강한 장치에서
`InvalidArgument`, 다른 thread는 `InvalidState`, 손실/퇴역 세대는 보존된 원인의 `DeviceLost`다.
정확한 ID로 복구한 새 device에서 다시 조회한다. 새 virtual API이므로 개발 버전 소비자는
재빌드한다. 이 API의 기능 검증은 장시간 메모리 plateau 합격을 뜻하지 않는다.

## Win32 창 루프에서 SwapChain 사용

[`examples/presentation`](../examples/presentation/README.md)는 설치된 SDK로 빌드하는 실제
창 예제다. creating thread에서 메시지를 처리하며 WM_SIZE는 client pixel 크기만 기록한다.
최소화·0 크기·숨김은 렌더링을 중지하고 메시지를 기다린다. 다음 활성 frame에서 resize를
처리하기 전에 acquired TexturePtr와 해당 backbuffer를 기록한 CommandList를 해제한다.
프레임은 RenderTarget → Present 상태 전환 후 제출·present·fence wait 순서로 진행한다.

acquire가 반환한 TexturePtr를 보유해도, 명시적인 texture 입력은 해당 swapchain이 살아 있고
그 texture가 현재 native backbuffer일 때만 허용된다. 다른 버퍼로 회전했거나 swapchain을
파괴했다면 texture transition/render attachment/copy와 해당 texture를 기록한 명령의 제출을
`InvalidState`로 거부한다. 거부된 submit은 native 실행·fence signal·resource state commit을
수행하지 않는다. 다른 device/generation은 기존 `WrongDevice`, 손실/퇴역은 `DeviceLost`가
먼저 적용된다. Texture의 CPU description/footprint는 swapchain 파괴 뒤에도 읽을 수 있다.

성공한 present는 제시한 backbuffer의 version을 변경한다. 따라서 present 전에 기록한
미제출 명령은 버퍼 순환으로 동일 texture가 다시 current가 되어도 새로 기록해야 한다.
보유한 TexturePtr 자체는 동일 버퍼가 다시 current가 되면 새 명령에 사용할 수 있다.
다른 swapchain의 present/파괴는 이 version이나 유효성을 바꾸지 않는다. 정상 frame의
submit → present 흐름과 backbuffer/command 참조를 해제한 뒤 resize하는 규칙은 유지한다.
[백버퍼 수명 보고서](PHASE2_SWAP_GUARD_REPORT.md)를 따른다.

`resize(0,0)`은 SDK에서 InvalidArgument이며 최소화를 표현하는 API가 아니다. 기존 swapchain을
유지하고 양수 client 크기가 복원될 때까지 대기한다. 창 닫기는 GPU 자원 해제 뒤 HWND를
파괴한다. 정상 present 성공은 실제 monitor scanout/사용자 육안 승인을 의미하지 않는다.
현재 SDK는 DXGI occlusion 상태를 별도 노출하지 않는다. 예제의 전체 이미지 readback과
controlled RemoveDevice 복구는 [별도 자동 검증 범위](PHASE2_PRESENTATION_REPORT.md)다.

Vulkan Win32 SwapChain은 기존 API 형태로 제공하며 `caps.swapChain`은 maintenance1 출력 자원
완료 펜스가 있는 경우에만 true다. 선택 queue의 실제 surface 지원은 생성 시 확인한다. 창의
client extent를 먼저 바꾸고 backbuffer wrapper/recording을 해제한 뒤 resize한다. 잘못된
크기·남은 참조는 기존 chain을 보존하지만 native 재생성이 실패하면 resize 재시도가 필요하다.
RGBA8 UNORM만 사용하고 VSync FIFO 또는 비동기 IMMEDIATE/MAILBOX를 선택한다. acquire와
present는 host 대기가 있는 초기 구현이며 present 완료 펜스는 모니터 표시 완료를 뜻하지 않는다.
현재 Intel driver의 해당 feature 미지원은 Unsupported로 반환한다. 공개 API/ABI 모양은 같다.
[Vulkan 상세 계약](VULKAN.md#win32-presentation)과 [검증 범위](PHASE3_VULKAN_PRESENTATION_REPORT.md)를 따른다.

현재 D3D12/Vulkan의 capability별 조건, 포맷·동기화·미지원 API와 후속 검사 ID는
[Phase 3 지원 범위표](PHASE3_SCOPE.md)에 정리한다. `DeviceCaps`에는 debug marker와
OS memory budget 지원 필드가 없으며 해당 API의 Result로 지원 여부를 구분한다.
정확한 Windows GPU/driver 출력 조합과 fallback 결정은
[Windows Vulkan 지원 정책](PHASE3_WINDOWS_SUPPORT.md)에 기록한다.
