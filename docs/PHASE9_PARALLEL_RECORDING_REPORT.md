# Phase 9 병렬 명령 기록 · Thread-local Command Allocator 보고서 (`P9-CMD-02`)

- 기준일: 2026-11-20
- 대상 ID: `P9-CMD-02` (완료), `P9-CMD-07` (Linux Vulkan · macOS Metal 실기/CI 검증 분리)
- 격리 빌드 / 설치 경로: `out/p9-parallel`, `out/p9-parallel-install`
- 설치 라이브러리 SHA-256 (`GraphicsSDKCore.lib`): `cc60c58a4dcb4aaf4879372553bea53d634ba7d0a590cf711a76fdde1ae64440`

---

## 1. 공개 API 및 스레드 소유권 계약

기존 13개 공개 헤더 구성과 `Device`/`CommandQueue`/`CommandPool::createCommandList`/`reset`의 생성 스레드 계약을 유지하면서 스레드 로컬 병렬 명령 기록과 단일 일괄 제출을 추가했다.

1. **`CommandPool::bindRecordingThread()`** ([CommandList.hpp](../include/graphics/CommandList.hpp))
   - `Device::createCommandPool()`로 생성한 풀의 초기 기록 스레드는 `Device` 생성 스레드다(기존 `P9-CMD-01` 오프스레드 거부 계약 완전 보존).
   - 워커 스레드에서 `pool->bindRecordingThread()`를 호출하면 해당 풀의 기록 친화성이 호출 스레드로 이전된다.
   - 풀 소속 명령 목록이 기록 중이거나 제출이 미완료(in-flight)인 상태에서는 `ErrorCode::InvalidState`로 거부한다.
   - 서로 다른 `CommandPool`에 바인딩된 워커 스레드들은 락 경합 없이 동시에 `begin()`·명령 기록·`end()`를 수행한다.
   - 바인딩된 스레드 외의 다른 스레드(메인 스레드 포함)가 해당 풀 소속 `CommandList`를 기록하면 `ErrorCode::InvalidState`로 거부한다.
   - `Device` 생성/조회, `CommandPool::createCommandList()`, `CommandPool::reset()`, `CommandQueue::submit()`은 계속 생성 스레드 전용이다.
2. **`CommandQueue::submit(std::span<CommandList* const> lists)`** ([CommandQueue.hpp](../include/graphics/CommandQueue.hpp))
   - 정렬된 `CommandList*` 배열을 **단일 네이티브 큐 제출**(`ExecuteCommandLists(N)`, `vkQueueSubmit(N)`, Metal 순차 `[cmdBuf commit]`)과 **단일 펜스**로 제출한다.
   - **원자적 사전 검증**: 빈 span, `nullptr`, 중복 포인터(`InvalidArgument`), 타 장치/세대(`WrongDevice`), 미종료/이미 제출/리셋됨/기록 중 풀/stale 버전(`InvalidState`)을 GPU 제출 전에 모두 거부한다.
   - **교차 리스트 읽기/변경 충돌 검사**: 동일 배치 내 여러 리스트는 읽기 전용 리소스(`VertexBuffer`, `IndexBuffer`, `ConstantBuffer`, `ShaderRead` 텍스처, `CopySource`, `ResolveSource`, `DepthRead`, `Pipeline`)를 안전하게 공유할 수 있다. 반대로 어느 한 리스트가 상태 전이 또는 쓰기(`mutated=true`: `transition`, 복사/리졸브 대상, RT/DepthWrite, UAV)를 수행한 리소스를 같은 배치의 다른 리스트가 동시에 참조하면 `ErrorCode::InvalidState`로 원자적 거부한다.

---

## 2. 백엔드별 스레드 안전성 구현

- **D3D12** ([D3D12.cpp](../src/backends/d3d12/D3D12.cpp))
  - `State::lostReason`/`retired` 원자화 및 워커 전용 lock-free 생존 검사(`checkAliveForWorker`).
  - `DXCommandPoolState::mutex`와 `std::atomic<std::thread::id> recordingThread`로 풀별 독립 동기화(서로 다른 풀 간 공유 락 0).
  - 워커의 `begin()`은 `State::pending`/`children`을 변형하지 않고 thread-safe `ID3D12Fence::GetCompletedValue()`와 풀별 `ID3D12CommandAllocator`만 사용.
  - `DXDevice::submit(span)`은 모든 리스트를 단일 `ExecuteCommandLists(N, nativeLists)` + 단일 `Signal`로 제출.
- **Vulkan** ([Vulkan.cpp](../src/backends/vulkan/Vulkan.cpp))
  - `State::completed`/`retired`/`lost` 원자화, `State::recordingsMutex`는 `begin()`/`retire()`/`resourceStatistics()`에서만 획득(명령 기록 핫 패스 락 0).
  - `NativeCommandPool::mutex`와 `recordingThread`, 각 `CommandPool`의 독립 `VkCommandPool` 및 스냅샷별 독립 `VkDescriptorPool`로 병렬 기록.
  - `Pending::recordings`를 `std::vector<std::shared_ptr<Recording>>`으로 확장하고 단일 `VkFence` + 단일 `vkQueueSubmit(commandBufferCount = N)` 실행.
