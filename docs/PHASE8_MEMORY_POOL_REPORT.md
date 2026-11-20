# Phase 8 공개 Upload Ring · Readback/Transient Resource Pool 보고서 (`P8-MEM-01`)

- 기준일: 2026-11-20
- 대상 작업 ID: `P8-MEM-01` (완료: 공개 API · 공통 백엔드 구현 · Windows D3D12/Vulkan 실기/Validation/WARP/설치 소비자 검증 및 동일 조건 전후 실측; Linux Vulkan 및 macOS Metal 실기/CI 검증은 `P8-MEM-03`으로 분리)
- SDK 스냅샷: `0.0.1` (`GraphicsSDKCore.lib` SHA-256: `4F34C4C28073BE5007C44B1A553474895F49EA7A8CEA5FADEFADA66B69A8052E`)
- 격리 빌드/설치 경로: `out/p8-memory-pool`, `out/p8-memory-pool-install`

## 1. 공개 API 및 아키텍처 계약 (13개 공개 헤더 · 5개 CORE 유닛 유지)

Vulkan의 기본 `createBuffer`/`createTexture` 전용 할당 계약을 보존하면서, 명시적 opt-in 재사용 계층을 기존 [Resources.hpp](../include/graphics/Resources.hpp) · [Device.hpp](../include/graphics/Device.hpp) 및 공통 [Backend.cpp](../src/foundation/Backend.cpp)에 구현해 D3D12·Vulkan·Metal 3개 백엔드 전체에서 동일하게 동작하도록 구성했다.

| API | 역할 | 핵심 수명·동기화·오류 계약 |
| --- | --- | --- |
| `Device::createUploadRing(UploadRingDesc)` (`UploadRing`) | 세그먼트 기반(2..16개) Fence 회수형 스테이징 링 (`allocate`, `upload`, `commit`, `discardPending`, `reclaim`, `statistics`) | 활성 세그먼트 내 power-of-two 정렬 패킹(256B CB/행, 512B 텍스처 복사), 세그먼트 wrap, 전 세그먼트 포화 시 `OutOfMemory`, 미제출 롤백(`discardPending`), `upload()` 쓰기 실패 시 원자적 커서 복구 |
| `Device::createReadbackPool(ReadbackPoolDesc)` (`ReadbackPool`) | 2의 거듭제곱 버킷(최소 256B) 기반 `Memory::Readback` 풀 (`acquire`, `recycle`, `reclaim`, `trim`, `statistics`) | Fence 완료 전 재사용 차단, 중복 반환(`InvalidState`)·타 장치/외부 버퍼(`WrongDevice`) 거부, LRU 상한(`maxPooledBytes`/`maxPooledBuffers`) 자동 축출 |
| `Device::createTransientResourcePool(TransientPoolDesc)` (`TransientResourcePool`) | 버퍼·텍스처 겸용 임시 자원 풀 + `BudgetPressurePolicy` (`acquireBuffer`, `acquireTexture`, `recycle`, `reclaim`, `trim`, `applyBudgetPressure`) | 버퍼(메모리·storage·2x 이내 버킷) 및 텍스처(정확한 `TextureDesc`) 재사용, OS VRAM 예산 조회(D3D12 실측 / Vulkan `Unavailable` 정직 반환) 및 결정적 `Normal`/`Soft`/`Hard` LRU 축출 |
| `Device` 소멸 가드 (`lifetimeGuard`) | 장치 파괴 후 풀 호출 보호 | `Device` 파괴 뒤 풀 메서드 호출 시 크래시 없이 `DeviceLost` 반환; WARP `removeForTesting()` 시 전 연산 `DeviceLost` 전파 및 `recover()` 후 정상 재생성 |

또한 `P9-CMD-03`의 D3D12 Copy 큐에서 동일 명령 목록 내 `COPY_DEST -> COPY_SOURCE` 텍스처 전환 시 Windows 11 D3D12 Debug+GBV `#1334`가 발생하지 않도록 `StateAfter = D3D12_RESOURCE_STATE_COMMON` 전이 후 다음 복사에서 안전하게 재승격하도록 [D3D12.cpp](../src/backends/d3d12/D3D12.cpp)를 보강했다.

## 2. 실제 검증 결과 (Windows)

### 2.1 `gfx_memory_pool_tests` 5개 모드 전체 결과

| 실행 모드 | 어댑터 | 결과 | Assertions | Rejections | Validation 오류 |
| --- | --- | --- | ---: | ---: | ---: |
| `--d3d12 --benchmark` | NVIDIA GeForce GTX 1660 SUPER | **PASS** | 18,129 | 73 | 미요청 |
| `--d3d12 --validation` | NVIDIA GeForce GTX 1660 SUPER | **PASS** | 385 | 73 | **0** (Debug + GBV) |
| `--d3d12 --warp` | Microsoft Basic Render Driver | **PASS** (소멸 순서 + 손실·복구 포함) | 754 | 137 | 미요청 |
| `--vulkan --benchmark` | NVIDIA GeForce GTX 1660 SUPER | **PASS** | 18,129 | 73 | 미요청 |
| `--vulkan --validation` | NVIDIA GeForce GTX 1660 SUPER | **PASS** | 385 | 73 | **0** (고정 VVL 1.3.290 core+sync) |

