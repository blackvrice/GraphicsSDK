# Phase 10 Multi-Window / Multi-Surface·다중 스왑체인 보고서 (`P10-SURF-01`)

## 1. 목적 및 범위

`P10-SURF-01`은 기존 13개 공개 헤더, 5개 `CORE` 소스 유닛, `SwapChainDesc` 하위 호환 계약을 유지하면서 단일 `Device`에서의 다중 창·다중 스왑체인 동시 렌더링, 공유 GPU 자원 접근, 실시간 DPI/최소화/가림 상태 조회, 독립 리사이즈 동기화(`syncExtent`) 및 다중 스왑체인 일괄 프레젠트(`presentSwapChains`)를 구현·검증한다.

- **공개 API (`Surface.hpp`, `SwapChain.hpp`, `Device.hpp`, `Backend.cpp`)**:
  - `SurfaceMetrics`: `clientWidth`, `clientHeight`, `dpiScale`, `dpi`, `minimized`, `occluded`
  - `MultiPresentResult`: `presentedCount`, `skippedMinimizedCount`
  - `Device::querySurfaceMetrics(Surface)`
  - `Device::presentSwapChains(std::span<SwapChain* const>, bool skipMinimized = true)` — 빈 목록·null·중복 포인터 실행 전 원자적 `ErrorCode::InvalidArgument` 거부, 최소화(`0x0`) 창 안전 건너뛰기
  - `SwapChain::{width, height, surfaceMetrics, syncExtent}` — 크기 불변 시 `false`, 변경 시 리사이즈 후 `true`, 최소화(`0x0`) 시 기존 스왑체인 무손상 `ErrorCode::InvalidState` 거부
- **백엔드 구현**:
  - **D3D12**: `GetClientRect`/`IsIconic`/`GetDpiForWindow`(동적 조회·96 DPI 폴백) 및 `DXGI_STATUS_OCCLUDED` 추적
  - **Vulkan**: Win32 `GetClientRect`/`IsIconic`/`GetDpiForWindow` 및 Linux XCB `xcbWindow` 지오메트리 연동
  - **Metal**: `CAMetalLayer` `drawableSize`/`bounds`/`contentsScale` 기반 DPI·최소화 조회

Linux Vulkan(XCB) 및 macOS Metal(`CAMetalLayer`) 실제 런타임 검증은 **`P10-SURF-02`**(`todo`)로 분리했다.

---

## 2. 실제 빌드·설치·런타임 검증 결과

- **격리 Release 빌드·설치**: `out/p10-multi-surface` / `out/p10-multi-surface-install` (MSVC x64, 경고 0 / 오류 0, 공개 헤더 13개 설치)
- **설치 `GraphicsSDKCore.lib` SHA-256**: `C1B0CA2F9F28D7C0DA5FFA427C8AAF57D345148376C692899175AF72D545C2F6`
- **`gfx_multi_surface_tests` 5/5 모드 PASS** (단일 장치 3중 스왑체인 동시 생성·공유 텍스처 배치 제출·`presentSwapChains` 원자적 거부·독립 창 리사이즈 `syncExtent`·최소화 `0x0` 건너뛰기 및 복원·부분 창 파괴 후 잔여 창 지속 렌더링·자원 누수 0):
  - `--d3d12` (GTX 1660 SUPER + Intel UHD 770): `484 assertions / 12 rejections` PASS (`128x96`, `dpi=96`, `scale=1`)
  - `--d3d12 --validation` (Debug+GBV, 2 GPU): **`validationErrorCount=0`**, `486 assertions / 12 rejections` PASS
  - `--d3d12 --warp` (`removeForTesting -> recover()` 포함): `484 assertions / 12 rejections` PASS
  - `--vulkan` (GTX 1660 SUPER 3중 창 전체 통과 + Intel UHD 770 `Unsupported` 거부): `248 assertions / 9 rejections` PASS
  - `--vulkan --validation` (고정 VVL 1.3.290): **`validationErrorCount=0`**, `250 assertions / 9 rejections` PASS
- **독립 설치 패키지 소비자** (`tests/consumer`, `find_package(GraphicsSDK 0.0.1 EXACT CONFIG REQUIRED)`): 빌드·실행 PASS (`GraphicsSDK 0.0.1`)
- **전체 CTest 회귀** (`out/p10-multi-surface`, 140개): **140/140 (100% PASS, `162.08s`)** — 신규 `multi_surface_*` 5종 및 `hdr_colorspace_*`, `frame_pacing_*`, `swapchain_guards_*`, `vulkan_presentation`, `fence_wait_*`, `device_recovery_*` 전체 통과.

---

## 3. 재현 절차

```powershell
$cmake = "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$ctest = "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe"
& $cmake -S . -B out/p10-multi-surface -G "Visual Studio 17 2022" -A x64 -DGFX_ENABLE_D3D12=ON -DGFX_ENABLE_VULKAN=ON -DGFX_VULKAN_VALIDATION_TEST=ON -DGFX_BUILD_TESTS=ON -DCMAKE_INSTALL_PREFIX=out/p10-multi-surface-install
& $cmake --build out/p10-multi-surface --config Release --parallel 1
& $cmake --install out/p10-multi-surface --config Release
$env:VK_LAYER_PATH = "D:\source\GraphicsSDK\out\vulkan-validation-runtime\build\Vulkan-ValidationLayers\layers\Release"
& $ctest --test-dir out/p10-multi-surface -C Release -R multi_surface --output-on-failure
```
