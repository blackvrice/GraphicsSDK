# 새 Vulkan 할당 후보의 OOM·fallback·복구 검증

2026-10-24 / Windows x64 / MSVC 19.44. 앞선 혼합 churn 단계에서 남겨 둔 **새 후보
fingerprint의 전체 allocation-failure 재시험**을 수행하고, 혼합 크기·기록 보유 상태의
추가 fallback 시험을 구현했다. 제품 소스·후보 소스·이전 실험은 변경하지 않았다.
제품 `2e511b8e53dbef4bbb2bf3d86ab4b19cf03e8765111de2b3dad4fe45fa551401`,
후보 `40a467f7d34faaf412ce3e0568c8f76ccc0c2cd80baf7cf4d7b402551bf8aed8`을 그대로 사용한다.
**제품의 dedicated 기본 정책을 유지하며 pooling은 채택하지 않았다.**

## 새 구현과 검증 범위

[기존 15개 사례](../experiments/vulkan_buffer_pool_fault/README.md)의 원본 CMake·C++·레이어를
수정하지 않고 새 build root에서 현재 후보 설치본에 링크했다. 기존 실패 plan의 CPU 계약
**914 assertions / GPU_calls=0**도 새 빌드에서 통과했다. 예전 결과를 새 fingerprint의
결과로 바꿔 표기하지 않는다.

[추가 소비자](../experiments/vulkan_pool_rebase_fault/README.md)는 실제 설치 SDK와 기존
binding observer를 사용한다. 각 cycle에서 4KiB/64-byte pool buffer와 8KiB standalone
buffer를 유지하고, 경우에 따라 다른 4KiB buffer와 기록이 보유한 Readback도 함께 둔다.

| 추가 사례 | 주입과 기대 결과 |
|---|---|
| mixed-host / mixed-device | 8KiB block 확장에 host/device OOM → 4KiB 정확한 크기 fallback 성공 |
| mixed-double | block host OOM → exact fallback device OOM → 기존 자원 보존·정상 재시도 |
| mixed-unknown | -13 오류 → OOM fallback을 시도하지 않고 오류 전달 → 정상 재시도 |
| held-fallback-bind | block device OOM → exact 할당 성공 → bind host OOM → 명시적 정리·보유 기록 데이터 유지 |
| reuse-then-host / reuse-then-device | 기존 slot 재사용 중 allocation OOM 미소비 → 다음 16KiB block 확장에서 소비 → exact fallback 성공 |

재사용 사례에서는 기존 8KiB block의 offset 4096을 실제로 사용하고 native memory
수·bytes는 증가하지 않아야 한다. 그때 allocation fault는 pending 상태로 남는다.
다음 생성의 16KiB 확장에서 오류가 소비되고, 새 4KiB 메모리의 offset 0으로 바인딩돼야
한다. 다른 성공 fallback도 실제 요구량·예약량이 모두 4096이며 기존 메모리와 별도다.
실패 사례는 guard의 native handle/range와 public 통계를 유지하고, 재시도는 원래
8KiB block 성장으로 복귀한다. Metadata와 native interval counts를 함께 대조했다.

