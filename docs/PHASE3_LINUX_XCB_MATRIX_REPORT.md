# P3-WSI-07 — Linux XCB 통합 출력 참조·validation

2026-10-27. 다중 창 검사와 기존 표시·수명·resize·이벤트 검사를 하나의 설치 Core로 묶는다.
제품·검증 도구 기준은 `e07c10c0a8c6b512a1e7bec3de4e345dc0b8cf62`다.
일반·고정 core/sync validation의 다섯 소비자가 모두 성공해 `P3-WSI-07`을 완료했다.

## 추가한 범위

두 XCB connection/window와 swapchain을 같은 device에서 동시에 유지한다. 양쪽 backbuffer를
먼저 획득한 뒤, 서로 다른 clear·scissor draw·blend 순서로 출력하고 모든 texel과 row padding을
CPU 참조와 비교한다. 화면의 일부 sample이나 누적 checksum만으로 통과시키지 않는다.

요청 buffer 수 2/3/4와 첫 창의 vsync false/true 조합으로 여섯 쌍을 실행한다. 두 번째 창은 반대
vsync다. 실제 native format·present mode·선택 mode별 최소/최대 image 수와 실제 allocation 수를
관측한다. 요청 buffer 수는 최소 요청이며 실제 allocation 수와 같다고 가정하지 않는다.

각 쌍의 순서는 다음과 같다.

1. 17×9와 65×7 두 창에서 각 세 종류의 전체 이미지 참조.
2. 첫 창의 texture/recording을 보유한 resize 거부 중 두 번째 창의 정상 출력.
3. 두 번째 창을 129×13으로 실제 resize하고 출력. 첫 창은 recording 참조를 해제한 뒤 83×11로 resize.
4. 두 창의 세 종류 이미지 참조 반복.
5. 첫 chain을 파괴한 뒤 보유 recording 제출과 texture 입력 거부. 두 번째 창 출력 유지.
6. 첫 창의 새 chain 출력과 해제 후에도 두 번째 창 정상 출력. 모든 객체·present 완료 정리.

각 구성에서 쌍마다 17개, 합계 102개 전체 이미지를 확인했다. UNORM 변환에는 채널당
1.1 byte 이내 오차를 허용하고 padding은 sentinel byte와 정확히 대조한다. 제품 소스 수정은 없다.
새 검사는 caller thread에서 interleave하며 API의 다중 thread 동시 호출을 수용하는 시험이 아니다.

## 같은 설치 SDK의 통합 실행

[전용 workflow](../.github/workflows/linux-xcb-matrix.yml)는 한 번 빌드·설치한 Core를 다섯 소비자에
연결한다. 일반 구성과 고정 core/sync validation 구성을 분리하며, 모든 실행의 SDK fingerprint와
실제 Core library 해시가 일치하는지 확인한다.

| 소비자 | 검사 범위 |
|---|---|
| present | 요청 buffer 2/3/4 × FIFO/IMMEDIATE, 전체 clear/draw/blend·전송 참조, UNORM 부재·FIFO만 제공·MAILBOX 대체 mode 제어 |
| lifetime | 보유/퇴역·미완료 GPU 작업·present completion·부분 실패와 native 자원 순서 |
| resize | 실제 geometry·out-of-date/suboptimal 상태 제어·미완료 resize drain·native 재생성 실패 뒤 퇴역/재시도 |
| events | 실제 Openbox 숨김/최소화/복원·닫기 protocol·파괴/재사용·연결 단절·별도 소실 status 제어 |
| matrix | 두 창의 동시 획득·독립 출력/resize/해제·전체 이미지·present completion·자원 회수 |

판정 도구는 각 소비자의 기존 validator를 실행한 뒤 동일 소스·구성·완료 상태를 다시 확인한다.
기존 검사도 포함해 native stderr를 teardown까지 읽고 전달된 validation 오류를 거부한다.
Xvfb/Openbox는 private 실행이며 외부 runtime binary를 SDK/source ZIP에 배포하지 않는다.

초기 두 구성은 기존 이벤트 검사에서 첫 창의 활성화 대기 시간이 초과됐다. 진단 실행에서
`mapped=0, WM_STATE=0, managed=1, connection_error=0, events=2`를 관측했다. root의 WM 표시는
존재했지만 실제 창 MapNotify/NormalState가 오지 않았다. 초기 로그에는 WM 종료 상태가 없어
Openbox 내부 지연과 조기 종료 중 어느 쪽인지 단정하지 않는다.

