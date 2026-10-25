# Phase 3 — Windows Vulkan GPU·드라이버·출력 지원 정책

2026-10-25 · 작업 `P3-SCP-02` · 기준 커밋 `5d48c0d`.

이 문서는 초기 개발 snapshot의 Windows 지원 조합과 fallback 결정을 고정한다. 제품 SDK fingerprint는 `2e511b8e53dbef4bbb2bf3d86ab4b19cf03e8765111de2b3dad4fe45fa551401`이며 SDK 코드·ABI·드라이버를 변경하거나 재빌드하지 않았다. 기존 제품 binary로 짧은 실제 출력 검사를 다시 수행했다. 지원표는 아래 정확한 장치·드라이버·OS와 관측한 범위에 한정한다. Phase 3 최종 회귀·출시 수용은 `P3-GATE-01`/`P3-GATE-03`에 남아 있다.

## 지원 조합과 실행 근거

현재 OS는 **Windows 10.0.26220 build 26220, 64-bit**다. 드라이버 번호는 WMI `Win32_VideoController.DriverVersion`의 Windows 패키지 버전이며 Vulkan의 packed `driverVersion`이나 제조사 마케팅 버전으로 변환하지 않았다. VEN/DEV는 장치 식별용이고 공개 API의 backend-scoped `AdapterId`를 대신하지 않는다.

| GPU / VEN:DEV / Windows driver | Offscreen | Win32 출력 | 초기 배포 정책 |
|---|---|---|---|
| NVIDIA GeForce GTX 1660 SUPER / `10DE:21C4` / `32.0.16.2101` | 기존 기본 GPU suite 근거 유지 | 이번 제품 suite에서 실제 생성·acquire/present·readback·resize·수명 검사. `swapchainMaintenance1=1`, `caps.swapChain=true` 경로 | 이 정확한 Windows 조합을 출력 지원 후보로 유지. 최종 통합 회귀 뒤에만 해당 범위의 출시 수용 |
| Intel(R) UHD Graphics 770 / `8086:A780` / `32.0.101.6129` | 기존 여덟 기본 offscreen suite 근거 유지. 이번에 그 전체 suite를 재실행한 것은 아님 | native 조회에서 `VK_EXT_swapchain_maintenance1` 없음, feature 0. 제품 suite에서 caps=false 및 createSwapChain Unsupported 확인 | 이 조합의 Vulkan offscreen은 유지하며 Win32 출력은 미지원. Intel 전체 제품군·다른 드라이버까지 확대하지 않음 |
| 다른 NVIDIA/Intel/AMD GPU, 다른 드라이버·OS build, Vulkan software ICD | 미검증 | 미검증 | 기능 조회만으로 배포 검증 완료로 간주하지 않음. 범위 확대 시 `P3-CAP-06` 실행 근거 필요 |

Intel의 미지원 응답은 이 driver의 출력 완료 기능 조회 결과다. 과거 Intel AV 원인이나 하드웨어 불량을 의미하지 않는다. D3D12 지원·복구 근거와도 별도이며 이번 작업은 D3D12를 재검증하지 않았다.

새 [관측 요약·해시](../benchmarks/reports/2026-10-25-phase3-windows-support/summary.json), [Windows metadata](../benchmarks/reports/validation/2026-10-25-phase3-windows-support/run/windows-drivers.stdout), [native WSI 조회](../benchmarks/reports/validation/2026-10-25-phase3-windows-support/run/native-wsi-query.stdout), [driver-only](../benchmarks/reports/validation/2026-10-25-phase3-windows-support/run/presentation-driver.stdout), [required validation](../benchmarks/reports/validation/2026-10-25-phase3-windows-support/run/presentation-validation.stdout)을 보존한다. 과거 offscreen/출력 증거는 [기존 출력 보고서](PHASE3_VULKAN_PRESENTATION_REPORT.md)와 해당 fingerprint에 남긴다.

## 출력 지원 판정 순서

판정은 제조사 이름이나 드라이버 버전 allowlist 대신 native 기능과 실제 surface 조건을 사용한다. 위 표는 검증 범위이며 런타임 코드에 새 whitelist를 넣은 것이 아니다.