검증 항목:
- CPU 디스크립터 경계 18건(`validateUploadRingDesc`, `validateReadbackPoolDesc`, `validateTransientPoolDesc`)
- `UploadRing`: Copy 큐 3세그먼트 × 4 KiB 링에서 정렬(256B/512B), `discardPending()`, 3세그먼트 포화 `OutOfMemory`, 10프레임 연속 wrap(≥ 3회전) 전 바이트 readback 일치, 512B 정렬 `copyBufferToTextureRegion` 16×16 RGBA8 전 픽셀 일치
- `ReadbackPool`: 버킷 재사용 적중, Fence 회수, 중복/외부/오큐 거부, `maxPooledBuffers` 상한 축출 및 `trim(0)`
- `TransientResourcePool`: 버퍼·`RenderTarget` 텍스처 재사용 적중(동일 포인터 재사용 + 2회 렌더 픽셀 검증), D3D12 실측 OS VRAM 예산 조회 / Vulkan `Unavailable` 정직 반환, 결정적 `Normal(50%)` / `Soft(85%)` / `Hard(95%)` 단계별 축출
- `resourceStatistics()` 전체 항목 0 회수, 장치 선소멸 후 `DeviceLost`, WARP `removeForTesting()`/`recover()`

### 2.2 동일 조건 전후 CPU/GPU/자원 실측 비교표 (GTX 1660 SUPER)

- 워크로드: 24프레임 × (16회 × 16 KiB 버퍼 업로드 = 256 KiB/프레임 + 256×256 RGBA8 임시 RenderTarget Clear/복사 + Readback), Warmup 1회 + 측정 5회 중앙값, validation OFF, 동일 GPU·동일 커맨드 시퀀스.
- **Baseline**: 매 프레임 `createBuffer(Upload)` × 16 + `createTexture(256×256)` + `createBuffer(Readback)` 신규 생성·파괴.
- **Pooled**: `UploadRing`(3 × 512 KiB) + `TransientResourcePool` + `ReadbackPool` 재사용(초기 프레임 이후 신규 생성 0회).

| 백엔드 | 측정 항목 | Baseline (매 프레임 신규 할당) | Pooled (`UploadRing` + Pools) | 개선 효과 |
| --- | --- | ---: | ---: | --- |
| **D3D12** (GTX 1660 SUPER) | Wall-clock 중앙값 (24프레임) | `107.112 ms` | **`7.833 ms`** | **13.675x 가속** |
| **D3D12** | GPU Timestamp 합계 중앙값 | `0.932 ms` | `0.926 ms` | 동일 GPU 작업 확인 |
| **D3D12** | 피크 고유 SDK 자원 수 / 할당 바이트 | `20개` / `1,900,544 B` | `7개` / `2,424,832 B` | 고유 자원 **65% 절감**, 풀 적중 `46/48` |
| **Vulkan** (GTX 1660 SUPER) | Wall-clock 중앙값 (24프레임) | `71.534 ms` | **`7.217 ms`** | **9.912x 가속** |
| **Vulkan** | GPU Timestamp 합계 중앙값 | `2.097 ms` | `2.067 ms` | 동일 GPU 작업 확인 |
| **Vulkan** | 피크 고유 SDK 자원 수 / 할당 바이트 | `19개` / `1,048,576 B` | `6개` / `2,359,296 B` | 고유 자원 **68% 절감**, 풀 적중 `46/48` |

> [!NOTE]
> Pooled 모드의 피크 할당 바이트는 3-세그먼트 `UploadRing`(1.5 MiB)을 사전 예약한 고정 워킹셋이며, 프레임이 반복되어도 추가 드라이버 메모리 할당·해제가 발생하지 않는다.

### 2.3 독립 설치 소비자 및 CTest 회귀

- **설치 `find_package(GraphicsSDK 0.0.1 EXACT CONFIG REQUIRED)` 소비자**: D3D12(GTX 1660 SUPER · Intel UHD 770 · WARP) 및 Vulkan(GTX 1660 SUPER · Intel UHD 770) 비검증·검증 별도 프로세스 전체 통과 (`validationErrorCount == 0`).
- **CTest 회귀**: 11개 CPU 계약 + Phase 8/9 전체 + `memory_pool_*` 5종 + `multi_queue_*` 5종 + Vulkan 코어/타임스탬프/검증 **73/73 통과 (100%, 47.21초)**.

## 3. 미실행 경계

- **Linux Vulkan 및 macOS Metal 실기/CI 실행 미실행**: 본 검증은 Windows 물리 GPU 2종·WARP 기준이며, Linux Vulkan과 macOS Metal 환경에서의 빌드·실행 검증은 `P8-MEM-03`으로 분리해 `todo`로 유지한다.
