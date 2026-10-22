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

Texture::footprint는 buffer-texture copy의 총 바이트, aligned rowPitch,
유효 rowBytes 및 높이를 반환한다. 행 padding을 이미지 데이터로 해석하지 않는다.
Buffer::write는 Upload, Buffer::read는 Readback 메모리에만 허용한다.

CommandList는 transition, copy, color/depth render pass, pipeline, resource/vertex/index binding,
scissor, draw/indexed draw/instancing, debug marker, 2개 timestamp 슬롯을 지원한다.
GPU timestamp read는 resolve 후 submit fence 완료 시에만 가능하다.

ShaderBinary는 stage와 바이트 배열이다. compileHlsl/reflectShader는 Windows bootstrap
DXBC 경로이며 [SHADER_PIPELINE](SHADER_PIPELINE.md)의 한계를 따른다.
caps는 실제 구현 범위를 나타낸다. backendCompiled는 컴파일 여부이며 런타임 가용성을
보장하지 않는다. Result 실패를 확인하는 전체 예제는 `examples/Triangle.cpp`에 있다.

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
Upload의 native GENERIC_READ는 유지한다. 이 논리 상태를 vertex/index 상태와 동시에 사용하지 않는다.
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

caps.bindingLayouts는 현재 D3D12에서 true다. shader stage spoof/missing/type/space/count/visibility
불일치는 pipeline 생성 시 거부한다. UAV/storage/compute, dynamic sampler 및 sampler array,
comparison sampler, unbounded array, cube/3D/MSAA/integer texture는 Unsupported다.
지원 resource 종류는 reflectShader의 type, cbuffer 크기는 minimumBytes로 확인할 수 있다.
새 virtual 함수와 descriptor 변경이 있으므로 개발 버전 consumer는 헤더·라이브러리를 함께 재빌드한다.

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
caps.depthAttachment/multipleRenderTargets는 D3D12에서 true다. 새 virtual 함수와 descriptor로
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

`recover()`는 손실된 native 세대를 퇴역시키고 같은 DeviceDesc 선택 정책으로 새 Device를 반환한다.
이전 CPU wrapper를 보관해도 native 자원·descriptor·PSO/root·명령·swapchain과 pending batch를
해제한다. 약한 참조 등록은 wrapper를 영구 소유하지 않으며, 퇴역 중에는 전체 snapshot을 먼저
강하게 고정해 다른 자원의 해제로 인한 수명 문제를 막는다. lost device에서는 GPU 완료를 기다리지 않는다.
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
