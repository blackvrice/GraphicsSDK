# Vulkan 로더 초기화 실패·재시도 검증

2026-10-24 / Windows x64 / MSVC 19.44. 이전 제품 생성 출력 소유권 수정에서 남았던
**instance, debug messenger, device, Win32 surface의 실제 로더 실패 주입**을 추가했다.
테스트 레이어와 독립 설치 SDK 소비자를 구현했고, 이번 단계에서 제품 소스와 public API는
변경하지 않았다. 제품 fingerprint는
`2e511b8e53dbef4bbb2bf3d86ab4b19cf03e8765111de2b3dad4fe45fa551401`이다.

## 구현과 범위

[Own explicit layer/client](../tests/vulkan_loader_fault/README.md)는
[Khronos loader layer interface](https://github.com/KhronosGroup/Vulkan-Loader/blob/main/docs/LoaderLayerInterface.md)의
negotiation v2와 instance/device link chain을 따른다. 성공 핸들은 wrapping 없이 전달한다.
실패 전 controller가 기대 DLL의 절대 경로를 preload/pin하며, SDK 생성이 같은 모듈의
native ledger에 관측돼야 시험을 시작한다. 단순 DLL preload를 레이어 활성화 증거로 쓰지 않는다.

각 호출에 host OOM(-1), device OOM(-2)를 한 번씩 반환한다. 실제 heap 고갈이나 device
loss는 유도하지 않는다. 로더 소유 dispatchable 출력(instance/device)은 null이다.
Messenger/surface는 null 및 non-null poison을 각각 시험한다. 실패 출력 소유권은
[Vulkan 오류 계약](https://docs.vulkan.org/spec/latest/chapters/fundamentals.html#fundamentals-errorcodes)에
따라 판단한다. 무효 핸들 정리 시도는 own layer에서 검출·차단해 하위 VVL/driver로 보내지 않는다.
이전 제품의 오류를 관측하는 통제 시험에도 같은 차단을 적용한다.

Instance/device/messenger 실패 중에도 별도 guard device와 4KiB 데이터 buffer를 보유한다.
실패 전후 native live owner와 public resource statistics가 같아야 하며, 기존 내용을 덮어쓰지
않고 readback한다. 재시도 device에서도 실제 GPU copy/readback 후 명시적으로 정리한다.
Surface 재시도는 숨겨진 실제 HWND에 swapchain을 만들고 acquire/clear/copy/present한 뒤
321×197 전체 pixel과 staging row padding을 검사한다. 생성·파괴 ledger는 이 네 native
객체 종류의 소유권 증거이며, 모든 Vulkan 객체나 native 메모리 할당량의 계측이 아니다.

## 실제 결과

| 범위 | 현재 제품 프로세스 | 합성 오류·재시도 cycle | 검증 조건 |
|---|---:|---:|---|
| Instance | 8 | 24 | NVIDIA/Intel, validation OFF/ON, host/device OOM |
| Debug messenger | 8 | 24 | NVIDIA/Intel, validation ON, host/device OOM, null/poison |
| Device | 8 | 24 | NVIDIA/Intel, validation OFF/ON, host/device OOM |
| Win32 surface | 8 | 24 | NVIDIA, validation OFF/ON, host/device OOM, null/poison |
| 합계 | **32** | **96** | 현재 제품의 무효 정리 **0** |

NVIDIA GeForce GTX 1660 SUPER / Intel UHD Graphics 770을 exact name과 device/driver
identity, 환경 정보로 대조했다. Intel은 현재 제품에서 `caps().swapChain=false`이므로
표면 실패 주입을 지원 대상으로 세지 않는다. Probe에 두 GPU의 기능 결과를 보존했다.
이 결과는 Intel의 일반 Vulkan surface 가능 여부나 과거 충돌 원인을 판정하지 않는다.

현재 제품의 assertion **2,700**, full buffer readback **360회**, 재시도 후 present
**24회**, 검증한 buffer/pixel bytes **7,545,312**가 통과했다. 이 값은 row padding 검증량을
bytes 합계에 더하지 않는다. 네 종류 native 객체 **360개 생성·360개 파괴**, 최종 live=0,
무효 정리=0, trace 오류=0이다. Validation ON은 고정 VVL DLL의 실제 경로를 확인하고,
기존 guard와 재시도 device의 validation error count가 0이어야 한다. 정상 프로세스의
stderr도 비어 있어야 한다.

수정 전 설치 제품(`44b92cdf…`)에 같은 클라이언트를 링크한 **8개 통제 프로세스**에서
messenger/surface의 무효 정리 **24회**를 재현했다. 모두 테스트 레이어에서 차단됐으며,
유효 생성분은 명시적으로 파괴하고 live=0이었다. Instance/device의 poison 재현은 이
로더 프로파일에서 시도하지 않는다. 두 API의 null 오류 반환·오류 코드·재시도는 실제
현재 제품으로 확인했고 generic helper의 poison 계약은 앞선 CPU 시험에 남아 있다.

레이어 manifest 누락, 잘못된 DLL/GPU, 잘못된 CLI/지원되지 않는 fault profile의
**10개 fail-closed 프로세스**는 결과 JSON 없이 실패했다. Probe 1개를 더해 총 **51 commands**다.
CPU plan 계약은 before/after 빌드 각각 **216 assertions / GPU_calls=0**이다.
이번 단계의 Python 전체 시험은 **550개**, 건너뛴 시험 없이 통과했다.

제품 소스/설치 라이브러리는 그대로이므로 앞선 **55 selected product CTests**는
[이전 제품 증거](VULKAN_CREATION_OWNERSHIP_REPORT.md)의 결과로 구분한다. 이번에 그 전체
GPU gate를 다시 실행했다고 주장하지 않는다. 새 CPU plan과 실제 로더 하드웨어 프로파일은
별도 실행했다. 장시간 메모리 기준과 과거 Intel AV 조사는 계속 보류하며 세 memory
reclamation 테스트의 등록·정책을 바꾸거나 실행하지 않았다. Buffer pooling 채택이나
성능 우위·물리 메모리 회수에 관한 새 주장은 없다.

## 재현·증거

- [전체 결과](../benchmarks/results/2026-10-24-vulkan-loader-fault-v2/results.json),
  [raw command ledger](../benchmarks/results/2026-10-24-vulkan-loader-fault-v2/commands.json),
  [요약](../benchmarks/reports/2026-10-24-vulkan-loader-fault/summary.json).
- [빌드](../benchmarks/reports/validation/2026-10-24-vulkan-loader-fault/verification-scripts/build-v4.py),
  [실행](../benchmarks/reports/validation/2026-10-24-vulkan-loader-fault/verification-scripts/run-matrix.py),
  [검사](../benchmarks/reports/validation/2026-10-24-vulkan-loader-fault/verification-scripts/checks.py).
  MSVC Release `--parallel 1`, before/after 설치본과 별도 소비자 build root를 사용했다.
  Runner는 child별 Vulkan 환경을 정리하고 implicit layer를 비활성화한다.
- [의존성 inventory](../licenses/generated/windows-vulkan-loader-fault-release/inventory.json)는
  기존 보수적 closure에 새 소비자의 실제 compiler/linker read logs 각 6개를 추가한다.
  **915 inputs / 21 components**, 기존 36/35 logs와 새 6/6 logs를 구분한다.
  Vulkan-Headers 고정 pin의 `vulkan.h`와 Win32 선언을 명시적으로 검토·기록했다.
  새 third-party 구성요소나 바이너리 재배포는 없다. 이는 법률 승인이나 symbol 도달성의 증명이 아니다.
- [Evidence manifest](../benchmarks/reports/2026-10-24-vulkan-loader-fault/evidence.json)는
  source ZIP, 원시 결과, 빌드·검사 로그, 이전 증거와 23개 역사적 배포 ZIP의 hash를 보존한다.
  이전 소스 snapshot은 기존 검증 경로로만 해석하며 이전 inventory/report는 수정하지 않는다.

최초 빌드의 Win32 선언 include/enum initializer 오류, 첫 pilot의 Python/CMake 정렬 차이와
기존 UUID 문자열 표기 검증, 너무 작은 HWND 크기, 첫 전체 실행의 Intel 미지원 surface
사전조건 오류도 삭제하지 않았다. 빌드 로그, pilot 원시 출력, 최초 부분 실행을 보존했다.
최종 승인 matrix는 v4 소비자와 `…vulkan-loader-fault-v2` 결과이며 실패한 시도는 여기에 합산하지 않는다.

보고서 변조 검사는 먼저 정상 복사본이 통과하는지 확인한 뒤 단일 필드를 변조한다.
이 과정에서 Python의 `False == 0` 비교가 요약 복사본의 scalar 타입 변조를 놓치는 문제를
발견해 JSON 타입을 보존하는 비교로 수정했다. 최종 **12개 변조 결과 거부**, inventory
변조·notice 누락 **2개 거부**, 타입 비교와 schema의 불리언 거부 unit test를 포함했다.

다음 후보 승격 작업에는 mixed-size/occupancy/churn과 성능 재검증이 남아 있다.
이번 bounded 초기화 계약은 모든 Vulkan 오류나 모든 운영체제의 검증 완료를 뜻하지 않는다.
