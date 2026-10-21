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

CommandList는 transition, copy, 단일 color render pass, pipeline, texture/vertex/index binding,
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
