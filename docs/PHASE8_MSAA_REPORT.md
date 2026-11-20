# Phase 8 MSAA 텍스처 및 Resolve 경로 보고서 (`P8-TEX-03`)

- 기준일: 2026-11-20
- 대상 ID: `P8-TEX-03` (완료), `P8-TEX-06` (Linux Vulkan · macOS Metal 실기/CI 검증 분리)
- 격리 빌드 / 설치 경로: `out/p8-msaa`, `out/p8-msaa-install`
- 설치 라이브러리 SHA-256 (`GraphicsSDKCore.lib`): `3b66ca94594f06685f16722356b9f190d5302b8ba5c78e04d5cd3f6e79f5283a`

---

## 1. 공개 API 및 검증 계약

기존 13개 공개 헤더 구성과 기존 호출자 하위 호환성을 유지하면서 MSAA(2x/4x/8x)와 명시적 Resolve 경로를 추가했다.

1. **`include/graphics/Types.hpp`**
   - `ResourceState`: `ResolveSource`, `ResolveDestination` 추가.
   - `TextureDesc`: `std::uint32_t sampleCount{1};` 추가 (허용 값: `1`, `2`, `4`, `8`).
   - `FormatSupport`: `std::uint32_t sampleCounts{1};` 추가 (해당 포맷으로 어댑터·SDK가 지원하는 샘플 수 비트마스크: `1 | 2 | 4 | 8`).
2. **`include/graphics/RenderPass.hpp`**
   - `RenderTargetLayout`: `std::uint32_t sampleCount{1};` 추가 (`operator==`에 포함되어 `createCachedGraphicsPipeline` 캐시 키를 자동 구분).
3. **`include/graphics/CommandList.hpp`**
   - `CommandList::resolveTexture(const TexturePtr& source, const TexturePtr& destination)` 추가.
     - 렌더 패스 밖에서 실행하며 `source`는 `sampleCount > 1` 컬러 텍스처(`ResolveSource`), `destination`은 동일 크기·동일 포맷의 `sampleCount == 1` 컬러 텍스처(`ResolveDestination`)여야 한다.
     - Depth 텍스처 및 스왑체인 백버퍼 Resolve는 `ErrorCode::Unsupported`를 반환한다.
     - 모든 검증 실패는 원자적으로 거부되어 기존 기록 상태를 보존한다.
4. **`src/foundation/RenderPass.cpp`**
   - `validateTextureDescription`: `sampleCount ∈ {1, 2, 4, 8}`, `sampleCount > 1` 시 `renderTarget || depthStencil` 필수 및 `storage == false` 강제.
   - `validateTextureView`: MSAA 텍스처의 `RenderTarget`/`DepthStencil` 뷰 허용, `ShaderRead` 뷰는 `Unsupported` 거부.
   - `validateRenderTargets` / `validateRenderPass`: 레이아웃 샘플 수 검증 및 패스 내 모든 컬러·깊이 어태치먼트의 동일 `sampleCount` 강제.

---

## 2. 백엔드별 구현 요약

- **D3D12** ([D3D12.cpp](../src/backends/d3d12/D3D12.cpp))
  - `queryFormatSupport`: `D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS`(2/4/8) 및 컬러 포맷의 `MULTISAMPLE_RENDERTARGET | MULTISAMPLE_RESOLVE` 교차 확인.
  - `createTexture` / `initViews` / `createTextureView`: `SampleDesc.Count = desc.sampleCount`, MSAA 텍스처는 `GetCopyableFootprints` 미호출(`footprint = {}`), `D3D12_RTV_DIMENSION_TEXTURE2DMS` · `D3D12_DSV_DIMENSION_TEXTURE2DMS` 뷰 생성.
  - `createGraphicsPipeline`: `pso.SampleDesc.Count = targets.sampleCount`, `pso.RasterizerState.MultisampleEnable = TRUE`.
  - `resolveTexture`: `D3D12_RESOURCE_STATE_RESOLVE_SOURCE` / `D3D12_RESOURCE_STATE_RESOLVE_DEST` 전이 후 `ID3D12GraphicsCommandList::ResolveSubresource` 기록.
- **Vulkan** ([Vulkan.cpp](../src/backends/vulkan/Vulkan.cpp))
  - `queryFormatSupport`: `vkGetPhysicalDeviceImageFormatProperties`(`COLOR_ATTACHMENT | TRANSFER_SRC` 또는 `DEPTH_STENCIL_ATTACHMENT`)와 `framebufferColorSampleCounts`/`framebufferDepthSampleCounts` 교집합 반환.
  - `createTexture` / `beginRenderPass` / `createGraphicsPipeline`: `VkSampleCountFlagBits`(1/2/4/8) 적용, 파이프라인·패스 샘플 수 일치 검증.
  - `resolveTexture`: `VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL` / `VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL` 및 전송 배리어 후 `vkCmdResolveImage` 기록, 대상의 `contentsInitialized = true` 갱신.
