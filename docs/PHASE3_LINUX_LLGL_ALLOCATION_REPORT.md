# Linux LLGL 자원 생성·사용·공개 해제 정확성 준비

2026-11-01. 승인 LLGL 프로필의 Linux 자원 correctness 소비자를 이식했다.
고정 VVL에서 **정상 8개·core/sync 제어 2개·거부 제어 13개, 총 23개 직렬 프로세스**가 통과했다.
**버퍼 8,008개·전체 이미지 8,008개, 109,837,728바이트**, 공개 자원 생성/해제 **16,016쌍**과
native 자원 identity 확인 **16,016회**를 검증했다. software ICD 정확성 준비이며
`P3-LBEN-04`의 실제 Linux GPU·세 SDK fresh 성능 수용은 미완료다.

## 본문·의존성 보존

[`port_linux_llgl_allocation.py`](../tools/port_linux_llgl_allocation.py)는 고정 Windows 자원 main과
공통 PSO Support·Context·draw observer에서 Linux main/support/platform/observer를 결정적으로 생성한다.
`Totals`·`Allocation`의 생성·복사·전체 readback·공개 해제·cleanup 본문과 validation control은 원본과 정확히 같다.
관찰기도 export 속성 외에는 그대로며 원래 Payload·Observer header를 직접 컴파일한다.
기존 Windows 소스와 승인 LLGL 10줄 patch·의존성 checkout은 수정하지 않았다.
생성 manifest는 50개 adaptation, 원본 raw hash·생성 파일 hash와
`workload_bodies_changed=false`를 보존한다. 시스템 OpenSSL Crypto는 별도 소비자의 전제조건이며 SDK 의존성이 아니다.

공개 LLGL API로 device buffer/texture를 만든 뒤 각 자원의 upload/download staging을 별도로 생성한다.
upload와 download는 각각 별도 command submission·fence 완료·WaitIdle로 분리한다.
이는 고정 backend의 public CopyBuffer transfer barrier 부재를 처리한 기존 계약이며,
일반 workload에 native Vulkan 명령을 삽입하지 않는다. 전체 bytes와 SHA-256 확인 후 공개 핸들을 해제한다.
staging은 device 자원 생성/해제 경계 밖이며 GPU 사용·readback의 성능 시간은 측정하지 않는다.

동시에 보유한 공개 자원 핸들과 그에 대응하는 native 핸들의 중복을 검사한다.
buffer는 native type·nonzero VkBuffer, texture는 native image·65×37×1 RGBA8·1 mip/array layer/sample과
sampled/copy-src/copy-dst usage를 확인한다. 16,016회는 반복별 확인 합계이며 시간 전체에서 핸들이 재사용되지 않는다는 뜻은 아니다.
**native 자원 핸들 확인은 내부 메모리 할당 수·물리 GPU 메모리 회수의 확인을 의미하지 않는다.**
`native_allocation_count_verified=false`, `GPU_memory_reclamation_verified=false`, `allocator_cache_state_known=false`를 유지한다.

## 새 실행·장치 귀속

