# Phase 10 HDR 및 Color Space 보고서 (`P10-HDR-01`)

## 1. 목적 및 범위

`P10-HDR-01`은 기존 13개 공개 헤더, 5개 `CORE` 소스 유닛, `SwapChainDesc` 하위 호환 계약을 유지하면서 다음 기능을 독립 SDK 공개 API와 D3D12/Vulkan/Metal 백엔드에 구현·검증한다.

- **포맷 확장 (`Format`)**: `RGBA8SRGB`, `BGRA8SRGB`, `RGB10A2UNorm`(10-bit) 추가 (`RGBA16Float`과 함께 sRGB·10-bit·HDR 오프스크린 렌더 타깃 및 스왑체인 지원; sRGB `storage`는 `ErrorCode::Unsupported` 거부).
- **색 공간 및 HDR 메타데이터 (`Surface.hpp`, `SwapChain.hpp`, `Device.hpp`)**:
  - `ColorSpace`: `SrgbNonlinear`, `LinearSrgb`, `DisplayP3Nonlinear`, `Rec2020Linear`, `Hdr10St2084`, `ExtendedLinearScRgb`
  - `HdrMetadata`: CIE 1931 xy 색도 좌표(`red/green/blue/whitePoint`), `maxMasteringLuminance`, `minMasteringLuminance`, `maxContentLightLevel`, `maxFrameAverageLightLevel` 및 공통 범위 검증(`validateHdrMetadata`)
  - `Device::querySurfaceColorCapabilities(Surface)` -> `SurfaceColorCapabilities`(지원 `(Format, ColorSpace)` 쌍, 10-bit 출력, HDR 메타데이터 지원 여부, 디스플레이 HDR 상태 및 휘도)
  - `SwapChain::{format, colorSpace, setHdrMetadata, hdrMetadata}` 및 `resize()` 시 포맷·색 공간·메타데이터 보존

Linux Vulkan(XCB) 및 macOS Metal(`CAMetalLayer`) 실제 런타임 검증은 **`P10-HDR-02`**(`todo`)로 분리했다.

---

## 2. 플랫폼·어댑터별 색 공간 지원 매트릭스

| 백엔드 / 어댑터 | `SrgbNonlinear` | `LinearSrgb` | `DisplayP3Nonlinear` | `Rec2020Linear` | `Hdr10St2084` (10-bit) | `ExtendedLinearScRgb` (FP16) | 정적 HDR10 메타데이터 | 실제 검증 상태 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Windows D3D12 (GTX 1660 SUPER / Intel UHD 770 / WARP) | 지원 (`RGBA8`/`BGRA8`/`SRGB`/`RGB10A2`) | 지원 (`RGBA16Float`) | 미지원 (`Unsupported`) | 미지원 (`Unsupported`) | 지원 (`RGB10A2UNorm`, `SetColorSpace1`) | 지원 (`RGBA16Float`, `SetColorSpace1`) | 지원 (`IDXGISwapChain4::SetHDRMetaData`) | **물리 GPU + Debug+GBV + WARP 복구 완료** |
| Windows Vulkan (GTX 1660 SUPER, `VK_EXT_swapchain_colorspace` + `VK_EXT_hdr_metadata`) | 지원 (`BGRA8`/`RGBA8`/`SRGB`/`RGB10A2`) | 지원 (`RGBA16Float`) | 드라이버 미보고 (`Unsupported`) | 드라이버 미보고 (`Unsupported`) | 지원 (`RGB10A2UNorm`, `HDR10_ST2084_EXT`) | 지원 (`RGBA16Float`, `EXTENDED_SRGB_LINEAR_EXT`) | 지원 (`vkSetHdrMetadataEXT`) | **물리 GPU + 고정 VVL 1.3.290 완료** |
| Windows Vulkan (Intel UHD 770) | 오프스크린 통과 / 스왑체인 미지원 (`swapchainMaintenance1` 부재) | 오프스크린 FP16 통과 | 미지원 | 미지원 | 미지원 | 미지원 | 미지원 | **오프스크린 전 픽셀 + 스왑체인 거부 완료** |
| Linux Vulkan (XCB) | 구현 (`VK_EXT_swapchain_colorspace` 선택 활성화) | 구현 | 드라이버 의존 | 드라이버 의존 | 드라이버 의존 | 드라이버 의존 | `VK_EXT_hdr_metadata` 선택 활성화 | `P10-HDR-02` 대기 |
| macOS Metal (`CAMetalLayer`) | 구현 (`BGRA8`/`SRGB`/`RGB10A2`) | 구현 (`RGBA16Float`) | 구현 (`kCGColorSpaceDisplayP3`) | 구현 (`ExtendedLinearITUR_2020`) | 구현 (`ITUR_2100_PQ`) | 구현 (`ExtendedLinearSRGB`) | 미지원 (`Unsupported`) | `P10-HDR-02` 대기 |

