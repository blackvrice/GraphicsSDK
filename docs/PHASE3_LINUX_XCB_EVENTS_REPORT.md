# P3-WSI-06 — XCB 숨김·최소화·닫기·연결 단절·surface 소실

2026-10-27. 제품·시험 기준은 `c0cc91b5e6b24d118d82396062e67a4525d24059`다.
일반·고정 core/sync validation의 XCB/Openbox 이벤트 검증을 통과했다. Windows 회귀는
63/65로 기존 fence 예외 두 개를 보존하며, Python 719개는 전부 통과했다.

## SDK에서 수정한 경로

기존 XCB acquire는 창을 조회했지만, 획득 뒤 창이 파괴된 경우 기록한 명령의 제출과 texture
연산에는 XCB 유효성 검사가 없었다. backbuffer 입력과 제출 전에 실제 window attributes를
확인하고, 이미 만료를 관측한 surface는 영구 만료로 둔다. 같은 숫자의 XID를 재사용해 새 창을
만들어도 이전 surface를 되살리지 않는다. 새 window에는 새 swapchain/surface를 사용한다.

Vulkan SURFACE_LOST를 관측하면 native surface에 소실 상태를 보관한다. acquire·present·resize는
InvalidState와 원래 nativeCode를 유지하고, 이전 recording/texture를 새 GPU 작업에 사용하지
못한다. 특히 같은 VkSurface로 resize를 반복하지 않고 새 native surface를 만드는 경로를
명시한다. 일반 OUT_OF_DATE의 같은 surface resize와 구분한다. surface capability/format/mode
질의와 native chain 생성의 소실 결과도 이 상태를 기록한다.

XCB 조회 함수는 private이고 libxcb를 동적으로 사용한다. 공개 헤더·Core hard link에 XCB/WM을
추가하지 않는다. SDK는 X 이벤트를 소비하거나 창을 숨기고 최소화하지 않는다. caller가
connection allocation을 SDK children 정리가 끝날 때까지 유지해야 한다. freed pointer를
안전하게 판별하는 기능으로 설명하지 않는다. 서버가 연결을 끊은 경우에도 allocation을
유지한 채 오류를 관측하고 정리한다.

창 상태 질의는 동기 XCB attribute snapshot이다. 서버·다른 클라이언트와 native 제출을 원자적
트랜잭션으로 묶는 기능이 아니며 IPC 비용도 있다. 오류를 관측한 뒤 재사용을 차단하는 초기
정확성 경로로 검증했다. 처리량·장시간 메모리·모든 compositor race 수용을 주장하지 않는다.

## caller-owned 이벤트 루프

[`examples/xcb_presentation`](../examples/xcb_presentation/README.md)은 설치 SDK를 사용하는 Linux
예제다. MapNotify/UnmapNotify와 WM_STATE로 활성·숨김·최소화 상태를 구분한다. 활성 시 기존
FramePacer로 60 fps 상한을 적용하고, 비활성·0 논리 크기에서는 poll의 무기한 이벤트 대기를
사용한다. acquire/resize를 반복해 pause 상태를 표현하지 않는다.

WM_DELETE_WINDOW는 종료 상태로 전환하고 chain을 drain한 뒤 caller 창을 파괴한다.
DestroyNotify와 XCB connection error도 새 프레임을 중단한다. OUT_OF_DATE는 geometry 확인 뒤
명시적 resize를 요청한다. SURFACE_LOST는 이전 chain을 해제해 다음 시도에서 새 VkSurface를
생성한다. 교체한 surface도 성공 프레임 전에 다시 소실되면 오류로 종료해 무한 재시도를 막는다.

SDK가 창이나 connection을 소유하는 API를 추가한 것은 아니다. 예제는 빌드하고, 공유 Session/
Policy 경로를 실제 이벤트 검증에서 사용했다. 독립 Main의 사용자 GUI 조작·물리 모니터 표시
수용은 WSI-08과 구분한다. 별도 native-independent policy executable도 CI에서 실행한다.

## 실제 창 서버·WM 검사

