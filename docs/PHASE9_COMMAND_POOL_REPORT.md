# Phase 9 명시적 Command Pool 및 CommandList 재사용 보고서 (`P9-CMD-01`)

- 작성일: 2026-11-20
- 기준 커밋: `7defb926` (`Add dynamic setViewport API and multi-backend implementations`)
- 설치 SDK 라이브러리 SHA-256 (`out/p9-command-pool-install/lib/GraphicsSDKCore.lib`): `fe5576007709fa9ab5dc60c49c1e8698bcb0624ad554d22ea4feed79b67cb73d`
- 완료 ID: `P9-CMD-01` (API · D3D12 · Windows Vulkan 구현 및 실기/Validation/WARP·손실복구/설치 소비자 검증)
- 분리 후속 ID: `P9-CMD-06` (Linux Vulkan 및 macOS Metal CI/실기 실행 검증)

## 1. 공개 API 및 소유권 계약

기존 13개 공개 헤더 구성(`include/graphics/`)과 5개 `GraphicsSDKCore` portable 번역 단위를 유지하며 아래 인터페이스를 추가했다.

- [`include/graphics/CommandList.hpp`](../include/graphics/CommandList.hpp): `class CommandPool`
  - `createCommandList()`: 생성 스레드 전용. 이 풀의 네이티브 allocator/pool을 공유하는 재사용 가능 `CommandList`를 할당한다. 동일 풀에서 기록 중인 리스트가 있으면 `InvalidState`로 거부한다.
  - `reset()`: 생성 스레드 전용. 동일 풀의 리스트 중 기록 중인 항목이 있거나 풀에서 제출된 GPU 작업이 완료되지 않았으면 `InvalidState`로 거부한다. 성공 시 네이티브 allocator/pool을 일괄 리셋하고, 미제출·완료 기록이 보유한 자원 참조를 즉시 해제하며, 살아 있는 풀 소속 리스트를 재기록 가능한 초기 상태로 되돌린다.
- [`include/graphics/Device.hpp`](../include/graphics/Device.hpp): `Device::createCommandPool()`
  - 기존 `Device::createCommandList()` 독립 경로의 동작과 호환성을 변경하지 않는다.

### 공통 상태·수명 규칙 (D3D12 / Vulkan / Metal 동일)
1. **단일 동시 기록 제약**: 같은 `CommandPool` 소속 리스트는 한 시점에 최대 1개만 기록(`begin`~`end`)할 수 있으며, 다른 리스트가 열려 있을 때 `begin()`, `createCommandList()`, `reset()`은 모두 `InvalidState`를 반환하고 진행 중인 기록을 오염시키지 않는다.
2. **Epoch 기반 재사용**: 같은 epoch에서 이미 기록(`end` 또는 `submit`)한 풀 소속 리스트의 `begin()`은 `CommandPool::reset()` 호출 전까지 `InvalidState`를 반환한다.
3. **In-flight 보호**: 풀 소속 리스트가 제출되면 풀의 `lastSubmitted`가 갱신되며, GPU가 해당 fence 값에 도달하기 전 `reset()`은 `InvalidState`로 거부한다.
4. **미제출 기록 정리**: `end()` 후 제출하지 않은 풀 소속 리스트는 `reset()` 시 폐기되며 보유하던 자원 참조가 즉시 해제된다(`submit()` 재시도 시 `InvalidState`).
5. **안전한 조기 파괴**:
   - 기록 중인 풀 소속 `CommandList`가 `end()` 없이 파괴되어도 풀의 `openCount`가 정확히 감소하고(D3D12는 닫힘 처리, Vulkan은 `retiredBuffers`로 이관 후 `reset()` 시 해제) 풀은 정상 재사용된다.
   - 이미 제출된 배치가 GPU에서 실행 중인 상태에서 `CommandPool`과 `CommandList`를 먼저 파괴해도 `Batch`/`Pending`이 네이티브 allocator/pool 수명을 완료 시점까지 보존한다.
   - `CommandPool`이 파괴된 뒤 살아남은 미제출 풀 소속 `CommandList`의 `begin()`/`submit()`은 `InvalidState`를 반환하고 파괴 시 모든 자원을 회수한다.

## 2. 백엔드별 구현 요약

