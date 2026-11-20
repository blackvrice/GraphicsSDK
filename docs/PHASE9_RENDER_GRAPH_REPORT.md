# Phase 9 선택적 Render Graph (`P9-RG-01`) 보고서

- 작성일: 2026-11-20
- 대상 작업 ID: `P9-RG-01` (완료), `P9-RG-02` (Linux Vulkan 및 macOS Metal 실기/CI 실행 검증 분리 `todo`)
- 기준 환경: Windows 11, MSVC 19.44.35228.0 / Windows SDK 10.0.26100.0, NVIDIA GeForce GTX 1660 SUPER + Intel(R) UHD Graphics 770 + Microsoft Basic Render Driver (WARP), 고정 Windows Vulkan Validation Layers 1.3.290 (core + sync validation)
- 격리 빌드/설치 경로: `out/p9-render-graph`, `out/p9-render-graph-install`, `out/p9-render-graph-consumer`
- 설치 `GraphicsSDKCore.lib` SHA-256: `D206BD2E35C6478FEB3361845C477348F5CA178869B412CD2DA927EA32DF321E`

---

## 1. 구현 범위 및 아키텍처

기존 직접 `CommandList` / `CommandQueue` API, 13개 공개 헤더 구성(`tools/macos_metal_package.py`), 5개 `CORE` 번역 단위(`tools/macos_core.py`), Vulkan 기본 자원별 전용 할당 계약을 모두 보존하면서 상위 선택 모듈 `gfx::RenderGraph`를 `include/graphics/Device.hpp` 및 `src/foundation/Backend.cpp`에 구현했다. 백엔드 종속 코드 중복 없이 D3D12·Vulkan·Metal 공통 공개 RHI 위에서 동작한다.

### 공개 API 요약 (`include/graphics/Device.hpp`)
- 핸들·사용 기술자: `RGResourceHandle`, `RGResourceUse`, `RenderGraphResources`, `RGPassDesc`
- 진단·통계 구조체: `RGBarrierInfo`, `RGSyncEdge`, `RGCompileStats`
- `class RenderGraph`:
  - 리소스 선언: `createBuffer`, `createTexture`, `importBuffer`, `importTexture`
  - 패스 등록: `addPass(RGPassDesc)` (QueueType별 상태 호환성·Upload 쓰기/Readback 읽기·동일 패스 충돌 상태·비-storage UAV·비-RT RenderTarget 사전 거부)
  - 컴파일: `compile()` -> `Result<RGCompileStats>`
    - 명시적 `dependsOn` 및 리소스 RAW/WAW/WAR 의존성 DAG 구축
    - 결정적 선언 순서 Kahn 위상 정렬 및 순환 의존성(`ErrorCode::InvalidState`) 검출
    - `sideEffect` 패스 및 imported 리소스 쓰기 패스 기준 역방향 BFS로 미사용 패스·미사용 transient 리소스 자동 Culling
    - 연속 동일 `QueueType` 패스 배치 병합 및 교차 큐 의존성 `TimelineFence` 동기화 엣지 생성
    - 생존 transient 리소스 수명 구간 `[firstExec, lastExec]` 기반 Greedy Interval Coloring 물리 슬롯 Aliasing(교차 큐 재사용 시 암묵적 동기화 의존성 자동 추가)
    - 자동 상태 전이 배리어(`transition`), 연속 UAV 쓰기 후 `uavBarrier`, 교차 큐 `ResourceState::Common` 해제 배리어, transient 반납 전 `Common` 정규화, imported `finalState` 전이 계획
  - 실행: `execute(Device&, TransientResourcePool* = nullptr)`
    - 단일 배치는 `CommandQueue::submit(CommandList&)` + `Fence` 대기로 경량 실행
    - 다중 배치는 GPU-to-GPU 전용 `syncTimelines`(호스트 대기 미호출)와 큐별 최종 배치 호스트 완료 전용 `hostTimelines`를 분리해 반복 실행 시 VVL 1.3.290 SyncVal 호환성과 `CommandPool` 재사용을 동시 보장
    - `TransientResourcePool*` 전달 시 물리 슬롯만 획득하고 완료 후 즉시 반납해 프레임 간 100% 풀 재사용 달성; 미전달 시 실행 직후 해제되어 `resourceStatistics()` 누수 0 유지
  - 진단 및 정리: `dumpText()`, `dumpDot()`, `stats()`, `barriers()`, `syncEdges()`, `reset()`, `releaseGpuResources()`

---

## 2. 검증 결과

### 2.1 `gfx_render_graph_tests` 5개 실행 모드
모든 모드에서 CPU 계약(거부 20건, 순환 검출, Dead Pass/Transient Culling, Aliasing, `dumpText`/`dumpDot`), 4단계 복합 멀티큐 그래프(`Copy -> Compute(texture_generate) -> [DeadPass Culled] -> Graphics(sample fullscreen) -> Copy(readback)` 전체 16×16 픽셀 참조), 3개 비겹침 32×32 RenderTarget → 1 물리 슬롯 Aliasing 전 픽셀 참조, 연속 Compute UAV-to-UAV 자동 `uavBarrier` 전체 256요소 비트 일치, `resourceStatistics() == 0` 누수 검사를 수행했다.

