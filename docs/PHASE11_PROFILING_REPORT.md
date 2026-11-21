# Phase 11 GPU Profiling 확장 (`P11-PROF-01`) 보고서

- 작성일: 2026-11-21
- 대상 작업 ID: `P11-PROF-01` (완료), `P11-PROF-02` (Linux Vulkan 및 macOS Metal 실기/CI 실행 검증 분리 `todo`)
- 기준 환경: Windows 11, MSVC 19.44.35228.0 / Windows SDK 10.0.26100.0, NVIDIA GeForce GTX 1660 SUPER + Intel(R) UHD Graphics 770 + Microsoft Basic Render Driver (WARP), 고정 Windows Vulkan Validation Layers 1.3.290 (core + sync validation)
- 격리 빌드/설치 경로: `out/p11-profiling`, `out/p11-profiling-install`, `out/p11-profiling-consumer`
- 설치 `GraphicsSDKCore.lib` SHA-256: `023578C2434DEFFB52A03DA0B7EE1BD74C24AE189B1E679D29B26A99493CD48C`

---

## 1. 구현 범위 및 계약

기존 13개 공개 헤더 구성(`tools/macos_metal_package.py`), 5개 `CORE` 번역 단위(`tools/macos_core.py`), 기존 2-slot 타임스탬프(`writeTimestamp(0/1)` / `resolveTimestamps()` / `elapsedGpuMilliseconds()`) 계약을 모두 보존하면서 다중 GPU Timestamp Scope, 계측 ON/OFF 분리, 정확한 명령/캐시/스톨 카운터, VRAM 예산 및 미측정 `std::nullopt` 계약을 추가했다.

### 공개 API 요약 (`Types.hpp`, `CommandList.hpp`, `Device.hpp`)
- 상수·구조체 (`include/graphics/Types.hpp`):
  - `kMaxTimestampScopesPerCommandList = 32` (명령 목록당 최대 32개 중첩/순차 GPU 스코프 = 64 슬롯, 기존 2-slot `0..1`과 독립된 슬롯 `2..65` 사용)
  - `TimestampScopeResult` (`name`, `queue`, `depth`, `startTicks`, `endTicks`, `std::optional<double> gpuMilliseconds`)
  - `ProfilingCounters` (`drawCalls`, `indexedDrawCalls`, `instancedDrawCalls`, `dispatchCalls`, `renderPasses`, `copyCommands`, `barriers`, `uavBarriers`, `submissions`, `pipelineCacheHits`, `pipelineCacheMisses`, `queueWaits`, `timelineWaits`)
  - `ProfilingSnapshot` (`enabled`, `counters`, `std::optional<double> cpuStallMilliseconds`, `std::optional<std::uint64_t> vramBudgetBytes`, `std::optional<std::uint64_t> vramUsageBytes`, `std::vector<TimestampScopeResult> gpuTimestampScopes`)
- 다중 GPU Timestamp Scope (`include/graphics/CommandList.hpp`):
  - `beginTimestampScope(std::string_view name)`, `endTimestampScope()`, `resolveTimestampScopes()`, `timestampScopes()`
  - D3D12: 66-slot `D3D12_QUERY_HEAP_TYPE_TIMESTAMP` + lazy `scopeReadback` (512B, 스코프 미사용 시 할당 0) + `GetTimestampFrequency` 하드웨어 변환
  - Vulkan: 66-slot `VkQueryPool` (`begin()` 시 `vkCmdResetQueryPool(0, 66)`) + `VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT` + 기존 검증된 `vulkan::timestampMilliseconds` valid-bit modular 변환기 재사용
  - Copy 큐 및 Metal: 정직하게 `ErrorCode::Unsupported` 반환 (CPU 시간으로 대체하지 않음)
- 장치 프로파일링 제어 (`include/graphics/Device.hpp`):
  - `setProfilingEnabled(bool)`, `isProfilingEnabled() const noexcept` (기본값 `false`), `profilingSnapshot()`, `resetProfilingStatistics()`
  - Thread-safe atomic `detail::ProfilingTracker` (`src/foundation/DeviceGeneration.hpp`)를 통해 병렬 명령 기록(`P9-CMD-02`)에서도 안전하게 수락된 명령만 정확히 카운팅(거부된 명령은 0 증가)
  - **계측 OFF `null` 계약**: `enabled == false`, 모든 카운터 `0`, `cpuStallMilliseconds == std::nullopt`, `vramBudgetBytes == std::nullopt`, `vramUsageBytes == std::nullopt`, `gpuTimestampScopes` 비어 있음
  - **미지원 항목 `null` 계약**: D3D12는 `queryMemoryBudget(Local)` 실측 VRAM 예산/사용량을 채우고, Vulkan/Metal 등 `Unsupported` 환경은 `vramBudgetBytes`/`vramUsageBytes`를 `std::nullopt`로 유지
  - **`RenderGraph` 자동 프로파일링 연동 (`src/foundation/Backend.cpp`)**: `device.isProfilingEnabled() && device.caps().timestampQueries`일 때 `Copy` 이외의 배치에서 패스 이름 기반 GPU Timestamp Scope를 자동 기록·수집

---

## 2. 검증 결과

