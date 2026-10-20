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

CommandList는 transition, copy, 단일 color render pass, pipeline, texture binding,
scissor, draw/instancing, debug marker, 2개 timestamp 슬롯을 지원한다.
GPU timestamp read는 resolve 후 submit fence 완료 시에만 가능하다.

ShaderBinary는 stage와 바이트 배열이다. compileHlsl/reflectShader는 Windows bootstrap
DXBC 경로이며 [SHADER_PIPELINE](SHADER_PIPELINE.md)의 한계를 따른다.
caps는 실제 구현 범위를 나타낸다. backendCompiled는 컴파일 여부이며 런타임 가용성을
보장하지 않는다. Result 실패를 확인하는 전체 예제는 `examples/Triangle.cpp`에 있다.