private Xvfb와 실제 Openbox 3.6.1을 실행한다. Openbox는 SDK 의존성이 아닌 외부 테스트
runtime이며 버전·실제 실행 파일 해시·dpkg 목록·WM 로그를 보존한다. SDK에 binary를 배포하지
않고, 일반 사용에서 Openbox를 강제하지 않는다.

| 경우 | 실제 확인 |
|---|---|
| 숨김·복원 | 실제 unmap/map, 비활성 frame suppression과 poll(-1), 복원 뒤 전체 이미지 |
| 최소화·복원 | WM_CHANGE_STATE를 실제 WM에 요청, WM_STATE=IconicState와 unmap 관측, NormalState 복귀 |
| 닫기 protocol | 실제 X ClientMessage 전달·처리, 새 작업 중단, 창 파괴 전 chain 정리. 사람의 버튼 클릭 수용은 아님 |
| 파괴된 창·XID 재사용 | 실제 서버 창 파괴, 보유 texture/recording의 제출·사용·acquire/present/resize 거부, 같은 XID 새 창으로도 이전 surface가 복원되지 않음, 새 chain의 정상 프레임 |
| 연결 단절 | 별도 client로 해당 test client를 실제 disconnect, allocation을 유지한 채 새 작업 거부와 event loop 종료 |
| acquire 소실 | native acquire 전에 SURFACE_LOST status control, 새 surface에서 정상 프레임 |
| present 소실 | 실제 present·semaphore 소비·completion fence를 유지한 상태 제어, 새 surface에서 정상 프레임 |
| 질의 소실 | resize의 surface capability 질의 status control, 같은 surface 반복 질의 차단과 새 surface 복구 |
| sticky loss | nativeCode 보존·이전 recording 거부·동일 surface resize 차단·새 surface 프레임 |
| 교체 실패 | 새 surface도 첫 성공 전에 소실되면 bounded error exit |
| 미완료 제출 중 닫기 | 실제 event로 native fence NOT_READY 확인, WM_DELETE 처리 중 DeviceWaitIdle 진입 후 해제, readback 대조와 창 파괴 전 GPU drain |

숨김/최소화/복원·창 파괴·연결 단절은 실제 X 서버/WM 동작이다. WM_DELETE 전달은 실제
protocol 전달이지만 사람의 close button 조작은 아니다. Vulkan SURFACE_LOST 값은 전용 proxy의
분기 제어이며 실제 driver가 자연스럽게 해당 값을 반환했다고 주장하지 않는다. present 제어는
실제 호출을 먼저 전달해 실제 semaphore 소비와 present fence 완료를 유지한다. reset/TDR이나
공개 SDK fault API는 사용하지 않았다.

