# Phase 11 Zero-Copy Video 기반 보고서 (`P11-VID-01`)

- 작성일: 2026-11-22
- 관련 작업 ID: `P11-VID-01` (완료 — API·D3D12·Win Vulkan 다중 평면 NV12/P010·Zero-Copy 공유·GPU BT.709 색 변환·안전 복사 폴백), `P11-VID-02` (대기 — Linux Vulkan / macOS Metal 실기·CI 검증)
- 기준 산출물: `out/p11-zero-copy-video-install/lib/GraphicsSDKCore.lib` (SHA-256: `FB60165BDAA7EA413F68DE45218928497914692ADA2C50E357C4B3AEE48BF876`)

---

## 1. 공개 API 및 다중 평면 YUV 계약

기존 13개 공개 헤더와 5개 `CORE` 번역 단위 계약을 유지하면서 다중 평면 4:2:0 비디오 포맷(`NV12`, `P010`)과 평면별 샘플링/전송용 단일·이중 채널 포맷을 추가했다.

- **`Format` 확장 (`include/graphics/Types.hpp`)**:
  - `R8UNorm`, `RG8UNorm`, `R16UNorm`, `RG16UNorm` (단일/이중 채널 평면 포맷 및 안전 복사 폴백 포맷)
  - `NV12` (8-bit 4:2:0 2-plane: Plane 0 = `R8UNorm` `W×H`, Plane 1 = `RG8UNorm` `(W/2)×(H/2)`)
  - `P010` (10-bit MSB-aligned 4:2:0 2-plane: Plane 0 = `R16UNorm` `W×H`, Plane 1 = `RG16UNorm` `(W/2)×(H/2)`)
- **평면 선택 (`planeSlice`)**:
  - `TextureViewDesc::planeSlice` (기본값 `0`): 다중 평면 텍스처에서 `planeSlice=0`(Y)·`planeSlice=1`(UV) `ShaderRead` 뷰 생성, `format` 생략 시 평면 포맷 자동 추론, 불일치 포맷 또는 `planeSlice >= 2` 거부.
  - `TextureCopyRegion::planeSlice` (기본값 `0`): `copyBufferToTextureRegion`/`copyTextureToBufferRegion`으로 Plane 0·Plane 1 독립 업로드/다운로드(단일 평면 `copyBufferToTexture` 호출은 `InvalidArgument` 거부).
- **공통 유틸리티 (`include/graphics/RenderPass.hpp`, `src/foundation/RenderPass.cpp`)**:
  - `isMultiPlanarFormat`, `formatPlaneCount`, `formatPlaneFormat`, `computeTexturePlaneFootprint`.
  - 홀수 해상도, `renderTarget`/`depthStencil`/`storage`, `mipLevels > 1`, `sampleCount > 1`은 `validateTextureDescription`에서 명시적 거부.

---

## 2. 백엔드 구현

- **D3D12 (`src/backends/d3d12/D3D12.cpp`)**:
  - `DXGI_FORMAT_NV12`, `DXGI_FORMAT_P010`, `R8_UNORM`, `R8G8_UNORM`, `R16_UNORM`, `R16G16_UNORM` 매핑.
  - `CopyTextureRegion`에서 `SubresourceIndex = planeSlice`, `PlacedFootprint.Footprint.Format = native(planeFormat)` 적용.
  - `createTextureView`에서 `srvDesc.Texture2D.PlaneSlice = resolved->planeSlice` 설정.
  - `TextureDesc::shared = true` + `exportTextureHandle`/`importTextureHandle` + `createSharedTimelineFence`로 외부 디코더 장치와 Zero-Copy 텍스처 공유.
- **Vulkan (`src/backends/vulkan/Vulkan.cpp`)**:
  - `VK_FORMAT_G8_B8R8_2PLANE_420_UNORM`(`NV12`), `VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16`(`P010`) 매핑.
  - 다중 평면 이미지 생성/import 시 `VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT | VK_IMAGE_CREATE_EXTENDED_USAGE_BIT` 지정 및 기본 단일-aspect `VkImageView` 생략.
  - `createTextureView`: `VK_IMAGE_ASPECT_PLANE_0_BIT`/`PLANE_1_BIT`와 Vulkan 규격 호환 평면 포맷(`NV12`: `R8_UNORM`/`R8G8_UNORM`, `P010`: `VK_FORMAT_R10X6_UNORM_PACK16`/`VK_FORMAT_R10X6G10X6_UNORM_2PACK16`) 적용.
  - `importTextureHandle`에서 외부 장치 초기화 텍스처의 `contentsInitialized = true` 반영.
