# Linux LLGL 업로드 전체 입력·core/sync 정확성 준비

2026-10-30. 승인 LLGL 프로필의 Linux 업로드 소비자를 이식하고 고정 VVL에서
**정상 8개·core/sync 제어 2개·거부 제어 5개, 총 15개 프로세스**를 실행했다.
**32개 전체 changing image, 862,694,560바이트**와 독립 CPU 참조 hash가 일치했다.
이는 **software ICD 정확성 준비 완료**이며 실제 Linux GPU 성능 비교나 `P3-LBEN-01` 최종 완료는 아니다.

## 이식과 입력 계약

[`port_linux_llgl_upload.py`](../tools/port_linux_llgl_upload.py)는 기존 Windows correctness 소비자에서
Linux `main.cpp`·`Platform.hpp`를 결정적으로 생성한다. platform include·선택/모듈·native identity·
Linux LUID/null·범위 metadata와 SHA-256 구현만 바꾼다. 생성기는 `validationControl`과 `Upload` 본문이
원본과 정확히 같은지 검사한다. 원래 payload header도 직접 사용하며 기존 Windows source는 수정하지 않았다.
생성 manifest는 입력별 raw SHA-256, 16개 adaptation, 생성 파일 hash와 `workload_bodies_changed=false`를 보존한다.

업로드는 MapBuffer/UnmapBuffer → CopyTextureFromBuffer → Submit(command/fence)/WaitFence,
readback은 CopyBufferFromTexture → Submit(command/fence)/WaitFence → ReadBuffer를 그대로 사용한다.
row/layer stride 0의 tight layout이며 SDK padded staging과 같다고 표시하지 않는다.
RGBA16F는 기존 finite half payload를 사용한다. LLGL 일반 workload를 native Vulkan으로 우회하지 않는다.

Windows BCrypt 대신 시스템 OpenSSL Crypto의 SHA-256을 사용했다. 이는 별도 benchmark 소비자의 시스템 전제조건이며
SDK에 새 crypto 의존성을 추가한 것이 아니다. OpenSSL header/library는 실제 입력·runtime hash로 기록하고 재배포하지 않는다.
GPU readback과 expected bytes를 먼저 전체 비교하고, 결과 SHA-256도 기존 독립 stdlib CPU reference와 대조한다.
기존 CPU hash 상수는 reference data로 재사용했다. 과거 GPU 결과나 timing을 새 표본으로 붙인 것은 아니다.

## 실제 실행과 같은 장치 귀속

