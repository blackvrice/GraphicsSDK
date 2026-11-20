# Phase 9 Compute Shader · Compute Pipeline · Storage Buffer/Texture(UAV) · Dispatch 보고서 (`P9-CMP-01`)

- 작성일: 2026-11-20
- 기준 커밋: `5383a566` (`Ignore .idea directory in .gitignore`, 직전 기능 커밋 `b309379e`)
- 설치 SDK 라이브러리 SHA-256 (`out/p9-compute-install/lib/GraphicsSDKCore.lib`): `13099ff5ee4e774dc2c3e3973fdf3839415d2a1b30e0bd02a73ee30d171c19b5`
- 완료 ID: `P9-CMP-01` (API · CPU/리플렉션 · D3D12 · Windows Vulkan 구현 및 실기/Validation/WARP·손실복구/설치 소비자 검증)
- 분리 후속 ID: `P9-CMP-03` (Linux Vulkan 및 macOS Metal CI/실기 실행 검증)

## 1. 공개 API 및 계약

기존 13개 공개 헤더(`include/graphics/`)와 5개 `GraphicsSDKCore` 번역 단위 구성을 유지하며 Compute·UAV 계약을 확장했다.

- [`include/graphics/Types.hpp`](../include/graphics/Types.hpp):
  - `ResourceState::UnorderedAccess` 추가.
  - `BufferDesc::storage` (`Memory::Device` 전용) 및 `TextureDesc::storage` (비깊이 컬러 포맷 전용) 추가.
  - `FormatSupport::storage` 추가(어댑터의 typed UAV / storage image 지원 여부).
  - `DeviceCaps::compute`를 D3D12·Vulkan·Metal 모두 `true`로 갱신.
- [`include/graphics/Shader.hpp`](../include/graphics/Shader.hpp), [`src/shader/Shader.cpp`](../src/shader/Shader.cpp), [`src/backends/vulkan/Spirv.hpp`](../src/backends/vulkan/Spirv.hpp):
  - `ShaderStage::Compute`, `ShaderResourceType::StorageBuffer`, `ShaderResourceType::StorageTexture2D` 추가.
  - HLSL `compileHlsl(..., Compute)` (`cs_5_1`), `compileHlslDxil(..., Compute)` (`cs_6_0`), `reflectShader` (DXBC/DXIL/SPIR-V 1.3 `GLCompute`), Metal MSL `gfx_compute` 엔트리 포인트 지원.
- [`include/graphics/Bindings.hpp`](../include/graphics/Bindings.hpp), [`src/foundation/Bindings.cpp`](../src/foundation/Bindings.cpp):
  - `BindingType::StorageBuffer`, `BindingType::StorageTexture2D`, `ShaderVisibility::Compute` 추가.
  - `StorageBufferBinding` (256바이트 정렬 오프셋, 4바이트 배수 크기), `StorageTextureBinding` (`texture` 또는 `TextureViewUsage::Storage` `view`, 동시 지정 거부) 추가.
- [`include/graphics/Pipeline.hpp`](../include/graphics/Pipeline.hpp), [`include/graphics/Device.hpp`](../include/graphics/Device.hpp), [`include/graphics/CommandList.hpp`](../include/graphics/CommandList.hpp):
  - `ComputePipelineDesc`, `Device::createComputePipeline`, `CommandList::setComputePipeline`, `bindComputeResources`, `dispatch(x, y, z)`, `uavBarrier(BufferPtr)`, `uavBarrier(TexturePtr)` 추가.
- [`src/foundation/RenderPass.cpp`](../src/foundation/RenderPass.cpp):
  - `TextureViewUsage::Storage`를 `TextureDesc::storage == true` 컬러 텍스처에서 활성화하고 비스토리지 텍스처에서는 `InvalidArgument`로 거부.

## 2. 백엔드별 구현 요약

