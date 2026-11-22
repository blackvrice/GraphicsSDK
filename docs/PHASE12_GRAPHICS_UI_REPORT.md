# Phase 12 GraphicsUI 공통 기반 통합 검증 보고서 (`P12-UI-01`)

## 1. 목적 및 범위

`P12-UI-01`은 상위 UI 위젯 라이브러리 의존성 없이 **GraphicsSDK 공개 헤더(`<graphics/Graphics.hpp>`)만**으로 GraphicsUI가 요구하는 핵심 렌더링 파이프라인을 구현·검증한다. 본 작업은 SDK 공개 헤더(13개) 및 코어 소스(5개)를 전혀 수정하지 않고 독립 예제와 통합 테스트로 기존 공개 API 계약의 충분성을 증명했다.

- **독립 통합 예제**: [`examples/graphics_ui_foundation/UiBatchRenderer.hpp`](../examples/graphics_ui_foundation/UiBatchRenderer.hpp)
  - **2D 알파 블렌딩**: `alphaBlend = true` (Porter-Duff `srcAlpha / 1 - srcAlpha`)
  - **동적 VB/IB 프레임당 업로드**: `gfx::UploadRing` (`stageBatch()` stage-before-record 계약) + `copyBufferRegion` -> 장치 로컬 `VertexBuffer` / `IndexBuffer` + `drawIndexed`
  - **Scissor 클리핑 및 동적 Viewport**: 쿼드별 독립 `setScissor` 및 `setViewport`
  - **텍스처/폰트 아틀라스 부분 갱신**: 64×64 `RGBA8UNorm` 아틀라스에 4개의 32×32 타일을 `copyBufferToTextureRegion`으로 부분 업로드 후 알파 블렌딩 샘플링
  - **오프스크린 타깃 전 픽셀 CPU 참조 검증**: 128×128 (`16,384` 픽셀) 독립 소프트웨어 Porter-Duff 합성기 대조
  - **다중 창 저지연 출력**: 단일 장치에서 2개의 독립 Win32 스왑체인(`FramePacingMode::LowLatency`, `maxFrameLatency=1`)을 프레임당 1회 제출 + `presentSwapChains`로 출력하고 실제 `FramePacingStats` 실측
- **테스트**: [`tests/GraphicsUiFoundationTests.cpp`](../tests/GraphicsUiFoundationTests.cpp) (`graphics_ui_vulkan`, `graphics_ui_vulkan_validation`, `graphics_ui_d3d12`, `graphics_ui_d3d12_validation`, `graphics_ui_warp`)

Linux Vulkan(XCB) 및 macOS Metal(`CAMetalLayer`) 실기·CI 검증은 **`P12-UI-02`**(`todo`)로 분리했다.

---

## 2. 실제 검증 및 저지연 프레임 타이밍 결과

- **빌드**: `out/p12-graphics-ui` (MSVC x64 Release, 경고 0 / 오류 0, SDK 공개 헤더·코어 무수정)
- **오프스크린 128×128 (`16,384` 픽셀) CPU 참조 일치**: 전 어댑터 100% PASS (불투명 패널 + Scissor 클리핑 50% 알파 패널 + 부분 갱신 50% 알파 아틀라스 오버레이)
- **다중 창(2중 스왑체인, `maxFrameLatency=1`, 8프레임) 저지연 프레임 타이밍 실측**:

| 모드 / 어댑터 | 오프스크린 16,384px 참조 | WinA 평균 (ms) | WinA 큐 지연 (ms) | WinB 평균 (ms) | WinB 큐 지연 (ms) | Validation 오류 |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| D3D12 / NVIDIA GeForce GTX 1660 SUPER | PASS | `1.122` | `0.588` | `1.068` | `0.653` | N/A |
| D3D12 / Intel(R) UHD Graphics 770 | PASS | `2.101` | `0.683` | `1.453` | `0.751` | N/A |
| D3D12 Validation (Debug+GBV, 2 GPU) | PASS | `1.512` / `3.915` | `1.009` / `1.882` | `1.403` / `3.225` | `1.105` / `1.974` | **`0`** |
| D3D12 WARP (+ `removeForTesting -> recover()`) | PASS | `0.528` | `0.294` | `0.516` | `0.333` | N/A |
| Vulkan / NVIDIA GeForce GTX 1660 SUPER | PASS | **`0.249`** | **`0.127`** | **`0.237`** | **`0.186`** | N/A |
| Vulkan / Intel(R) UHD Graphics 770 | PASS | `Unsupported` (스왑체인 미지원) | - | - | - | N/A |
| Vulkan Validation (고정 VVL 1.3.290, 2 GPU) | PASS | `1.620` | `1.280` | `1.615` | `1.534` | **`0`** |

- **CTest (`ctest -R graphics_ui`)**: **5/5 PASS (`2.85s`)**, 자원 누수 `uniqueResources == 0` 확인.

---

## 3. 재현 절차

```powershell
$cmake = "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$ctest = "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe"
& $cmake -S . -B out/p12-graphics-ui -G "Visual Studio 17 2022" -A x64 -DGFX_ENABLE_D3D12=ON -DGFX_ENABLE_VULKAN=ON -DGFX_VULKAN_VALIDATION_TEST=ON -DGFX_BUILD_TESTS=ON
& $cmake --build out/p12-graphics-ui --config Release --target gfx_graphics_ui_tests --parallel 1
$env:VK_LAYER_PATH = "D:\source\GraphicsSDK\out\vulkan-validation-runtime\build\Vulkan-ValidationLayers\layers\Release"
& $ctest --test-dir out/p12-graphics-ui -C Release -R graphics_ui --output-on-failure
```
