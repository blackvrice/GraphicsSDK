# Phase 9 다중 큐·Timeline Fence·비동기 전송 보고서 (`P9-CMD-03`)

- 기준일: 2026-11-20
- 대상 작업 ID: `P9-CMD-03` (완료: 공개 API · D3D12 · Windows Vulkan 실기/Validation/WARP/설치 소비자 검증 및 Async Overlap 실측; Linux Vulkan 및 macOS Metal 실기/CI 검증은 `P9-CMD-08`로 분리)
- SDK 스냅샷: `0.0.1` (`GraphicsSDKCore.lib` SHA-256: `6A4DD16C87BE7C08D70B846392525BE838893FDBA09AE60BD67F1B534B67B6FC`)
- 격리 빌드/설치 경로: `out/p9-multi-queue`, `out/p9-multi-queue-install`

## 1. 공개 API 계약 (기존 13개 공개 헤더 유지)

새 공개 헤더 파일을 추가하지 않고 기존 헤더 안에 다중 큐와 타임라인 동기화 계약을 통합했다 (`tools/macos_metal_package.py`의 13개 공개 헤더 계약 및 `tools/macos_core.py`의 5개 CORE 유닛·11개 CPU 테스트 계약 유지).

| 헤더 | 추가·확장 항목 | 계약 요약 |
| --- | --- | --- |
| [Types.hpp](../include/graphics/Types.hpp) | `enum class QueueType { Graphics, Compute, Copy }`, `DeviceCaps::{dedicatedComputeQueue, dedicatedCopyQueue, timelineFences}` | 어댑터별 전용 Compute/Copy 큐 가용 여부와 Timeline Fence 지원 여부를 정직하게 노출 |
| [CommandList.hpp](../include/graphics/CommandList.hpp) | `CommandList::queueType()`, `CommandPool::queueType()` | 명령 목록·풀이 소속된 큐 종류를 불변으로 식별하고, Copy/Compute 큐에서 비호환 연산·상태 전이를 기록 시점에 거부 |
| [CommandQueue.hpp](../include/graphics/CommandQueue.hpp) | `TimelineFence` (`completedValue`, `signal`, `wait`), `TimelineWait`, `TimelineSignal`, `SubmitDesc`, `CommandQueue::type()`, `CommandQueue::submit(const SubmitDesc&)` | GPU-only 큐 간 wait/signal 및 CPU-GPU 단조 증가 64비트 타임라인 동기화 계약 |
| [Device.hpp](../include/graphics/Device.hpp) | `Device::queue(QueueType)`, `createCommandList(QueueType)`, `createCommandPool(QueueType)`, `createTimelineFence(uint64_t initialValue = 0)` | 기존 무인자 `queue()`/`createCommandList()`/`createCommandPool()`은 `QueueType::Graphics` 기본 경로로 유지 |

## 2. 백엔드 구현 요약

### D3D12 ([D3D12.cpp](../src/backends/d3d12/D3D12.cpp))
- `DIRECT`, `COMPUTE`, `COPY` 3개 전용 `ID3D12CommandQueue`와 큐별 독립 `ID3D12Fence`·세대 ID·pending 배치 회수 큐를 생성한다 (`dedicatedComputeQueue=true`, `dedicatedCopyQueue=true`, `timelineFences=true`).
- `DXTimelineFence`는 네이티브 `ID3D12Fence`로 구현하며 CPU `signal()`/`wait()`와 `ID3D12CommandQueue::Wait()`/`Signal()`을 지원한다. 내부 큐 펜스를 사용자 타임라인 펜스보다 먼저 시그널해 `timeline->wait()` 직후 readback/pool reset/statistics가 레이스 없이 즉시 완료를 관찰한다.
- Copy 명령 목록은 `COMMON` 진입·암묵적 승격(implicit promotion)과 `ExecuteCommandLists` 후 `COMMON` 자동 감쇠(decay) 규칙을 `Tracker::nativeState`로 추적해 D3D12 Debug Layer + GBV(`#1334 INCOMPATIBLE_BARRIER_LAYOUT`) 오류 0을 보장한다.