- **D3D12 (`src/backends/d3d12/D3D12.cpp`)**: `D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS`, `DXGI_FORMAT_R32_TYPELESS` Raw Buffer UAV(`RWByteAddressBuffer`), Typed Texture2D UAV(`RWTexture2D`), `CreateComputePipelineState`, `SetComputeRootSignature`/`SetComputeRootDescriptorTable`, `Dispatch`, `D3D12_RESOURCE_BARRIER_TYPE_UAV` 구현.
- **Vulkan (`src/backends/vulkan/Vulkan.cpp`, `Spirv.hpp`)**: `vkCreateComputePipelines`·`vkCmdDispatch` 로더 추가, SPIR-V 1.3 `GLCompute` / `LocalSize` / `StorageBuffer` / `OpTypeImage(sampled=2)` 리플렉션·검증, `VK_BUFFER_USAGE_STORAGE_BUFFER_BIT`, `VK_IMAGE_USAGE_STORAGE_BIT`, `VK_IMAGE_LAYOUT_GENERAL`, `VK_DESCRIPTOR_TYPE_STORAGE_BUFFER`/`STORAGE_IMAGE`, Compute→Compute `uavBarrier` 및 Compute→Graphics `UnorderedAccess -> ShaderRead` 동기화 배리어 구현.
- **Metal (`src/backends/metal/Metal.cpp`, `MetalGraphics.hpp`)**: `MTLTextureUsageShaderWrite`, `gfx_compute` `MTLComputePipelineState`, `OperationKind::Compute` (`MTLComputeCommandEncoder`, `dispatchThreadgroups:threadsPerThreadgroup:`), `uavBarrier` 및 `caps.compute = true` 구현. (실제 macOS 실행 검증은 `P9-CMP-03`에서 수행)

## 3. 실제 빌드 및 검증 결과 (Windows)

- **SPIR-V 1.3 Compute 픽스처 검증** (`out/vulkan-draw-shader-tools/Release/spirv_fixture_tool.exe`, Khronos SPIRV-Tools):
  - `buffer_scale_add`: `PASS Vulkan1.1 assembly and validation words=195`
  - `texture_generate`: `PASS Vulkan1.1 assembly and validation words=237`
- **환경**: MSVC 19.44.35228.0 / Windows SDK 10.0.26100.0 Release (`out/p9-compute`, `--parallel 1`, 경고 0 · 오류 0)
- **CPU 계약 5종**: `core_contracts` (3), `render_pass_contracts` (66), `binding_layout_contracts` (19), `spirv_interface_contracts` (181), `spirv_binding_contracts` (73) 모두 PASS.
- **물리 GPU 2종**: NVIDIA GeForce GTX 1660 SUPER, Intel UHD Graphics 770 / **소프트웨어**: WARP / **고정 VVL**: VVL 1.3.290 (core+sync)

| 실행 대상 | 어댑터 수 | Assertions | Dispatches | 검증 Images | Rejections | Validation 오류 | 결과 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `gfx_compute_tests --d3d12` | 2 | 234 | 10 | 4 | 46 | N/A | PASS |
| `gfx_compute_tests --d3d12 --validation` (Debug+GBV) | 2 | 238 | 10 | 4 | 46 | **0** | PASS |
| `gfx_compute_tests --d3d12 --warp` (+손실/복구) | 1 | 156 | 6 | 2 | 25 | N/A | PASS |
| `gfx_compute_tests --vulkan` | 2 | 226 | 10 | 4 | 46 | N/A | PASS |
| `gfx_compute_tests --vulkan --validation` (VVL 1.3.290) | 2 | 230 | 10 | 4 | 46 | **0** | PASS |
| 설치 소비자 (`find_package 0.0.1 EXACT`, 5모드 전체) | 2/2/1/2/2 | 동일 | 동일 | 동일 | 동일 | **0** | **5/5 PASS** |

- **CTest 회귀 (`out/p9-compute`)**: `core_contracts`, `binding_layout_contracts`, `render_pass_contracts`, `spirv_*`, `vulkan_transfer*`, `vulkan_resource_bindings`, `vulkan_binding_validation`, `capabilities_*`, `viewport_*`, `command_pool_*`, `texture_view_*`, `compute_*` **34/34 통과** (33.62s).
- **검증 항목**: `StorageBuffer` 2-pass `uavBarrier` 연쇄 연산 및 비영 256바이트 오프셋 슬라이스 전체 2,048바이트 일치, `StorageTexture2D`(`TextureViewUsage::Storage` 뷰 + 원시 `TexturePtr` + `uavBarrier`) 절차적 그라데이션 생성, `UnorderedAccess -> ShaderRead` 전이 후 그래픽스 샘플링 패스 전체 픽셀 일치(Compute-Graphics 동기화), `CommandPool` + GPU 타임스탬프 연계, 잘못된 스테이지/오프셋/상태/렌더패스 내부 호출 거부, D3D12 WARP 손실·복구, `resourceStatistics()` 0 전수 회수.

## 4. 미실행 및 잔여 조건

- Linux Vulkan 및 macOS Metal 런타임 실행은 현재 Windows 환경에서 수행하지 않았으며 `P9-CMP-03`으로 분리해 추적한다.
- Async Compute 전용 큐 분리 및 Indirect/MDI/Bindless/Mesh Shader는 후속 `P9-CMD-03` 및 `P9-CMP-02`에서 진행한다.
