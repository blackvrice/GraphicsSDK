# Linux LLGL 승인 프로필 build·최소 software probe

2026-10-30. 승인된 `llgl-fixed-pin-transfer-src-v1`을 Linux GCC에서 새로 빌드하고
**정상 probe 4개·실제 callback 제어 1개·거부 제어 3개, 총 8개 프로세스**를 검증했다.
두 smoke의 changing buffer 전체 참조 **24,576바이트**가 통과했다.
**Linux CPU/software 장치의 최소 준비 검증**이며 물리 GPU·전체 workload·성능 수용은 미완료다.

## 이번 개발과 실제 환경

Windows 전용 probe와 기존 소비자는 변경하지 않고 Linux 전용 CMake·C++ probe와
[`linux_llgl_probe.py`](../tools/linux_llgl_probe.py)를 추가했다.
실행 source commit은 `0396f9e2d789db98f9081dad380473446a316cd3`,
[실제 고정 VVL Linux CI](https://github.com/blackvrice/GraphicsSDK/actions/runs/41050420425)는 성공했다.
같은 revision에서 새 SDK/SDL qualification을 만든 뒤 그 장치·driver·환경과 LLGL을 대조했다.

| 항목 | 실제 관측·입력 |
|---|---|
| Linux / compiler | Ubuntu hosted x86_64, kernel `7.0.0-1017-azure`, GCC 14.2.0 |
| CPU / RAM | AMD EPYC 7763, 16,760,344,576 bytes |
| Native 장치 | `llvmpipe (LLVM 20.1.2, 256 bits)`, CPU 종류 4 |
| Device / driver UUID | `6d65736132352e322e382d3075627500` / `6c6c766d706970655555494400000000` |
| Adapter ID / LUID | 문자열 `5892941257450743403` / JSON null |
| 실제 queue | graphics family 0, index 0, flags 15. `vkGetDeviceQueue`와 LLGL의 native queue 일치 |
| Device / loader 지원 API | 1.4.318 / 1.3.275. instance 생성 요청 인수는 가로채 관측하지 않음 |
| LLGL | pin `649c0cc8fa608312c5e4b94dae409a5859fc7553` + 기존 승인 10줄 overlay |
| Vulkan-Headers / VVL | `b379292b2ab6df5771ba9870d53cf8b2c9295daf` / 고정 1.3.290 runtime lock |
| Probe fingerprint | `b39af5b76d5ede6d6dc0a5bb48199bf3cfb01497389c458750a6e699f52b577e` |

LLGL의 실제 `VkPhysicalDevice`·`VkDevice`·`VkQueue`를 `GetNativeHandle`로 조회했다.
그 physical device의 UUID·driver·이름·vendor/device/API·종류가 native qualification과 모두 일치해야 한다.
SDK/SDL 선행 probe의 유일한 이름 귀속을 SDL native handle 직접 관측으로 바꾸지는 않는다.

## 승인 프로필과 빌드 경계

원본 `.deps/LLGL`은 pin/clean 상태를 유지했다. `VKCommandContext.cpp` 한 파일만
별도 출력 폴더의 overlay로 컴파일했고, 기존 승인 artifact의 원본·수정본 바이트와 정확히 대조했다.
추가 10줄의 transfer-src access/stage 변경을 확대하지 않았으며 upstream 원본으로 표시하지 않는다.
이 최소 buffer smoke는 image barrier 변경의 전체 Linux 동작을 검증한 것이 아니다.

`LLGL`, `LLGL_Vulkan`, 자체 probe를 GNU Release `-O2 -DNDEBUG`로 빌드했다.
다른 renderer·static library·examples/tests·XR·debug wrapper·SPIR-V reflection·GPU-assisted validation·Wayland 옵션을 OFF로
고정하고 LTO·unity build·CPU별 tuning을 사용하지 않았다. Linux core가 요구하는 X11/Xrandr 시스템 입력은 사용하지만
창을 만들거나 실제 표시를 수용하지 않았다. driver의 간접 shared-library 의존성을 LLGL 옵션과 혼동하지 않는다.

실제 verbose build와 Ninja target graph의 명령, CMake source/output, 보존한 GNU `-MD/-MF` 의존성 파일을 대조했다.
컴파일 명령은 **128개**, 실제 compiler dependency 입력은 **768개**다.

| 입력 분류 | 수 | 처리 |
|---|---:|---|
| LLGL source/header | 367 | tracked pin과 각 BSD notice 확인, BSD-3-Clause |
| 승인 overlay | 1 | 기존 승인 원본/수정본과 정확한 바이트·10줄 추가 확인 |
| Vulkan header | 13 | 고정 pin·SPDX별 허용 branch 확인 |
| 자체 probe/helper | 2 | MIT |
| GNU/Linux 시스템 입력 | 385 | 시스템 전제조건, 재배포하지 않음 |

원본 command·GNU depfile 텍스트와 해시, 입력 분류·notice를 [inputs.json](../benchmarks/reports/validation/2026-10-30-phase3-linux-llgl-probe/native/inputs.json)에 보존했다.
compiler source/header 의존성 감사이며 전체 symbol reachability나 모든 linker 입력을 추론한 것은 아니다.
실제 실행 shared objects는 별도로 경로·해시를 기록했다. 동결 입력 기록 982개와 관측 runtime 파일 40개의 변경을 검사했다.
외부 LLGL/VVL/loader/driver/system binary는 artifact에 포함하지 않는다.

## 실제 probe·제어 결과

| 프로세스 | 결과·범위 |
|---|---|
| plain identity | actual renderer/device/queue, validation library 없음, 참조 bytes 0 |
| plain smoke | 4096 bytes × 3 changing rounds 전체 일치, 오류 0 |
| required-validation identity | 고정 layer의 실제 load·장치·queue 확인, 오류 0 |
| required-validation smoke | 같은 전체 12,288 bytes 일치, delivered/LLGL 오류 0 |
| callback-control | 잘못된 host properties2 sType의 실제 callback 오류 1개, 기대 VUID 일치, GPU command 제출 0 |
| wrong UUID / wrong kind / missing required layer | 각각 의도한 문구와 exit 1, 성공 JSON 없음 |

smoke는 LLGL 공개 WriteBuffer → CopyBuffer → Submit(command/fence) → 제한된 WaitFence → ReadBuffer를 사용한다.
native Vulkan 작업으로 LLGL workload를 우회하지 않았다. 실제 native API 사용은 장치·queue 조회와 host-only 오류 제어다.
callback 제어는 `VUID-VkPhysicalDeviceProperties2-sType-sType`를 실제 callback과 stderr 양쪽에서 확인한다.
core callback delivery를 확인했으며 모든 sync hazard control이나 GPU-assisted 동작까지 증명하지 않는다.
core/sync·submit-time 설정 요청과 정확한 layer load, 정상 실행 오류 0의 관측 범위를 유지한다.

Docker에서는 먼저 실제 GNU compile·plain smoke·입력 감사와 배포판 VVL 1.3.275 callback 동작을 진단했다.
위 수용 자료는 Docker의 임시 진단을 대신 붙인 것이 아니라 별도 고정 VVL 1.3.290 CI의 새 실행이다.
Docker package 다운로드의 최초 hash 불일치는 검증을 해제하지 않고 HTTPS 저장소 재시도로 해결했다.
호스트 driver·시스템 정책이나 `.deps/LLGL` 원본은 변경하지 않았다.

## 보존·재검사와 잔여 항목

[요약](../benchmarks/reports/validation/2026-10-30-phase3-linux-llgl-probe/summary.json),
[원본 receipt](../benchmarks/reports/validation/2026-10-30-phase3-linux-llgl-probe/native/receipt.json),
[source ZIP](../benchmarks/reports/validation/2026-10-30-phase3-linux-llgl-probe/native/source.zip),
[manifest](../benchmarks/reports/validation/2026-10-30-phase3-linux-llgl-probe/manifest.json),
[저장 증거 verifier](../benchmarks/reports/validation/2026-10-30-phase3-linux-llgl-probe/verify.py)를 연결한다.
source ZIP은 선택한 17파일의 실행 snapshot이며 전체 repository/dependency checkout을 대신하는 배포물은 아니다.
검사기는 repository validator를 사용하고 ZIP 코드나 저장 executable을 실행하지 않는다.
원본 hash는 외부 신뢰 서명이 아니며 offline 재생은 실제 GPU 재실행을 대신하지 않는다.

새 회귀 테스트 **17개**, Windows/Linux 전체 Python 테스트 **각 1,281개**가 통과했다.
[Windows receipt](../benchmarks/reports/validation/2026-10-30-phase3-linux-llgl-probe/python-windows/receipt.json)와
[Linux receipt](../benchmarks/reports/validation/2026-10-30-phase3-linux-llgl-probe/python-linux/receipt.json)는
실행 revision과 해당 runner의 source hash를 보존한다. 받은 native 원본의 Windows offline 재검사도 통과했다.

```text
python3 tools/linux_llgl_probe.py run --qualification out/linux-comparison-evidence --output out/NEW_LLGL
python3 tools/linux_llgl_probe.py check --output out/NEW_LLGL --live
python tools/linux_llgl_probe.py check --output benchmarks/reports/validation/2026-10-30-phase3-linux-llgl-probe/native
python benchmarks/reports/validation/2026-10-30-phase3-linux-llgl-probe/verify.py
python tools/roadmap.py --check
```

다음 구현은 **승인 LLGL 프로필의 Linux 업로드 correctness 소비자 이식**이다.
draw/instancing·PSO·자원 생성의 Linux 소비자와 전체 참조도 아직 없다.
실제 Linux 물리 GPU qualification, 세 SDK의 전체 correctness와 분리된 fresh 성능 반복 측정이 남아 있다.
`P3-BEN-07`~`08`, `P3-LBEN-01`~`04`, 실제 화면·최종 gate는 todo를 유지한다.
현재 성능 표본은 0개이며 Windows 시간값 재사용이나 Linux hardware 수용은 하지 않았다.