실행 source commit은 `2ad384975acaf3c8db8c8bf8301742124901d2e6`이고
[고정 VVL Linux CI](https://github.com/blackvrice/GraphicsSDK/actions/runs/41278687309)가 성공했다.
같은 revision에서 새 SDK/SDL qualification·승인 LLGL GCC build/probe 후,
동결한 core/backend shared library를 재사용해 소비자·관찰기만 새로 빌드했다.
과거 GPU 결과나 timing을 현재 표본으로 재사용하지 않는다.

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
| Consumer fingerprint | `71508f36de1d1f65b845e9ea3e57d0fb668747061706d9de29a0c50c036bdf5e` |

실제 LLGL native handle의 장치·driver·UUID·API·queue를 qualification과 대조한다.
UUID query instance와 LLGL instance/device 생성·수명을 관찰하고 Context 종료 후 live count 0을 확인한다.
query는 device나 draw를 만들지 않으며 자원 workload의 native draw count도 0이다.
실제 로드된 관찰기·crypto·driver·고정 VVL의 path/hash를 기록했다.

## 전체 데이터·제어

각 입력의 plain/required-validation을 분리 프로세스로 실행한다. 프로세스당 warmup 1+정확성 반복 3,
각 반복의 모든 자원 전체 bytes를 검사한다. payload는 uint32 wrap 연산으로 byte 위치·반복·자원 index를 섞는다.
GPU readback과 원래 CPU payload 전체 비교 후, 고정 독립 Python CPU SHA-256 상수와도 대조한다.
2026-10-29 reference 파일은 이식 가능한 CPU 상수 데이터로 재사용했다. 과거 GPU 결과·timing의 재사용이 아니다.
원본 reference 파일을 source ZIP·입력 hash·receipt에 묶고 현재의 고정 reference와도 대조한다.

| 입력 | 자원 크기 / 반복당 개수 | 두 모드의 생성·해제 쌍 및 full readback 수 | 전체 bytes |
|---|---|---:|---:|
| buffer1 | 4,096 bytes / 1 | 8 | 32,768 |
| buffer1000 | 4,096 bytes / 1,000 | 8,000 | 32,768,000 |
| texture1 | 65×37 RGBA8 / 1 | 8 | 76,960 |
| texture1000 | 65×37 RGBA8 / 1,000 | 8,000 | 76,960,000 |

texture는 tight row/layer stride 0, buffer는 vertex/copy-src/copy-dst·CPU access 0이며 초기 데이터를 공급하지 않는다.
정상 오류는 0이다. `sample_count=3`은 correctness 반복 수이고
`performance_samples=0`, `samples_ms=[]`, `gpu_ms=null`, `GPU_use_in_timing=false`, `performance_accepted=false`다.

| 제어 | 실제 기대 결과 |
|---|---|
| core-control | `VUID-VkPhysicalDeviceProperties2-sType-sType`, 오류 1개 |
| sync-control | `SYNC-HAZARD-WRITE-AFTER-WRITE`, 오류 1개 |
| missing layer / UUID / kind | 각각 guard 오류·exit 1·성공 JSON 없음 |
| missing-resource | 공개 자원 생성 개수 불일치·exit 1 |
| alias-resource | 중복 live public resource handle·exit 1 |
| skip-release | 공개 자원 해제 개수 불일치·exit 1 |
| wrong-resource-payload / stale-iteration / skip-copy / corrupt-readback | 전체 changing resource 참조 불일치·exit 1 |
| short-readback | 불완전한 전체 자원 readback·exit 1 |
| missing-observer | 실제 module 부재로 exit 1 |
| sync-disabled | 분리 설정에서 기대 sync diagnostic 부재로 exit 1 |

오류 제어의 invalid native 명령은 record만 하며 GPU에 제출하지 않는다.

## 입력 감사·원본 보존·재검사

실제 verbose/Ninja compiler command **2개**와 GNU dependency 입력 **463개**를 감사했다.
분류는 LLGL 86, Vulkan headers 13, 자체 header 3,
생성 소스/platform/support 4, 시스템 357개다.
고정 tracked header·허용 license와 원래 Windows Context/Support의 컴파일 혼입 부재를 확인했다.
`-O2 -DNDEBUG`, no LTO/unity/CPU tuning이며 시스템 header/library는 재배포하지 않는다.
동결 입력 1,537개와 runtime 파일 42개를 실행 전·최종 대조했다.
원본 32파일 source ZIP, 생성 코드·adaptation·observer manifest·CPU reference,
raw argv/environment/PID/직렬 구간/stdout/stderr, 실제 .d·분류·source/license/runtime hash와 선행 qualification을 보존했다.
외부 binary는 포함하지 않는다.

[요약](../benchmarks/reports/validation/2026-11-01-phase3-linux-llgl-allocation/summary.json),
[원본 receipt](../benchmarks/reports/validation/2026-11-01-phase3-linux-llgl-allocation/native/receipt.json),
[입력 감사](../benchmarks/reports/validation/2026-11-01-phase3-linux-llgl-allocation/native/inputs.json),
[저장 증거 verifier](../benchmarks/reports/validation/2026-11-01-phase3-linux-llgl-allocation/verify.py)를 연결한다.
verifier는 repository validator를 사용하며 ZIP 소스나 native code를 실행하지 않는다.
manifest는 외부 신뢰 서명이 아니며 offline 재생은 GPU 재실행을 대신하지 않는다.

새 회귀 테스트 **16개**, [Windows·Linux 전체 CI](https://github.com/blackvrice/GraphicsSDK/actions/runs/41278687318)의
각 **1,343개** 테스트가 통과했다. 두 OS receipt에 실행 revision과 runner/generator source hash를 보존했다.
Docker 배포판 VVL 진단은 개발용이며 최종 증거는 고정 VVL CI의 새 실행이다.

```text
python3 tools/linux_llgl_allocation.py run --probe out/linux-llgl-evidence --output out/NEW_ALLOCATION
python3 tools/linux_llgl_allocation.py check --output out/NEW_ALLOCATION --live
python tools/linux_llgl_allocation.py check --output benchmarks/reports/validation/2026-11-01-phase3-linux-llgl-allocation/native
python benchmarks/reports/validation/2026-11-01-phase3-linux-llgl-allocation/verify.py
python tools/roadmap.py --check
```

## 남은 범위

이번 이식으로 승인 LLGL의 Linux 업로드·draw/instancing·PSO·자원 4종 correctness 소비자 준비를 완료했다.
SDK/SDL 전체 software matrix와 같은 source revision에서 실행한 네 LLGL 소비자의 결과를 통합해
물리 GPU 환경에서 사용할 실행·수용 준비를 연결하는 작업이 다음 단계다.
실제 Linux GPU qualification과 동일 GPU·driver의 세 SDK fresh 성능 반복 비교가 있어야 LBEN-01~04를 완료한다.
BEN-07·08, 실제 화면·최종 gate의 미완료 상태와 기존 task 143개의 status를 유지했다.