준비 판정은 [Openbox의 --startup 명령](https://manpages.debian.org/trixie/openbox/openbox.1.en.html)에서
기록하는 새 marker로 보강하고, native client 실행 전/후 WM 생존 상태를 확인한다. 각 private 실행에 새 marker만
허용하고, 준비 시간 초과나 조기 종료는 실패로 처리한다. 실패한 독립 검사가 있어도 다른
소비자의 결과를 수집하며, 전체 통과 조건은 완화하지 않는다. 최종 일반·validation 구성은 이 변경 뒤 모두 통과했다.
이는 외부 WM 시작 동기화 보강이며 SDK 변경·자연 driver 손실이나 Intel 충돌 원인 규명이 아니다.

validation 구성에서는 별도 raw Vulkan process에서 기존 core/sync/counter control을 같은 layer와
ICD로 재실행한다. 이 raw instance는 XCB surface를 만들지 않는다. size VUID 32개·장벽이 누락된
copy recording hazard 1개·sync 비활성 비교 1개·합성 counter callback 1025개가 예상 결과다.
잘못된 명령은 GPU에 제출하지 않으며, SDK 출력 경로의 오류 0과 의도한 control 오류를 분리한다.

## 수용 경계

범위는 Linux X11/XCB + Xvfb/Openbox + software ICD와 기록한 runtime 조합이다. 상태 제어를
자연 driver 손실로 세지 않는다. 물리 GPU·scanout·사용자 육안은 별도 `P3-WSI-08`/`P3-LNX-15`다.
Wayland/XWayland·다른 WM·처리량·장시간 메모리 증가·Intel 충돌 원인·전체 Phase 3 완료를
추론하지 않는다. 다른 driver의 mode 조합이나 포맷 fallback은 별도 실행 없이 확대 수용하지 않는다.

SDK 28개 소스의 fingerprint는 WSI-06과 같다. Windows 실제 bytes:
`197bd38300429ca10e77214bae33a269ec938cf78f112a66fb3182a37298582b`, Linux Git bytes:
`fdcb274bf57381a3b426f3c22b420d6719c3ba37e4ba038b6902a4b2bebb6f79`.
이 작업은 새로운 Windows Vulkan GPU 실행을 요구하는 제품 변경이 아니다. 이전 Windows
63/65·기존 fence 예외 두 개는 [WSI-06 보고서](PHASE3_LINUX_XCB_EVENTS_REPORT.md)의 당시 결과로
유지하며 이번 native Linux 결과로 바꾸지 않는다.


## 최종 결과와 보존 자료

| 소비자 | 구성마다 전체 참조 | native present / 완료 wait |
|---|---|---|
| present | 48개 이미지와 별도 8개 byte 전송 | 24 / 24 |
| lifetime | 미완료 경계의 전체 readback 3개 | 11 / 11 |
| resize | 일반 43개 + 미완료 경계 3개 | 45 / 45 |
| events | 19개 이미지 | 20 / 20 |
| matrix | 102개 이미지 | 102 / 102 |

합계는 구성마다 전체 참조 218개·native present 202회다. present 수와 참조 수는 일대일이
아니다. offscreen 참조와 present 전후 별도 경계 검사를 구분한다. SDK 오류 0은 각 소비자의
counter와 teardown까지의 stderr를 함께 확인했다.

새 두 창 matrix는 texel 383832 bytes·padding 1062행·거부 24개를 검사했다. chain 30개,
surface 18개, view/semaphore 각각 100개, fence 318개와 device/instance 생성·해제 수가 일치했다.
파괴 순서 오류는 0이다. runtime의 실제 buffer allocation 수는 요청 최소 수와 구분한다.

WM 준비 보강 뒤 첫 validation 통합 실행도 다섯 SDK 소비자와 raw control이 성공했지만, 도구의
로드 경로 비교가 `/layers/./libVkLayer_khronos_validation.so`를 정규 경로와 다르게 보아 실패했다.
실제 canonical 경로를 보고한 C++ instance 세 개와 LD_DEBUG init 경로를 대조하고, POSIX dot
segment를 정규화하도록 수정했다. 다른 runtime 경로·종료 로그만 있는 경우는 계속 거부한다.
당시 campaign 실패 영수증은 수정하지 않고 원본으로 보존했다. 최종 실행은 수정된 판정으로
처음부터 다시 실행해 일반·validation 둘 다 통과했다.

Python은 새 matrix/WM/로드 판정 17개를 포함해 로컬·Windows CI·Linux CI에서 각각 736개
성공·실패/오류/건너뜀 0·검사 중 입력 변경 0이다. 기존 Windows Vulkan 예외를 이 성공에
합산하지 않는다. 별도 예제 policy CTest도 각 통합 구성에서 1/1 성공했다.

- [최종 통합 CI](https://github.com/blackvrice/GraphicsSDK/actions/runs/40366852374),
  [기본 Windows/Linux CI](https://github.com/blackvrice/GraphicsSDK/actions/runs/40366852481).
- [요약](../benchmarks/reports/2026-10-27-phase3-linux-xcb-matrix/summary.json),
  [원본 목록](../benchmarks/reports/validation/2026-10-27-phase3-linux-xcb-matrix/inventory.json),
  [artifact ID·해시](../benchmarks/reports/validation/2026-10-27-phase3-linux-xcb-matrix/artifacts.json).
- [초기 실패 분석과 원본](../benchmarks/reports/validation/2026-10-27-phase3-linux-xcb-matrix/history/analysis.json)은
  성공 결과에 합산하지 않는다. 수정 전 harness 네 파일을 해당 Git revision 그대로 보존한다.
- [Windows source manifest](../benchmarks/reports/validation/2026-10-27-phase3-linux-xcb-matrix/source-manifest-windows.json)와
  [Linux source manifest](../benchmarks/reports/validation/2026-10-27-phase3-linux-xcb-matrix/source-manifest-linux-git.json)는
  SDK 28개를 포함한 각 88개 자체 재현 소스를 보존한다. 외부 headers/runtime은 workflow의 고정 버전으로
  준비한다. compiled binary·외부 runtime binary·Python cache는 포함하지 않는다.

다음 명령은 source ZIP·SDK fingerprint·raw artifact·Core/layer/WM identity·모든 native 결과·
Python 영수증·초기 실패 원본을 오프라인으로 재검증한다. GPU/native 작업을 다시 실행하지 않는다.

```sh
python -B benchmarks/reports/validation/2026-10-27-phase3-linux-xcb-matrix/verify.py
```

[오프라인 검사 결과](../benchmarks/reports/validation/2026-10-27-phase3-linux-xcb-matrix/offline-verification.log).
남은 Linux 표시 수용은 `P3-WSI-08`이며 물리 GPU gate `P3-LNX-15`도 별도로 남아 있다.
