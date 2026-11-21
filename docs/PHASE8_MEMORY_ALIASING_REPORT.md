# Phase 8 Fragmentation 분석·Residency·Resource Aliasing·Sparse 기반 보고서 (`P8-MEM-02`)

- 기준일: 2026-11-21
- 작업 ID: `P8-MEM-02` (Windows D3D12/Vulkan 구현·실기 검증 완료) · 후속 분리 `P8-MEM-04` (Linux Vulkan 및 macOS Metal 실기/CI 검증 대기)
- 빌드·설치 경로: `out/p8-memory-aliasing`, `out/p8-memory-aliasing-install` (MSVC 19.44.35228.0 / Windows SDK 10.0.26100.0 Release, 경고 0 / 오류 0)
- 설치 라이브러리 SHA-256 (`GraphicsSDKCore.lib`): `1EBA9A211F835D4E48B1FD8AEA65734A2BE348746972B7C8CB4975D0F737D37C`
- 공개 헤더 제약: 기존 13개 공개 헤더(`include/graphics/*.hpp`) 및 5개 `CORE` 번역 단위 유지

## 1. 공개 API 및 설계 요약

기존 `Device::createBuffer`/`createTexture`의 자원별 전용 할당(dedicated per-resource allocation) 기본 계약은 변경하지 않고, 명시적 opt-in `MemoryHeap` 기반으로 단편화 분석·Residency 제어·비겹침 수명 Resource Aliasing·Sparse/Virtual Texture 조회를 추가했다.

- **Capability (`DeviceCaps`, [Types.hpp](../include/graphics/Types.hpp))**:
  - `resourceAliasing`: D3D12 `true`, Vulkan `true`, Metal `true`
  - `residencyControl`: D3D12 `true` (`ID3D12Device::Evict` / `MakeResident`), Vulkan `false`, Metal `false`
  - `sparseBinding`, `sparseResidencyTexture2D`: D3D12 `D3D12_FEATURE_DATA_D3D12_OPTIONS.TiledResourcesTier >= TIER_1`, Vulkan `VkPhysicalDeviceFeatures::{sparseBinding, sparseResidencyImage2D}`, Metal `MTLGPUFamilyApple6`
- **타입·진단 ([Types.hpp](../include/graphics/Types.hpp), [Resources.hpp](../include/graphics/Resources.hpp), [Backend.cpp](../src/foundation/Backend.cpp))**:
  - `MemoryHeapType` (`Buffers`, `Textures`, `RenderTargets`), `MemoryHeapDesc`, `ResourceMemoryRequirements { size, alignment, heapType }`
  - `HeapAllocationRange`, `HeapFragmentationStats` (`totalBytes`, `allocatedBytes`, `virtualRequestedBytes`, `aliasedSavedBytes`, `freeBytes`, `largestFreeBlockBytes`, `liveAllocations`, `freeRegionCount`, `externalFragmentationRatio`, `resident`)
  - `validateMemoryHeapDesc` (64 KiB 배수, 최대 4 GiB), `computeHeapFragmentationStats` (병합 구간 합집합 기반 외부 단편화 비율 `1 - largestFree / free` 및 중복 배치 절감량 `virtualRequested - allocated` 계산)
  - `MemoryHeap` (`description()`, `fragmentationStats()`, `setResident(bool)`, `isResident()`)
  - `SparseTextureProperties` (`supported`, `tileWidth`, `tileHeight`, `tileDepth`, `standardTileBytes`, `totalTiles`, `packedMipStart`, `packedMipCount`, `packedMipTiles`)
- **장치 및 명령 API ([Device.hpp](../include/graphics/Device.hpp), [CommandList.hpp](../include/graphics/CommandList.hpp))**:
  - `Device::queryBufferMemoryRequirements`, `queryTextureMemoryRequirements`, `createMemoryHeap`, `createPlacedBuffer`, `createPlacedTexture`, `querySparseTextureProperties`
  - `CommandList::aliasingBarrier(before, after)` (버퍼/텍스처 오버로드; `before`는 nullable, `after`는 non-null placed 자원 필수, 렌더 패스 밖 호출, committed 자원 및 동일 자원 거부)
