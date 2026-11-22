# Phase 11 디버깅 고도화 보고서 (`P11-DBG-01`)

## 1. 목적 및 범위

`P11-DBG-01`은 기존 13개 공개 헤더, 5개 `CORE` 소스 유닛 계약을 유지하면서 GPU Crash Breadcrumbs, 자동 Resource Leak Detection, RenderDoc/PIX 네이티브 마커 호환성, 통제 Fault Injection 및 결정적 입력 Fuzz 테스트를 구축한다.

- **공개 API (`Types.hpp`, `Device.hpp`, `DeviceGeneration.hpp`, `Backend.cpp`)**:
  - `kMaxCrashBreadcrumbs = 128`, `BreadcrumbEntry{sequence, queue, label, submittedFence, completed}`, `DiagnosticReport{liveTrackedObjects, liveUniqueResources, liveAllocationBytes, pendingSubmissions, hasLeaks, deviceLost, breadcrumbs, summary}`
  - `Device::recordBreadcrumb(label, queue, fenceValue)`: 1..256바이트 라벨 및 유효 큐 검증 후 최근 128개 링 버퍼 기록
  - `Device::markBreadcrumbsCompleted(queue, completedFence)` / `Device::clearBreadcrumbs()`
  - `Device::diagnosticReport()`: 완료 펜스 자동 갱신, 잔여 자원 누수(`hasLeaks`) 판별, `DeviceLost` 이후에도 마지막 미완료 크래시 브레드크럼과 요약 문자열을 안전하게 반환
- **Fuzz 테스트로 검출·수정된 실제 드라이버 방어 버그**:
  - 64비트 Windows에서 `SIZE_MAX == UINT64_MAX`이므로 `UINT64_MAX` 근처 크기의 `createBuffer` 입력이 D3D12/Vulkan 드라이버의 내부 64KB 정렬 올림에서 오버플로(`0xC0000005`)를 유발하는 경계를 Fuzz 테스트가 검출했다.
  - `D3D12.cpp::createBuffer` 및 `Vulkan.cpp::createBuffer`에 64비트 정렬 오버플로 방지 상한(`1 TiB = 1ull << 40`)을 추가해 모든 백엔드에서 결정적 `ErrorCode::InvalidArgument`로 방어하도록 수정했다.
- **RenderDoc / PIX 캡처 호환성**:
  - `CommandList::debugMarker`(D3D12 `SetMarker` / Vulkan `vkCmdInsertDebugUtilsLabelEXT` / Metal `insertDebugSignpost`) 및 `tools/DebugCapture.cpp`(`gfx_debug_capture`) 빌드·실행 호환성을 확인했다. (외부 GUI 도구 RenderDoc/PIX 수동 캡처 파일 생성은 호스트 GUI 도구 설치 시 `gfx_debug_capture.exe`로 즉시 재현 가능.)

Linux Vulkan 및 macOS Metal 실기·CI 검증은 **`P11-DBG-02`**(`todo`)로 분리했다.

---

## 2. 실제 검증 결과

- **격리 Release 빌드·설치**: `out/p11-diagnostics-fuzz` / `out/p11-diagnostics-fuzz-install` (MSVC x64, 경고 0 / 오류 0, 공개 헤더 13개 설치, `gfx_debug_capture.exe` 포함)
- **설치 `GraphicsSDKCore.lib` SHA-256**: `D7FC089561685FD4E844CBF7DA5B2F2EBFE0EEF1DEF5C16499C6D1A42E11E78D`
- **`gfx_diagnostics_fuzz_tests` 5/5 모드 & `ctest -R diagnostics_fuzz` 5/5 PASS (`1.93s`)**:
  - `--d3d12` (GTX 1660 SUPER + Intel UHD 770): `1,378 assertions / 1,006 rejections / 1,000 fuzz cases` PASS
  - `--d3d12 --validation` (Debug+GBV, 2 GPU): **`validationErrorCount=0`**, `1,380 assertions / 1,000 fuzz cases` PASS
  - `--d3d12 --warp` (`removeForTesting` Fault Injection + `DeviceLost` 후 `CrashSite:ShadowPass` 브레드크럼 보존 + `recover()`): `697 assertions / 504 rejections / 500 fuzz cases` PASS
  - `--vulkan` (GTX 1660 SUPER + Intel UHD 770): `1,378 assertions / 1,006 rejections / 1,000 fuzz cases` PASS
  - `--vulkan --validation` (고정 VVL 1.3.290, 2 GPU): **`validationErrorCount=0`**, `1,380 assertions / 1,000 fuzz cases` PASS
- **독립 설치 패키지 소비자** (`tests/consumer`, `find_package(GraphicsSDK 0.0.1 EXACT CONFIG REQUIRED)`): 빌드·실행 PASS (`GraphicsSDK 0.0.1`)

---

## 3. 재현 절차

```powershell
$cmake = "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$ctest = "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe"
& $cmake -S . -B out/p11-diagnostics-fuzz -G "Visual Studio 17 2022" -A x64 -DGFX_ENABLE_D3D12=ON -DGFX_ENABLE_VULKAN=ON -DGFX_VULKAN_VALIDATION_TEST=ON -DGFX_BUILD_TESTS=ON -DCMAKE_INSTALL_PREFIX=out/p11-diagnostics-fuzz-install
& $cmake --build out/p11-diagnostics-fuzz --config Release --target gfx_diagnostics_fuzz_tests gfx_debug_capture --parallel 1
$env:VK_LAYER_PATH = "D:\source\GraphicsSDK\out\vulkan-validation-runtime\build\Vulkan-ValidationLayers\layers\Release"
& $ctest --test-dir out/p11-diagnostics-fuzz -C Release -R diagnostics_fuzz --output-on-failure
```
