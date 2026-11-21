# Phase 9 Async Compute·Indirect/MDI·GPU Culling·Bindless·Mesh/Task 기반 보고서 (`P9-CMP-02`)

- 기준일: 2026-11-21
- 작업 ID: `P9-CMP-02` (Windows D3D12/Vulkan 구현·실기 검증 완료) · 후속 분리 `P9-CMP-04` (Linux Vulkan 및 macOS Metal 실기/CI 검증 대기)
- 빌드·설치 경로: `out/p9-indirect-gpu-driven`, `out/p9-indirect-gpu-driven-install` (MSVC 19.44.35228.0 / Windows SDK 10.0.26100.0 Release, 경고 0 / 오류 0)
- 설치 라이브러리 SHA-256 (`GraphicsSDKCore.lib`): `611E3089BF5C1CD53B9F0F3EB52BE4B780DD7B354420E459FCE7CB2394125A37`
- 공개 헤더 제약: 기존 13개 공개 헤더(`include/graphics/*.hpp`) 및 5개 `CORE` 번역 단위 유지

## 1. 공개 API 및 백엔드 구현 요약

- **표준 Indirect 인자·상태 ([Types.hpp](../include/graphics/Types.hpp))**:
  - `ResourceState::IndirectArgument` (버퍼 전용; 텍스처 및 Copy 큐·Readback 버퍼 전이는 `InvalidArgument`로 전 백엔드 통일 거부)
  - `DrawIndirectCommand` (16B), `DrawIndexedIndirectCommand` (20B), `DispatchIndirectCommand` (12B) — D3D12/Vulkan/Metal 네이티브 레이아웃 1:1 일치 (`static_assert` 검증)
- **Capability 확장 (`DeviceCaps`, [Types.hpp](../include/graphics/Types.hpp))**:
  - `indirectDraw`, `multiDrawIndirect`, `maxDrawIndirectCount`
  - `descriptorIndexing`, `maxBindlessDescriptors` (D3D12 Resource Binding Tier 3 + SM6.0, Vulkan `VK_EXT_descriptor_indexing`, Metal Argument Buffers Tier 2)
  - `meshShaders` (D3D12 `D3D12_FEATURE_DATA_D3D12_OPTIONS7::MeshShaderTier >= TIER_1`, Vulkan `VK_EXT_mesh_shader`, Metal `MTLGPUFamilyApple7`/`Mac2`)
- **명령 및 파이프라인 API ([CommandList.hpp](../include/graphics/CommandList.hpp), [Shader.hpp](../include/graphics/Shader.hpp), [Pipeline.hpp](../include/graphics/Pipeline.hpp), [Device.hpp](../include/graphics/Device.hpp))**:
  - `CommandList::drawIndirect(argsBuffer, argsOffset, drawCount, stride)`
  - `CommandList::drawIndexedIndirect(argsBuffer, argsOffset, drawCount, stride)`
  - `CommandList::dispatchIndirect(argsBuffer, argsOffset)`
  - `ShaderStage::{Task, Mesh}`, `MeshPipelineDesc`, `Device::createMeshPipeline` (인자 검증 후 현재 스냅샷에서 명시적 `ErrorCode::Unsupported` 반환; `compileHlsl` SM5.1도 `Task`/`Mesh`에 `Unsupported` 반환)
- **백엔드 핵심 구현**:
  - **D3D12** ([D3D12.cpp](../src/backends/d3d12/D3D12.cpp)): `(kind, stride)` 키 기반 스레드 안전 `ID3D12CommandSignature` 캐시 + `ExecuteIndirect`, `D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT`, `D3D12_FEATURE_DATA_D3D12_OPTIONS` / `D3D12_FEATURE_DATA_D3D12_OPTIONS7` 조회.
  - **Vulkan** ([Vulkan.cpp](../src/backends/vulkan/Vulkan.cpp)): `vkCmdDrawIndirect` / `vkCmdDrawIndexedIndirect` / `vkCmdDispatchIndirect`, `VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT`, `VK_ACCESS_INDIRECT_COMMAND_READ_BIT`, Compute 큐 `stageScope`에 `VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT` 포함(VVL 1.3.290 Sync Validation 오류 0), `VkPhysicalDeviceFeatures::multiDrawIndirect` 활성화 및 `VK_EXT_descriptor_indexing` / `VK_EXT_mesh_shader` 조회.
  - **Metal** ([Metal.cpp](../src/backends/metal/Metal.cpp)): `drawPrimitives:indirectBuffer:indirectBufferOffset:`, `drawIndexedPrimitives:indexType:indexBuffer:indexBufferOffset:indirectBuffer:indirectBufferOffset:` (MDI 루프), `dispatchThreadgroupsWithIndirectBuffer:indirectBufferOffset:threadsPerThreadgroup:`.