- **백엔드 핵심 구현**:
  - **D3D12** ([D3D12.cpp](../src/backends/d3d12/D3D12.cpp)): `CreateHeap` (`ALLOW_ONLY_BUFFERS` / `ALLOW_ONLY_NON_RT_DS_TEXTURES` / `ALLOW_ONLY_RT_DS_TEXTURES`, Tier 1/2 호환), `CreatePlacedResource`, `D3D12_RESOURCE_BARRIER_TYPE_ALIASING`, `Evict`/`MakeResident`(in-flight 중 eviction 거부, evicted 힙 배치/제출 `InvalidState` 거부), `Tracker::placedHeap`으로 제출 배치 완료까지 `ID3D12Heap` 수명 유지 및 `resourceStatistics()` 1회 고유 집계, `D3D12_FORMAT_SUPPORT2_TILED` 사전 검사 + 임시 `CreateReservedResource`/`GetResourceTiling` 즉시 해제.
  - **Vulkan** ([Vulkan.cpp](../src/backends/vulkan/Vulkan.cpp)): `MemoryHeapType`별 대표 probe로 호환 `memoryTypeBits` 조회 후 단일 `VkDeviceMemory` 할당, `VK_IMAGE_CREATE_ALIAS_BIT` + `vkBindBufferMemory`/`vkBindImageMemory`, `aliasingBarrier`에서 `VkMemoryBarrier` 발행 및 대상 텍스처의 `UNDEFINED` 레이아웃 재초기화 + `aliasedPending` 동기화 체인 연결(VVL 1.3.290 Core+Sync 오류 0), `setResident`는 명시적 `Unsupported`, `vkGetImageSparseMemoryRequirements` 기반 Sparse 2D 타일 조회(임시 `VkImage` 즉시 파괴).
  - **Metal** ([Metal.cpp](../src/backends/metal/Metal.cpp)): `MTLHeapDescriptor`(`MTLHeapTypePlacement`, `MTLStorageModePrivate`, `MTLHazardTrackingModeTracked`), `heapBufferSizeAndAlignWithLength:`/`heapTextureSizeAndAlignWithDescriptor:`, `newBufferWithLength:options:offset:`/`newTextureWithDescriptor:offset:`, `sparseTileSizeWithTextureType:pixelFormat:sampleCount:` 구현 (macOS 실기 검증은 `P8-MEM-04`로 분리).

## 2. 실측 검증 결과 (`gfx_memory_aliasing_tests`)

고정 Vulkan Validation Layers 1.3.290 (`VK_LAYER_PATH=out/vulkan-validation-runtime/build/Vulkan-ValidationLayers/layers/Release`) 및 D3D12 Debug Layer + GPU-Based Validation(GBV) 포함 5개 실행 모드 전부 통과.

