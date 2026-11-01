# Linux LLGL PSO 생성·사용·공개 해제 정확성 준비

2026-11-01. 승인 LLGL 프로필의 Linux PSO correctness 소비자를 이식했다.
고정 VVL에서 **정상 4개·core/sync 제어 2개·거부 제어 11개, 총 17개 직렬 프로세스**가 통과했다.
**전체 changing image 16개, 153,920바이트**, 공개 파이프라인 생성/해제 **72쌍**과
셰이더 핸들 생성/해제 **144쌍**을 확인했다. software ICD 정확성 준비이며,
`P3-LBEN-03`의 실제 Linux GPU·세 SDK fresh 성능 수용은 미완료다.

## 이식과 보존 범위

[`port_linux_llgl_pso.py`](../tools/port_linux_llgl_pso.py)는 고정 Windows PSO main·Support·Context와
draw observer에서 Linux main/support/platform/observer를 결정적으로 생성한다.
`Totals`·`PSO`의 생성·사용·공개 해제·readback 본문과 validation control은 원본과 정확히 같다.
관찰기의 dispatch·호출 전달·집계 본문도 export 속성 외에는 그대로다. 원래 Frame·shader·Observer header를
직접 컴파일한다. 기존 Windows 소스와 승인 LLGL 10줄 patch·의존성 checkout은 수정하지 않았다.
생성 manifest는 43개 adaptation, 원본 raw hash와 생성 파일 hash,
`workload_bodies_changed=false`를 보존한다. 시스템 OpenSSL Crypto는 소비자의 전제조건이며 SDK 의존성이 아니다.

각 반복에서 새 공개 shader/pipeline 핸들을 만든 뒤 shader 핸들을 해제하고,
PSO별 stripe를 draw한다. command/fence 완료와 WaitIdle 후 파이프라인 핸들을 공개 해제하고,
별도 command/fence로 readback해 전체 이미지를 확인한다. LLGL 공개 API의 경로를 보존했으며
native Vulkan으로 일반 workload를 대체하지 않는다. 동시에 보유한 공개 PSO 핸들은 중복되지 않아야 한다.

**공개 핸들 해제는 native pipeline 개수·물리 GPU 메모리 회수의 확인을 의미하지 않는다.**
관찰기는 실제 vkCmdDraw 전달과 native instance/device 수명만 본다.
`native_pipeline_count_verified=false`, `GPU_memory_reclamation_verified=false`,
`driver_cache_state_known=false`, `cache_state=uncontrolled_driver_cache_repeated_public_creation`을 유지한다.
셰이더 객체 생성/해제가 범위에 포함되지만 실제 성능 시간은 수집하지 않는다.

## 새 실행과 장치 귀속

