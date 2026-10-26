# P3-WSI-05 — XCB resize·상태·재생성 실패와 재시도

2026-10-26 개발·native 실행, 2026-10-27 완료 기록. 제품·시험 기준은
`54c14af29df976fc712b86fb45702776fb8261b2`다.
일반 XCB 실행과 고정 core/sync validation이 모두 통과했다. Windows 회귀·Python 판정 결과와
수정 전후 SDK source의 바이트를 별도로 보존한다.

## 수정한 대기 순서

기존 `resize()`는 GPU 대기 전에 texture wrapper와 Allocation의 추가 참조를 함께 검사했다.
공개 CommandList와 TexturePtr를 모두 해제해도 실제 미완료 제출이 Allocation을 보유하므로,
SDK 내부의 pending 참조까지 caller의 보유 참조처럼 판단해 InvalidState를 반환하는 순서였다.
이는 공개 참조를 해제한 뒤 queued 작업을 기다리는 기존 resize 계약을 충족하지 못한다.

공개 texture wrapper는 먼저 검사하되, DeviceWaitIdle과 pending 정리를 수행한 뒤 Allocation의
남은 참조를 검사하도록 순서를 바꿨다. 아직 보유한 공개 recording은 완료 뒤에도 resize를
막는다. native 재생성 전에 거부하므로 기존 chain을 유지한다. 실패한 configuration 검사와
wrapper 참조 거부도 기존 chain을 보존한다. 새 공개 API/ABI나 자동 창 크기 변경은 추가하지 않았다.

원래 순서의 문제는 보존한 소스의 참조 검사 위치에서 확인한 것이다. 수정 전 SDK로 같은
GPU 실패를 재현했다고 표시하지 않는다. 수정 후에는 실제 미완료 fence와 host event 대기로
SDK 내부만 보유한 제출을 만들고, resize의 실제 대기·정리·재생성·전체 readback을 검증했다.

## 실제 창·재생성과 보유 참조

설치된 Core만 사용하는 외부 소비자에서 실제 Xvfb/XCB 창을 65×7에서 83×11, 17×9, 1×1,
129×13, 65×7로 바꾼다. 각 native configure 요청과 geometry reply가 일치해야 한다.
재생성 뒤 texture description·native BGRA8·footprint와 전체 clear/draw/blend texel·padding을
대조한다. 1×1에서는 유효한 clear 참조만 사용한다.

창을 바꾸지 않고 다른 extent를 요청하거나 0 크기를 요청하면 InvalidArgument이고 이전 창
프레임을 계속 사용할 수 있어야 한다. 창을 바꾼 뒤 wrapper 또는 recording을 보유한 상태의
resize는 InvalidState이며 native CreateSwapchain 호출 수가 증가하면 실패다.

미완료 제출 경우는 실제 VkEvent 대기로 native submission fence의 NOT_READY를 관측한다.
공개 recording·image를 해제한 뒤 창을 바꾸고 resize한다. 전용 observer가 DeviceWaitIdle 진입을
관측하면 보조 thread가 event를 해제한다. 완료된 이전 이미지의 red readback과 새 extent의
clear/draw/blend를 독립 대조한다. 시험은 software adapter 한 개에서만 실행한다.

## out-of-date와 suboptimal 제어

네 가지 status control은 SDK의 분기 검증이다. 실제 창 서버가 자연스럽게 그 상태를 반환한
수용과 구분하며 창 소실·최소화·닫기 수용으로 확대하지 않는다.