### Vulkan ([Vulkan.cpp](../src/backends/vulkan/Vulkan.cpp))
- 물리 장치에서 Graphics, 전용 Compute(`COMPUTE & !GRAPHICS`), 전용 Transfer(`TRANSFER & !GRAPHICS & !COMPUTE`) 큐 패밀리를 탐색한다. 전용 패밀리가 없는 어댑터(예: Intel UHD Graphics 770)는 Graphics 패밀리를 공유하고 `dedicatedComputeQueue=false`, `dedicatedCopyQueue=false`를 정직하게 보고한다.
- 고유 패밀리가 2개 이상인 어댑터(예: GTX 1660 SUPER의 3개 패밀리)는 일반 버퍼·이미지를 `VK_SHARING_MODE_CONCURRENT`로 생성해 큐 패밀리 소유권 이전 배리어 없이 `Copy -> Compute -> Graphics -> Copy` 연쇄를 실행한다 (SwapChain 이미지는 `EXCLUSIVE` 유지).
- `VK_KHR_timeline_semaphore` 확장과 `VkPhysicalDeviceTimelineSemaphoreFeaturesKHR`를 활성화하고, `VkTimelineSemaphoreSubmitInfoKHR`로 큐 간 wait/signal을 연결한다. Copy 큐 배리어 및 `pWaitDstStageMask`는 `VK_PIPELINE_STAGE_TRANSFER_BIT`(+ `HOST_BIT`) 범위로 제한해 고정 VVL 1.3.290(core + sync) 오류 0을 유지한다.
- `VulkanTimelineFence::wait()`는 타임라인 완료 확인 후 해당 시그널을 포함한 내부 `VkFence` 배치를 회수해 `readback->read()`·`CommandPool::reset()` 레이스를 차단한다.

### Metal ([Metal.cpp](../src/backends/metal/Metal.cpp))
- 3개 `id<MTLCommandQueue>`와 `id<MTLSharedEvent>` 기반 `MetalTimelineFence`를 구현했다. `SubmitDesc`에 wait가 있으면 사전 wait 전용 `MTLCommandBuffer`에 `encodeWaitForEvent:value:`를 인코딩해 먼저 커밋하고, 마지막 작업 버퍼에 `encodeSignalEvent:value:`를 인코딩한다. (macOS 실기·CI 실행 검증은 `P9-CMD-08`로 분리)

## 3. 실제 검증 결과 (Windows)

### 3.1 `gfx_multi_queue_tests` 5개 모드 전체 결과

검증 범위:
- CPU `TimelineFence` 초기값·0ms/유한 timeout·`signal()` 단조 증가·`UINT64_MAX` 거부
- **3단계 GPU-only 교차 큐 버퍼 파이프라인**: `Copy(upload) -> timeline=1 -> Compute(scale+add) -> timeline=2 -> Copy(readback) -> timeline=3 -> 단일 CPU wait(3)` (1,024 × uint32 비트 단위 일치)
- **4단계 GPU-only 교차 큐 이미지 파이프라인**: `Copy(CB upload) -> 1 -> Compute(16×16 StorageTexture 생성) -> 2 -> Graphics(샘플링 렌더 패스) -> 3 -> Copy(readback) -> 4 -> 단일 CPU wait(4)` (전체 256픽셀 일치)
- **2-프레임 Async Overlap (Frames-in-Flight)**: 8프레임 연속 `CommandPool(Copy)` + `CommandPool(Compute)` 링 재사용 및 전 바이트 일치
- **거부·경계 47~52건**: 미지원 `QueueType`, 오큐 제출, Copy/Compute 비호환 명령·상태 전이, Graphics에서 남긴 `RenderTarget` 상태 텍스처의 Copy 전이 거부, 비단조/중복/외부 장치 `TimelineFence`, 타 큐 `Fence::wait` (`WrongDevice`), `resourceStatistics()` 0 회수, WARP 장치 손실·복구