## 2. 실측 검증 결과 (`gfx_indirect_gpu_driven_tests`)

고정 Vulkan Validation Layers 1.3.290 (`VK_LAYER_PATH=out/vulkan-validation-runtime/build/Vulkan-ValidationLayers/layers/Release`) 및 D3D12 Debug Layer + GBV 포함 5개 모드 전부 통과.

| 실행 모드 | 어댑터 | Validation 오류 | Indirect / MDI (`max`) | Bindless (`max`) | Mesh Shader Cap | 검증 시나리오 |
| --- | --- | ---: | --- | --- | --- | --- |
| `--d3d12` | NVIDIA GeForce GTX 1660 SUPER | 0 (off) | `yes / yes` (`65536`) | `yes` (`1,000,000`) | **`yes`** | GPU `dispatchIndirect`(3그룹 192원소) · Async Compute GPU Culling -> TimelineFence -> MDI 4-draw(4사분면 전 픽셀) · 비인덱스 MDI 2-draw · 거부 21건 · 프로파일링 카운터 · 누수 0 |
| `--d3d12` | Intel(R) UHD Graphics 770 | 0 (off) | `yes / yes` (`65536`) | `yes` (`1,000,000`) | **`no`** | 동일 전 항목 통과 + `mesh=no` 정확 보고 및 `createMeshPipeline` `Unsupported` |
| `--d3d12 --validation` | GTX 1660 SUPER + Intel UHD 770 | **0 (Debug+GBV)** | `yes / yes` (`65536`) | `yes` (`1,000,000`) | GTX `yes` / Intel `no` | `1,672 assertions / 42 rejections`, **`val_errors=0`** |
| `--d3d12 --warp` | Microsoft Basic Render Driver | 0 (off) | `yes / yes` (`65536`) | `yes` (`1,000,000`) | `yes` | `839 assertions / 22 rejections` + `removeForTesting -> DeviceLost -> recover()` 통과 |
| `--vulkan` | NVIDIA GeForce GTX 1660 SUPER | 0 (off) | `yes / yes` (`65536`) | `yes` (`1,000,000`) | **`yes`** | 동일 전 항목 통과 (3패밀리 Async Compute 큐 -> TimelineFence -> Graphics MDI) |
| `--vulkan` | Intel(R) UHD Graphics 770 | 0 (off) | `yes / yes` (`65536`) | `yes` (`1,000,000`) | **`no`** | 동일 전 항목 통과 + `mesh=no` 정확 보고 |
| `--vulkan --validation` | GTX 1660 SUPER + Intel UHD 770 | **0 (VVL 1.3.290 Core+Sync)** | `yes / yes` (`65536`) | `yes` (`1,000,000`) | GTX `yes` / Intel `no` | `1,666 assertions / 42 rejections`, **`val_errors=0`** |

- **독립 설치 패키지 소비자** (`find_package(GraphicsSDK 0.0.1 EXACT CONFIG REQUIRED)`): `out/p9-indirect-gpu-driven-consumer-build`에서 D3D12(비검증/검증 × 2 GPU + WARP) 및 Vulkan(비검증/검증 × 2 GPU) **9/9 조합 모두 통과 (`val_errors=0`)**.
- **전체 CTest 회귀**: **157/157 (100%) 통과** (`169.55s`).

## 3. 남은 범위

- Linux Vulkan (lavapipe / 물리 GPU) 및 macOS arm64/Intel Metal 실기/CI 실행 검증은 독립 작업 ID `P9-CMP-04`로 분리해 추적한다.
- 실제 `MeshPipeline` 실행 경로(SM6.5 DXIL `as_6_5`/`ms_6_5` 및 `VK_EXT_mesh_shader` / Metal Object+Mesh PSO) 및 동적 무제한 Bindless descriptor table 바인딩은 본 Capability 조회 및 명시적 `Unsupported` 계약 위의 차기 확장 범위다.
