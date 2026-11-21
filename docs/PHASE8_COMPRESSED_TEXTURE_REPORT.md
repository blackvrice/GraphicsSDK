# Phase 8 압축 텍스처 포맷 (`P8-TEX-04`: BC1~BC7·ASTC·ETC2/EAC) 구현 및 검증 보고서

- 기준일: 2026-11-21
- 작업 ID: `P8-TEX-04` (Windows D3D12/Vulkan/Validation/WARP/설치 소비자 검증 완료) · 후속 분리 `P8-TEX-08` (Linux Vulkan 및 macOS Metal 실기/CI 검증 대기)
- 기준 커밋: `eda2bab4` 이후 작업
- 격리 빌드/설치 경로: `out/p8-compressed-textures`, `out/p8-compressed-textures-install`
- 설치 라이브러리 SHA-256 (`GraphicsSDKCore.lib`): `36F4DCA045D3B2DFB51F994652E1B6A4117E0DAECA20C743FCF7B5BB40ECD290`

---

## 1. 구현 범위 및 공개 API 계약

기존 13개 공개 헤더(`include/graphics/*.hpp`)와 5개 `CORE` 번역 단위·11개 CPU CTest 구성을 유지하면서 14개 블록 압축 포맷과 휴대용 블록 메타데이터 조회를 추가했다.

### 1.1 공개 포맷 및 블록 메타데이터 (`include/graphics/Types.hpp`, `RenderPass.hpp`)
- `Format` 열거형 확장 (기존 `0..3` 값 보존):
  - **BC1~BC7 (`4×4`)**: `BC1UNorm`(8B), `BC2UNorm`(16B), `BC3UNorm`(16B), `BC4UNorm`(8B), `BC5UNorm`(16B), `BC6HUFloat`(16B), `BC7UNorm`(16B)
  - **ETC2/EAC (`4×4`)**: `ETC2RGB8UNorm`(8B), `ETC2RGBA8UNorm`(16B), `EACR11UNorm`(8B), `EACRG11UNorm`(16B)
  - **ASTC LDR**: `ASTC4x4UNorm`(`4×4`, 16B), `ASTC6x6UNorm`(`6×6`, 16B), `ASTC8x8UNorm`(`8×8`, 16B)
- `struct FormatBlockInfo { std::uint32_t blockWidth, blockHeight, bytesPerBlock; bool compressed; };`
- `Result<FormatBlockInfo> queryFormatBlockInfo(Format)`로 GPU 없이 모든 포맷의 블록 크기·바이트 수를 조회(알 수 없는 enum은 `InvalidArgument`).
- `computeTextureRegionFootprint(format, width, height, rowPitch)`:
  - `blocksX = ceil(width / blockWidth)`, `blocksY = ceil(height / blockHeight)`, `rowBytes = blocksX * bytesPerBlock`, `rowPitch = alignUp(rowBytes, 256)`, `size = rowPitch * blocksY`, `height = blocksY`.
  - 2×2·1×1 테일 mip도 1블록 행으로 정확히 정렬되며 D3D12 `GetCopyableFootprints`와 100% 일치.

### 1.2 검증 및 거부 계약 (`src/foundation/RenderPass.cpp`)
- 블록 압축 포맷은 `Texture2D`, `Texture2DArray`, `TextureCube`(Mipmap 포함) 전송 및 `ShaderRead` 샘플링 전용:
  - `renderTarget`/`storage` → `Unsupported`, `depthStencil`/`sampleCount > 1` → `InvalidArgument`, `Texture1D`/`Texture3D` → `Unsupported`, `RenderTargetLayout` 컬러 포맷 → `InvalidArgument`.
  - Mip 0 `width`/`height`는 반드시 `blockWidth`/`blockHeight` 배수(`InvalidArgument`).
  - 부분 영역 복사(`copyBufferToTextureRegion`/`copyTextureToBufferRegion`)는 원점 `(x, y)` 블록 정렬과 크기 `(width, height)` 블록 정렬 또는 서브리소스 경계 일치를 강제(`InvalidArgument`).

