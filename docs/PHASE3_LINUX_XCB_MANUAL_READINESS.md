# P3-WSI-08 — 실제 Linux 표시 검증 준비

2026-10-27. 사용자가 사용할 Linux 데스크톱 환경이 없다고 확인해, 실제 화면 수용 대신
세션 기록·검토 도구를 준비했다. **P3-WSI-08과 P3-LNX-15는 미완료로 유지한다.**
Windows/WSL·가상화·driver·사용자 데스크톱을 설치하거나 변경하지 않았다.

## 준비한 실행 도구

[`tests/linux_xcb_manual`](../tests/linux_xcb_manual/README.md)은 설치 Core를 사용하는 Linux x86_64
X11 전용 test consumer다. 기존 자체 fullscreen SPIR-V fixture와 scissor draw로 청회색 배경의
빨간 사각형을 이동시킨다. 60 fps 상한과 기존 caller-owned XCB event/pause/recovery 경로를
사용하며, window title의 세션 token과 진행 frame 수로 기록을 구분한다. SDK 제품 소스는 바꾸지 않았다.

[`tools/linux_xcb_manual.py`](../tools/linux_xcb_manual.py)는 다음 단계를 제공한다.

| 단계 | 용도 | 수용 결과 |
|---|---|---|
| `smoke` | private Xvfb/Openbox + 단일 명시 software ICD에서 12프레임 자동 실행·전체 texel/padding 대조·종료 | 준비 도구 검증만 성공 가능 |
| `run` | 실제 X11 desktop에서 명시한 ICD·정확한 adapter ID/종류로 사람이 조작할 창 실행 | 정상 capture와 빈 관측 양식만 생성 |
| `check` | 저장된 native trace·해시·identity·계층 로드·양식 바인딩 재검사 | 사람/하드웨어/작업 완료는 항상 false |

별도 native identity probe가 SDK fingerprint와 native UUID·driver UUID·종류·vendor/device·
API/driver version을 조회한다. UUID로부터 유도된 64-bit ID를 문자열로 대조하고 다른 adapter로
fallback하지 않는다. 실제 loader/ICD shared-library 로드를 관측해 file hash를 고정하며, SDK·
consumer·proxy·identity binary·ICD manifest·관련 자체 소스와 선택 VVL의 변경 여부를 다시 검사한다.
PCI 주소/driver 관측을 포함한 물리 GPU 수용은 기존 P3-LNX-15와 별개다.

실제 실행은 `local-monitor` 또는 `remote-desktop` 경로를 명시한다. 명시된 Wayland session은
거부한다. X11 환경 변수와 배포 정보는 launcher 환경으로 기록한다. 원격 화면을 보았다는
기록만으로 물리 모니터 scanout을 수용하지 않는다. 실제 environment/monitor/WM/compositor는
사람의 기록과 함께 검토해야 한다. 현재 제공된 접속 대상이나 Linux 표시 환경은 없다.

GPU 자원은 caller window/connection이 살아 있는 동안 정리한다. 정상 사용자 종료는
WM_DELETE_WINDOW이고, 자동 검사는 frame limit이다. 5분 deadline과 500ms 비활성 watchdog을
두며, 비활성 상태에서는 GPU frame을 발행하지 않는다. 실패·소실/복구·timeout은 일반적인
정상 수동 capture로 판정하지 않는다. proxy는 forwarding mode 0이며 fault injection을 하지 않는다.

## 사람의 관측 기록

`human-observation.json`은 다음에 묶여 생성된다.

- 새 session ID와 window title의 token
- native stdout SHA-256과 SDK fingerprint
- 선택한 native device UUID와 표시 경로

관측자·환경 설명·직접 진술·일곱 확인 항목은 처음에 모두 비어 있다. 화면의 실제 움직임,
resize, 최소화 후 2초 이상 대기/복원, 최대화/복원, 다른 창으로 가리기/다시 보기, 반응성,
마지막 X 버튼 종료를 직접 수행한 결과를 남긴다. JSON 파일의 체크만 채워도 `check`는
사람의 진술을 인증하거나 작업 완료로 바꾸지 않는다.

native 상태 기록은 supporting evidence다. 자동 protocol 전달을 사람의 클릭으로 세지 않으며,
geometry 변화만으로 최대화와 일반 resize를 구분하거나 가려진 화면·움직임의 가시성을 추론하지
않는다. 추후 실제 사람의 직접 관측과 환경/동작 기록을 검토한 뒤 P3-WSI-08 완료를 판단한다.

## 이번 검증

일반·고정 core/sync validation 두 구성에서 native consumer를 실제 빌드·설치·실행해 성공했다.
도구 기준 source는 `9458308a07da2380f866bd4e0526256295029e09`다. native `run`의 실제 desktop 실행과
사람의 화면 관측은 각각 0회이며, 자동 결과와 합산하지 않는다.

