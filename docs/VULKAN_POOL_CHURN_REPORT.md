# Vulkan 혼합 크기·점유율·반복 churn 검증

2026-10-24 / Windows x64 / MSVC 19.44. 최근 생성 출력 소유권 수정을 포함하는 제품을
기반으로 적응형 소형 buffer pool의 **새 격리 후보**와 독립 검증 소비자를 구현했다.
제품 소스와 public API는 바꾸지 않았으며 **제품에 pooling을 채택하지 않았다**.

제품 fingerprint는 `2e511b8e53dbef4bbb2bf3d86ab4b19cf03e8765111de2b3dad4fe45fa551401`,
새 후보는 `40a467f7d34faaf412ce3e0568c8f76ccc0c2cd80baf7cf4d7b402551bf8aed8`이다.
이전 hardened 후보 `3b0f54c6…`의 원본·실험·증거는 보존했다. 새
[생성기](../tools/prepare_vulkan_pool_churn.py)는 두 source identity, 원본 파일 hash,
유일한 수정 anchor를 확인하고 별도 `out/vulkan-pool-churn-source`만 만든다.
[Patch](../experiments/vulkan_pool_churn/candidate.patch)는 현재 제품과의 차이다.

## 구현과 계약

기존 BufferPool/BufferSlots의 정책 파일은 이전 후보와 byte-identical이다. Device-local,
native 요구 크기·정렬이 4KiB 이하이고 dedicated 요구/선호가 없는 buffer만 memory type과
power-of-two stride별로 공유한다. 클래스는 한 slot에서 시작해 live block의 최대 용량을
두 배씩 늘리며 block당 최대 64KiB다. 빈 slot을 우선 재사용하고 빈 block은 즉시 해제한다.
Host buffers, 큰 요구 크기·정렬, dedicated 요구/선호는 exact dedicated 경로다.

현재 제품의 기존 29개 생성 guard를 유지하면서 pool block 할당도 `acquireOutput`으로
받는다(후보 30개 지점). 실패 출력은 소유하지 않는다. Graphics pipeline의 정의된 부분
성공 정리는 그대로다. Public header, host-memory 선택, 제품 기본 dedicated 정책은 유지한다.