### 1.3 백엔드별 구현
- **D3D12 (`src/backends/d3d12/D3D12.cpp`)**:
  - `DXGI_FORMAT_BC1_UNORM`~`BC7_UNORM`(`BC6H_UF16`) 매핑. `ETC2`/`EAC`/`ASTC`는 `queryFormatSupport`에서 모든 플래그 `false`(`sampleCounts=0`)를 반환하고 `createTexture`에서 `ErrorCode::Unsupported`를 명시 반환.
  - `copyTextureRegion`에서 압축 포맷 테일 mip(`2×2`, `1×1`)의 `PlacedFootprint.Footprint.{Width, Height}`를 블록 배수로 올림 처리해 D3D12 Debug+GBV 오류 0 유지.
- **Vulkan (`src/backends/vulkan/Vulkan.cpp`)**:
  - `vkCreateDevice` 시 물리 장치가 지원하는 `textureCompressionBC`, `textureCompressionETC2`, `textureCompressionASTC_LDR` feature를 자동 활성화하고 `State`에 기록.
  - 14개 압축 포맷 전체를 대응 `VkFormat`에 매핑(`VK_FORMAT_BC1_RGBA_UNORM_BLOCK`으로 D3D12/Metal 1-bit alpha 일치).
  - 미지원 압축 패밀리는 `queryFormatSupport`에서 모두 `false`(`sampleCounts=0`), `createTexture`에서 `Unsupported` 반환.
  - `VkBufferImageCopy`에 `bufferRowLength = (rowPitch / bytesPerBlock) * blockWidth`, `bufferImageHeight = blocksY * blockHeight`를 계산해 VVL 1.3.290(core+sync) 오류 0 유지.
- **Metal (`src/backends/metal/Metal.cpp`)**:
  - `MTLPixelFormatBC1_RGBA`~`BC7_RGBAUnorm`, `ETC2_RGB8`/`EAC_RGBA8`/`EAC_R11Unorm`/`EAC_RG11Unorm`, `ASTC_4x4/6x6/8x8_LDR` 매핑, `supportsBCTextureCompression`/`supportsFamily:MTLGPUFamilyApple2` 기반 지원 조회 및 미지원 `Unsupported` 거부 구현(macOS 실기 검증은 `P8-TEX-08`로 분리).

---

## 2. 실제 실행 및 검증 결과 (Windows)

### 2.1 `gfx_compressed_texture_tests` 5모드 검증 (`out/p8-compressed-textures`)
테스트 범위:
- CPU 블록 메타데이터·footprint(4×4/6×6/8×8·테일 mip·커스텀 pitch)·미지원 조합 거부 계약.
- 백엔드별 전체 물리 GPU(NVIDIA GTX 1660 SUPER + Intel UHD Graphics 770) 및 WARP(+손실/복구) 순회:
  - 지원 포맷마다 5-mip(`16×16→8×8→4×4→2×2→1×1`, ASTC 6×6/8×8은 2-mip) 업로드 + 블록 정렬 부분 패치 + 비정렬 복사 거부 + 전 mip bit-exact 블록 readback + **실제 GPU 하드웨어 디코딩 `SampleLevel` 샘플링 참조 일치**.
  - **Intel(R) UHD Graphics 770 Vulkan에서 BC1~BC7(7종) + ETC2/EAC(4종) + ASTC 4×4/6×6/8×8(3종) 총 14개 압축 포맷 전체가 실제 하드웨어에서 업로드·샘플링·readback 검증 통과!**
  - 미지원 포맷(D3D12 전체 어댑터 및 GTX 1660 SUPER Vulkan의 ETC2/EAC/ASTC 7종)의 `queryFormatSupport == false` 및 `createTexture == Unsupported` 거부 검증.
  - `BC7UNorm` `Texture2DArray`(2-layer 2-mip) 및 `TextureCube`(6-face 2-mip) 업로드·샘플링 검증, 자원 0 회수.

