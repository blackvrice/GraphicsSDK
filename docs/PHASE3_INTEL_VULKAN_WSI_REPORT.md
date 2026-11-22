# Phase 3 Intel UHD Graphics 770 Vulkan 출력 확대 보고서 (`P3-CAP-05`)

- 작성일: 2026-11-22
- 관련 작업 ID: `P3-CAP-05` (완료 — `VK_EXT_swapchain_maintenance1` 미노출 어댑터의 완료 보장 경로 구현 및 Intel UHD 770 실기 검증)
- 기준 산출물: `out/p3-intel-wsi-install/lib/GraphicsSDKCore.lib` (SHA-256: `0BD612AF1D80C8FD5AF0A5D3F17F3A766921B755E9B3446C40C59D8FFFE47803`)

---

## 1. 완료 보장 경로 설계 (`src/backends/vulkan/Vulkan.cpp`, `src/backends/vulkan/Presentation.hpp`)

- **원인**: Intel UHD Graphics 770 Vulkan 드라이버는 `VK_KHR_swapchain`을 지원하지만 `VK_EXT_swapchain_maintenance1`을 노출하지 않아 기존 정책에서 `caps().swapChain == false`로 막혀 있었다.
- **분리 구현**:
  - `NativeDevice::presentation`(`VK_KHR_swapchain` 지원 여부)과 `NativeDevice::swapchainMaintenance1`(`VK_EXT_swapchain_maintenance1` 기능 지원 여부)을 분리했다.
  - **`swapchainMaintenance1 == true`** (NVIDIA GeForce GTX 1660 SUPER 등): 기존과 동일하게 `VkSwapchainPresentModesCreateInfoEXT` 및 `VkSwapchainPresentFenceInfoEXT` + `vkWaitForFences`를 사용한다.
  - **`swapchainMaintenance1 == false`** (Intel UHD Graphics 770 등):
    - `rebuild()`에서 `VkSwapchainPresentModesCreateInfoEXT`를 `pNext`에 체이닝하지 않는다.
    - `presentInternal()`에서 `VkSwapchainPresentFenceInfoEXT`를 체이닝하지 않고 `vkQueuePresentKHR` 직후 `vkQueueWaitIdle`로 완료를 동기화한다.
    - `NativeSwapchain::release()`에서 스왑체인·세마포어 파괴 전 `vkDeviceWaitIdle`을 수행해 세마포어/스왑체인 수명 안전성을 보장한다.

---

## 2. 실기 검증 결과 (NVIDIA GTX 1660 SUPER + Intel UHD 770, 고정 VVL 1.3.290)

| 테스트 스위트 | 검증 범위 (Intel UHD 770 포함 2 GPU) | Validation 오류 (VVL 1.3.290) | 결과 |
| --- | --- | ---: | --- |
| `gfx_vulkan_presentation_tests` (일반 & `--validation`) | `supported_adapters=2, unsupported_adapters=0`, 290 presents, 264 images, 120 draws, 14 resizes, 71,836,224 verified texel bytes | **0** | **PASS** |
| `gfx_frame_pacing_tests --vulkan --validation` | Intel UHD 770 Fifo/Immediate/Tearing + LowLatency/Balanced/Throughput 페이싱 | **0** | **PASS** |
| `gfx_hdr_colorspace_tests --vulkan --validation` | Intel UHD 770 4개 sRGB/UNORM 스왑체인 쌍 render+present+resize (`assertions=4,646`) | **0** | **PASS** |
| `gfx_multi_surface_tests --vulkan --validation` | Intel UHD 770 3중 창 배치 렌더·독립 리사이즈·최소화/복원·수명 (`assertions=486`) | **0** | **PASS** |
| `gfx_dirty_region_tests --vulkan --validation` | Intel UHD 770 비갱신 영역 보존 및 부분 렌더 (`49.25x` GPU 가속) | **0** | **PASS** |
| `gfx_graphics_ui_tests --vulkan --validation` | Intel UHD 770 2중 창 저지연 UI (`winA avg=2.335ms, winB avg=2.341ms`) | **0** | **PASS** |

- 독립 설치 소비자 (`tests/consumer`, `find_package(GraphicsSDK CONFIG REQUIRED)`): 빌드 및 `consumer.exe` 실행 **PASS** (`GraphicsSDK 0.0.1`).