1. Vulkan 1.1 loader/ICD와 graphics queue를 가진 정확한 adapter를 선택한다. adapter/software-kind 불일치나 없는 ID는 실패하며 다른 장치를 고르지 않는다.
2. Win32 instance에 `VK_KHR_surface`, `VK_KHR_win32_surface`, `VK_KHR_get_surface_capabilities2`, `VK_EXT_surface_maintenance1`이 있어야 한다.
3. device는 `VK_KHR_swapchain`, `VK_EXT_swapchain_maintenance1`과 `swapchainMaintenance1=true`가 필요하다. 이 조건으로 `caps.swapChain`을 설정한다. 확장 문자열만 있어서는 충분하지 않다.
4. createSwapChain에서 같은 process·생성 thread의 live HWND와 선택 graphics queue의 해당 surface presentation 지원을 검사한다. 별도 present queue나 다른 GPU로 바꾸지 않는다.
5. RGBA8 UNORM + SRGB nonlinear, color-attachment/transfer-src/transfer-dst usage, 실제 client extent·surface 한도, 요청 2..4 buffers를 검사한다. VSync는 FIFO, 비동기는 IMMEDIATE 우선·MAILBOX 차선이다. 비동기 mode가 없으면 Unsupported다.

`caps.swapChain=true`여도 4~5 단계에서 실패할 수 있다. Intel의 표면 조회에 RGBA8·FIFO/IMMEDIATE가 보이는 것은 3단계의 출력 완료 계약을 충족했다는 뜻이 아니다. 현재 pinned Vulkan-Headers v1.3.290 코드가 조회·활성화하는 이름은 **EXT**다. KHR 이름만 제공하는 미래 runtime을 지금 코드가 지원한다고 가정하지 않는다.

실제 판정은 [Vulkan instance/device 구현](../src/backends/vulkan/Vulkan.cpp)과 [surface/swapchain 구현](../src/backends/vulkan/Presentation.hpp)을 따른다. 이 조건을 `P3-GATE-01`의 최종 구성에서 다시 확인한다.

## Fallback 채택 결정

| 방식 | 결정 | 근거와 후속 조건 |
|---|---|---|
| 현재 maintenance1 present fence | **유지** | image별 semaphore와 매 present의 완료 fence를 사용한다. fence 대기 뒤 반환하므로 마지막 present 이후 추가 acquire가 없어도 출력 자원을 해제할 근거가 있다. monitor scanout 완료를 뜻하지는 않음 |
| maintenance1 없이 Queue/DeviceWaitIdle만 호출 후 파괴 | **미채택** | unextended present는 일반 submit fence와 같은 완료 관측 경로를 제공하지 않는다. WaitIdle만으로 최종 present의 semaphore/swapchain 수명을 정당화하지 않음 |
| image별 semaphore + 동일 image 재획득만으로 legacy 경로 추가 | **현재 미채택** | 재획득은 해당 image의 이전 present wait semaphore 재사용에는 도움이 된다. shutdown·surface loss·실패한 resize에서 마지막 image를 다시 획득하지 않는 경우까지 다루는 해제 계약이 별도로 필요 |
| present-wait/present-ID 또는 KHR maintenance 경로 | **미구현·미채택** | 다른 경로가 불가능하다고 결론 내린 것이 아니다. 실제 extension/feature·정상/오류·teardown 계약과 장치별 증거를 먼저 검토해야 한다. 현재 EXT 경로의 근거를 그대로 재사용할 수 없음 |
| Intel에서 다른 GPU나 D3D12로 자동 변경 | **미채택** | exact-adapter 선택과 자원·shader·generation 계약이 달라진다. 앱이 명시적으로 새 backend/device를 선택하는 것은 별도 흐름이며 SDK의 자동 fallback으로 주장하지 않음 |
| 드라이버 설치/업데이트, 기능 강제 활성화, BGRA를 RGBA로 간주 | **수행하지 않음** | 시스템 정책 변경이나 포맷 의미 변경으로 미지원을 숨기지 않는다. 향후 다른 driver는 독립 검증 대상으로 추가 |