실행 source commit은 `efdd979c6d34f7b234a14e32f96aea0fc93bfec5`이고
[고정 VVL Linux CI](https://github.com/blackvrice/GraphicsSDK/actions/runs/41276880120)가 성공했다.
같은 revision의 새 SDK/SDL qualification과 승인 LLGL GCC build/probe 후,
동결한 core/backend shared library를 그대로 재사용해 소비자·관찰기만 새로 빌드했다.
과거 Windows/Linux 측정이나 timing을 현재 표본으로 재사용하지 않는다.

| 항목 | 실제 관측·입력 |
|---|---|
| OS / compiler | Linux x86_64, kernel `7.0.0-1017-azure`, GCC 14.2.0 |
| CPU / RAM | AMD EPYC 9V45 96-Core Processor, 16,760,344,576 bytes |
| Native device / type | `llvmpipe (LLVM 20.1.2, 256 bits)`, 4 |
| Device / driver UUID | `6d65736132352e322e382d3075627500` / `6c6c766d706970655555494400000000` |
| Driver / native API raw | 104865800 / 4211006 |
| Queue family / index / flags | 0 / 0 / 15 |
| Actual LLGL instance API request | 4206867, 실제 vkCreateInstance 입력 |
| LLGL pin / profile | `649c0cc8fa608312c5e4b94dae409a5859fc7553` / `llgl-fixed-pin-transfer-src-v1` |
| Consumer fingerprint | `b31a481c37f74be9765aca211fa096b6a1882bfd2f16b15e9e64e1311c521cfe` |

실제 LLGL native handle의 장치·driver·UUID·API·queue를 qualification과 대조한다.
UUID query용 instance 1개와 LLGL instance/device 생성·수명을 관찰하고 Context 종료 후 live count 0을 검사한다.
query는 device나 draw를 만들지 않는다. 실제 로드된 관찰기·crypto·driver·고정 VVL의 path/hash를 기록한다.

## 전체 참조와 제어

65×37 RGBA8, scissor `[5,3,55,31]`, PSO당 3 vertices, firstVertex/firstInstance 0이다.
각 입력을 plain/required-validation의 별도 프로세스로 실행한다. 프로세스당 warmup 1+정확성 반복 3의
이미지 4개를 전부 확인하며 clear와 stripe 색을 교대한다. 전체 bytes와 독립 Python CPU reference SHA-256을 대조한다.
`pso8`은 정수 분할 폭이 다른 8개 stripe를 각기 다른 공개 파이프라인으로 그린다.

| 입력 | 반복당 PSO | 두 모드의 공개 PSO 생성/해제 쌍 | shader 핸들 생성/해제 쌍 | 실제 draw / vertices | 이미지 / bytes |
|---|---:|---:|---:|---|---|
| pso1 | 1 | 8 | 16 | 8 / 24 | 8 / 76,960 |
| pso8 | 8 | 64 | 128 | 64 / 192 | 8 / 76,960 |

정상 오류는 0이며 frame별 실제 전달 draw·instance·vertex 수와 인수를 검사한다.
`sample_count=3`은 correctness 반복 수다. `performance_samples=0`, `samples_ms=[]`, `gpu_ms=null`,
`GPU_use_in_timing=false`, `performance_accepted=false`다.

| 제어 | 실제 기대 결과 |
|---|---|
| core-control | `VUID-VkPhysicalDeviceProperties2-sType-sType`, 오류 1개, draw 0 |
| sync-control | `SYNC-HAZARD-WRITE-AFTER-WRITE`, 오류 1개, draw 0 |
| missing layer / UUID / kind | 각각 guard 오류·exit 1·성공 JSON 없음 |
| missing-pipeline | 공개 PSO 생성 개수 불일치·exit 1 |
| skip-release | 공개 PSO 해제 개수 불일치·exit 1 |
| wrong-pipeline / corrupt-readback / wrong-scissor | 전체 changing stripe 참조 불일치·exit 1 |
| skip-region | native PSO draw 개수·인수 불일치·exit 1 |
| sync-disabled | 분리 설정에서 기대 sync diagnostic 부재로 exit 1 |
| missing-observer | 실제 module 부재로 exit 1 |

오류 제어의 invalid native 명령은 record만 하며 GPU에 제출하지 않는다.

## 입력 감사·원본 보존·재검사

실제 verbose/Ninja compiler command **2개**와 GNU dependency 입력 **464개**를 감사했다.
분류는 LLGL 86, Vulkan headers 13, 자체 header 4,
생성 소스/platform/support 4, 시스템 357개다.
고정 tracked header·허용 license를 검사하며 시스템 header/library는 재배포하지 않는다.
원래 Windows Context/Support가 컴파일에 섞이지 않았는지도 확인했다.
`-O2 -DNDEBUG`, no LTO/unity/CPU tuning이다. 동결 입력 1,532개와 runtime 파일 42개를
실행 전·최종 대조했다. 원본 26파일 source ZIP, 생성 소스·adaptation·observer manifest,
raw argv/environment/PID/직렬 구간/stdout/stderr, 실제 .d·분류·source/license/runtime hash와 선행 qualification을 보존했다.
외부 binary는 포함하지 않는다.

[요약](../benchmarks/reports/validation/2026-11-01-phase3-linux-llgl-pso/summary.json),
[원본 receipt](../benchmarks/reports/validation/2026-11-01-phase3-linux-llgl-pso/native/receipt.json),
[입력 감사](../benchmarks/reports/validation/2026-11-01-phase3-linux-llgl-pso/native/inputs.json),
[저장 증거 verifier](../benchmarks/reports/validation/2026-11-01-phase3-linux-llgl-pso/verify.py)를 연결한다.
verifier는 repository validator를 사용하고 ZIP 소스나 native code를 실행하지 않는다.
manifest는 외부 신뢰 서명이 아니며 offline 재생은 GPU 재실행을 대신하지 않는다.

새 회귀 테스트 **16개**, [Windows·Linux 전체 CI](https://github.com/blackvrice/GraphicsSDK/actions/runs/41276880118)의
각 **1,327개** 테스트가 통과했다. 두 OS receipt에 실행 revision과 runner/generator source hash를 보존했다.
Docker 배포판 VVL 진단은 개발용이며 최종 증거는 고정 VVL CI의 새 실행이다.

```text
python3 tools/linux_llgl_pso.py run --probe out/linux-llgl-evidence --output out/NEW_PSO
python3 tools/linux_llgl_pso.py check --output out/NEW_PSO --live
python tools/linux_llgl_pso.py check --output benchmarks/reports/validation/2026-11-01-phase3-linux-llgl-pso/native
python benchmarks/reports/validation/2026-11-01-phase3-linux-llgl-pso/verify.py
python tools/roadmap.py --check
```

## 남은 단계

다음은 **LLGL Linux 자원 생성·사용·공개 해제 correctness 이식**이다.
`P3-LBEN-03`은 실제 Linux GPU qualification과 동일 GPU·driver의 세 SDK fresh 성능 반복 비교가 있어야 완료한다.
BEN-07·08, 나머지 LBEN, 실제 화면·최종 gate의 미완료 상태와 기존 task 143개의 status를 유지했다.
