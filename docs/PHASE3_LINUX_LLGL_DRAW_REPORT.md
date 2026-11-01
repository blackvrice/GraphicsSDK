# Linux LLGL draw·instancing 전체 정확성 준비

2026-11-01. 승인 LLGL 프로필의 Linux draw 소비자와 실제 Vulkan pass-through 관찰기를 이식했다.
**정상 6개·core/sync 제어 2개·거부 제어 9개, 총 17개 직렬 프로세스**가 고정 VVL에서 통과했다.
**전체 changing image 24개, 230,880바이트**와 독립 Python CPU 참조 SHA-256이 일치했다.
이는 software ICD 정확성 준비이며 `P3-LBEN-02`의 실제 Linux GPU·fresh 성능 수용은 미완료다.

## 이식과 보존 계약

[`port_linux_llgl_draw.py`](../tools/port_linux_llgl_draw.py)는 고정 Windows correctness main·Context·observer에서
Linux main/platform/observer를 결정적으로 생성한다. `validationControl`·`Draw` 본문은 원본과 정확히 같다.
observer도 export 속성만 바꾸며 dispatch·호출 전달·집계 본문을 그대로 보존한다. Frame·shader·Observer header는
원본을 직접 컴파일한다. 기존 Windows 소스와 승인 LLGL 10줄 patch·의존성 checkout은 수정하지 않았다.
생성 manifest에는 입력별 raw hash, 31개 adaptation,
생성 파일 hash와 `workload_bodies_changed=false`를 보존했다.

LLGL 공개 API로 offscreen pass, clear, viewport/scissor, red/green PSO 선택,
Draw/DrawInstanced, Submit(command/fence)/WaitFence를 실행한다. 별도 readback 명령도 fence 완료 후 전체 비교한다.
일반 workload를 native Vulkan으로 대체하지 않는다. native 오류 제어는 명령을 record할 뿐 GPU에 제출하지 않는다.
시스템 OpenSSL Crypto SHA-256은 별도 소비자의 전제조건이며 SDK에 새 의존성을 추가하지 않는다.

## 새 실행·장치 귀속

