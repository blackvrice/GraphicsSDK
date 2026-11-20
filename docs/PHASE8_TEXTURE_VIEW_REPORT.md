# Phase 8 Texture View 및 Subresource View 보고서 (`P8-TEX-01`)

- 작성일: 2026-11-20
- 기준 커밋: `60e8be8e` (`Add explicit CommandPool and reusable pooled CommandList API`)
- 설치 SDK 라이브러리 SHA-256 (`out/p8-texture-view-install/lib/GraphicsSDKCore.lib`): `c2341cba84da39505617e27aee9812a1f74cf634a32c20e8f0679590346fcda9`
- 완료 ID: `P8-TEX-01` (API · CPU · D3D12 · Windows Vulkan 구현 및 실기/Validation/WARP·손실복구/설치 소비자 검증)
- 분리 후속 ID: `P8-TEX-05` (Linux Vulkan 및 macOS Metal CI/실기 실행 검증)

## 1. 공개 API 및 검증 계약

기존 13개 공개 헤더(`include/graphics/`)와 5개 `GraphicsSDKCore` 번역 단위 구성을 유지하며 아래 API를 추가했다.

- [`include/graphics/Types.hpp`](../include/graphics/Types.hpp):
  - `enum class TextureViewUsage { ShaderRead, RenderTarget, DepthStencil, Storage };`
  - `struct TextureViewDesc { std::optional<Format> format; TextureViewUsage usage{ShaderRead}; uint32_t baseMipLevel{}; uint32_t mipLevelCount{1}; uint32_t baseArrayLayer{}; uint32_t arrayLayerCount{1}; };`
- [`include/graphics/Resources.hpp`](../include/graphics/Resources.hpp): `class TextureView` (`description()`, `texture()`) 및 `using TextureViewPtr = std::shared_ptr<TextureView>;`
- [`include/graphics/Device.hpp`](../include/graphics/Device.hpp): `Device::createTextureView(const TexturePtr&, TextureViewDesc = {})`
- [`include/graphics/RenderPass.hpp`](../include/graphics/RenderPass.hpp), [`src/foundation/RenderPass.cpp`](../src/foundation/RenderPass.cpp):
  - `validateTextureView(TextureDesc, TextureViewDesc)` — 미지정 `format`의 부모 포맷 상속, `Storage`(UAV) `Unsupported` 거부, 이종 포맷 재해석·깊이 샘플링 `Unsupported` 거부, 컬러/깊이 혼용·범위 초과 mip/array·용도 불일치 `InvalidArgument` 거부.
  - `ColorAttachment::view` 및 `DepthAttachment::view` 추가(기존 `texture` 초기화와 100% 하위 호환; `texture`와 `view` 동시 지정·용도 불일치·동일 부모 텍스처 중복 첨부는 `InvalidArgument`).
- [`include/graphics/Bindings.hpp`](../include/graphics/Bindings.hpp), [`include/graphics/CommandList.hpp`](../include/graphics/CommandList.hpp):
  - `TextureBinding::view` 및 `CommandList::bindTextureView(const TextureViewPtr&)` 추가(`texture`와 `view` 동시 지정·`ShaderRead` 외 용도·null/foreign view 거부).

## 2. 백엔드별 구현 요약