실행 source commit은 `9fe83d54cb31102758e95035cff76c63ebba7057`,
[실제 Linux 고정 VVL CI](https://github.com/blackvrice/GraphicsSDK/actions/runs/41057486742)는 성공했다.
이 revision에서 새 SDK/SDL qualification과 승인 LLGL probe/build를 완료한 뒤,
그 **같은 LLGL shared libraries를 변경 없이 재사용**해 소비자만 따로 컴파일했다.
이전 보고서의 다른 CPU/환경이나 binary를 현재 실행으로 재귀속하지 않는다.

| 항목 | 실제 관측·입력 |
|---|---|
| OS / compiler | Ubuntu hosted Linux x86_64, kernel `7.0.0-1017-azure`, GCC 14.2.0 |
| CPU / RAM | Intel Xeon Platinum 8573C, 16,759,308,288 bytes |
| Native 장치 | `llvmpipe (LLVM 20.1.2, 256 bits)`, CPU 종류 4 |
| Device / driver UUID | `6d65736132352e322e382d3075627500` / `6c6c766d706970655555494400000000` |
| Adapter ID / LUID | 문자열 `5892941257450743403` / JSON null |
| LLGL | `649c0cc8fa608312c5e4b94dae409a5859fc7553` + 승인 10줄 transfer-src overlay |
| VVL / headers | 고정 VVL 1.3.290 / Vulkan-Headers `b379292b2ab6df5771ba9870d53cf8b2c9295daf` |
| 소비자 fingerprint | `407d551d781e414f4861f1e57764b1213d09f434792c834eae9febeee5e48eb7` |
| Crypto | 실제 `/usr/lib/x86_64-linux-gnu/libcrypto.so.3`, 5,309,400 bytes, SHA-256은 receipt에 보존 |

정확한 UUID와 software 종류를 먼저 요구하고 LLGL actual native physical device/queue를 다시 조회한다.
선행 probe의 device/driver/API·queue·compiler·환경과 일치해야 한다. 일반 프로세스에는 validation library가 없어야 하며
검증 프로세스에는 정확한 고정 library가 있어야 한다. instance 생성 요청 인수는 직접 관측하지 않았다.
SDK/SDL 선행 귀속을 SDL native handle 직접 관측으로 확대하지 않는다.

## 전체 이미지와 오류 통제 결과

각 정상 프로세스는 warmup 1회와 추가 correctness 3회를 포함해 **네 이미지 모두**를 검사한다.
다음 합계는 새 업로드 실행만 세며 선행 probe·SDK/SDL matrix와 합산하지 않는다.

| 입력 | 이미지 크기 | 일반+validation 전체 이미지 | 검증 bytes |
|---|---|---:|---:|
| odd | 65×37 RGBA8 | 8 | 76,960 |
| upload1080 | 1920×1080 RGBA8 | 8 | 66,355,200 |
| upload4k | 3840×2160 RGBA8 | 8 | 265,420,800 |
| upload4k16f | 3840×2160 RGBA16F | 8 | 530,841,600 |

정상 실행의 delivered error는 0이다. 각 이미지의 SHA-256 4개를 raw stdout에 보존하며 모두 frozen CPU reference와 일치한다.
성능 시간은 측정하지 않는다. `sample_count=3`은 correctness 반복 수이고 `samples_ms=[]`, `performance_samples=0`, `gpu_ms=null`이다.

| 제어 | 실제 결과 |
|---|---|
| core-control | host properties2의 잘못된 sType으로 실제 `VUID-VkPhysicalDeviceProperties2-sType-sType`, 오류 1개 |
| sync-control | native command buffer에 두 write-after-write를 record, 실제 `SYNC-HAZARD-WRITE-AFTER-WRITE`, 오류 1개 |
| sync-disabled | 같은 sync 제어에서 기대 diagnostic 부재를 확인해 의도한 오류·exit 1·성공 JSON 없음 |
| missing layer / wrong UUID / wrong kind | 각각 의도한 guard 오류·exit 1·성공 JSON 없음 |
| corrupt readback | 마지막 byte를 바꾼 전체 참조 실패로 exit 1·성공 JSON 없음 |

core/sync positive controls와 sync-disabled는 분리 프로세스다. sync negative 명령은 record만 하고 GPU에 제출하지 않는다.
이 native API 사용은 오류 검출 제어이며 정상 LLGL workload를 대체하지 않는다. GPU-assisted·실제 TDR·표시 수용을 주장하지 않는다.

## 실제 컴파일·원본 보존과 재검사

새 소비자의 실제 verbose/Ninja compiler command **1개**, GNU dependency 입력 **459개**를 감사했다.
기존 Windows Context가 실수로 컴파일되지 않고 생성한 platform/main·원래 payload가 사용됐는지도 검사했다.

| 분류 | 수 | 처리 |
|---|---:|---|
| LLGL header | 86 | 고정 tracked pin과 BSD notice, BSD-3-Clause |
| Vulkan header | 12 | 고정 pin·SPDX별 허용 branch |
| 자체 helper/payload | 2 | MIT |
| 생성 main/platform | 2 | MIT, 생성 manifest와 원본 본문 대조 |
| 시스템 | 357 | GNU/OpenSSL 등 시스템 전제조건, 재배포하지 않음 |

컴파일은 `-O2 -DNDEBUG`, no-LTO·no-unity·no CPU tuning이다. 원래 승인 10줄 patch와 LLGL checkout은 그대로다.
새 소비자가 LLGL을 다시 빌드했다고 표시하지 않는다. 선행 LLGL의 감사·binary·runtime 해시를 묶고 새 소비자 입력을 추가했다.
동결 입력 기록 1,508개와 runtime 파일 41개를 실행 전·최종 대조했다. raw command·환경·PID·직렬 구간·stdout/stderr·
실제 GNU depfile·분류·SHA-256, 원본/생성 소스·adaptation·13파일 source ZIP과 선행 qualification을 보존했다.
외부 LLGL/VVL/driver/crypto binary는 artifact에 넣지 않는다. symbol reachability나 native allocation 회수는 추론하지 않는다.

[요약](../benchmarks/reports/validation/2026-10-30-phase3-linux-llgl-upload/summary.json),
[원본 receipt](../benchmarks/reports/validation/2026-10-30-phase3-linux-llgl-upload/native/receipt.json),
[입력 감사](../benchmarks/reports/validation/2026-10-30-phase3-linux-llgl-upload/native/inputs.json),
[생성 manifest](../benchmarks/reports/validation/2026-10-30-phase3-linux-llgl-upload/native/adaptation.json),
[저장 증거 verifier](../benchmarks/reports/validation/2026-10-30-phase3-linux-llgl-upload/verify.py)를 연결한다.
검사기는 repository validator를 사용하고 ZIP 코드나 저장 executable을 실행하지 않는다.
manifest hash는 외부 신뢰 서명이 아니며 offline 재생은 실제 GPU 재실행을 대신하지 않는다.

새 회귀 테스트 **16개**, 두 OS의 전체 Python 테스트 **각 1,297개**가 통과했다.
[Windows receipt](../benchmarks/reports/validation/2026-10-30-phase3-linux-llgl-upload/python-windows/receipt.json)와
[Linux receipt](../benchmarks/reports/validation/2026-10-30-phase3-linux-llgl-upload/python-linux/receipt.json)는 실행 revision과 runner/generator source hash를 기록한다.
Docker 배포판 layer 진단은 별도이며 최종 증거에는 고정 VVL CI의 실제 결과를 사용했다.
원본을 받은 뒤 Windows offline 재검사도 통과했다.

```text
python3 tools/linux_llgl_upload.py run --probe out/linux-llgl-evidence --output out/NEW_UPLOAD
python3 tools/linux_llgl_upload.py check --output out/NEW_UPLOAD --live
python tools/linux_llgl_upload.py check --output benchmarks/reports/validation/2026-10-30-phase3-linux-llgl-upload/native
python benchmarks/reports/validation/2026-10-30-phase3-linux-llgl-upload/verify.py
python tools/roadmap.py --check
```

## 남은 단계

다음은 **LLGL Linux draw/instancing correctness 이식**이다. PSO·자원 생성 소비자도 별도 이식·전체 참조가 남아 있다.
`P3-LBEN-01`은 실제 Linux 물리 GPU qualification과 세 SDK의 분리된 fresh 성능 반복 측정이 있어야 완료한다.
`P3-BEN-07`~`08`, 나머지 LBEN·실제 화면·최종 gate의 미완료 상태도 유지했다.