| 제어 | 실제 호출·동기화 | SDK 기대 동작 |
|---|---|---|
| acquire out-of-date | native acquire 전에 합성 OUT_OF_DATE 반환. fence는 아직 제출되지 않음 | InvalidState, 후속 acquire/present의 native 호출 차단, 명시적 resize 뒤 새 프레임 |
| acquire suboptimal | 실제 acquire 성공과 실제 acquisition fence를 유지한 채 반환 상태만 SUBOPTIMAL로 변경 | 받은 이미지를 정상 사용·present하고 명시적 resize 뒤 새 프레임 |
| present out-of-date | 실제 native present를 먼저 수행해 semaphore 소비·completion fence signal을 유지하고 반환 상태를 OUT_OF_DATE로 변경 | 실제 완료 fence wait 후 InvalidState. 이전 recording과 후속 native 사용 차단, resize 재시도 |
| present suboptimal | 실제 native present 성공 뒤 상태만 SUBOPTIMAL로 변경 | 실제 완료 fence wait 후 성공. 이전 recording은 stale이며 명시적 resize 뒤 새 프레임 |

제어가 실제 GPU 성공·이미지 획득·fence 완료를 합성하지 않는다. present OUT_OF_DATE 제어는
실제 native present가 수행됐으므로 native present 수에 포함한다. OS에서 발생한 native 오류
횟수로 세지 않는다. SUBOPTIMAL은 공개 상태 알림을 추가하지 않고 기존의 사용 가능한 성공
계약을 유지한다. 호출자가 geometry 변경을 확인하고 명시적으로 resize한다.

## 실패한 재생성과 oldSwapchain

