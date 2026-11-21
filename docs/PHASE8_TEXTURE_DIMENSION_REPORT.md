# Phase 8 다중 차원·Mipmap 텍스처 (`P8-TEX-02`) 구현 및 검증 보고서

- 기준일: 2026-11-21
- 작업 ID: `P8-TEX-02` (Windows D3D12/Vulkan/Validation/WARP/설치 소비자 검증 완료) · 후속 분리 `P8-TEX-07` (Linux Vulkan 및 macOS Metal 실기/CI 검증 대기)
- 기준 커밋: `05fd2211` 이후 작업
- 격리 빌드/설치 경로: `out/p8-texture-dimensions`, `out/p8-texture-dimensions-install`
- 설치 라이브러리 SHA-256 (`GraphicsSDKCore.lib`): `2895827C25E953A36F723665BF7E76F4EA2A06E50BAD499D70B112FF6FD7E58A`

---

## 1. 구현 범위 및 공개 API 계약

기존 13개 공개 헤더(`include/graphics/*.hpp`)와 5개 `CORE` 번역 단위·11개 CPU CTest 구성을 유지하면서 하위 호환 기본값으로 5개 텍스처 차원과 다중 Mipmap을 확장했다.

### 1.1 공개 타입 (`include/graphics/Types.hpp`, `Bindings.hpp`, `Shader.hpp`)
- `enum class TextureDimension { Texture2D = 0, Texture1D, Texture3D, Texture2DArray, TextureCube };`
- `TextureDesc` 말미에 기본값 필드 추가(기존 집계 초기화 100% 호환):
  - `TextureDimension dimension{TextureDimension::Texture2D};`
  - `std::uint32_t depth{1};`
  - `std::uint32_t mipLevels{1};`
  - `std::uint32_t arrayLayers{1};`
- `TextureViewDesc` 말미에 `std::optional<TextureDimension> dimension;` 추가:
  - 생략 시 부모 차원 상속(단, `Texture2DArray`/`TextureCube`의 단일 슬라이스 `RenderTarget`/`DepthStencil` 뷰는 `Texture2D`로 자동 해소).
- `TextureCopyRegion` 말미에 `std::uint32_t mipLevel{}, arrayLayer{}, z{}, depth{1};` 추가:
  - 3D 볼륨/슬라이스 복사 시 버퍼 내 각 z-슬라이스는 `rowPitch * height` 바이트 간격으로 연속 배치.
- `BindingType` 및 `ShaderResourceType` 말미에 `Texture1D`, `Texture3D`, `Texture2DArray`, `TextureCube` 추가:
  - `validateBindingLayout`에서 5개 샘플링 텍스처 타입은 동일 SRV 레지스터 뱅크(`t` / `VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE` / Metal `texture`)와 스테이지당 128개 한도를 공유.
  - HLSL(DXBC/DXIL), SPIR-V 1.3(`OpCapability Sampled1D` 포함), Metal MSL 리플렉션이 차원별 `ShaderResourceType`을 정확히 보고하며, `bindResources`/`bindComputeResources`는 바인딩 대상 텍스처/뷰의 차원과 파이프라인 레이아웃 타입의 일치를 강제.

