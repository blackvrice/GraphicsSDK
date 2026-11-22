# Phase 10 Dirty Region Rendering 보고서 (`P10-DIRTY-01`)

## 1. 목적 및 범위

`P10-DIRTY-01`은 기존 13개 공개 헤더, 5개 `CORE` 소스 유닛, `SwapChainDesc` 하위 호환 계약을 유지하면서 `LoadOp::Load`(비갱신 영역 보존), 동적 `setViewport`·`setScissor`, 부분 텍스처 복사(`copyBufferToTextureRegion`), 그리고 플랫폼별 Partial Present 지원 조회 및 Dirty Rect 프레젠트 API를 통합하고 전체 프레임 대비 부분 렌더링의 CPU/GPU 시간을 실측한다.

- **공개 API (`Surface.hpp`, `SwapChain.hpp`, `DeviceGeneration.hpp`)**:
  - `DirtyRect{x, y, width, height}`
  - `SurfacePresentCapabilities::{partialPresent, maxDirtyRects}`
  - `SwapChain::presentDirtyRects(std::span<const DirtyRect>)` — 빈 목록·0 크기·범위 초과·상한(64개) 초과는 실행 전 `ErrorCode::InvalidArgument` 거부
- **백엔드 구현 및 플랫폼 경계**:
  - **D3D12**: `DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL` + `IDXGISwapChain1::Present1(syncInterval, 0, &DXGI_PRESENT_PARAMETERS)`(`partialPresent=true`, `maxDirtyRects=64`). 생성·리사이즈 직후 첫 프레임은 DXGI 규격에 맞춰 자동 전체 프레젠트 후 후속 프레임부터 `pDirtyRects` 전달. `PresentMode::Tearing` 조합은 DXGI 규격상 불가하므로 명시적 `ErrorCode::Unsupported` 거부.
  - **Vulkan**: 선택적 장치 확장 `VK_KHR_incremental_present` 활성화 시 `VkPresentRegionsKHR` 체이닝(`partialPresent=true`, `maxDirtyRects=64`), 미노출 드라이버(Windows NVIDIA)에서는 `partialPresent=false`, `maxDirtyRects=0` 및 `ErrorCode::Unsupported` 반환.
  - **Metal**: `CAMetalLayer` 미지원(`partialPresent=false`, `maxDirtyRects=0`, `ErrorCode::Unsupported`).

Linux Vulkan(XCB) 및 macOS Metal(`CAMetalLayer`) 실제 런타임 검증은 **`P10-DIRTY-02`**(`todo`)로 분리했다.

---

## 2. 비갱신 영역 픽셀 보존 및 Partial Present 검증

- **격리 Release 빌드·설치**: `out/p10-dirty-region` / `out/p10-dirty-region-install` (MSVC x64, 경고 0 / 오류 0, 공개 헤더 13개 설치)
- **설치 `GraphicsSDKCore.lib` SHA-256**: `BBCDFFAC9495FEBA3796DEA58D94A577DC1AC01EED1E7816FBADE6BB3DD342CC`
- **256×256 (`65,536` 픽셀) 보존 검증**:
  - Pass 1 전체 Clear(Blue `(20, 40, 200, 255)`) -> Pass 2 `copyBufferToTextureRegion`(`64×48` Green `(30, 210, 60, 255)`) -> Pass 3 `LoadOp::Load` + `setViewport`/`setScissor`(`64×64` Red `(255, 0, 0, 255)`)
  - 전체 픽셀 readback 일치: `green=3,072`, `red=4,096`, `untouched_blue=58,368` (전 어댑터 100% 일치)
- **`gfx_dirty_region_tests` 5/5 모드 PASS**:
  - `--d3d12` (GTX 1660 SUPER + Intel UHD 770): `134,126 assertions / 10 rejections` PASS (`partialPresent=yes, maxDirtyRects=64`)
  - `--d3d12 --validation` (Debug+GBV, 2 GPU): **`validationErrorCount=0`**, `131,632 assertions / 10 rejections` PASS
  - `--d3d12 --warp` (`removeForTesting -> recover()` 포함): `132,705 assertions / 10 rejections` PASS
  - `--vulkan` (GTX 1660 SUPER `partialPresent=no` 거부 + Intel UHD 770 오프스크린 통과/스왑체인 거부): `133,986 assertions / 6 rejections` PASS
  - `--vulkan --validation` (고정 VVL 1.3.290): **`validationErrorCount=0`**, `131,492 assertions / 6 rejections` PASS
- **독립 설치 소비자** (`tests/consumer`) 및 **전체 CTest 회귀**: **145/145 (100% PASS, `160.81s`)**

---

## 3. 전체 프레임 vs 부분 렌더링 CPU/GPU 실측 비교

동일 백엔드·동일 장치·동일 셰이더·동일 `1024×1024` `RGBA8UNorm` 렌더 타깃·동일 32 레이어 오버드로우(16회 반복 평균, Validation OFF):
- **전체 프레임**: `LoadOp::Clear` + `1024×1024` Viewport/Scissor
- **부분 렌더링**: `LoadOp::Load` + `128×128` Dirty Rect (`1.56%` 면적) Viewport/Scissor

| 백엔드 / 어댑터 | 전체 CPU (ms) | 부분 CPU (ms) | 전체 GPU Timestamp (ms) | 부분 GPU Timestamp (ms) | GPU 가속 배율 |
| --- | ---: | ---: | ---: | ---: | ---: |
| D3D12 / NVIDIA GeForce GTX 1660 SUPER | `0.6238` | `0.0654` | `0.5355` | `0.0114` | **46.88x** |
| D3D12 / Intel(R) UHD Graphics 770 | `4.9087` | `0.1647` | `4.3156` | `0.0434` | **99.45x** |
| D3D12 / WARP (Software) | `1.3992` | `0.2646` | `1.2629` | `0.1625` | **7.77x** |
| Vulkan / NVIDIA GeForce GTX 1660 SUPER | `0.6104` | `0.0642` | `0.5435` | `0.0169` | **32.14x** |
| Vulkan / Intel(R) UHD Graphics 770 | `4.6825` | `0.2823` | `4.3473` | `0.0440` | **98.77x** |

---

## 4. 재현 절차

```powershell
$cmake = "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$ctest = "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe"
& $cmake -S . -B out/p10-dirty-region -G "Visual Studio 17 2022" -A x64 -DGFX_ENABLE_D3D12=ON -DGFX_ENABLE_VULKAN=ON -DGFX_VULKAN_VALIDATION_TEST=ON -DGFX_BUILD_TESTS=ON -DCMAKE_INSTALL_PREFIX=out/p10-dirty-region-install
& $cmake --build out/p10-dirty-region --config Release --parallel 1
& $cmake --install out/p10-dirty-region --config Release
$env:VK_LAYER_PATH = "D:\source\GraphicsSDK\out\vulkan-validation-runtime\build\Vulkan-ValidationLayers\layers\Release"
& $ctest --test-dir out/p10-dirty-region -C Release -R dirty_region --output-on-failure
```