[시험 코드](../experiments/vulkan_pool_churn/README.md)는 실제 설치 SDK 소비자다.
Own fault layer를 수정 없이 새 TU에 포함하고 read-only binding snapshot export를 추가했다.
`GetBufferMemoryRequirements2`의 size/alignment/memory bits와 dedicated 요구·선호를
실제 driver/VVL downstream에서 읽어 기존 요구값과 일치하는지도 확인한다.
[Vulkan 바인딩 계약](https://docs.vulkan.org/refpages/latest/refpages/source/vkBindBufferMemory.html)의
정렬·범위·memory type 조건과
[dedicated 요구](https://docs.vulkan.org/refpages/latest/refpages/source/VkMemoryDedicatedRequirements.html)를
기준으로 독립 모델과 실제 native bindings를 비교한다.

모델은 메모리 핸들·offset·실제 요구 범위를 대조해 live owner 집합, 같은 class/type의
첫 빈 slot 우선 재사용, 현재 live block 기반 성장, 별도 host 할당, 비중첩, 마지막 lease
해제 후 native 메모리 반환과 public 통계의 일치를 검사한다. 물리 주소나 OS residency는
관측하지 않는다. Ended recording과 SDK pending submission의 보유를 실제 wrapper 만료,
별도 payload 쓰기, 이전 buffer의 full readback으로 확인한다. Pending checkpoint는 SDK
poll을 호출하지 않으므로 GPU가 아직 실행 중이라는 시간적 가정을 하지 않는다.

## 실행 결과

NVIDIA GTX 1660 SUPER / Intel UHD Graphics 770 × 제품/후보 × validation OFF/ON에서
각각 세 패턴의 **24개 프로세스**를 순차 실행했다. Validation 모드별 native counts,
footprint checkpoints, 모델 counters, readback 수가 정확히 일치했다. 실제 VVL DLL 경로와
GPU/driver identity를 확인하고 정상 stderr와 SDK validation error count는 모두 0이었다.

| 패턴 | 프로세스당 반복 | 내용 |
|---|---:|---|
| mixed | 16 | 최대 96 buffers, 24개 경계 크기, 1/2·1/3·1/4 생존, 주기적 완전 해제 |
| sparse | 4 | 크기별 31 buffers → native block당 한 개 생존 → 빈 slot 재충전 → reset |
| held | 8 | 24개 wrapper 해제·recording 보유, 72개 replacement, 제출·기록 해제·wait·원본 검증 |

매 반복 한 번씩 BindBufferMemory에 device/host OOM(-2/-1 교대)을 주입해 실패한
생성의 slot/block/public 통계 rollback과 기존 데이터 보존을 검사했다. **224개 반복 /
224개 합성 오류**, 무효 free·겹침 guard·미소비 오류·implicit device cleanup·drop·trace
오류는 모두 0이었다. 실제 heap 고갈이나 device loss를 유도하지 않았다.

Full buffer readback **35,392회 / 107,417,152 bytes**, assertion **118,960**, 모델 검사
**22,968회**, stable/retained checkpoints **1,080개**가 통과했다. Native buffer
**75,840개 생성·파괴**, memory **68,476회 할당·해제 / 249,273,600 bytes 요청·반환**이
일치했고 최종 live buffer/image/memory/bytes=0이다. 보조 Upload/Readback과 실패한
bind의 rollback 호출도 이 native 합계에 포함한다. Native 요청/FreeMemory 호출은
물리 메모리 회수나 장시간 메모리 plateau의 증명이 아니다.

새 후보의 selected product CTests **55/55**와 두 소비자의 CPU 모델 계약
각 **172 assertions / GPU_calls=0**가 통과했다. 세 memory reclamation 시험은 최초
CTest 선택에서 `-E ^memory_reclamation_`로 제외하고 등록·정책은 유지했다.
Python 전체 시험 **581개**가 건너뛴 시험 없이 통과했다. Manifest 누락·잘못된 레이어
DLL/GPU·잘못된 CLI의 **8개 통제 프로세스**는 결과 없이 실패했다. Probe 1개를 포함해
총 **33 commands**다.

## 예약량 관측과 결정

이번 두 GPU의 native class 결과는 같았다. Mixed/held는 stride 64/128/256/512/1024/
2048/4096, sparse는 요청한 64/256/1024/4096 class를 실제로 사용했다.

| 후보의 해당 checkpoint | buffers | native allocations | native 예약 bytes | live 요구 bytes | 예약/요구 |
|---|---:|---:|---:|---:|---:|
| mixed round 14, sparse | 24 | 13 | 141,696 | 24,064 | 약 5.89배 |
| sparse, block당 한 개 | 20 | 20 | 168,640 | 27,200 | **6.2배** |
| held round 0, 완료 후 replacement | 72 | 23 | 331,392 | 296,064 | 약 1.12배 |

Native allocation을 공유해도 각 block에 한 개만 남으면 예약 bytes는 유지된다.
모든 lease를 해제하면 native live=0, 이어 새 4KiB 요청은 4KiB 한 slot으로 다시 시작했다.
따라서 관측한 잔존 예약은 이 정책의 내부 빈 공간이며 누수로 분류하지 않는다. 동시에
이 결과가 모든 희소 점유에 효율적이라는 근거도 되지 않는다.

Sparse는 **native block당 한 개**를 남기는 소유권 시험이므로 dedicated 제품은
124개, 후보는 20개 논리 buffer를 남긴다. 서로 다른 생존 집합의 전체 할당 수·bytes를
공정한 성능 비교나 같은 메모리 작업량의 우위로 사용하지 않는다. Mixed/held의 논리
입력 순서는 제품·후보가 같지만 이 실행에는 trace/metadata/model 검사가 들어간다.
새 지연 측정이나 이전 timing의 새 fingerprint 재귀속은 하지 않는다.

후보는 혼합 크기·보유·rollback의 이번 bounded 정확성 계약을 통과했다. **기본값으로
승격하지 않는다.** 새 fingerprint의 전체 allocation-OOM/fallback 재시험, 예약량 상한
정책의 선택과 맞춘 성능 비교가 남아 있다. 이전 격리 fault study를 이 후보의 새 실행으로
표기하지 않는다. 장시간 메모리 기준과 과거 Intel AV 원인 조사는 계속 보류한다.

## 재현과 증거

- [SDK 빌드·55 CTests](../benchmarks/reports/validation/2026-10-24-vulkan-pool-churn/verification-scripts/build-sdk.py),
  [최종 소비자 빌드](../benchmarks/reports/validation/2026-10-24-vulkan-pool-churn/verification-scripts/build-consumers-v2.py).
  새 source/build/install root, MSVC Release `--parallel 1`을 사용했다.
- [Runner](../tools/run_vulkan_pool_churn.py), [원시 결과](../benchmarks/results/2026-10-24-vulkan-pool-churn/results.json),
  [실행 명령](../benchmarks/results/2026-10-24-vulkan-pool-churn/commands.json),
  [요약](../benchmarks/reports/2026-10-24-vulkan-pool-churn/summary.json).
  VVL/레이어 설정은 정리된 child 환경에만 적용하고 전역 설정은 바꾸지 않았다.
- [의존성 감사](../licenses/generated/windows-vulkan-pool-churn-release/inventory.json):
  **996 conservative inputs / 21 components**, 새 SDK·소비자의 compiler read **34** /
  linker read **33** logs. 고정 Vulkan-Headers와 기존 own layer를 사용하며 새 third-party
  의존성·vendor 수정·바이너리 재배포는 없다. 법률 승인이나 symbol 도달성의 증명이 아니다.
- [Evidence manifest](../benchmarks/reports/2026-10-24-vulkan-pool-churn/evidence.json)는
  새 own-source ZIP, candidate snapshot/receipt, 원시 결과·빌드 로그·source patch,
  이전 증거와 23개 기존 배포 ZIP의 hash를 보존한다. Vendor trees, EXE/DLL/LIB/PDB,
  dump/ETL/VVL runtime은 source ZIP에 넣지 않는다.
- 소비자 v1 빌드와 예비 실행도 보존했다. 최종 소비자 v2는 checkpoint에 논리 payload
  합계까지 기록해 readback 작업량을 독립 검산한다. 정상 원본을 먼저 승인한 뒤 변조한
  보고서 통제 12개와 감사 통제 2개를 별도로 확인했다.
