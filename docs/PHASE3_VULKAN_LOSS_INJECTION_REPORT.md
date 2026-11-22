# Phase 3 Vulkan 손실 시험 경로 보고서 (`P3-CAP-03`)

- 작성일: 2026-11-22
- 관련 작업 ID: `P3-CAP-03` (완료 — opt-in 합성 손실 주입 계약, 실제 TDR/hot-unplug 경계 구분 및 전체 error coverage/복구 검증)
- 기준 산출물: `out/p3-loss-injection-install/lib/GraphicsSDKCore.lib` (SHA-256: `61EA780E0DAC585CFAC6D240B04EB0CCD187DEBCBF9A6C862C9B1AE0B7886AD2`)

---

## 1. 선택 방식 및 합성/실제 TDR 경계 (`src/backends/vulkan/Vulkan.cpp`)

- **Opt-in 계약 (`DeviceDesc::enableDeviceLossTesting`, 기본값 `false`)**:
  - `enableDeviceLossTesting == false`: `caps().deviceLossInjection == false`, `removeForTesting()`은 `ErrorCode::Unsupported` 반환(기존 계약 보존).
  - `enableDeviceLossTesting == true`: `caps().deviceLossInjection == true`, 생성 스레드에서 `removeForTesting()` 호출 시 대기 중 작업을 `vkDeviceWaitIdle`로 정리한 뒤 **합성(synthetic) 장치 손실 상태(`VK_ERROR_DEVICE_LOST`)**로 전환한다.
  - 타 스레드 호출은 `ErrorCode::InvalidState`, 이미 손실/퇴역된 장치 재호출은 `ErrorCode::DeviceLost`.
- **실제 하드웨어 TDR/Hot-Unplug 수용과의 경계**:
  - 본 경로는 애플리케이션의 복구 로직·오류 전파·세대 격리(generation retirement)·크래시 브레드크럼 보존을 결정적으로 테스트하기 위한 **통제된 합성 손실 시험 경로**이며, OS/드라이버에서 발생하는 예상 밖 하드웨어 TDR/hot-unplug 수용(`P2-ACC-05` 등)을 대체하지 않는다.

---

## 2. Error Coverage 및 복구 실기 검증 (`tests/VulkanLossInjectionTests.cpp`)

- **Error Coverage (손실 주입 직후)**: `checkStatus()`, `createBuffer()`, `createTexture()`, `createCommandList()`, `createTimelineFence()`, `queryFormatSupport()`, `queryMemoryBudget()`, `resourceStatistics()`, 재호출 `removeForTesting()` 모두 `ErrorCode::DeviceLost` (`nativeCode == VK_ERROR_DEVICE_LOST`) 반환, `diagnosticReport().deviceLost == true` 및 미완료 크래시 브레드크럼 보존 확인.
- **세대 복구 및 격리**: `recover()`로 동일 `adapterId`의 새 정상 세대 생성, 이중 `recover()` `InvalidState` 거부, 옛 세대 버퍼·텍스처의 새 세대 명령 목록 사용 시 `WrongDevice` 거부, 복구 후 64×64 Graphics Draw 전 픽셀 일치 및 고정 VVL 1.3.290 `validationErrorCount=0` 확인.

| 테스트 / 어댑터 | Assertions | Rejections | Validation 오류 | 결과 |
| --- | ---: | ---: | ---: | --- |
| `gfx_vulkan_loss_injection_tests` (GTX 1660 SUPER, Intel UHD 770) | 74 | 30 | N/A | **PASS** |
| `gfx_vulkan_loss_injection_tests --validation` (고정 VVL 1.3.290, 2 GPU) | 82 | 30 | **0** | **PASS** |
| `gfx_vulkan_tests --validation` (고정 VVL 1.3.290, 2 GPU) | 724 | - | **0** | **PASS** |

- CTest (`vulkan_loss_injection`, `vulkan_loss_injection_validation`, `vulkan_transfer`): **3/3 PASS (`3.94s`)**.
- 독립 설치 소비자 (`tests/consumer`, `find_package(GraphicsSDK CONFIG REQUIRED)`): 빌드 및 `consumer.exe` 실행 **PASS** (`GraphicsSDK 0.0.1`).