- **Metal** ([Metal.cpp](../src/backends/metal/Metal.cpp), [MetalGraphics.hpp](../src/backends/metal/MetalGraphics.hpp))
  - `queryFormatSupport` / `createTexture` / `createTextureView`: `[device supportsTextureSampleCount:]`, `MTLTextureType2DMultisample`, `descriptor.sampleCount`, `descriptor.rasterSampleCount` 적용.
  - `resolveTexture`: `OperationKind::Resolve`에서 `MTLLoadActionLoad` + `MTLStoreActionMultisampleResolve` + `resolveTexture` 렌더 패스로 하드웨어 리졸브 인코딩.

---

## 3. 실제 실행 및 검증 결과 (Windows)

빌드 환경: MSVC 19.44.35228.0 / Windows SDK 10.0.26100.0 Release (`--parallel 1`, 경고 0, 오류 0).
검증 스위트: [RenderPassTests.cpp](../tests/RenderPassTests.cpp), [MsaaTests.cpp](../tests/MsaaTests.cpp) (기존 검증된 `vertexFixtures::position3`/`color` SPIR-V 1.3 및 대응 HLSL 사용).

| 검증 대상 | 환경 / 모드 | 결과 | 세부 수치 |
| --- | --- | --- | --- |
| CPU 계약 (`render_pass_contracts`) | Windows x64 CPU | **PASS** | `79 assertions` |
| `gfx_msaa_tests --d3d12` | 물리 GPU 2종 (GTX 1660 SUPER, Intel UHD 770) | **PASS** | `adapters=2 assertions=7856 resolves=14 images=16 rejections=58 errors=0` |
| `gfx_msaa_tests --d3d12 --validation` | D3D12 Debug + GBV (물리 GPU 2종) | **PASS** | `adapters=2 assertions=7858 resolves=14 images=16 rejections=58 errors=0` |
| `gfx_msaa_tests --d3d12 --warp` | D3D12 WARP + `removeForTesting`/`recover` | **PASS** | `adapters=1 assertions=7859 resolves=14 images=16 rejections=60 errors=0` |
| `gfx_msaa_tests --vulkan` | Windows Vulkan 물리 GPU 2종 | **PASS** | `adapters=2 assertions=7854 resolves=14 images=16 rejections=58 errors=0` |
| `gfx_msaa_tests --vulkan --validation` | 고정 VVL 1.3.290 (core+sync, 물리 GPU 2종) | **PASS** | `adapters=2 assertions=7856 resolves=14 images=16 rejections=58 errors=0` |
| 설치 소비자 (`find_package 0.0.1 EXACT`) | `out/p8-msaa-consumer-build` -> `out/p8-msaa-install` | **PASS** | D3D12/GBV/WARP 및 Vulkan/고정 VVL **5/5 모드 PASS** (`errors=0`) |
| CTest 회귀 | `out/p8-msaa` (CPU 11종 + Phase 8/9 + Vulkan 코어) | **PASS** | **47/47 PASSED** (`43.16s`) |

### 검증 커버리지 상세
- **실제 안티에일리어싱 대조 (4x vs 1x)**: `32×32` 대각선 삼각형(`x == y`) 렌더링에서 1x는 경계 중간값 픽셀 `0개`, 4x MSAA 리졸브는 경계 중간값 픽셀 `>= 24개`와 내부 465픽셀·외부 465픽셀 전수 일치 확인.
- **MSAA Depth + `TextureView` (RTV/DSV) + 조기 뷰 파괴 + `CommandPool` + GPU Timestamp**: 4x `RGBA8UNorm` + 4x `D32Float` 깊이 가림(근거리 녹색 사각형 vs 원거리 파란 배경) 전 픽셀 검증, 제출 전 뷰 파괴 수명 보존, GPU timestamp 정상.
- **다중 포맷 및 추가 샘플 수**: 어댑터가 보고한 2x·8x `RGBA8UNorm`, 4x `BGRA8UNorm`(BGRA 채널 순서), 4x `RGBA16Float`(half-float 전 픽셀) 리졸브 검증.
- **파이프라인 캐시 구분**: `createCachedGraphicsPipeline`에서 동일 셰이더의 1x와 4x 키가 충돌 없이 분리 적중/미스 동작 확인.
- **거부·원자성·손실복구·자원 회수**: 잘못된 샘플 수, 비어태치먼트/스토리지 MSAA, MSAA 직접 복사/샘플링, 패스/파이프라인 샘플 수 불일치, 패스 내부/동일/깊이/단일샘플/크기·포맷·상태 불일치/타장치 Resolve 거부, 거부 후 기존 기록 정상 실행, D3D12 WARP `removeForTesting`/`recover` 세대 격리, `resourceStatistics() == 0` 회수 및 `validationErrorCount() == 0`.

---

## 4. 미실행 범위 (정직한 분리)

- **Linux Vulkan (lavapipe/물리 GPU) 및 macOS Metal (arm64/Intel)** 런타임 실행 검증은 Windows 로컬 환경에서 수행할 수 없으므로 **`P8-TEX-06`** (`todo`)으로 분리 유지한다.