### 1.2 검증 및 거부 계약 (`src/foundation/RenderPass.cpp`)
- `Texture1D`: `width 1..16384`, `height=depth=arrayLayers=1`; `renderTarget`/`depthStencil`/`storage`는 `Unsupported`.
- `Texture3D`: `width/height/depth 1..2048`, `arrayLayers=1`; `renderTarget`/`depthStencil`/`storage`는 `Unsupported`.
- `Texture2DArray`: `width/height 1..16384`, `depth=1`, `arrayLayers 1..2048`; `storage`는 `Unsupported`; raw `beginRenderPass`는 `InvalidArgument`(단일 슬라이스 `TextureView` 필수).
- `TextureCube`: 정사각형 `width == height` (`1..16384`), `depth=1`, `arrayLayers=6`; `depthStencil`/`storage`는 `Unsupported`; `TextureCube` `ShaderRead` 뷰는 `baseArrayLayer=0, arrayLayerCount=6` 필수, `Texture2DArray` 뷰 재해석 지원.
- `Texture2DArray`/`TextureCube`의 `Texture2D` `ShaderRead` 뷰는 D3D12(`D3D12_TEX2D_SRV`가 `FirstArraySlice` 미지원) 포함 3개 백엔드 일관성을 위해 `Unsupported`로 명시 거부.
- MSAA(`sampleCount > 1`): `Texture2D`, `mipLevels=1`, `arrayLayers=1`, `depth=1`만 허용.
- `mipLevels`: `1 .. 1 + floor(log2(max(w, h, d)))` 범위 검증.
- `validateRenderPass`: `TextureView::baseMipLevel`에 따라 `max(1, width >> mip)`, `max(1, height >> mip)`로 mip 스케일 어태치먼트 크기 일치 및 viewport/scissor 경계 검증.
- `Backend.cpp`: `sameTextureDesc`와 `estimateTextureBytes`가 `dimension`/`depth`/`mipLevels`/`arrayLayers`를 반영해 `TransientResourcePool`과 `RenderGraph` 별칭 판정에 통합.

### 1.3 백엔드별 구현
- **D3D12 (`src/backends/d3d12/D3D12.cpp`)**:
  - `D3D12_RESOURCE_DIMENSION_TEXTURE1D/2D/3D`, `DepthOrArraySize`, `MipLevels`, `fillD3D12SrvDesc`(1D/2D/3D/2DArray/Cube), 단일 슬라이스 `TEXTURE2DARRAY` RTV/DSV, `SubresourceIndex = mipLevel + arrayLayer * mipLevels`, 3D `D3D12_BOX` 및 `PlacedFootprint.Footprint.Depth` 복사, mip 스케일 `passWidth`/`passHeight`.
- **Vulkan (`src/backends/vulkan/Vulkan.cpp`, `Spirv.hpp`)**:
  - `VK_IMAGE_TYPE_1D/2D/3D`, `VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT`, `GetPhysicalDeviceImageFormatProperties` 한도 검사, 전체 subresource 범위(`0..mipLevels`, `0..arrayLayers`) 배리어 통일, 다중 mip 2D 어태치먼트용 단일 mip `attachmentView` 분리, `VkSamplerCreateInfo::maxLod = VK_LOD_CLAMP_NONE`, `bufferImageHeight = region.height` 및 3D offset/extent 복사, `OpCapability Sampled1D` + `OpTypeImage` 5차원 SPIR-V 1.3 리플렉션.
- **Metal (`src/backends/metal/Metal.cpp`, `MetalGraphics.hpp`)**:
  - `MTLTextureType1D/2D/3D/2DArray/Cube`, `mipmapLevelCount`, `arrayLength`(Cube는 1), `depth`, `newTextureViewWithPixelFormat:textureType:levels:slices:`, blit `sourceSize`/`destinationSlice`/`destinationLevel`/`destinationOrigin`, MSL 5차원 리플렉션 및 mip 스케일 렌더패스 구현 완료(실제 macOS 실행 검증은 `P8-TEX-07`로 분리).

---

## 2. 실제 실행 및 검증 결과 (Windows)

### 2.1 `gfx_texture_dimension_tests` 5모드 검증 (`out/p8-texture-dimensions`)
테스트 범위: 미지원/잘못된 조합 거부 34~35건(1D/3D/2DArray/Cube/MSAA/뷰/복사 경계/교차 차원 바인딩/리플렉션), 다중 mip `Texture2D`(16x16 4-mip, mip 1 RTV 렌더 + 단일 mip SRV + 전 mip 샘플링/readback), `Texture1D`(16x1 3-mip 좌우 분할 패턴 샘플링/readback), `Texture2DArray`(16x16 4-layer 3-mip, `(layer=2, mip=1)` RTV 렌더 + 부분 범위 SRV 뷰 + 전체 readback), `TextureCube`(16x16 6-face 2-mip, face 3 RTV 렌더 + 6면 방향 샘플링 + `Texture2DArray` 뷰 샘플링 + 전체 readback), `Texture3D`(8x8x4 2-mip, 전체 볼륨 + 단일 z=2 슬라이스 패치 업로드/샘플링/readback), 자원 0 회수, WARP `removeForTesting` → `DeviceLost` → `recover()`.

