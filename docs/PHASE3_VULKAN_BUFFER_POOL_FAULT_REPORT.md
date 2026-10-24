# Phase 3 — Vulkan 버퍼 할당 실패 주입·소유권 수정

2026-10-24 Windows x64 / MSVC 19.44. 다음 단계인 적응형 소형 버퍼 후보의 OOM·bind
failure 경로를 검사하고 **실패 출력 핸들의 잘못된 소유권 획득을 재현·수정**했다.
수정은 새 격리 후보의 buffer 할당 경로에 한정된다. 제품 원본·public ABI·vendor
소스와 과거 고정/적응형 후보·결과는 변경하지 않았으며 제품 채택은 보류한다.

## 재현한 결함과 수정

적응형 후보의 최종 exact-size `AllocateMemory` 호출은 결과를 확인하기 전에
`Allocation::memory`로 출력받았다. 오류 출력에 non-null 값이 남으면 실패 반환 중
RAII 정리가 그 값을 실제 메모리처럼 free하려 했다. Pool block 쪽은 이미 오류 시
출력을 지우지만, 최종 fallback·dedicated·host buffer 쪽에는 같은 방어가 없었다.
[Vulkan 오류 계약](https://docs.vulkan.org/spec/latest/chapters/fundamentals.html#fundamentals-errorcodes)은
별도 규정이 없는 오류 반환의 출력값을 보장하지 않는다. 기존 객체는 OOM 뒤에도
사용할 수 있으므로 기존 데이터와 정상 재시도도 확인해야 한다.

새 후보는 지역 임시 핸들로 출력받고 `VK_SUCCESS`를 확인한 뒤에만 소유 객체에
옮긴다. 실패한 출력은 소유하지 않으므로 destructor가 잘못된 handle을 free하지 않는다.
수정 diff는 [candidate.patch](../experiments/vulkan_buffer_pool_fault/candidate.patch)이며,
[복사본 생성 도구](../tools/prepare_vulkan_buffer_pool_fault.py)는 이전 snapshot의
모든 파일 SHA와 SDK fingerprint를 확인하고 새 `out/` 경로에만 복사·수정한다.
성장 정책·slot lease·공유 block 통계는 바꾸지 않았다.

**제품 원본에는 여전히 직접 출력받는 생성 경로가 있다.** 이번 결과를 제품 전체
Vulkan 생성 API의 오류 출력 소유권 수정·승인으로 해석하면 안 된다. 해당 경로들의
감사와 후속 제품 수정은 별도 항목으로 남긴다.

## 안전한 합성 오류 관측

[own layer/client/plan](../experiments/vulkan_buffer_pool_fault/README.md)을 새로 추가했다.
기존 own native trace 코드를 별도 파일로 파생했으며 vendor나 이전 진단 코드는 수정하지 않았다.
자식 process별 explicit layer에서 최대 allocation 오류 2개와 bind 오류 1개를 arm한다.
소비하지 않은 plan을 덮어쓰거나 실패 단계 밖에서 arm할 수 없다. 실제 driver 자원을
고갈시키거나 device loss/TDR를 유도하지 않는다. 전역 layer·system 설정도 바꾸지 않는다.

Allocation 오류는 일부러 non-null 가짜 핸들을 출력한다. 그 가짜 핸들의 free는 own
layer에서 검출하고 **VVL/driver에 전달하지 않는다**. 이 guard가 수정 전 결함을
안전하게 관측하는 장치이며 제품의 복구 로직은 아니다. 반환 오류에 대해서만 SDK
행동을 검사하고 실제 GPU 고갈·driver 장애나 운영체제의 메모리 회수를 재현하지 않는다.

각 성공 buffer의 memory type·정렬·범위·현재 생존 binding의 비중첩을 확인한다.
Bound buffer가 있는 memory를 free하거나 모르는 buffer/memory를 정리하는 경우도
검출한다. Handle wrapping은 하지 않는다. 모든 정상이던 실제 객체의 Vulkan 호출은
원래 chain으로 전달한다. 실제 VVL을 아래 chain에 로드하고 SDK synchronization
validation과 pinned DLL 경로를 확인한다.

## 15개 실패·복구 시나리오

오류 값 -1은 host OOM, -2는 device OOM, -13은 unknown error다. `control`은 오류
없는 기준이며 pool-host/device는 큰 block OOM 뒤 exact-size fallback 성공이다.
double/mixed는 pool와 fallback의 연속 실패, pool-unknown은 OOM 외 오류 전파다.
Bind 시험은 빈 class·새 성장 block·기존 free slot·OOM fallback allocation을 구분한다.
dedicated/Upload/Readback OOM 및 ended recording이 보유한 slot의 보호도 포함한다.

| Case | 기존 target 수 | Allocation 오류 | Bind 오류 | 대상 bytes |
|---|---:|---|---|---:|
| control | 0 | 없음 | 없음 | 4096 |
| pool-host | 1 | -1 | 없음 | 4096 |
| pool-device | 1 | -2 | 없음 | 4096 |
| double-host | 1 | -1,-1 | 없음 | 4096 |
| double-device | 1 | -2,-2 | 없음 | 4096 |
| mixed-oom | 1 | -1,-2 | 없음 | 4096 |
| pool-unknown | 1 | -13 | 없음 | 4096 |
| bind-fresh | 0 | 없음 | -2 | 4096 |
| bind-grow | 1 | 없음 | -1 | 4096 |
| bind-reuse | 2 | 없음 | -2 | 4096 |
| fallback-bind | 1 | -2 | -1 | 4096 |
| dedicated-oom | 1 | -2 | 없음 | 8192 |
| upload-oom | 1 | -1 | 없음 | 4096 host |
| readback-oom | 1 | -2 | 없음 | 4096 host |
| held-bind | 2 (recording 보유) | 없음 | -2 | 4096 |

각 process는 같은 device에서 **3회** 준비→실패→재시도/GPU 확인→해제→idle을
반복한 뒤 device teardown을 별도 기록한다. 오류 반환 code/SDK ErrorCode, 사용된
오류 plan, allocation 요청 bytes와 bind offset, 실패 직후 자원 통계 보존을 검사한다.
실패한 buffer는 파괴하고 새 block은 비우면 free하며 기존 block의 실패 slot은 반환한다.
재시도가 추가 block 없이 같은 free slot을 사용하는지는 native call 개수로 검사한다.

Guard buffer에는 반복·자원별 payload A를 먼저 써 두고 실패 뒤 덮어쓰지 않고 읽는다.
새 target에는 다른 payload B를 쓰고 전체를 읽는다. `held-bind`는 guard wrapper를
해제한 뒤 ended recording이 native slot을 보유한 상태에서 다른 slot의 bind를 실패시킨다.
재시도와 새 target 기록 뒤 보류 recording을 제출해 원래 A가 남았는지 확인한다.
구간별 객체/bytes conservation과 마지막 native live=0을 검사하며, device destruction이
대신 청소한 자원/bytes는 0이어야 한다. Public 통계만으로 native 정리를 추정하지 않는다.

## 실행 결과

NVIDIA GeForce GTX 1660 SUPER / Intel UHD Graphics 770 × validation OFF/ON × 15 cases의
**60개 정상 process / 180회 cycle**이 통과했다. 합성 오류 **216회**, full GPU payload
**540회 / 2,260,992 bytes**를 확인했다.
잘못된 free, bind/ownership guard, accounting, dropped event, 암묵적 device cleanup,
미소비 plan은 모두 0이다. 정상 case 안의 실제 driver 실패는 허용하지 않는다.

동일한 새 시험 실행 파일을 **수정 전** frozen adaptive 설치본에 링크한 비교도 했다.
double-device/dedicated-oom × 두 GPU × validation OFF/ON의 **8개 process / 24회 cycle**은
각 cycle마다 잘못된 free 1회를 검출했다(총 **24회**). 이 process는 예상 exit 2를
반환하며 가짜 free를 driver 앞에서 차단한다. 수정 후 같은 경우는 0회다.
누락 fault layer·다른 GPU·잘못된 CLI **8개**는 결과 없이 실패했다.
총 native 명령은 probe 1 + 정상 60 + guarded regression 8 + fail-closed 8 = **77개**다.

새 후보 SDK를 D3D12+Vulkan으로 MSVC `--parallel 1` 빌드했고, 처음부터
`-E '^memory_reclamation_'`로 보류된 3개를 제외했다. SDK **57/57 CTests**가 통과했다.
이는 기존 D3D12 hardware/GBV/WARP와 Vulkan 양 GPU 기능·수명/validation 및 적응형
range/GPU 검사다. 새 소비자 두 variant의 plan CTest **2/2**, 각 **914 CPU assertions**,
Python **506/506**(신규 28)이 통과했다. Report 변조 controls **10개**와 audit controls
**2개**도 거부를 확인했다.

초기 실행은 Python Windows path 정렬과 CMake의 문자열 정렬 차이로 source hash가
불일치해 중단됐다. SDK 실행 자체의 오류가 아니며 정렬을 맞춘 뒤 새 `-v2` 결과 root에서
전체 matrix를 통과했다. 최초 raw 출력과 실패 로그도 보존했다.

## 채택 범위와 재현

이번 수정 후보의 source fingerprint는 `3b0f54c6caf225a0d9ed3d43f00543e3f29579d5f72e00e2c481aeadbeb94808`이며, 비교한
적응형 후보는 `4f4e63639037f18613227e71cc5635956fa3a2b095807621aadb6fcba6aecb43`, 시험 layer/client는
`a1c4c1afeac077217593c78de5942d0922f4e28f025a7bd97cf4d879aa351db1`다. 제품 원본은 `44b92cdf3a4ae01c97af10fe25d57b6dea487bf6ed7826a7f322e338a1d39e25`로 유지했다.

**새 성능 측정은 하지 않았다.** 이전 후보의 성능값을 이 수정 후보의 실측값으로
옮기지 않는다. 앞선 단일 Intel 생성 회귀, 혼합 size/점유율/churn 성능·정책 검증,
제품 생성 경로 전체 소유권 감사는 여전히 남아 있다. Device loss 조합·실제 heap 고갈,
일반 다중 thread allocator나 모든 Vulkan 생성 API를 완료한 구현이라는 주장도 하지 않는다.
장시간 메모리 기준과 과거 Intel AV 원인 조사는 보류를 유지한다.

[Dependency inventory](../licenses/generated/windows-vulkan-buffer-pool-fault-release/inventory.json)는
**886 conservative inputs / 21 components**, 새 compiler read 35 / linker read 34개다.
실제 새 SDK·수정 전/후 소비자 입력과 이전 SDK/SDL closure를 확인했다. 새 third-party
dependency나 vendor 수정은 없고 symbol reachability/법적 승인 주장은 아니다.
이전 증거와 SDK package 23개를 SHA로 보존했다. Source ZIP에는 own sources/fixtures,
실험 코드·patch·tools·문서·audit가 포함되고 vendor trees·binaries·dump·VVL runtime은 없다.

[Build](../benchmarks/reports/validation/2026-10-24-phase3-vulkan-buffer-pool-fault/verification-scripts/build.py),
[runner](../tools/run_vulkan_buffer_pool_fault.py),
[원본](../benchmarks/results/2026-10-24-vulkan-buffer-pool-fault-v2/results.json),
[summary](../benchmarks/reports/2026-10-24-phase3-vulkan-buffer-pool-fault/summary.json),
[case CSV](../benchmarks/reports/2026-10-24-phase3-vulkan-buffer-pool-fault/cases.csv),
[evidence](../benchmarks/reports/2026-10-24-phase3-vulkan-buffer-pool-fault/evidence.json),
[verifier](../benchmarks/reports/validation/2026-10-24-phase3-vulkan-buffer-pool-fault/verification-scripts/verify.py)를 따른다.
프로파일 경로는 현재 snapshot에 고정돼 있으므로 재실행은 새 source/build/install/result
root에서 runner/audit 경로를 함께 맞춘다. Linux, 더 넓은 경쟁 SDK와 Phase 3 전체 완료는 별도다.