- **D3D12 (`src/backends/d3d12/D3D12.cpp`)**: `DXCommandPoolState`(`ID3D12CommandAllocator` + `epoch`·`lastSubmitted`·`openCount`·`liveLists`)를 `state->children`에 추적한다. 풀 소속 `DXCommands::begin()`은 `list->Reset(pool->allocator.Get(), nullptr)`만 호출하며, `DXCommandPool::reset()`이 완료 fence 확인 후 `pool->allocator->Reset()`을 수행한다.
- **Vulkan (`src/backends/vulkan/Vulkan.cpp`)**: `DEVICE_FUNCTIONS`에 `vkResetCommandPool`·`vkFreeCommandBuffers`를 추가했다. `NativeCommandPool`이 단일 `VkCommandPool`을 영구 유지하고 풀 소속 `VulkanCommands`가 `VkCommandBuffer`와 `NativeQueryPool`을 epoch 간 재사용한다(epoch마다 `vkCreateCommandPool`/`vkAllocateCommandBuffers`/`vkCreateQueryPool` 재호출 없음). 파괴된 리스트의 `VkCommandBuffer`는 `retiredBuffers`에 보관했다가 안전한 `reset()` 시점에 `vkFreeCommandBuffers`로 해제한다.
- **Metal (`src/backends/metal/Metal.cpp`)**: `NativeCommandPool`·`MetalCommandPool`을 구현해 동일한 상태·epoch·in-flight·조기 파괴 계약을 제공하며 `operations`/`uses` 버퍼 용량을 재사용한다. (실제 macOS 실행 검증은 `P9-CMD-06`에서 수행)

## 3. 실제 빌드 및 검증 결과 (Windows)

- **환경**: MSVC 19.44.35228.0 / Windows SDK 10.0.26100.0 Release (`out/p9-command-pool`, `--parallel 1`, 경고 0 · 오류 0)
- **물리 GPU 2종**: NVIDIA GeForce GTX 1660 SUPER, Intel UHD Graphics 770
- **소프트웨어 어댑터**: Microsoft Basic Render Driver (WARP)
- **고정 VVL**: `out/vulkan-validation-runtime` (VVL 1.3.290, core+sync)

| 실행 대상 | 어댑터 수 | Assertions | Epochs | Submissions | Rejections | In-flight Reset Rejections | Validation 오류 | 결과 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `gfx_command_pool_tests --d3d12` | 2 | 2,390 | 64 | 140 | 216 | 2 | N/A | PASS |
| `gfx_command_pool_tests --d3d12 --validation` (Debug+GBV) | 2 | 2,624 | 64 | 144 | 214 | 0 | **0** | PASS |
| `gfx_command_pool_tests --d3d12 --warp` (+손실/복구) | 1 | 1,228 | 32 | 71 | 112 | 1 | N/A | PASS |
| `gfx_command_pool_tests --vulkan` | 2 | 2,390 | 64 | 140 | 216 | 2 | N/A | PASS |
| `gfx_command_pool_tests --vulkan --validation` (VVL 1.3.290) | 2 | 2,394 | 64 | 140 | 216 | 2 | **0** | PASS |
| 설치 소비자 (`find_package 0.0.1 EXACT`, 5모드 전체) | 2/2/1/2/2 | 동일 | 동일 | 동일 | 동일 | 2/0/1/2/2 | **0** | **5/5 PASS** |

- **CTest 회귀 (`out/p9-command-pool`)**: `core_contracts`, `render_pass_contracts`, `copy_atomicity_*`, `vulkan_transfer*`, `vulkan_draw*`, `vulkan_timestamps`, `buffer_region_*`, `texture_region_*`, `capabilities_*`, `viewport_*`, `command_pool_*` **34/34 통과** (27.00s).
- **검증 항목**: 32-epoch × 2리스트 재사용(컬러 패스 + `copyBufferRegion` + GPU 타임스탬프 전체 바이트/픽셀 일치), 상태기계 거부 보존, 미제출 기록 폐기 및 참조 즉시 회수, 기록 중 `CommandList` 파괴 복원, 다중 `CommandPool` 독립성, in-flight 중 `CommandPool` 파괴 후 완료 보장, 실 GPU 워크로드 in-flight `reset()` 거부, 타 스레드 거부, D3D12 WARP `removeForTesting()`/`recover()` 세대 격리, 종료 시 `resourceStatistics()` 0 전수 회수.

## 4. 미실행 및 잔여 조건

- Linux Vulkan(lavapipe/물리 GPU) 및 macOS arm64/Intel Metal 런타임 실행은 현재 Windows 환경에서 수행하지 않았으며 `P9-CMD-06`으로 분리해 추적한다.
- 스레드별 병렬 명령 기록 및 멀티스레드 CPU 성능 실측은 후속 `P9-CMD-02`에서 진행한다.
