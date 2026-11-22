# Phase 3 Vulkan Memory Budget 계약 보고서 (`P3-CAP-04`)

- 작성일: 2026-11-22
- 관련 작업 ID: `P3-CAP-04` (완료 — `VK_EXT_memory_budget` 기반 OS 프로세스 예산·사용량 계약 및 미지원 어댑터 `Unsupported` 보존)
- 기준 산출물: `out/p3-vulkan-budget-install/lib/GraphicsSDKCore.lib` (SHA-256: `7C690A14B1226E8380F8E03CCBA1422D490B28F67FDC08183872BA312E5EFAEF`)

---

## 1. 필드 매핑 및 의미 계약 (`src/backends/vulkan/Vulkan.cpp`)

`VK_EXT_memory_budget` 확장이 어댑터에서 노출된 경우에만 장치 확장으로 선택 활성화하고, Vulkan 1.1 core `vkGetPhysicalDeviceMemoryProperties2`에 `VkPhysicalDeviceMemoryBudgetPropertiesEXT`를 체이닝해 OS 프로세스별 실시간 메모리 예산과 사용량을 조회한다.

| `gfx::MemoryBudget` 필드 | `VK_EXT_memory_budget` 매핑 | 비고 |
| --- | --- | --- |
| `adapterId` | 선택된 Vulkan 어댑터 `AdapterId` | 정확 어댑터 일치 |
| `segment` | `Local` (`VK_MEMORY_HEAP_DEVICE_LOCAL_BIT` 힙 합산) / `NonLocal` (비-Device-Local 힙 합산) | 그 외 세그먼트 `InvalidArgument` 거부 |
| `budgetBytes` | 해당 세그먼트 힙들의 `heapBudget[i]` 포화 합산 | OS/드라이버 실시간 프로세스 예산 |
| `usageBytes` | 해당 세그먼트 힙들의 `heapUsage[i]` 포화 합산 | OS/드라이버 실시간 프로세스 사용량 |
| `availableForReservationBytes` | `0` | Vulkan 확장에 DXGI reservation 개념 부재 |
| `reservationBytes` | `0` | 합성 값 생성 금지 |

### 미지원 환경 원칙 (Heap Capacity 대체 금지)
- 어댑터/ICD가 `VK_EXT_memory_budget`을 노출하지 않으면 `queryMemoryBudget()`은 **절대 `VkMemoryHeap::size` (heap capacity)로 대체하지 않고** 명시적 `ErrorCode::Unsupported`를 반환한다.
- 생성 스레드 외 호출은 `InvalidState`, 손실/퇴역 장치는 `DeviceLost`를 인자 검사보다 먼저 반환한다.

---

## 2. 실기 검증 결과 (Windows 물리 GPU 2종 + 고정 VVL 1.3.290)

| 어댑터 | `VK_EXT_memory_budget` | Local `usageBytes` / `budgetBytes` | Validation 오류 | 결과 |
| --- | --- | --- | ---: | --- |
| NVIDIA GeForce GTX 1660 SUPER | 지원 | `11 MiB / 5,403 MiB` | **0** | **PASS** |
| Intel(R) UHD Graphics 770 | 지원 | `0 MiB / 48,149 MiB` | **0** | **PASS** |

- `gfx_vulkan_tests` (일반 & `--validation` 고정 VVL 1.3.290): `PASS assertions=721 transfers=48 verified_bytes=57042546 adapters=2`.
- `gfx_memory_pool_tests --vulkan --validation`: `TransientResourcePool::applyBudgetPressure()`가 Vulkan에서도 실측 OS VRAM 예산 압력을 정상 감지, `PASS assertions=385 frames=10 rejections=73`.
- `gfx_profiling_tests --vulkan --validation`: `Device::profilingSnapshot()`의 OS VRAM 예산 필드가 Vulkan에서도 실측값으로 정상 채워짐, `PASS val_errors=0 scopes_verified=37 assertions=465`.
- CTest (`memory_pool_vulkan`, `profiling_vulkan`, `vulkan_transfer`): **3/3 PASS (`2.22s`)**.
- 독립 설치 소비자 (`tests/consumer`, `find_package(GraphicsSDK CONFIG REQUIRED)`): 빌드 및 `consumer.exe` 실행 **PASS** (`GraphicsSDK 0.0.1`).
