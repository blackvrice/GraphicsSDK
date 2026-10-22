# Phase 2 메모리 스트레스와 회수 검증

2026-10-22. 자원 보유 진단과 반복 스트레스 도구를 구현하고, 진단 메시지 유실로 오류 수가
잘못 0으로 보일 수 있던 SDK 집계를 수정했다. **장시간 메모리 acceptance는 미완료다.**
고정한 세 모드 중 hardware/WARP는 통과했으나 GBV의 메모리 증가가 기준을 초과했다.
검증 범위는 아래에 고정한 한 호스트의 process들이다. 시간 제한 없는 누수
부재, 모든 애플리케이션의 안정성 또는 SDK release 완료를 주장하지 않는다.

## 구현

`Device::resourceStatistics()`는 완료된 batch/만료된 weak 등록을 정리하고 살아 있는 객체,
고유 native resource, allocation bytes, 미완료 batch/COM 참조 수를 반환한다. buffer/texture,
녹화 중인 native command의 사용 자원/timestamp readback, wrapper가 사라진 제출 batch까지
합치고 resource 중복을 제거한다. draw hot path에 카운터 갱신을 추가하지 않는다.

allocation bytes는 adapter별
[GetResourceAllocationInfo](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-getresourceallocationinfo%28uint_uint_constd3d12_resource_desc%29)의
크기 합이며 실제 residency, descriptor heap/PSO 또는 driver cache를 포함하는 총 사용량이 아니다.
조회는 생성 thread에서만 가능하고, 손실·퇴역 세대에서는 저장된 원인의 DeviceLost를 반환한다.
자세한 필드 계약은 [공개 API](PUBLIC_API.md#자원-보유-진단)에 있다.

독립 도구 `tools/MemoryStress.cpp`는 매 회차 새로운 pipeline과 8개 frame을 제출한다. 각 frame은
64×64 sampled texture upload, 서로 다른 256-byte CBV snapshot 두 개, MRT 두 장과 D32 depth,
256KiB upload→device→readback copy, timestamp resolve를 실행한다. 제출 뒤 readback wrapper만
남기고 command 및 입력/output texture wrapper를 버린다. 마지막 fence 대기 뒤 모든 color/depth
pixel과 복사 buffer의 모든 byte를 CPU 기준과 비교한다. readback과 pipeline도 해제한 뒤
waitIdle/query를 수행하며 다섯 SDK 보유 값이 모두 0인지 **매 회차** 확인한다.

사전 집계 계약은 buffer alias 중복 제거, 64KiB 단위 committed allocation 합, 녹화된 command가
wrapper 파괴 뒤 보유하는 native resource/timestamp readback, fence 후 회수, weak registry의
600회 생성·해제 및 다른 thread의 거부를 검사한다. 별도 recovery test는 native 제거와 퇴역 후
진단 조회의 DeviceLost 및 새 세대의 빈 통계를 검사한다. 설치 consumer도 64MiB 보유·해제를 확인한다.

## 실행 전에 고정한 기준

[policy](../tools/memory_stress_policy.json)의 해시를 campaign 시작 시 저장한다. 실행 도중
SDK source fingerprint, 도구/분석기/policy source 및 exe/library 해시를 바꾸면 runner가 실패한다.

| 기준 | 값 |
|---|---|
| hardware | 30초 warmup + 900초 측정 |
| hardware debug + GPU-based validation | 30초 warmup + 300초 측정 |
| WARP | 30초 warmup + 300초 측정 |
| 측정 | 별도 process로 순차 실행, 5초 간격 live/drained 및 final |
| 최소 작업 | 측정 구간 초당 8 frame 이상, frame당 전체 이미지 3개/복사 256KiB |
| 회수 | 모든 회차/모든 drained checkpoint에서 SDK 다섯 값 0 |
| 정확성·검증 | 모든 reference 일치, debug+GBV ERROR/CORRUPTION 0 |
| CPU 양성 대조 | touched VirtualAlloc 32MiB, 보유/해제 private commit 차이 각각 최소 24MiB |
| GPU 양성 대조 | committed Device buffer 64MiB, SDK 자원 1개/최소 64MiB, 해제 후 0 |
| hardware GPU 관측 대조 | copy로 사용한 자원의 DXGI local usage 보유/해제 차이 각각 최소 32MiB |

각 측정 구간의 후반 50%를 plateau 판정에 사용한다. 해당 구간의 앞/뒤 25% median 차이,
전체 후반 구간의 최소제곱 직선 기울기와 max−min 범위를 모두 검사한다. 감소 추이는 허용한다.

| 관측값 | 허용 median 증가 | 허용 증가 기울기/분 | 허용 후반 범위 |
|---|---:|---:|---:|
| process private commit | 32MiB | 8MiB | 128MiB |
| handles | 16 | 4 | 64 |
| DXGI local / nonlocal 각각 | 16MiB | 4MiB | 128MiB |

이 기준은 warmup 후 회수 안정성에 대한 허용치다. driver/runtime cache가 process 초기값으로
돌아와야 한다고 가정하지 않는다. 초기 CPU/GPU 양성 대조를 plateau에 섞지 않는다.
최소 실행 시간/작업량/연속 checkpoint/전체 readback/footer/source fingerprint와 양성 대조를
통과해야 한다. 일반 CTest의 mode별 2초 실행은 이 gate를 통과할 수 없다.

## 관측 의미와 한계

OS observer는 process private commit/working set/handle 수와 node 0의 DXGI local/nonlocal
CurrentUsage/Budget을 기록한다. hardware adapter 이름의 일치가 유일한지 검사하고 LUID를
함께 저장한다. 이름이 중복되면 추측하지 않고 실패한다. WARP는 EnumWarpAdapter로 선택한다.

[PROCESS_MEMORY_COUNTERS_EX](https://learn.microsoft.com/en-us/windows/win32/api/psapi/ns-psapi-process_memory_counters_ex)의
PrivateUsage는 private committed memory이고 WorkingSetSize는 현재 working set이다.
[QueryVideoMemoryInfo](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_4/nf-dxgi1_4-idxgiadapter3-queryvideomemoryinfo)의
CurrentUsage는 process의 해당 segment 사용량이다. 이 보고서의 최대값은 **관측한 checkpoint의
최대값**이며 연속 peak가 아니다. Phase 1 ETW lifetime/peak 검증과 별개다.

실패한 DXGI query는 HRESULT와 null을 기록한다. hardware campaign은 관측 실패를 거부한다.
WARP의 미지원 항목은 명시적으로 미지원으로 유지하며 0으로 합성하지 않는다. 성공한 WARP
query의 0도 물리 VRAM 사용량을 의미하지 않는다. WARP에는 hardware residency 양성 대조를
적용하지 않지만 SDK allocation 대조와 CPU/SDK 회수 검사는 동일하게 적용한다.

## 결과

측정 시점 SDK fingerprint는 `ee53ebe89c94f62604280b5f8dc233451547fe070262c4640a55f2c91383917a`다.
오류 집계 수정 후 최종 SDK는 `833978d58dc9e18daf939d12ef24eb69ca965b323caab41fd30682131baa89f1`이다.
원래 측정과 수정 후 회귀/요인 검사의 source·binary를 별도 보존했다. 아래 25분 측정은 수정 후
SDK의 새 장시간 campaign으로 재분류하지 않는다. 현재 SDK에서는 별도 30초 요인 검사들을 재실행했다.

| 관측 mode | 측정 시간 | frame / 전체 이미지 | drained checkpoint | 메모리 기준 |
|---|---:|---:|---:|---|
| hardware | 900.042초 | 93,312 / 279,936 | 181 | 통과 |
| hardware debug+GBV | 300.064초 | 18,008 / 54,024 | 61 | **실패** |
| WARP | 300.030초 | 59,672 / 179,016 | 61 | 통과 |

합계 170,992 frames, 512,976 전체 이미지, buffer readback 44,824,526,848 bytes가 CPU 기준과
일치했다. 매 회차와 모든 drained checkpoint의 SDK 보유 다섯 값은 0이었다. hardware에서
완료되지 않은 batch가 최대 2개인 live snapshot도 관측하여 wrapper 없는 native 제출 보유를 확인했다.

| 후반 구간 관측 | hardware | GBV | WARP |
|---|---:|---:|---:|
| private commit median 증가 | 7.656MiB | 5,296.195MiB | 0.047MiB |
| private commit 증가 기울기/분 | 1.187MiB | 2,661.002MiB | -0.130MiB |
| nonlocal usage 증가 기울기/분 | 0.283MiB | 2,643.494MiB | 0MiB |
| handles 증가 기울기/분 | 0.314 | 6.019 | 0 |

GBV는 private commit, nonlocal usage, handles 기준을 실패했다. 마지막 private commit은
15,392,358,400 bytes(약 14.34GiB)였다. strict gate는 **exit 1**, 최종 판정은 **accepted=false**다.
원래 runner는 GBV 실패 시 중단했으며, 같은 고정 바이너리·source·policy의 WARP를 별도 process로
마저 수행했다. 기준을 완화하거나 실패를 통과로 바꾸지 않았다. hardware의 작은 양의 증가도
표에 그대로 남긴다. 이 제한된 구간의 허용 기준을 통과했다는 뜻이며 무제한 plateau 증명은 아니다.

- [원본 campaign](../benchmarks/reports/validation/2026-10-22-phase2-memory-stress/campaign/)
- [실패 수용 판정](../benchmarks/reports/2026-10-22-phase2-memory-stress/acceptance.json)
- [최종 요약·해시](../benchmarks/reports/2026-10-22-phase2-memory-stress/)

처음 짧은 도구 실행은 잘못 지정한 CBV binding-layout index 때문에 실패했다. SDK binding
검사가 요청을 거부한 결과이며 수정 전 출력을 exploration으로 보존한다. 인덱스를 고친 뒤 세
모드의 전체 reference/집계 사전 검사가 통과했고, 그 다음에 policy와 source를 고정해 본 측정을 시작했다.

## 발견한 집계 결함과 수정

처음 요인 검사에서 native queue가 1,024개 경고로 포화되고 수천 개의 메시지가 버려진 것을
확인했다. 기존 `validationErrorCount()`는 저장된 메시지만 훑었으므로 늦은 ERROR를 놓칠 수 있었다.
따라서 이전 5분 GBV 출력의 `validation_errors=0`은 전체 native 오류 범위의 증거로 쓰지 않는다.
이미지/버퍼 참조와 메모리 실패 관측은 그대로 유효하다. 저장 한도가 포화된 원래 요인 검사도
진단 범위가 불완전한 원본으로 보존하며, 전체 native 오류 0으로 승격하지 않는다.

SDK는 이제 ID3D12InfoQueue1의 unfiltered callback에서 ERROR/CORRUPTION을 원자적으로 누적한다.
native text storage를 지우거나 무제한으로 늘리지 않는다. callback 미지원 validation 장치는
Unsupported다. Microsoft의 [callback 계약](https://microsoft.github.io/DirectX-Specs/d3d/MessageCallback.html)에
따라 callback 안에서 D3D 호출을 하지 않으며, unregister가 진행 중 callback 종료를 보장한 뒤
State를 퇴역·파괴한다. 필터·텍스트 유실과 독립적인 관측이며 GPU 완료 뒤 누적 snapshot을 읽는다.

독립 native counter 검사는 한도를 8개로 낮춰 warning 64개로 포화시킨 뒤 intentional ERROR를
넣고, clear/filter/CORRUPTION/worker-thread/공유 SDK owner/다른 owner 파괴를 거쳐 오류 6개를
정확히 확인했다. 마지막 SDK 파괴 뒤 native owner가 메시지를 생성해도 dangling callback이 없다.
이는 정상 렌더링의 오류 0 검사와 다른 의도적 양성 대조다.

## 수정 후 요인 검사와 미해결 원인

현재 SDK로 아래 세 검사를 각각 새 process에서 30초 수행했다. 모든 전체 이미지/복사 참조가
일치했고, 수정된 unfiltered 오류 counter는 각각 0이다. native text가 포화돼도 callback 집계는
유지되며 위 intentional test로 실제 ERROR의 누락 방지를 확인했다.

| case | frames / 전체 이미지 | private 증가 | 마지막 SDK 보유 |
|---|---:|---:|---:|
| pipeline 재사용, command 신규 생성 | 2,968 / 8,904 | 2,418.484MiB | 0 |
| pipeline/8 command 재사용, resource 신규 생성 | 3,424 / 10,272 | 2,778.953MiB | 0 |
| 8 command 재사용, pipeline/resource 신규 생성 | 3,448 / 10,344 | 2,807.465MiB | 0 |

명령/PSO 재사용만으로 증가가 멈추지 않았고, native text queue clear 뒤에도 메모리는 회수되지
않았다. 마지막 [ReportLiveDeviceObjects + IGNORE_INTERNAL](https://learn.microsoft.com/en-us/windows/win32/api/d3d12sdklayers/ne-d3d12sdklayers-d3d12_rldo_flags)
보고에는 device/queue/fence만 남았으며 외부 참조가 있는 buffer/texture/PSO/command는 없었다.
이 관측은 앱이 보유하는 객체와 runtime 내부 보유를 구분할 뿐, 정확한 내부 할당 원인을 증명하지는
않는다. 재사용한 command에서도 자원 생성은 계속되므로 resource 신규 생성/GBV 추적 경로에 대한
추가 분리가 필요하다. 특정 driver/runtime 결함으로 단정하거나 SDK의 누수 부재를 선언하지 않는다.

[현재 원본 요인 검사](../benchmarks/reports/validation/2026-10-22-phase2-memory-stress/diagnostics-final/)와
[수정 전 원본/보존 입력](../benchmarks/reports/validation/2026-10-22-phase2-memory-stress/observed-inputs.json)을
함께 확인할 수 있다. 이 30초 진단을 5분/15분 acceptance로 사용하지 않는다.

최종 회귀 검증은 core CTest 22/22, Windows D3D12-OFF 4/4, 공통 benchmark correctness 72/72,
Python 72/72와 설치 consumer/복구 consumer hardware·WARP가 통과했다. 현재 core의 native
복구 12회 및 GBV 정상 구간은 수정된 callback 집계로 검사한다. 현재 세 요인 검사 외에 별도
counter 계약 1개가 통과했다. SDK ZIP과 세 감사 inventory는 최종 해시 증거에 함께 남긴다.

## 배포와 남은 범위

자체 도구 외 신규 외부 dependency나 competitor 변경은 없다. `sdk-stress` 감사 profile은
SDK와 `tools/MemoryStress.cpp`, `tools/memory_validation_probe/main.cpp`만 별도 compiler/linker closure에 추가한다. PSAPI/DXGI/D3D12는
Windows 시스템 전제조건이다. SDK/common/stress inventory와 새 SDK ZIP을 별도 경로에 생성하며
과거 원본·패키지·감사를 덮어쓰지 않는다. 새 진단 virtual API를 쓰는 consumer는 재빌드한다.

이 soak는 장치 손실을 주입하거나 swapchain을 생성하지 않는다. 정상 장치의 반복적 RHI 회수
gate이며 recovery는 별도 native RemoveDevice 회귀 검증이다. multi-hour/overnight soak,
interactive presentation, 예상치 못한 driver TDR/물리 hot-unplug, 다른 adapter/driver/OS,
native Linux/macOS/Vulkan/Metal 및 remote CI는 이번 실행 범위 밖이다. 새 성능 비교도 실행하지 않았다.