[Vulkan 오류 계약](https://docs.vulkan.org/spec/latest/chapters/fundamentals.html#fundamentals-errorcodes)에
따라 실패한 할당 출력은 소유하지 않는다. Own layer는 오류 출력에 non-null poison을
넣고 잘못된 FreeMemory를 하위 VVL/driver 전에 차단한다.
[vkAllocateMemory 계약](https://docs.vulkan.org/refpages/latest/refpages/source/vkAllocateMemory.html)의
성공 요청 크기를 기준으로 fallback bytes를 확인한다. 실제 heap 고갈은 유도하지 않으며,
unknown 오류는 애플리케이션 오류 분기 확인용 합성 반환이다.

## 실제 결과

NVIDIA GTX 1660 SUPER / Intel UHD Graphics 770 × validation OFF/ON에서 기존 사례
**60개**, 추가 사례 **28개**, 총 **88개 프로세스 / 264개 cycle**이 통과했다.
두 모드의 native interval counts/bytes와 readback 작업량은 각각 일치했다. SDK의 VVL
error count=0, 정상 stderr는 비어 있으며 고정 VVL DLL의 실제 경로를 확인했다.
Device/driver UUID와 native/DXGI LUID, 환경 정보도 같은 GPU별로 대조했다.

- 합성 오류 **324회**: allocation OOM **228**, unknown **24**, bind 오류 **72**.
- 사용 가능한 exact fallback **72회**, fallback 메모리 할당 성공 **96회**.
  그중 bind 실패로 반환·정리한 경우 **24회**이며 사용 성공 수와 구분한다.
- 두 연속 allocation 실패 **48회**, 재사용 뒤 pending OOM이 다음 확장에서 소비된 경우 **24회**.
- Full GPU readback **1,200회 / 4,975,104 bytes**, native assertion **4,104**.
- Buffer **2,844개 생성·파괴**, 메모리 시도 **2,916회**, 성공·FreeMemory **2,664회**,
  성공 요청·반환 bytes **11,768,832**로 일치했다.
- 무효 free, binding guard, drop/accounting 오류, implicit device cleanup,
  미소비 fault, 최종 native buffer/image/memory/bytes는 모두 **0**.

각 정상 cycle은 기존 guard 데이터를 덮어쓰지 않고 읽으며, target/retry에는 별도 payload를
쓴 뒤 전체 내용을 읽는다. 기록 보유 사례는 wrapper 만료 뒤에도 원래 allocation을 기록이
보유하고, 실패·재시도 후 제출한 결과가 원래 데이터와 일치해야 한다. 최종 release/idle/
device teardown도 별도 구간으로 검사해 device 파괴가 누수를 숨기지 않도록 했다.

Missing manifest, 잘못된 DLL/GPU, 잘못된 CLI의 **14개 fail-closed 통제**는 결과 JSON
없이 실패했다. Probe 1개를 합한 실행 명령은 **103개**다. Python 전체 시험 **614개**가
건너뛴 시험 없이 통과했고, 정상 복사본을 먼저 승인한 뒤 보고서 변조 **12개**와 inventory
변조·notice 누락 **2개**를 거부했다. 최초 두 pilot은 assertion 수를 잘못 지정한 결과
검사기 때문에 승인이 중단됐으며 원시 출력은 보존했다. 최종 수는 C++ 검사 지점과 맞춰
case별로 검산하고 검증 모드의 추가 검사도 구분한다. Native 소비자를 바꾸지 않았다.

## 결정과 제한

현재 후보는 **기존 15개와 추가 7개의 bounded 할당 장애·복구 계약**을 새 실행으로
통과했다. 제품·후보·설치 라이브러리가 그대로이므로 앞선 후보의 **55 selected CTests**와
혼합 churn은 [이전 증거](VULKAN_POOL_CHURN_REPORT.md)로 구분한다. 이번에 그 전체 gate를
재실행했다고 주장하지 않는다. 세 memory reclamation 시험의 등록·정책도 바꾸지 않았다.

관측한 요청 bytes와 FreeMemory 호출은 OS commitment/residency나 물리 VRAM 회수의
증거가 아니다. 장시간 메모리 기준과 과거 Intel AV 원인 조사는 계속 보류한다. 실제
exhaustion, device loss, 다른 OS/driver와 모든 Vulkan 장애를 검증한 것이 아니다.

앞서 관측한 **sparse 예약/실제 요구량 6.2배**의 정책 부담은 남아 있다. 이번 단계는 이를
줄이거나 새 성능 우위를 확인하지 않았다. 후보 승격 전에는 예약량 정책과 같은 논리 작업을
수행하는 새 성능 비교가 필요하다. 이전 timing을 현재 후보의 새 측정으로 사용하지 않는다.

## 재현·증거

- [Build script](../benchmarks/reports/validation/2026-10-24-vulkan-pool-rebase-fault/verification-scripts/build.py):
  기존 프로젝트와 새 소비자 모두 새 root에서 MSVC Release `--parallel 1`로 빌드했다.
- [Runner](../tools/run_vulkan_pool_rebase_fault.py),
  [raw results](../benchmarks/results/2026-10-24-vulkan-pool-rebase-fault/results.json),
  [commands](../benchmarks/results/2026-10-24-vulkan-pool-rebase-fault/commands.json),
  [요약](../benchmarks/reports/2026-10-24-vulkan-pool-rebase-fault/summary.json).
  child별 Vulkan 환경을 정리하고 implicit layer를 비활성화하며 전역 설정은 변경하지 않는다.
- [의존성 inventory](../licenses/generated/windows-vulkan-pool-rebase-fault-release/inventory.json):
  **998 conservative inputs / 21 components**, 새 compiler/linker reads 각 **5개**와
  이전 closure를 구분한다. 같은 pinned Vulkan-Headers와 own layer/observer만 사용했다.
  새 third-party 의존성·vendor 수정·바이너리 재배포는 없다. 법률 승인이나 symbol 도달성의 증명이 아니다.
- [Evidence manifest](../benchmarks/reports/2026-10-24-vulkan-pool-rebase-fault/evidence.json)는
  own-source ZIP, raw process/빌드·검사 logs, 이전 증거·후보 source/설치본과 기존 배포
  ZIP 23개의 hash를 보존한다. Source ZIP에는 vendor trees, EXE/DLL/LIB/PDB,
  dump/ETL/VVL runtime을 포함하지 않는다.