[Khronos oldSwapchain 계약](https://docs.vulkan.org/refpages/latest/refpages/source/VkSwapchainCreateInfoKHR.html)에
따라 native 재생성 호출에 전달한 이전 chain은 생성 실패 후에도 새 acquire 대상으로 되돌릴 수 없다.
세 가지 제어를 각각 실행한다.

- 실제 CreateSwapchain에 allocation callback을 전달하고 1회 할당 요청을 거부해
  SDK OutOfMemory를 받는다. 의도적인 callback 실패이며 실제 시스템 메모리 고갈이 아니다.
- 새 native chain 생성 성공 뒤 첫 image enumeration을 합성 OutOfHostMemory로 거부한다.
- 새 native chain 생성 뒤 두 번째 image view 생성을 합성 OutOfHostMemory로 거부한다.

각 시도는 살아 있는 이전 handle을 oldSwapchain으로 전달해야 한다. 실패 뒤 이전·부분 생성
chain을 파괴하고 acquire/present를 거부해야 하며, 잘못된 0 크기 재시도가 이를 되살리면 실패다.
그 다음 유효한 resize는 NULL oldSwapchain으로 새 chain을 생성하고 새 전체 프레임을 검증한다.
관찰자는 retired handle로 native acquire를 전달하거나 이미 파괴한 handle을 oldSwapchain으로
전달하는지 검사한다. driver의 내부 retired flag를 별도 질의했다고 주장하지 않는다.

## 결과와 범위

한 구성은 7개 경우, 성공한 resize 13회, 거부 24개다. 일반 전체 이미지 43개와 이전 제출의
전체 readback 3개를 비교한다. texel 범위는 127792 bytes, 별도 queued-work readback은 padding을
포함해 10752 bytes, 일반 이미지의 padding 행은 409개다.

실제 CreateSwapchain 호출 23회 중 22개가 생성되고 1회가 allocation callback 실패다.
oldSwapchain을 전달한 시도는 13회, NULL로 시작한 시도는 10회다. 실제 present 45회 모두
completion fence 연결과 성공한 wait가 필요하다. software 조합에서 view 61개·semaphore 61개·
fence 139개, chain 22개·surface 7개·device 1개·instance 2개의 생성과 해제가 일치했다.

고정 validation은 기존 VVL 1.3.290/XCB profile·dependency lock을 사용한다. 실제 로딩된 layer의
경로·binary/manifest 해시와 빈 settings를 보존하고 SDK counter 및 종료 이후 stderr 오류를
검사한다. fault/event API는 전용 test proxy에만 있으며 제품 SDK에 노출하지 않는다.

Windows 전체 CTest는 이번 SDK로 재빌드해 **63/65** 통과했다. 기존 memory_reclamation GPU/
validation 두 제외 외에 추가 제외는 없다. `fence_wait_gpu/validation` 두 예외를 별도로 보존하며
전체 Windows GPU 회귀 성공으로 바꾸지 않는다. 원인 조사·장시간 메모리 검증을 재개하지 않았다.
전체 Python 판정 검사는 새 8개를 포함해 **711개**가 통과했다.

WSI-06의 창 숨김·최소화·닫기·실제 surface 소실과 WSI-07~08의 전체 표시 matrix·사람의 화면
확인은 별도다. 물리 Linux GPU 수용 LNX-15, 장시간 메모리·Intel 충돌 원인, 전체 Phase 3 완료를
추론하지 않는다.

## source와 재현

Windows 실제 checkout SDK fingerprint:
`68254b70f26e547b89d11630b7ffb2d62f5600761cae306617dff04b94dfafa9`.
Linux Git SDK fingerprint:
`01934251a058504334a20b1dc94c192ebf26fabef00e2e5649cb2977eb1daf78`.
수정 전 28개 SDK source의 Windows `7182157752bc2c8ae39e5fc6eb0bc0b829f3f6db5cb1e2a1ae82bce5fdc7e3f8`,
Linux `64d4d3d994a3e987e1f0f541f310ed8404ab51c2e9b785b5a29c49ed607defc2`와 혼합하지 않는다.

[전용 workflow](../.github/workflows/linux-xcb-resize.yml),
[설치 소비자](../tests/linux_xcb_resize/CMakeLists.txt),
[판정 runner](../tools/run_linux_xcb_resize.py)를 따른다. 고정 headers·g++-14·XCB·Mesa software ICD·
Xvfb/xauth를 준비하고 매번 새 build/install/output 경로를 사용한다. source ZIP에는 제품 소스와
시험·재현 입력을 담고 compiled binary·외부 validation runtime·Python cache는 배포하지 않는다.

## 완료 근거

[일반·고정 validation resize CI](https://github.com/blackvrice/GraphicsSDK/actions/runs/40342798496)와
[Linux/Windows 기본 CI·Python 711](https://github.com/blackvrice/GraphicsSDK/actions/runs/40342798627)는
같은 제품·시험 커밋 `54c14af29df976fc712b86fb45702776fb8261b2`을 사용했다.
Windows의 로컬 전체 GPU 회귀 63/65는 기본 CI와 별도 결과다.

[요약 영수증](../benchmarks/reports/2026-10-26-phase3-linux-xcb-resize/summary.json),
[원본 해시 목록](../benchmarks/reports/validation/2026-10-26-phase3-linux-xcb-resize/inventory.json),
[오프라인 검증기](../benchmarks/reports/validation/2026-10-26-phase3-linux-xcb-resize/verify_evidence.py)를 제공한다.
source ZIP 2개는 각각 SDK 28개를 포함한 재현 입력 57개와 source receipt를 담는다.
수정 전 Presentation.hpp의 Windows/Linux 바이트도 보존하며, 다른 27개 SDK 파일과 결합한
fingerprint가 이전 제품과 일치하는지 검증한다. 이전 source 분석을 native 실패 실행으로 세지 않는다.

저장소 루트에서 다음 명령으로 GPU·네트워크 없이 source, raw artifact, 재생성·상태 제어·
전체 참조·자원 회수 결과와 Windows/Python 영수증을 재검증한다. 새 native 실행은 하지 않는다.

```sh
python -B benchmarks/reports/validation/2026-10-26-phase3-linux-xcb-resize/verify_evidence.py
```

후속 [P3-WSI-06 이벤트·소실 검증](PHASE3_LINUX_XCB_EVENTS_REPORT.md)은 창/연결 만료와
surface 소실의 공유 guard를 추가했다. 이 보고서와 원본 source fingerprint는 WSI-05 당시 값으로
유지하며 새 SDK 실행의 근거는 후속 보고서에 기록한다.