[XCB의 WM 상호작용](https://xcb.freedesktop.org/windowcontextandmanipulation/)과
[ICCCM](https://xorg.freedesktop.org/archive/current/doc/xorg-docs/icccm/icccm.html)의 map/unmap·iconify·
WM_DELETE 규약에 따라 구분한다. present 소실에서의 semaphore 처리와 완료는
[Khronos present 규약](https://docs.vulkan.org/refpages/latest/refpages/source/vkQueuePresentKHR.html)을 따른다.

## 관측 결과와 수용 경계

한 구성에서 11개 경우·거부 17개·전체 이미지 참조 19개를 검증한다. 129×73 18개와 147×79 1개의
texel 724476 bytes, padding 1393행을 비교한다. 미완료 종료 readback은 padding 포함 56064 bytes다.
실제 present는 20회이며 모두 completion fence 연결과 성공 wait를 관측한다. 일부 status 제어
present와 별도 stale-recording invalidate present는 독립 이미지 참조 수와 1:1이 아니다.

software 조합에서 chain/surface 각각 17개, view/semaphore 각각 51개, fence 67개와 device 1개·
instance 2개의 생성·해제가 일치했다. 닫기 이후 submission/acquire/present 및 surface capability
질의 전달과 파괴 순서 오류는 0이다. 정리에 필요한 native destroy/wait나 CPU command-pool
생성까지 전부 금지했다는 의미는 아니다.

Windows는 이번 SDK로 전체 CTest를 재빌드·실행해 **63/65** 통과했다. 기존 memory_reclamation
GPU/validation 두 제외를 유지했고 추가 제외는 없다. 기존 fence_wait_gpu/validation 예외 두 개를
보존하며 전체 GPU 회귀 성공으로 바꾸지 않는다. Python 판정 검사는 새 8개를 포함해 **719개**다.

native Wayland/XWayland·다른 WM·물리 Linux GPU·사용자 육안 표시·실제 driver TDR과 surface loss
수용을 추론하지 않는다. 전체 다중 창·오류 검출 matrix는 WSI-07, 실제 사람의 Linux 표시는
WSI-08에 남긴다. 장시간 메모리 증가·Intel 원인 조사나 전체 Phase 3 완료가 아니다.

## source와 재현

Windows 실제 SDK fingerprint는
`197bd38300429ca10e77214bae33a269ec938cf78f112a66fb3182a37298582b`,
Linux Git은 `fdcb274bf57381a3b426f3c22b420d6719c3ba37e4ba038b6902a4b2bebb6f79`다.
이전 SDK `68254b70f26e547b89d11630b7ffb2d62f5600761cae306617dff04b94dfafa9`(Windows),
`01934251a058504334a20b1dc94c192ebf26fabef00e2e5649cb2977eb1daf78`(Linux)와 구분한다.
수정 전 세 파일을 보존한 source 분석과 수정 후 native 실행을 별도로 기록한다.

[전용 workflow](../.github/workflows/linux-xcb-events.yml)의 고정 headers·g++-14·XCB·Mesa·Xvfb·Openbox와
기존 VVL 1.3.290/XCB profile·dependency lock을 따른다. 실제 layer load·binary/manifest 해시,
빈 settings, 종료 시점까지 전달된 validation 오류를 확인한다. compiled binary·외부 runtime·
Python cache를 source ZIP/저장소에 배포하지 않는다.

실제 CI runtime은 Openbox `3.6.1-12build5`, libxcb1 `1.15-1ubuntu2`, libvulkan1
`1.3.275.0-1build1`, Mesa `25.2.8-0ubuntu0.24.04.4`, Xvfb `2:21.1.12-1ubuntu1.9`다.
각 실행의 원본 `window-runtime.txt`에 기록하며 다른 WM/driver의 검증으로 확대하지 않는다.

- [제품 CI](https://github.com/blackvrice/GraphicsSDK/actions/runs/40352831413): 일반·validation 둘 다 성공,
  별도 policy CTest 1/1. 같은 제품 커밋의 18개 workflow 성공은 원본 CI 목록으로 보존한다.
- [요약](../benchmarks/reports/2026-10-27-phase3-linux-xcb-events/summary.json),
  [원본 목록](../benchmarks/reports/validation/2026-10-27-phase3-linux-xcb-events/inventory.json),
  [GitHub artifact ID·해시](../benchmarks/reports/validation/2026-10-27-phase3-linux-xcb-events/artifacts.json).
- SDK 28개와 재현용 자체 소스 등 각 66개 파일의 Windows 실제 bytes/Linux Git bytes를
  [Windows manifest](../benchmarks/reports/validation/2026-10-27-phase3-linux-xcb-events/source-manifest-windows.json)와
  [Linux manifest](../benchmarks/reports/validation/2026-10-27-phase3-linux-xcb-events/source-manifest-linux-git.json)에 보존했다.
- [오프라인 검사](../benchmarks/reports/validation/2026-10-27-phase3-linux-xcb-events/verify.py)는
  Python으로 실행한다. 원본 ZIP·해제 파일·source ZIP·SDK fingerprint·baseline 세 파일·native 결과·
  layer/WM identity·Windows 예외·Python 719개를 재판정한다. native GPU 작업을 재실행하지 않는다.
  [검사 결과](../benchmarks/reports/validation/2026-10-27-phase3-linux-xcb-events/offline-verification.log).

수정 전 창 검사 누락은 source 분석이다. 수정 전 native 실패를 재실행한 결과로 바꾸지 않는다.
기존 WSI-05 결과와 fingerprint는 당시 소스로 유지한다. 이 작업은 `P3-WSI-06`만 완료 처리한다.