출력 semaphore의 재사용과 마지막 출력 자원 해제의 차이는 [Khronos guide](https://docs.vulkan.org/guide/latest/swapchain_semaphore_reuse.html)를 확인했다. present fence의 신호 뒤 semaphore/swapchain 파괴 가능 조건과 scanout 완료의 차이는 [Khronos present-fence 명세](https://docs.vulkan.org/refpages/latest/refpages/source/VkSwapchainPresentFenceInfoKHR.html)를 따른다. 위 fallback 미채택은 이 SDK의 현재 계약·검증 범위에 대한 결정이다.

## 이번 검증 범위

| 구성 | 실제 결과 |
|---|---|
| 현재 제품, driver-only | 721 assertion, 145 presents, 132 full images, 60 draws, 7 resizes, 35,918,112 texel bytes |
| 같은 제품, core/synchronization validation | 동일 수량 통과, SDK validation ERROR 0 |
| 두 실행의 adapter 판정 | 각각 NVIDIA 출력 1개 / Intel 명시적 미지원 1개. Intel present 성공으로 합산하지 않음 |
| 합계 | 1,442 assertion, 290 presents, 264 images, 120 draws, 14 resizes, 71,836,224 texel bytes |

각 실행은 요청 2/3/4 buffers와 VSync on/off, stale/foreign/thread/retire와 파괴 시 수명을 기존 suite로 검사했다. 모두 미지원인 경우 suite는 실패한다. 설치된 드라이버 metadata와 native query는 같은 실행 묶음에서 관측했다. native query는 보존된 자체 diagnostic executable을 기존 해시와 대조해 재사용했고, 제품 실행은 출력에 기록한 현재 fingerprint를 검사했다. local validation runtime의 DLL/JSON은 전후 해시를 확인하고 설정은 child process에만 적용했다.

사람의 화면 관찰·monitor/compositor pixel·다중 모니터/DPI·물리 TDR/hot-unplug·일반 성능·장시간 메모리·Linux 수용은 이번 검증에 포함하지 않는다. 새 설치 소비자 실행·전체 backend 회귀·shader 재컴파일도 수행하지 않았다. `P3-GATE-01`과 관련 잔여 작업의 별도 근거가 필요하다.

재현 runner는 [run.py](../benchmarks/reports/validation/2026-10-25-phase3-windows-support/run.py)다. 보존된 local build/runtime이 필요하고 같은 근거 폴더를 덮어쓰지 않도록 기존 결과가 있으면 실패한다. 명령과 stdout/stderr는 [commands.json](../benchmarks/reports/validation/2026-10-25-phase3-windows-support/run/commands.json)에 연결한다.

## 추가 개발·검증 대상

| ID | 이번 결정 | 완료 전에 필요한 근거 |
|---|---|---|
| `P3-CAP-05` Intel 출력 확대 | 조건부·미완료 유지. 현재 Intel driver용 legacy fallback은 초기 지원 범위에서 제외 | 선택 시 새 완료 경로의 설계·정상/실패·최종 present/resize/surface loss·teardown 검증. 해당 Intel GPU/driver의 실제 present·readback·validation 필요 |
| `P3-CAP-06` 추가 GPU/driver | 조건부·미완료 유지. 초기 후보는 표의 정확한 조합으로 제한 | 다른 GPU·driver·OS 또는 software ICD로 범위를 넓히기 전 정확한 식별·feature/queue/surface·driver/validation·수명·설치 소비자 근거. 미보유 환경은 통과로 세지 않음 |
| `P3-GATE-01` 현재 지원 구성 통합 회귀 | 필수·미완료 유지 | Windows 위 정책과 Linux 필수 구성·marker·관련 모든 결과를 최종 source로 검사. 선택 기능의 제외 이유와 새 미지원 응답도 재확인 |
| `P3-GATE-03` Phase 3 완료 판정 | 필수·미완료 유지 | 통합 회귀와 비교·감사, 필수 잔여 완료 후 초기 지원 범위 수용을 기록 |

`P3-SCP-02`만 이 지원 정책·정확한 조합·fallback 결정의 근거를 갖춰 완료한다. 조건부 출력 확대와 추가 장치 검증을 대신 완료하지 않으며 보류된 장시간 메모리·Intel AV 조사는 그대로 유지한다.