| 모드 | 검증 어댑터 및 지원/미지원 포맷 수 | Validation | Assertions | Rejections | Sampled Passes | Val Errors | 결과 |
| --- | --- | --- | ---: | ---: | ---: | ---: | --- |
| `--d3d12` | GTX 1660 SUPER (7sup/7unsup); Intel UHD 770 (7sup/7unsup) | off | 2561 | 55 | 84 | 0 | PASS |
| `--d3d12 --validation` | GTX 1660 SUPER (7sup/7unsup); Intel UHD 770 (7sup/7unsup) | Debug + GBV | 2565 | 55 | 84 | 0 | PASS |
| `--d3d12 --warp` | Microsoft Basic Render Driver (7sup/7unsup + loss/recover) | off | 1451 | 37 | 47 | 0 | PASS |
| `--vulkan` | GTX 1660 SUPER (7sup/7unsup); **Intel UHD 770 (14sup/0unsup)** | off | 3408 | 62 | 113 | 0 | PASS |
| `--vulkan --validation` | GTX 1660 SUPER (7sup/7unsup); **Intel UHD 770 (14sup/0unsup)** | Pinned VVL 1.3.290 (core+sync) | 3412 | 62 | 113 | 0 | PASS |

### 2.2 설치 패키지 `find_package(GraphicsSDK 0.0.1 EXACT CONFIG REQUIRED)` 독립 소비자
경로: `out/p8-compressed-textures-consumer` (설치 트리 `out/p8-compressed-textures-install` 소비, validation 별도 프로세스 실행)

| 백엔드 | 어댑터 | 지원 / 미지원 포맷 | Validation | Val Errors | 결과 |
| --- | --- | :---: | --- | ---: | --- |
| D3D12 | NVIDIA GeForce GTX 1660 SUPER | 7 / 7 | off / Debug+GBV | 0 / 0 | PASS (2/2) |
| D3D12 | Intel(R) UHD Graphics 770 | 7 / 7 | off / Debug+GBV | 0 / 0 | PASS (2/2) |
| D3D12 | Microsoft Basic Render Driver (WARP) | 7 / 7 | off | 0 | PASS (1/1) |
| Vulkan | NVIDIA GeForce GTX 1660 SUPER | 7 / 7 | off / Pinned VVL 1.3.290 | 0 / 0 | PASS (2/2) |
| Vulkan | Intel(R) UHD Graphics 770 | **14 / 0** | off / Pinned VVL 1.3.290 | 0 / 0 | PASS (2/2) |

### 2.3 전체 CTest 회귀
- `ctest --test-dir out/p8-compressed-textures -C Release --output-on-failure` (고정 VVL 1.3.290 `VK_LAYER_PATH` 설정): **147/147 (100%) PASS, 0 Failed (`186.95 sec`)**.

---

## 3. 남은 범위 및 재현 절차

- **미실행 범위**: Linux Vulkan 및 macOS Metal 실기/CI 실행 검증은 현 Windows 환경에서 수행할 수 없으므로 `P8-TEX-08`(`todo`)로 분리했다.
- **재현 절차**:
  ```powershell
  cmake -S . -B out/p8-compressed-textures -G "Visual Studio 17 2022" -A x64 `
    -DGFX_ENABLE_D3D12=ON -DGFX_ENABLE_VULKAN=ON -DGFX_VULKAN_VALIDATION_TESTS=ON `
    -DGFX_BUILD_TESTS=ON -DGFX_BUILD_EXAMPLES=ON -DCMAKE_INSTALL_PREFIX=out/p8-compressed-textures-install
  cmake --build out/p8-compressed-textures --config Release --parallel 1
  cmake --install out/p8-compressed-textures --config Release --prefix out/p8-compressed-textures-install
  $env:VK_LAYER_PATH = "D:\source\GraphicsSDK\out\vulkan-validation-runtime\build\Vulkan-ValidationLayers\layers\Release"
  ctest --test-dir out/p8-compressed-textures -C Release --output-on-failure
  ```