| 실행 모드 | 어댑터 | `dedicatedCompute` / `dedicatedCopy` / `timelineFences` | 결과 | Assertions | Validation 오류 |
| --- | --- | --- | --- | ---: | ---: |
| `--d3d12 --benchmark` | NVIDIA GeForce GTX 1660 SUPER | `1 / 1 / 1` | **PASS** | 5,268 | 미요청 |
| `--d3d12 --validation` | NVIDIA GeForce GTX 1660 SUPER | `1 / 1 / 1` | **PASS** | 456 | **0** (Debug + GBV) |
| `--d3d12 --warp` | Microsoft Basic Render Driver | `1 / 1 / 1` | **PASS** (손실·복구 포함) | 566 | 미요청 |
| `--vulkan --benchmark` | NVIDIA GeForce GTX 1660 SUPER | `1 / 1 / 1` | **PASS** | 5,259 | 미요청 |
| `--vulkan --validation` | NVIDIA GeForce GTX 1660 SUPER | `1 / 1 / 1` | **PASS** | 448 | **0** (고정 VVL 1.3.290 core+sync) |

### 3.2 Async Transfer + Compute Overlap 실측 벤치마크 (GTX 1660 SUPER)

- 동일 워크로드: 12프레임 × 4.000 MiB 버퍼 업로드(총 48 MiB/실행) + 프레임당 4회 Compute UAV 패스(`uavBarrier` 연쇄) + 최종 readback 검증, Warmup 2회 + 측정 7회 중앙값(Wall-clock ms).
- 비교 조건: 동일 어댑터·동일 셰이더·동일 48 MiB/48 디스패치·validation OFF.

| 백엔드 | 단일 큐 순차 (Graphics, 중앙값) | 다중 큐 Async Overlap (Copy + Compute 2-slot, 중앙값) | 가속 배율 |
| --- | ---: | ---: | ---: |
| **D3D12** (GTX 1660 SUPER) | `10.925 ms` | `8.237 ms` | **1.326x** |
| **Vulkan** (GTX 1660 SUPER) | `9.400 ms` | `7.624 ms` | **1.233x** |

### 3.3 독립 설치 `find_package(GraphicsSDK 0.0.1 EXACT CONFIG REQUIRED)` 소비자 검증

`out/p9-multi-queue-install`에서 독립 소비자를 빌드해 `enumerateAdapters()`로 발견된 전체 어댑터에서 `Copy -> Compute -> Graphics` 3단계 교차 큐 타임라인 파이프라인을 비검증/검증 별도 프로세스로 검증했다.

| 백엔드 | 어댑터 | `dedicatedCompute` / `dedicatedCopy` / `timelineFences` | 비검증 | Validation (오류 0) |
| --- | --- | --- | --- | --- |
| D3D12 | NVIDIA GeForce GTX 1660 SUPER | `1 / 1 / 1` | PASS | PASS (Debug+GBV) |
| D3D12 | Intel(R) UHD Graphics 770 | `1 / 1 / 1` | PASS | PASS (Debug+GBV) |
| D3D12 | Microsoft Basic Render Driver (WARP) | `1 / 1 / 1` | PASS | — (WARP 제외) |
| Vulkan | NVIDIA GeForce GTX 1660 SUPER | `1 / 1 / 1` (`CONCURRENT`) | PASS | PASS (고정 VVL 1.3.290) |
| Vulkan | Intel(R) UHD Graphics 770 | `0 / 0 / 1` (`EXCLUSIVE` 공유 큐 폴백) | PASS | PASS (고정 VVL 1.3.290) |

### 3.4 CTest 회귀 검증

- `out/p9-multi-queue` Release + 고정 VVL 1.3.290 환경에서 11개 CPU 계약 테스트 + Phase 8/9 전체 + `multi_queue_*` 5종 + Vulkan timestamp/validation 테스트 총 **68/68 통과 (100%, 52.49초, 실패 0)**.

## 4. 미실행 경계 및 후속 항목

- **Linux Vulkan 및 macOS Metal 실기/CI 실행 미실행**: 본 검증은 Windows 물리 GPU 2종·WARP 기준이며, Linux Vulkan과 macOS Metal 환경에서의 빌드·실행·동기화 검증은 `P9-CMD-08`로 분리해 `todo`로 유지한다.