SDK 28개 소스는 WSI-07과 byte 단위로 같다. Windows 실제 fingerprint:
`197bd38300429ca10e77214bae33a269ec938cf78f112a66fb3182a37298582b`, Linux Git fingerprint:
`fdcb274bf57381a3b426f3c22b420d6719c3ba37e4ba038b6902a4b2bebb6f79`.
새 Windows Vulkan GPU 실행·성능·장시간 메모리·Intel 충돌 원인 수용으로 바꾸지 않는다.

## 실제 환경이 준비되면

[빌드·실행 절차](../tests/linux_xcb_manual/README.md)를 따라 새 설치와 새 output 경로를 사용한다.
필요한 것은 Linux x86_64 X11 desktop, 표시를 볼 사람과 경로, 작동하는 Vulkan ICD와 정확한
adapter ID다. 고정 Vulkan-Headers와 선택 VVL은 기존 검토된 profile/lock을 사용한다.

```sh
python3 tools/linux_xcb_manual.py run \
  --binary out/manual-consumer/gfx_xcb_manual \
  --proxy out/manual-consumer/proxy/libvulkan.so.1 \
  --identity-binary out/manual-consumer/identity/gfx_linux_vulkan_device \
  --sdk-library out/manual-install/lib/libGraphicsSDKCore.a \
  --icd /absolute/path/to/physical-icd.json --adapter-id EXACT_DECIMAL_ID \
  --display-route local-monitor --output out/fresh-manual-session
python3 tools/linux_xcb_manual.py check --output out/fresh-manual-session
```

`--validation-dir`은 검토된 external layer 폴더를 지정한다. `--software`는 명시한 CPU renderer를
선택할 때만 사용한다. GPU/driver나 표시 경로가 달라지면 같은 수용으로 재귀속하지 않는다.


## 실제 준비 검증 결과와 원본

두 구성은 각각 129×73 전체 frame 12개를 GPU readback으로 대조했다. 사각형 x 위치는
0, 7, …, 77로 달랐고, 모든 texel과 padding byte를 검사했다. native present 12회와 completion
wait 12회, chain/surface 각각 1개·device 1개·instance 2개의 생성/해제가 일치했다. SDK 오류
계수와 teardown까지 전달된 validation 오류는 0이다. 창은 GPU 정리 완료까지 살아 있었다.

검증한 renderer는 llvmpipe `LLVM 20.1.2` software ICD다. native device ID는
`5892941257450743403`, device UUID는 `6d65736132352e322e382d3075627500`다. 요청한 실제
GPU 목록/driver metadata와 두 runtime file의 해시는 각 원본에 보존한다. 실제 physical GPU
정상 suite 실행이나 사람의 모니터 관측으로 바꾸지 않는다.

이 smoke에서 resize·최소화·복원·사용자 X 클릭을 수행했다고 세지 않는다. 종료 원인은
`frame_limit`, 관측자와 관측 항목은 null, `human_display_accepted`·`physical_gpu_accepted`·
`task_completed`는 모두 false다. 양식의 체크를 임의로 채워도 자동 수용되지 않는 등 새 판정
검사 14개를 포함해 로컬·Windows CI·Linux CI Python은 각각 750개 성공했다.

- [준비 도구 CI](https://github.com/blackvrice/GraphicsSDK/actions/runs/40433314502),
  [Windows/Linux 기본 CI](https://github.com/blackvrice/GraphicsSDK/actions/runs/40433314478).
- [준비 요약](../benchmarks/reports/2026-10-27-phase3-linux-xcb-manual-readiness/summary.json),
  [원본 목록](../benchmarks/reports/validation/2026-10-27-phase3-linux-xcb-manual-readiness/inventory.json),
  [GitHub artifact ID·해시](../benchmarks/reports/validation/2026-10-27-phase3-linux-xcb-manual-readiness/artifacts.json).
- [일반 구성의 빈 관측 양식](../benchmarks/reports/validation/2026-10-27-phase3-linux-xcb-manual-readiness/normal/manual-evidence/human-observation.json),
  [validation 구성의 빈 관측 양식](../benchmarks/reports/validation/2026-10-27-phase3-linux-xcb-manual-readiness/validation/manual-evidence/human-observation.json).
- [Windows source manifest](../benchmarks/reports/validation/2026-10-27-phase3-linux-xcb-manual-readiness/source-manifest-windows.json),
  [Linux source manifest](../benchmarks/reports/validation/2026-10-27-phase3-linux-xcb-manual-readiness/source-manifest-linux-git.json).
  SDK 28개를 포함한 각 58개 자체 재현 파일을 보존한다. compiled binary·외부 runtime binary·
  Python cache는 배포하지 않는다.

다음 명령은 source ZIP·SDK 보존·artifact·native identity/trace·양식 바인딩·빈 human 필드·
actual layer/WM/runtime hash·Python 750개 기록을 오프라인으로 재검사한다. native/GPU를
다시 실행하거나 사람의 표시 수용을 부여하지 않는다.

```sh
python -B benchmarks/reports/validation/2026-10-27-phase3-linux-xcb-manual-readiness/verify.py
```

[오프라인 검사 결과](../benchmarks/reports/validation/2026-10-27-phase3-linux-xcb-manual-readiness/offline-verification.log).