| 모드 | 어댑터 | Validation 오류 | 결과 | 비고 |
| --- | --- | ---: | --- | --- |
| `--d3d12 --benchmark` | NVIDIA GeForce GTX 1660 SUPER | 0 (off) | **PASS** | `graphs_compiled=6, executed=6, rejections=20, assertions=9116` |
| `--d3d12 --validation` | NVIDIA GeForce GTX 1660 SUPER | **0** (Debug+GBV) | **PASS** | `graphs_compiled=6, executed=6, rejections=20, assertions=175` |
| `--d3d12 --warp` | Microsoft Basic Render Driver | 0 (off) | **PASS** | `removeForTesting` → `DeviceLost` → `recover()` 정상 실행 (`assertions=178`) |
| `--vulkan --benchmark` | NVIDIA GeForce GTX 1660 SUPER | 0 (off) | **PASS** | `graphs_compiled=6, executed=6, rejections=20, assertions=9109` |
| `--vulkan --validation` | NVIDIA GeForce GTX 1660 SUPER | **0** (Pinned VVL 1.3.290 core+sync) | **PASS** | `graphs_compiled=6, executed=6, rejections=20, assertions=171` |

### 2.2 독립 설치 소비자 (`find_package(GraphicsSDK 0.0.1 EXACT CONFIG REQUIRED)`)
`out/p9-render-graph-consumer`에서 Dead Pass Culling(1건) + 교차 큐 `Graphics -> Copy -> Graphics -> Copy` 2-RT Aliasing(`slots=1, aliased=1, syncs=3`) + `TransientResourcePool` 재사용(`hits=1, misses=1`) + 전체 픽셀 검증을 열거된 모든 어댑터에서 비검증·검증 별도 프로세스로 실행했다.

| 백엔드 | 어댑터 | 비검증 | 검증 (`val_errors`) |
| --- | --- | --- | --- |
| D3D12 | NVIDIA GeForce GTX 1660 SUPER | **PASS** | **PASS (`0`, Debug+GBV)** |
| D3D12 | Intel(R) UHD Graphics 770 | **PASS** | **PASS (`0`, Debug+GBV)** |
| D3D12 | Microsoft Basic Render Driver (WARP) | **PASS** | - (SW 어댑터 비검증 경로) |
| Vulkan | NVIDIA GeForce GTX 1660 SUPER | **PASS** | **PASS (`0`, VVL 1.3.290)** |
| Vulkan | Intel(R) UHD Graphics 770 (공유 큐 폴백) | **PASS** | **PASS (`0`, VVL 1.3.290)** |

### 2.3 전체 CTest 회귀
`ctest --test-dir out/p9-render-graph -C Release --output-on-failure` (고정 VVL 1.3.290 `VK_LAYER_PATH` 설정):
- **127 / 127 통과 (100%, 실패·건너뜀 0, 총 179.26초)** — CPU 계약 11종, Phase 8/9 전체, `render_graph_*` 5종, Vulkan 코어/타임스탬프/검증, D3D12 하드웨어/검증/WARP/복구 포함.

---

## 3. 수동 배리어 대비 성능 비교 (실측)

- 조건: 동일 어댑터(`NVIDIA GeForce GTX 1660 SUPER`), 동일 4단계 복합 워크로드(`Copy(256B CB) -> Compute(16x16 gradient) -> Graphics(fullscreen sample -> 16x16 RT) -> Copy(readback)`), 24프레임 반복, Warmup 2회 + Measured 5회 중앙값, Validation OFF, `TransientResourcePool` 물리 슬롯 3개 재사용(`pool_hits=1005, pool_misses=3`), 최종 픽셀 전체 일치 확인.
- CPU Wall 시간 기준(GPU timestamp 미측정: `null`).

| 백엔드 | 수동 배리어 24F (ms) | RenderGraph Precompiled 24F (ms) | Precompiled 대비 차이 | RenderGraph 매프레임 재구축+컴파일 24F (ms) | 매프레임 재구축 대비 차이 | 평균 `compile()` (µs) | GPU Timestamp |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| D3D12 | `59.149` | `57.328` | **`-3.08%`** | `65.953` | `+11.50%` | `9.473` | `null` |
| Vulkan | `43.684` | `42.800` | **`-2.02%`** | `45.483` | `+4.12%` | `7.857` | `null` |

- 사전 컴파일된 `RenderGraph`는 큐별 호스트 완료 타임라인 분리와 `CommandPool`/`TransientResourcePool` 재사용으로 수동 배리어와 동등하거나 미세 우세(`-2.0% ~ -3.1%`)한 24프레임 실행 시간을 보였다.
- 매 프레임 `reset()` → 패스 선언 → `compile()`까지 새로 수행하는 동적 경로에서도 컴파일 비용은 프레임당 **7.9 ~ 9.5 µs** 수준이며, 전체 24프레임 워크로드 대비 오버헤드는 Vulkan `+4.12%`, D3D12 `+11.50%` 범위다.

---

## 4. 미실행 범위 및 후속 분리

- **Linux Vulkan 및 macOS Metal 런타임 검증**은 현재 Windows 호스트에서 직접 실행할 수 없으므로 `P9-RG-02`로 분리해 `todo`로 유지한다.
- 물리 메모리 서브할당(Placed Resource / `vkBind*Memory` 기반 힙 Aliasing)은 `P8-MEM-02` 범위이며, 본 `P9-RG-01`의 Aliasing은 비겹침 수명 구간에 대한 `TransientResourcePool` 기반 **물리 슬롯(리소스 객체) 재사용**이다.