> 참고: D3D12 `IDXGIOutput6::GetDesc1()` 기준 GTX 1660 SUPER 연결 디스플레이에서 `displayHdr=no`, `displayMaxNits=1499`가 관측되었으며, Intel UHD 770 / WARP에서는 출력 미연결로 `displayMaxNits=0`이 보고되었다.

---

## 3. 실제 빌드·설치·런타임 검증 결과

- **격리 Release 빌드·설치**: `out/p10-hdr-colorspace` / `out/p10-hdr-colorspace-install` (MSVC x64, 경고 0 / 오류 0, 공개 헤더 13개 설치)
- **설치 `GraphicsSDKCore.lib` SHA-256**: `C0ED7321FA8F09C1046611EE2A2AA42FF01F391CA56E2DB6A97AD110C03BD50A`
- **`gfx_hdr_colorspace_tests` 5/5 모드 PASS** (오프스크린 `RGBA8SRGB`/`BGRA8SRGB` 하드웨어 감마 변환 `~(188,137,0,255)`, `RGB10A2UNorm` 10-bit 팩킹 `(1023,512,256,3)`, `RGBA16Float` HDR `>1.0` `(2.5,1.5,0.5,1.0)` 전 픽셀 검증 + 보고된 8/8 스왑체인 쌍 생성·렌더·프레젠트·리사이즈·HDR 메타데이터 보존 + 미지원/잘못된 입력 거부):
  - `--d3d12` (GTX 1660 SUPER + Intel UHD 770): `4,774 assertions / 26 rejections` PASS
  - `--d3d12 --validation` (Debug+GBV, 2 GPU): **`validationErrorCount=0`**, `4,776 assertions / 26 rejections` PASS
  - `--d3d12 --warp` (`removeForTesting -> recover()` 포함): `4,774 assertions / 26 rejections` PASS
  - `--vulkan` (GTX 1660 SUPER 8/8 쌍 + Intel UHD 770 오프스크린 통과/스왑체인 거부): `4,503 assertions / 17 rejections` PASS
  - `--vulkan --validation` (고정 VVL 1.3.290): **`validationErrorCount=0`**, `4,505 assertions / 17 rejections` PASS
- **독립 설치 패키지 소비자** (`tests/consumer`, `find_package(GraphicsSDK 0.0.1 EXACT CONFIG REQUIRED)`): 빌드·실행 PASS (`64MiB retain/release verified`, `1024 pixels verified`, `NVIDIA GeForce GTX 1660 SUPER`)
- **전체 CTest 회귀** (`out/p10-hdr-colorspace`, 135개): **133/135 PASS (`147.17s`)** — 신규 `hdr_colorspace_*` 5종 및 `frame_pacing_*`, `swapchain_guards_*`, `vulkan_presentation`, `capabilities_*`, `device_recovery_*` 전체 통과. 2건 실패(`fence_wait_gpu`, `fence_wait_validation`)는 기존 로컬 드라이버 예외로 본 변경과 무관함.

---

## 4. 재현 절차

```powershell
$cmake = "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$ctest = "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe"
& $cmake -S . -B out/p10-hdr-colorspace -G "Visual Studio 17 2022" -A x64 -DGFX_ENABLE_D3D12=ON -DGFX_ENABLE_VULKAN=ON -DGFX_VULKAN_VALIDATION_TEST=ON -DGFX_BUILD_TESTS=ON -DCMAKE_INSTALL_PREFIX=out/p10-hdr-colorspace-install
& $cmake --build out/p10-hdr-colorspace --config Release --parallel 1
& $cmake --install out/p10-hdr-colorspace --config Release
$env:VK_LAYER_PATH = "D:\source\GraphicsSDK\out\vulkan-validation-runtime\build\Vulkan-ValidationLayers\layers\Release"
& $ctest --test-dir out/p10-hdr-colorspace -C Release -R hdr_colorspace --output-on-failure
```