### 2.1 `gfx_profiling_tests` 5개 실행 모드
모든 모드에서 계측 OFF `null` 계약, 파이프라인 캐시 hit/miss(1 miss + 2 hits), 거부 명령 비계수, 정확한 Draw/Indexed/Instanced/Dispatch/UAV Barrier/Copy/Barrier/RenderPass/Submit/Wait 카운터, Legacy 2-slot + 중첩/순차 5-scope 공존 및 시간 일관성, 32-scope 최대 용량 + 33번째 거부, `RenderGraph` 자동 GPU 스코프 연동, `resourceStatistics() == 0` 누수 검사를 통과했다.

| 모드 | 어댑터 | Validation 오류 | 검증 스코프 | 결과 | 비고 |
| --- | --- | ---: | ---: | --- | --- |
| `--d3d12 --benchmark` | NVIDIA GeForce GTX 1660 SUPER | 0 (off) | 37 | **PASS** | `rejections=18, assertions=21233`, `avg_gpu_scope_ms=0.002` |
| `--d3d12 --validation` | NVIDIA GeForce GTX 1660 SUPER | **0** (Debug+GBV) | 37 | **PASS** | `rejections=18, assertions=469` |
| `--d3d12 --warp` | Microsoft Basic Render Driver | 0 (off) | 37 | **PASS** | `removeForTesting` → `DeviceLost` → `recover()` 정상 (`assertions=476`) |
| `--vulkan --benchmark` | NVIDIA GeForce GTX 1660 SUPER | 0 (off) | 37 | **PASS** | `rejections=18, assertions=21507`, `avg_gpu_scope_ms=0.010` |
| `--vulkan --validation` | NVIDIA GeForce GTX 1660 SUPER | **0** (Pinned VVL 1.3.290 core+sync) | 37 | **PASS** | `rejections=18, assertions=465` |

### 2.2 계측 ON/OFF 분리 및 오버헤드 관측 (GTX 1660 SUPER, 20F × 64 draws = 1,280 draws, 5회 중앙값)
- D3D12: Profiling OFF `1.254 ms` vs Profiling ON (카운터 + 프레임당 GPU Timestamp Scope 포함) `1.624 ms` (GPU scope 평균 `0.002 ms`, `draws=1280` 정확 일치)
- Vulkan: Profiling OFF `2.860 ms` vs Profiling ON (카운터 + 프레임당 GPU Timestamp Scope 포함) `1.597 ms` (GPU scope 평균 `0.010 ms`, `draws=1280` 정확 일치; 짧은 20F 워크로드의 스케줄링 변동 범위 내)

### 2.3 독립 설치 소비자 (`find_package(GraphicsSDK 0.0.1 EXACT CONFIG REQUIRED)`)
`out/p11-profiling-consumer`에서 OFF `null` 계약 + ON 3-scope 중첩 GPU 타임스탬프 + 정확한 카운터 + VRAM 지원/미지원 `null` 계약 + `val_errors == 0`을 열거된 모든 어댑터에서 비검증·검증 별도 프로세스로 실행했다.

| 백엔드 | 어댑터 | 비검증 | 검증 (`val_errors`) | VRAM 보고 |
| --- | --- | --- | --- | --- |
| D3D12 | NVIDIA GeForce GTX 1660 SUPER | **PASS (`scopes=3`)** | **PASS (`0`, Debug+GBV)** | `yes` |
| D3D12 | Intel(R) UHD Graphics 770 | **PASS (`scopes=3`)** | **PASS (`0`, Debug+GBV)** | `yes` |
| D3D12 | Microsoft Basic Render Driver (WARP) | **PASS (`scopes=3`)** | - (SW 비검증 경로) | `yes` |
| Vulkan | NVIDIA GeForce GTX 1660 SUPER | **PASS (`scopes=3`)** | **PASS (`0`, VVL 1.3.290)** | `null` |
| Vulkan | Intel(R) UHD Graphics 770 | **PASS (`scopes=3`)** | **PASS (`0`, VVL 1.3.290)** | `null` |

### 2.4 전체 CTest 회귀
`ctest --test-dir out/p11-profiling -C Release --output-on-failure` (고정 VVL 1.3.290 `VK_LAYER_PATH` 설정):
- **132 / 132 통과 (100%, 실패·건너뜀 0, 총 202.36초)** — CPU 계약 11종, Phase 8/9 전체, `profiling_*` 5종, Vulkan 코어/타임스탬프/검증, D3D12 하드웨어/검증/WARP/복구 포함.

---

## 3. 미실행 범위 및 후속 분리

- **Linux Vulkan 및 macOS Metal 실기/CI 실행 검증**은 현재 Windows 호스트에서 직접 수행할 수 없으므로 `P11-PROF-02`로 분리해 `todo`로 유지한다.
- Metal GPU 하드웨어 타임스탬프 카운터(`MTLCounterSampleBuffer`) 구현은 물리 Mac 실기 검증 환경 확보 시 별도 확장하며, 현재 Metal 백엔드는 `beginTimestampScope`/`endTimestampScope`/`resolveTimestampScopes`/`timestampScopes`에서 정직하게 `ErrorCode::Unsupported`를 반환한다.