- **Metal** ([Metal.cpp](../src/backends/metal/Metal.cpp))
  - `State::completed`/`retired`/`hasFailure` 원자화, `State::recordingsMutex` 및 지연 컴파일 보호용 `State::tailMutex` 추가.
  - `NativeCommandPool::mutex`와 `recordingThread`, `Pending::extraBuffers`로 다중 `MTLCommandBuffer`를 정확히 `0..N-1` 순서로 `[cmdBuf commit]`하고 완료 추적.

---

## 3. 실제 정확성·Validation·손실복구 검증 결과 (Windows)

빌드 환경: MSVC 19.44.35228.0 / Windows SDK 10.0.26100.0 Release (`--parallel 1`, 경고 0, 오류 0).
검증 스위트: [ParallelRecordingTests.cpp](../tests/ParallelRecordingTests.cpp).

| 검증 대상 | 환경 / 모드 | 결과 | 세부 수치 |
| --- | --- | --- | --- |
| `gfx_parallel_recording_tests --d3d12` | 물리 GPU 2종 (GTX 1660 SUPER, Intel UHD 770) | **PASS** | `adapters=2 assertions=114 batches=16 images=48 rejections=14 errors=0` |
| `gfx_parallel_recording_tests --d3d12 --validation` | D3D12 Debug + GBV (물리 GPU 2종) | **PASS** | `adapters=2 assertions=116 batches=16 images=48 rejections=14 errors=0` |
| `gfx_parallel_recording_tests --d3d12 --warp` | D3D12 WARP + `removeForTesting`/`recover` | **PASS** | `adapters=1 assertions=116 batches=16 images=48 rejections=15 errors=0` |
| `gfx_parallel_recording_tests --vulkan` | Windows Vulkan 물리 GPU 2종 | **PASS** | `adapters=2 assertions=114 batches=16 images=48 rejections=14 errors=0` |
| `gfx_parallel_recording_tests --vulkan --validation` | 고정 VVL 1.3.290 (core+sync, 물리 GPU 2종) | **PASS** | `adapters=2 assertions=116 batches=16 images=48 rejections=14 errors=0` |
| 설치 소비자 (`find_package 0.0.1 EXACT`) | `out/p9-parallel-consumer-build` -> `out/p9-parallel-install` | **PASS** | D3D12/GBV/WARP 및 Vulkan/고정 VVL **5/5 모드 PASS** (`errors=0`) |
| CTest 회귀 | `out/p9-parallel` (CPU 11종 + `command_pool_*` + Phase 8/9 + Vulkan 코어) | **PASS** | **52/52 PASSED** (`51.22s`) |

---

## 4. 1 vs 2 vs 4 스레드 CPU 명령 기록 실측 벤치마크

- **측정 조건**: Windows x64 MSVC Release (`out/p9-parallel/Release/gfx_parallel_recording_tests.exe --{d3d12,vulkan} --benchmark`), Validation OFF, 영구 워커 스레드 풀(스레드 생성 오버헤드 제외), 동일 어댑터 **NVIDIA GeForce GTX 1660 SUPER** (`hw_threads=28`), 동일 워크로드 **프레임당 16개 CommandList × 1,024 Draw (`setViewport`+`setScissor`+`draw`) = 16,384 Draw**, 워밍업 2프레임 후 10프레임 측정, 매 구성 종료 시 16개 타깃 전체 픽셀 Readback 일치 검증 완료. GPU timestamp 미측정(CPU 기록 구간과 제출+대기 구간 분리 표기).

| 백엔드 | 워커 스레드 | CPU 기록 p50 (ms) | CPU 기록 min (ms) | 1스레드 대비 가속 | CPU 제출+대기 p50 (ms) | GPU Timestamp | 픽셀 검증 타깃 |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| D3D12 | 1 | `1.903` | `1.848` | `1.000x` | `0.241` | `null` | `16/16` |
| D3D12 | 2 | `0.983` | `0.944` | **`1.936x`** | `0.222` | `null` | `16/16` |
| D3D12 | 4 | `0.493` | `0.478` | **`3.858x`** | `0.192` | `null` | `16/16` |
| Vulkan | 1 | `1.529` | `1.469` | `1.000x` | `0.480` | `null` | `16/16` |
| Vulkan | 2 | `0.803` | `0.772` | **`1.903x`** | `0.489` | `null` | `16/16` |
| Vulkan | 4 | `0.430` | `0.401` | **`3.552x`** | `0.533` | `null` | `16/16` |

- 동일 16,384-draw 프레임 기록에서 4스레드 병렬 기록 시 **D3D12는 3.858배(96.5% 병렬 효율), Vulkan은 3.552배(88.8% 병렬 효율)** CPU 기록 시간 단축을 실측 확인했다.

---

## 5. 미실행 범위 (정직한 분리)

- **Linux Vulkan (lavapipe/물리 GPU) 및 macOS Metal (arm64/Intel)** 병렬 기록·일괄 제출 런타임 검증은 Windows 로컬 환경에서 수행할 수 없으므로 **`P9-CMD-07`** (`todo`)으로 분리 유지한다.
