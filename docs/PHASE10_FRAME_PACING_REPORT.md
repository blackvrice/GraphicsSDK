# Phase 10 Frame Pacing·Present Mode·VRR·최대 지연·저지연/처리량 모드 보고서 (`P10-PRES-01`)

- 기준일: 2026-11-21
- 작업 ID: `P10-PRES-01` (Windows D3D12/Vulkan 구현·실기 검증 완료) · 후속 분리 `P10-PRES-02` (Linux Vulkan 및 macOS Metal 실기/CI 검증 대기)
- 빌드·설치 경로: `out/p10-frame-pacing`, `out/p10-frame-pacing-install` (MSVC 19.44.35228.0 / Windows SDK 10.0.26100.0 Release, 경고 0 / 오류 0)
- 설치 라이브러리 SHA-256 (`GraphicsSDKCore.lib`): `D1DFDF0B95AABD6F6B0CB77C53207FED856691CAF42E4E73E1AFB523AF473E29`
- 공개 헤더 제약: 기존 13개 공개 헤더(`include/graphics/*.hpp`) 및 5개 `CORE` 번역 단위 유지, 기존 `SwapChainDesc` 초기화 구문 100% 하위 호환

## 1. 공개 API 및 백엔드 구현 요약

- **공개 타입 ([Surface.hpp](../include/graphics/Surface.hpp))**:
  - `PresentMode`: `Fifo` (VSync), `Immediate`, `Mailbox`, `Tearing` (VRR/Variable Refresh Rate)
  - `FramePacingMode`: `Balanced` (기본 지연 `min(2, buffers)`), `LowLatency` (기본 지연 `1`), `Throughput` (기본 지연 `min(3, buffers)`)
  - `SurfacePresentCapabilities`: `fifo`, `immediate`, `mailbox`, `tearing`, `frameLatencyWaitable`, `minImageCount`, `maxImageCount`
  - `FramePacingStats`: `presentedFrames`, `lastFrameMilliseconds`, `averageFrameMilliseconds`, `frameTimeStdDevMilliseconds`, `lastAcquireWaitMilliseconds`, `lastPresentWaitMilliseconds`, `lastQueueLatencyMilliseconds`, `gpuStallCount`, `activePresentMode`, `activePacingMode`, `maxFrameLatency`
  - `SwapChainDesc` 후행 기본값 확장: `std::optional<PresentMode> presentMode`, `FramePacingMode pacingMode`, `std::uint32_t maxFrameLatency` (`0`=모드 기본값, `1..4` 명시), `double stallThresholdMilliseconds` (기본 `50.0ms`)
- **장치 및 스왑체인 인터페이스 ([Device.hpp](../include/graphics/Device.hpp), [SwapChain.hpp](../include/graphics/SwapChain.hpp))**:
  - `Device::querySurfacePresentCapabilities(Surface)`
  - `SwapChain::{presentMode, pacingMode, maxFrameLatency, waitForNextFrame, framePacingStats, resetFramePacingStats}`
- **백엔드 핵심 구현**:
  - **공통 추적기** ([DeviceGeneration.hpp](../src/foundation/DeviceGeneration.hpp)): `detail::resolveFrameLatency`, `detail::FramePacingTracker` (Welford 온라인 평균·표준편차, acquire/present 대기, CPU-GPU 큐 지연, 임계치 초과 GPU Stall 감지).
  - **D3D12** ([D3D12.cpp](../src/backends/d3d12/D3D12.cpp)): `IDXGIFactory5::CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING)` 조회, `DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT` + `SetMaximumFrameLatency` + `GetFrameLatencyWaitableObject` (`WaitForSingleObjectEx`), `Tearing` 시 `DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING` + `DXGI_PRESENT_ALLOW_TEARING` 및 `ResizeBuffers` 플래그 보존, `Mailbox` 명시적 `ErrorCode::Unsupported` 거부.
  - **Vulkan** ([Presentation.hpp](../src/backends/vulkan/Presentation.hpp), [Vulkan.cpp](../src/backends/vulkan/Vulkan.cpp)): `vkGetPhysicalDeviceSurfacePresentModesKHR` 기반 `Fifo`/`Immediate`/`Mailbox`/`Tearing` 정확한 조회 및 선택, 미지원 모드 `ErrorCode::Unsupported` 거부, `waitForNextFrame` 인플라이트 큐 지연 제한 및 실제 wall-clock 계측.
  - **Metal** ([MetalPresentation.hpp](../src/backends/metal/MetalPresentation.hpp), [Metal.cpp](../src/backends/metal/Metal.cpp)): `CAMetalLayer` `Fifo`(`displaySyncEnabled=YES`)/`Immediate`(`NO`) 지원, `Mailbox`/`Tearing` 명시적 `Unsupported` 거부, `waitForNextFrame` 및 `FramePacingStats` 구현.

## 2. 실측 검증 결과 (`gfx_frame_pacing_tests`)

고정 Vulkan Validation Layers 1.3.290 (`VK_LAYER_PATH=out/vulkan-validation-runtime/build/Vulkan-ValidationLayers/layers/Release`) 및 D3D12 Debug Layer + GBV 포함 5개 실행 모드 전부 통과.