실행 source commit은 `0e0448e7a7d3fffbef836236c5e3cd317863e59c`이며
[고정 VVL Linux CI](https://github.com/blackvrice/GraphicsSDK/actions/runs/41274079902)가 성공했다.
같은 revision에서 새 SDK/SDL qualification·승인 LLGL GCC build/probe를 실행한 후,
그대로 동결한 core/backend shared library를 사용해 새 소비자와 관찰기만 빌드했다.
과거 Windows/Linux GPU 결과·timing을 새 표본으로 재사용하지 않는다.

| 항목 | 실제 관측·입력 |
|---|---|
| OS / compiler | Linux x86_64, kernel `7.0.0-1017-azure`, GCC 14.2.0 |
| CPU / RAM | Intel(R) Xeon(R) Platinum 8370C CPU @ 2.80GHz, 16,759,304,192 bytes |
| Native device / type | `llvmpipe (LLVM 20.1.2, 256 bits)`, 4 |
| Device / driver UUID | `6d65736132352e322e382d3075627500` / `6c6c766d706970655555494400000000` |
| Driver / native API raw | 104865800 / 4211006 |
| Queue family / index / flags | 0 / 0 / 15 |
| Actual LLGL instance API request | 4206867, 관찰기의 실제 vkCreateInstance 입력 |
| LLGL pin / profile | `649c0cc8fa608312c5e4b94dae409a5859fc7553` / `llgl-fixed-pin-transfer-src-v1` |
| Consumer fingerprint | `93d4316a53009c2911bb815c0b905f41af0773f5f0dea4ba5cb04496a8bf3dec` |

UUID·driver·API·native queue를 실제 LLGL native handle과 선행 qualification 양쪽에서 대조한다.
관찰기는 UUID 선택 query instance 1개와 LLGL instance 1개, LLGL device 1개의 생성·수명을 확인한다.
query는 device나 draw를 만들지 않는다. 마지막 LLGL instance API 요청을 직접 기록하며,
Context 종료 후 두 instance와 device의 live count가 모두 0인지 검사한다.
관찰기 shared object와 시스템 crypto·driver·고정 VVL은 실제 로드된 module path/hash로 귀속한다.

## 전체 이미지와 native 호출

65×37 RGBA8, scissor `[5,3,55,31]`, 3 vertices, firstVertex/firstInstance 0을 사용한다.
각 입력의 plain/required-validation을 별도 프로세스로 실행한다. 프로세스당 warmup 1+정확성 반복 3의
전체 이미지 4개를 검사한다. 색을 교대하며 전체 bytes·독립 CPU SHA-256 양쪽을 대조한다.

| 입력 | frame당 calls × instances | 두 모드의 전체 native calls / instances / vertices | 전체 이미지 / bytes |
|---|---|---|---|
| draw1 | 1 × 1 | 8 / 8 / 24 | 8 / 76,960 |
| draw1000 | 1000 × 1 | 8,000 / 8,000 / 24,000 | 8 / 76,960 |
| instanced1000 | 1 × 1000 | 8 / 8,000 / 24,000 | 8 / 76,960 |

vkCmdDraw를 실제 다음 dispatch로 전달한 횟수, instances·vertices·잘못된 인수,
최소/최대 instance 수를 frame별 delta와 최종 합계 양쪽에서 검사한다.
정상 실행의 delivered error는 모두 0이다. `sample_count=3`은 correctness 반복 수이며
`performance_samples=0`, `samples_ms=[]`, `CPU_intervals_ms=[]`, `gpu_ms=null`이다.

| 제어 | 실제 기대 결과 |
|---|---|
| core-control | `VUID-VkPhysicalDeviceProperties2-sType-sType`, 오류 1개, draw 0 |
| sync-control | `SYNC-HAZARD-WRITE-AFTER-WRITE`, 오류 1개, draw 0 |
| missing layer / UUID / kind | 각 guard 오류·exit 1·성공 JSON 없음 |
| corrupt-readback / wrong-scissor | 전체 changing frame 참조 불일치·exit 1 |
| wrong-call-count / wrong-instance-count | native 호출/인수 계약 불일치·exit 1 |
| sync-disabled | 분리 설정에서 기대 sync diagnostic 부재로 exit 1 |
| missing-observer | 실제 module 부재로 exit 1 |

## 빌드 입력·보존·재검사

실제 verbose/Ninja compiler command **2개**와 GNU dependency 입력 **463개**를 감사했다.
분류는 LLGL 86, Vulkan headers 13, 자체 header 4,
생성 소스/platform 3, 시스템 357개다.
고정 tracked LLGL/Vulkan header와 허용 license를 확인하고 시스템 header·library는 재배포하지 않는다.
컴파일은 `-O2 -DNDEBUG`, no LTO/unity/CPU tuning이며 원래 Windows Context가 컴파일에 섞이지 않았는지도 검사한다.
동결 입력 1,524개와 runtime 파일 42개를 실행 전·최종 대조했다.
원본 20파일 source ZIP, 생성 코드, adaptation, observer layer manifest,
raw argv/environment/PID/직렬 구간/stdout/stderr, 실제 .d와 source/license/runtime hash를 보존했다.
외부 binary는 포함하지 않는다.

[요약](../benchmarks/reports/validation/2026-11-01-phase3-linux-llgl-draw/summary.json),
[원본 receipt](../benchmarks/reports/validation/2026-11-01-phase3-linux-llgl-draw/native/receipt.json),
[입력 감사](../benchmarks/reports/validation/2026-11-01-phase3-linux-llgl-draw/native/inputs.json),
[저장 증거 verifier](../benchmarks/reports/validation/2026-11-01-phase3-linux-llgl-draw/verify.py)를 연결한다.
verifier는 repository validator를 사용하며 ZIP 소스나 native code를 실행하지 않는다.
manifest는 외부 신뢰 서명이 아니며 offline 재생은 GPU 재실행을 대신하지 않는다.

새 회귀 테스트 **14개**, [Windows·Linux 전체 CI](https://github.com/blackvrice/GraphicsSDK/actions/runs/41274079910)의
각 **1,311개** 테스트가 통과했다. 두 OS receipt는 실행 revision과 실제 테스트한 runner/generator hash를 기록한다.
Docker 배포판 VVL 진단은 개발용으로만 사용했다. 최종 증거는 고정 VVL CI의 새 실행이다.

```text
python3 tools/linux_llgl_draw.py run --probe out/linux-llgl-evidence --output out/NEW_DRAW
python3 tools/linux_llgl_draw.py check --output out/NEW_DRAW --live
python tools/linux_llgl_draw.py check --output benchmarks/reports/validation/2026-11-01-phase3-linux-llgl-draw/native
python benchmarks/reports/validation/2026-11-01-phase3-linux-llgl-draw/verify.py
python tools/roadmap.py --check
```

## 남은 단계

다음은 **LLGL Linux PSO correctness 소비자 이식**이다. 자원 생성 소비자도 별도 이식·전체 참조가 남아 있다.
`P3-LBEN-02`는 실제 Linux GPU qualification과 동일 GPU·driver의 세 SDK fresh 성능 반복 측정이 있어야 완료한다.
BEN-07·08, 나머지 LBEN, 실제 화면·최종 gate의 미완료 상태와 전체 task 143개의 status는 유지했다.