| 실행 모드 | 어댑터 | Validation 오류 | 단편화 지표(체크보드 -> 해제) | 2×128 KiB 버퍼 Aliasing (물리 / 절감) | 2×256² RGBA8 RT Aliasing (가상 -> 물리 / 절감) | Residency | Sparse 2D (1024² RGBA8, 11 mips) |
| --- | --- | ---: | --- | --- | --- | --- | --- |
| `--d3d12` | NVIDIA GeForce GTX 1660 SUPER | 0 (off) | `0.50 -> 0.00` | `131,072 B` / `131,072 B` (50.0%) | `524,288 B -> 262,144 B` / `262,144 B` (50.0%) | `yes` (Evict/MakeResident PASS) | `128×128×1`, 64 KiB, `86` tiles (`packed 4..10`) |
| `--d3d12` | Intel(R) UHD Graphics 770 | 0 (off) | `0.50 -> 0.00` | `131,072 B` / `131,072 B` (50.0%) | `655,360 B -> 327,680 B` / `327,680 B` (50.0%) | `yes` (Evict/MakeResident PASS) | `128×128×1`, 64 KiB, `86` tiles (`packed 4..10`) |
| `--d3d12 --validation` | GTX 1660 SUPER + Intel UHD 770 | **0 (Debug+GBV)** | `0.50 -> 0.00` | `131,072 B` / `131,072 B` (50.0%) | GTX 50.0% / Intel 50.0% | `yes` PASS | 동일 (probe 누수 0) |
| `--d3d12 --warp` | Microsoft Basic Render Driver | 0 (off) | `0.50 -> 0.00` | `131,072 B` / `131,072 B` (50.0%) | `524,288 B -> 262,144 B` / `262,144 B` (50.0%) | `yes` + 손실/복구 PASS | `128×128×1`, 64 KiB, `92` tiles (`packed=0`) |
| `--vulkan` | NVIDIA GeForce GTX 1660 SUPER | 0 (off) | `0.50 -> 0.00` | `131,072 B` / `131,072 B` (50.0%) | `524,288 B -> 262,144 B` / `262,144 B` (50.0%) | `no` (`Unsupported` PASS) | `128×128×1`, 64 KiB, `86` tiles (`packed 4..10`) |
| `--vulkan` | Intel(R) UHD Graphics 770 | 0 (off) | `0.50 -> 0.00` | `131,072 B` / `131,072 B` (50.0%) | `532,736 B -> 327,680 B` / `266,368 B` (50.0% 가상 중복 제거 / 38.5% 물리 절감) | `no` (`Unsupported` PASS) | `128×128×1`, 64 KiB, `96` tiles (`packed 4..10`) |
| `--vulkan --validation` | GTX 1660 SUPER + Intel UHD 770 | **0 (VVL 1.3.290 Core+Sync)** | `0.50 -> 0.00` | `131,072 B` / `131,072 B` (50.0%) | GTX 50.0% / Intel 50.0% 가상 중복 제거 | `no` PASS | 동일 (probe 누수 0) |

- **검증 상세**:
  - `gfx_memory_aliasing_tests`: `--d3d12` 382 assertions(51 rejections), `--d3d12 --validation` 386 assertions(51 rejections, `val_errors=0`), `--d3d12 --warp` 216 assertions(35 rejections, `removeForTesting -> DeviceLost -> recover()` 포함), `--vulkan` 338 assertions(49 rejections), `--vulkan --validation` 342 assertions(49 rejections, `val_errors=0`).
  - 비겹침 수명 GPU 검증: 단일 커맨드 리스트 내에서 `bufA` 기록·읽기 -> `aliasingBarrier(bufA, bufB)` -> `bufB` 기록·읽기 전체 128 KiB 바이트 일치, `rtA` 빨강 렌더·복사 -> `aliasingBarrier(rtA, rtB)` -> `rtB` 초록 렌더·복사 전체 256×256 픽셀 일치.
  - 수명/정리: in-flight 중 `MemoryHeapPtr`·`BufferPtr` 조기 drop 후 정상 완료 및 최종 `resourceStatistics().uniqueResources == 0`, `resourceAllocationBytes == 0`.
  - 독립 설치 패키지 소비자(`find_package(GraphicsSDK 0.0.1 EXACT CONFIG REQUIRED)`): `out/p8-memory-aliasing-consumer-build`에서 D3D12(비검증/검증 × 2 GPU + WARP) 및 Vulkan(비검증/검증 × 2 GPU) **9/9 조합 모두 통과 (`val_errors=0`)**.
  - 전체 CTest 회귀: **152/152 (100%) 통과** (`188.18s`).

## 3. 남은 범위

- Linux Vulkan (lavapipe / 물리 GPU) 및 macOS arm64/Intel Metal 실기/CI 실행 검증은 독립 작업 ID `P8-MEM-04`로 분리해 추적한다.
- 실제 페이지 매핑(Sparse `UpdateTileMappings` / `vkQueueBindSparse` / `updateTextureMapping:`)은 본 기반 조회(`querySparseTextureProperties` + `sparseBinding`/`sparseResidencyTexture2D` capability) 위의 차기 확장 범위다.