### 2.1 Present Mode별 프레임 시간·편차·큐 지연 실측 (비검증 기준)

| 백엔드 | 어댑터 | Present Mode | 지원 여부 | 평균 프레임 시간 (ms) | 표준편차 (ms) | 큐 지연 (ms) | Validation 오류 (검증 모드) |
| --- | --- | --- | --- | ---: | ---: | ---: | ---: |
| D3D12 | NVIDIA GeForce GTX 1660 SUPER | `Fifo` | `yes` | `16.498` | `1.561` | `14.525` | `0` (Debug+GBV) |
| D3D12 | NVIDIA GeForce GTX 1660 SUPER | `Immediate` | `yes` | `0.332` | `0.403` | `0.143` | `0` (Debug+GBV) |
| D3D12 | NVIDIA GeForce GTX 1660 SUPER | `Tearing` (VRR) | `yes` | `0.283` | `0.332` | `0.108` | `0` (Debug+GBV) |
| D3D12 | NVIDIA GeForce GTX 1660 SUPER | `Mailbox` | `no` | — (`Unsupported` 거부) | — | — | `0` |
| D3D12 | Intel(R) UHD Graphics 770 | `Fifo` | `yes` | `13.098` | `4.490` | `16.007` | `0` (Debug+GBV) |
| D3D12 | Intel(R) UHD Graphics 770 | `Immediate` | `yes` | `0.641` | `0.285` | `0.370` | `0` (Debug+GBV) |
| D3D12 | Intel(R) UHD Graphics 770 | `Tearing` (VRR) | `yes` | `0.601` | `0.217` | `0.299` | `0` (Debug+GBV) |
| D3D12 | WARP (Software) | `Fifo` / `Immediate` / `Tearing` | `yes` (`Mailbox=no`) | `14.072` / `0.212` / `0.105` | `3.027` / `0.158` / `0.020` | `16.876` / `0.146` / `0.106` | `0` (+ 손실/복구 PASS) |
| Vulkan | NVIDIA GeForce GTX 1660 SUPER | `Fifo` | `yes` | `5.125` | `4.723` | `11.756` | `0` (VVL 1.3.290) |
| Vulkan | NVIDIA GeForce GTX 1660 SUPER | `Immediate` | `yes` | `0.705` | `0.771` | `2.732` | `0` (VVL 1.3.290) |
| Vulkan | NVIDIA GeForce GTX 1660 SUPER | `Mailbox` | `yes` | `0.264` | `0.088` | `0.202` | `0` (VVL 1.3.290) |
| Vulkan | NVIDIA GeForce GTX 1660 SUPER | `Tearing` | `yes` | `0.243` | `0.074` | `0.200` | `0` (VVL 1.3.290) |
| Vulkan | Intel(R) UHD Graphics 770 | 전체 | `swapchain=no` | — (`Unsupported` 거부) | — | — | `0` (VVL 1.3.290) |

### 2.2 Frame Pacing Mode별 실측 (`LowLatency=1`, `Balanced=2`, `Throughput=3`)

- **D3D12 GTX 1660 SUPER**: `LowLatency(1)` `avg=0.177ms / stddev=0.061ms / queue_lat=0.141ms`, `Balanced(2)` `avg=0.114ms / stddev=0.051ms / queue_lat=0.081ms`, `Throughput(3)` `avg=0.132ms / stddev=0.072ms / queue_lat=0.081ms`.
- **D3D12 Intel UHD 770**: `LowLatency(1)` `avg=0.729ms`, `Balanced(2)` `avg=0.445ms`, `Throughput(3)` `avg=0.372ms`.
- **Vulkan GTX 1660 SUPER**: `LowLatency(1)` `avg=0.442ms / queue_lat=0.205ms`, `Balanced(2)` `avg=0.461ms / queue_lat=0.199ms`, `Throughput(3)` `avg=0.510ms / queue_lat=0.476ms`.
- **GPU Stall 감지**: `stallThresholdMilliseconds = 0.0001` 설정 시 전 어댑터에서 stall 카운트 정상 감지; `resetFramePacingStats()` 후 0 초기화 확인; `Tearing`/`Immediate` resize 후 플래그 보존 및 렌더링 정상 확인.
- **독립 설치 패키지 소비자** (`find_package(GraphicsSDK 0.0.1 EXACT CONFIG REQUIRED)`): `out/p10-frame-pacing-consumer-build`에서 D3D12·Vulkan 전체 어댑터 비검증·검증 **9/9 조합 모두 통과 (`val_errors=0`)**.
- **전체 CTest 회귀**: **160/162 통과** (`184.73s`; `frame_pacing_*` 5/5, `swapchain_guards_*` 2/2, `vulkan_presentation*` 2/2, `device_recovery_*` 3/3 포함 전체 스왑체인/프레젠테이션 회귀 통과. 미통과 2건은 기존 문서에 알려진 로컬 예외 `fence_wait_gpu`, `fence_wait_validation`이며 본 변경과 무관).

## 3. 남은 범위

- Linux Vulkan (XCB) 및 macOS arm64/Intel Metal (`CAMetalLayer`) 실기/CI 실행 검증은 독립 작업 ID `P10-PRES-02`로 분리해 추적한다.