- **D3D12 (`src/backends/d3d12/D3D12.cpp`)**: `DXTextureView`가 부모 `DXTexture`를 보유하고 용도별 네이티브 RTV 힙(`CreateRenderTargetView`, `MipSlice`), DSV 힙(쓰기/읽기전용 2슬롯 `CreateDepthStencilView`), SRV 기술자(`MostDetailedMip`/`MipLevels`)를 생성한다. 렌더 패스·`bindResources`에서 뷰의 네이티브 힙/SRV를 사용하고 `Batch`에 수명을 유지한다.
- **Vulkan (`src/backends/vulkan/Vulkan.cpp`)**: `NativeTextureView`가 `vkCreateImageView`로 지정 subresource range(`aspectMask`, `baseMipLevel`, `mipLevelCount`, `baseArrayLayer`, `arrayLayerCount`)의 전용 `VkImageView`를 생성하고 `Recording`/`NativeBindings`에서 GPU 완료 시점까지 보존한다. `State::retire()`는 `imageViews`를 `allocations`보다 먼저 해제한다.
- **Metal (`src/backends/metal/Metal.cpp`)**: `NativeTextureView`가 `[MTLTexture newTextureViewWithPixelFormat:textureType:levels:slices:]`로 네이티브 뷰를 생성하고 `Attachment`/`ResourceView`/`Pending`에 연결한다. (실제 macOS 실행 검증은 `P8-TEX-05`에서 수행)

## 3. 실제 빌드 및 검증 결과 (Windows)

- **환경**: MSVC 19.44.35228.0 / Windows SDK 10.0.26100.0 Release (`out/p8-texture-view`, `--parallel 1`, 경고 0 · 오류 0)
- **CPU 계약 (`render_pass_contracts`)**: `PASS: 65 assertions, 0 full images` (`validateTextureView` 16건 + 뷰 기반 `validateRenderPass` 5건 포함)
- **물리 GPU 2종**: NVIDIA GeForce GTX 1660 SUPER, Intel UHD Graphics 770 / **소프트웨어**: WARP / **고정 VVL**: VVL 1.3.290

| 실행 대상 | 어댑터 수 | Assertions | 생성 Views | 검증 Images | Rejections | Validation 오류 | 결과 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `gfx_texture_view_tests --d3d12` | 2 | 260 | 14 | 12 | 42 | N/A | PASS |
| `gfx_texture_view_tests --d3d12 --validation` (Debug+GBV) | 2 | 264 | 14 | 12 | 42 | **0** | PASS |
| `gfx_texture_view_tests --d3d12 --warp` (+손실/복구) | 1 | 162 | 9 | 7 | 23 | N/A | PASS |
| `gfx_texture_view_tests --vulkan` | 2 | 256 | 14 | 12 | 42 | N/A | PASS |
| `gfx_texture_view_tests --vulkan --validation` (VVL 1.3.290) | 2 | 260 | 14 | 12 | 42 | **0** | PASS |
| 설치 소비자 (`find_package 0.0.1 EXACT`, 5모드 전체) | 2/2/1/2/2 | 동일 | 동일 | 동일 | 동일 | **0** | **5/5 PASS** |

- **CTest 회귀 (`out/p8-texture-view`)**: `core_contracts`, `render_pass_contracts`, `vulkan_draw*`, `vulkan_resource_bindings`, `vulkan_binding_validation`, `vulkan_depth*`, `buffer_region_*`, `texture_region_*`, `capabilities_*`, `viewport_*`, `command_pool_*`, `texture_view_*` **38/38 통과** (43.34s).
- **검증 항목**: RTV+DSV+SRV 뷰만으로 풀스크린 샘플링·깊이 쓰기/읽기전용 패스 전체 픽셀/깊이 일치, 호출자 `TexturePtr` 해제 후 `TextureViewPtr`의 부모 수명 유지, in-flight 중 모든 `TexturePtr`/`TextureViewPtr` 조기 파괴 후 완료·픽셀 일치, 잘못된 범위/포맷/용도/중복/foreign/타 스레드 거부, D3D12 WARP `removeForTesting`/`recover` 세대 격리, 종료 시 `resourceStatistics()` 0 전수 회수.

## 4. 미실행 및 잔여 조건

- Linux Vulkan 및 macOS Metal 런타임 실행은 현재 Windows 환경에서 수행하지 않았으며 `P8-TEX-05`로 분리해 추적한다.
- 다중 mip/array/1D/3D/Cubemap 차원 확장은 `P8-TEX-02`, UAV(`TextureViewUsage::Storage`) 실동작은 `P9-CMP-01`에서 이어간다.