- **Metal (`src/backends/metal/Metal.cpp`)**:
  - `R8UNorm`/`RG8UNorm`/`R16UNorm`/`RG16UNorm`은 `MTLPixelFormatR8Unorm`/`RG8Unorm`/`R16Unorm`/`RG16Unorm` 매핑(안전 복사 폴백 경로 지원), `NV12`/`P010`은 `MTLPixelFormatInvalid`(`Unsupported`).

---

## 3. GPU BT.709 색 변환 및 실기 검증 결과 (`tests/ZeroCopyVideoTests.cpp`)

- **GPU 색 변환 Compute Shader**:
  - D3D12 HLSL `cs_5_1`(`Texture2D.Load`) 및 Vulkan 순수 SPIR-V 1.3(`tests/fixtures/ComputeFixtures.hpp::yuv_to_rgb_bt709`, `OpCapability Shader` 단일, `texelFetch`)로 128×128(16,384픽셀) BT.709 Limited Range YUV→RGB 변환 후 독립 CPU BT.709 참조와 전 픽셀(±2/255, alpha=255) 대조.
- **검증 경로 (어댑터별 최대 6프레임)**:
  1. `NV12` Safe-Copy Fallback (`R8UNorm` + `RG8UNorm` 단일 평면 쌍)
  2. `P010` Safe-Copy Fallback (`R16UNorm` + `RG16UNorm` 단일 평면 쌍)
  3. `NV12` Direct Multi-Planar (`planeSlice=0/1` 업로드·뷰)
  4. `NV12` Zero-Copy Shared Interop (독립 `decoderDev` → `exportTextureHandle` + `exportTimelineFenceHandle` → `renderDev` GPU `TimelineWait` → BT.709 Compute)
  5. `P010` Direct Multi-Planar (`planeSlice=0/1` 업로드·뷰)
  6. `P010` Zero-Copy Shared Interop (`decoderDev` → `renderDev` Zero-Copy BT.709 Compute)

| 실행 모드 | 검증 GPU 어댑터 | NV12/P010 지원 | 검증 프레임 | Assertions | Rejections | Validation 오류 | 결과 |
| --- | --- | --- | ---: | ---: | ---: | ---: | --- |
| `--d3d12` | GTX 1660 SUPER, Intel UHD 770 (2) | NV12=yes, P010=yes | 12 | 538 | 18 | N/A | **PASS** |
| `--d3d12 --validation` (Debug+GBV) | GTX 1660 SUPER, Intel UHD 770 (2) | NV12=yes, P010=yes | 12 | 550 | 18 | **0** | **PASS** |
| `--d3d12 --warp` | Microsoft Basic Render Driver (1) | NV12=yes, P010=yes | 4 | 187 | 14 | N/A | **PASS** |
| `--vulkan` | GTX 1660 SUPER, Intel UHD 770 (2) | NV12=yes, P010=yes | 12 | 536 | 18 | N/A | **PASS** |
| `--vulkan --validation` (VVL 1.3.290) | GTX 1660 SUPER, Intel UHD 770 (2) | NV12=yes, P010=yes | 12 | 548 | 18 | **0** | **PASS** |

- **CTest**: `zero_copy_video_*` **5/5 PASS (`4.11s`)**, `native_interop_*` **5/5 PASS (`3.39s`)**, 기존 회귀(CPU 계약 + WARP + Validation) **44/44 PASS (`28.81s`)**.
- **독립 설치 소비자 (`tests/consumer`, `find_package(GraphicsSDK CONFIG REQUIRED)`)**: 빌드 및 `consumer.exe` 실행 **PASS** (`GraphicsSDK 0.0.1`).

---

## 4. 재현 절차 및 잔여 범위 (`P11-VID-02`)

```powershell
$env:VK_LAYER_PATH = "D:\source\GraphicsSDK\out\vulkan-validation-runtime\build\Vulkan-ValidationLayers\layers\Release"
cmake -S . -B out/p11-zero-copy-video -DGFX_ENABLE_VULKAN=ON -DGFX_VULKAN_VALIDATION_TEST=ON
cmake --build out/p11-zero-copy-video --config Release --target gfx_zero_copy_video_tests -j 8
ctest --test-dir out/p11-zero-copy-video -C Release -R zero_copy_video --output-on-failure
```

- Linux Vulkan 및 macOS Metal에서의 NV12/P010 및 Safe-Copy Fallback 실행 검증은 **`P11-VID-02`**로 분리해 추적한다.