| 모드 | 어댑터 | Validation | Assertions | Rejections | Subresources | Sampled Passes | Val Errors | 결과 |
| --- | --- | --- | ---: | ---: | ---: | ---: | ---: | --- |
| `--d3d12` | NVIDIA GeForce GTX 1660 SUPER | off | 949 | 34 | 38 | 37 | 0 | PASS |
| `--d3d12 --validation` | NVIDIA GeForce GTX 1660 SUPER | Debug + GBV | 951 | 34 | 38 | 37 | 0 | PASS |
| `--d3d12 --warp` | Microsoft Basic Render Driver | off (+loss/recover) | 1094 | 35 | 45 | 43 | 0 | PASS |
| `--vulkan` | NVIDIA GeForce GTX 1660 SUPER | off | 935 | 34 | 38 | 37 | 0 | PASS |
| `--vulkan --validation` | NVIDIA GeForce GTX 1660 SUPER | Pinned VVL 1.3.290 (core+sync) | 937 | 34 | 38 | 37 | 0 | PASS |

### 2.2 설치 패키지 `find_package(GraphicsSDK 0.0.1 EXACT CONFIG REQUIRED)` 독립 소비자
경로: `out/p8-texture-dimensions-consumer` (설치 트리 `out/p8-texture-dimensions-install` 소비, validation 별도 프로세스 실행)

| 백엔드 | 어댑터 | Validation | Val Errors | 결과 |
| --- | --- | --- | ---: | --- |
| D3D12 | NVIDIA GeForce GTX 1660 SUPER | off / Debug+GBV | 0 / 0 | PASS (2/2) |
| D3D12 | Intel(R) UHD Graphics 770 | off / Debug+GBV | 0 / 0 | PASS (2/2) |
| D3D12 | Microsoft Basic Render Driver (WARP) | off | 0 | PASS (1/1) |
| Vulkan | NVIDIA GeForce GTX 1660 SUPER | off / Pinned VVL 1.3.290 | 0 / 0 | PASS (2/2) |
| Vulkan | Intel(R) UHD Graphics 770 | off / Pinned VVL 1.3.290 | 0 / 0 | PASS (2/2) |

### 2.3 전체 CTest 회귀 및 기존 테스트 정리
- 기존 `tests/BindingTests.cpp` 100행의 미지원 리소스 예시가 `Texture3D<float4>`였으나 `P8-TEX-02`에서 `Texture3D<float4>`가 정식 지원되므로 실제로 미지원인 `Texture2DMS<float4>`로 갱신했다.
- 전체 CTest (`ctest --test-dir out/p8-texture-dimensions -C Release --output-on-failure`, 고정 VVL 1.3.290 `VK_LAYER_PATH` 설정): **142/142 (100%) PASS, 0 Failed (`182.28 sec`)**.

---

## 3. 남은 범위 및 재현 절차

- **미실행 범위**: Linux Vulkan 및 macOS Metal 실기/CI 실행 검증은 현 Windows 환경에서 수행할 수 없으므로 `P8-TEX-07`(`todo`)로 분리했다.
- **재현 절차**:
  ```powershell
  cmake -S . -B out/p8-texture-dimensions -G "Visual Studio 17 2022" -A x64 `
    -DGFX_ENABLE_D3D12=ON -DGFX_ENABLE_VULKAN=ON -DGFX_VULKAN_VALIDATION_TESTS=ON `
    -DGFX_BUILD_TESTS=ON -DGFX_BUILD_EXAMPLES=ON -DCMAKE_INSTALL_PREFIX=out/p8-texture-dimensions-install
  cmake --build out/p8-texture-dimensions --config Release --parallel 1
  cmake --install out/p8-texture-dimensions --config Release --prefix out/p8-texture-dimensions-install
  $env:VK_LAYER_PATH = "D:\source\GraphicsSDK\out\vulkan-validation-runtime\build\Vulkan-ValidationLayers\layers\Release"
  ctest --test-dir out/p8-texture-dimensions -C Release --output-on-failure
  ```
