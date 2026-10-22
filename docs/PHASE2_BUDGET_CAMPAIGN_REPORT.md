# Phase 2 메모리 예산 API의 resource-churn campaign 연결

2026-10-22. 스트레스 도구의 각 체크포인트에서 공개 `Device::queryMemoryBudget`을
독립 DXGI 관측과 함께 기록하고 검증하도록 연결했다. **이 작업은 도구 연결과 짧은 기능
검증이며, 원래 GBV 장시간 메모리 gate를 통과한 결과는 아니다.** 공개 SDK·library와
메모리 policy는 그대로이며 SDK fingerprint는
`9f3235c37c9a907b1156fc5d3577f6d5e867529a78d1147800c96b1b3e00c7f3`이다.

## 기록과 판정 계약

`MemoryStress.cpp`의 schema 3는 기존 process private/working-set/handle 및 native
local/nonlocal usage/budget을 유지한다. 각 segment에서 native-before → SDK → native-after
순서로 조회하고 usage, budget, available-for-reservation, current-reservation 네 필드를
그대로 기록한다. SDK 결과의 실제 adapter ID와 segment도 요청과 일치해야 한다.
native-only·bounded-reuse 진단은 기존 overload와 schema를 유지한다.

검증기는 native 양 끝의 네 값이 모두 같을 때 SDK와 네 필드가 정확히 같은지 검사한다.
달라지는 구간은 `unstable_native_brackets`로 구분하며 정확한 일치를 주장하지 않는다.
성공한 segment에는 최소 한 번의 안정된 비교가 있어야 한다. 시간차가 있는 관측이므로
세 호출의 원자성이나 호출 사이 모든 변화를 보장하지 않는다. 재시도나 임의 오차 허용은 없다.

SDK counter는 bool/float/음수/uint64 overflow/누락을 거부한다. 성공한 0과 usage > budget은
유효하다. `Unsupported`는 원래 실패 코드와 네 필드의 명시적 null을 남긴다. 이 campaign의
unavailable 판정은 기존 hardware 필수 관측 조건을 유지하고 WARP의 일치하는 native 실패만
허용한다. 지원 상태가 중간에 바뀌거나 실패를 0으로 바꾸면 거부한다. 다른 SDK 오류는 native
process 실패로 남으며 완전한 성공 footer를 만들지 않는다.

hardware는 SDK local usage로도 64MiB 보유/해제 양성 대조를 확인한다. 최소 증가·감소량은
기존 policy의 native 기준 32MiB를 그대로 사용한다. 새 API 관측이 SDK resource ownership과
OS process accounting의 의미를 합치거나 연속 peak 측정이 되는 것은 아니다.

runner는 짧은 진단과 guard-aborted 실행에도 SDK 기록을 검사한다. 현재 실행에는 schema 3를
요구하므로 SDK fingerprint만 같은 과거 stress executable은 재빌드해야 한다. 역사적 schema
1/2 분석은 유지하며 `public_memory_budget.recorded=false`로 공개 API 증거가 없음을 명시한다.
기존 native plateau metric·900/300/300초·30초 warmup·증가 한도 및 실패 처리 기준은 바꾸지
않았다. 추가 조회 비용이 있으므로 과거 실행과의 성능 비교나 동일 계측 비용을 주장하지 않는다.

## 실제 실행과 회귀

새로 열거한 NVIDIA GTX 1660 SUPER / Intel UHD 770 / Microsoft WARP를 명시적으로 선택했다.
이번 OS 세션 ID는 96650 / 106766 / 106679이며 영구 식별자로 취급하지 않는다.
hardware 각각 일반/전체 GBV와 WARP의 총 다섯 3초 진단에서 **2,536 frames / 7,608 전체
이미지 / 664,797,184 buffer bytes**를 참조와 비교했다. **146개 SDK segment snapshot**의 네
필드가 안정된 native 양 끝과 모두 일치했다. 모든 실행의 validation error는 0이며 짧은 진단의
`accepted=false`를 유지했다. 이 호스트에서는 unsupported나 변동 bracket이 발생하지 않았고,
지원 실패·변동 bracket·over-budget 분기는 합성 회귀로 검증했다.

1MiB 증가 한도 대조는 NVIDIA 전체 GBV의 첫 8-frame 회차 뒤 native exit 2로 중단됐다.
14개 SDK segment snapshot, guard/final 기록, SDK 보유 다섯 값 0, 24개 전체 이미지와
`aborted/private_growth_limit` footer를 보존했다. runner exit 1과 `accepted=false`도 확인했다.
이 낮은 한도는 초기 shader/driver/검증 비용을 포함하므로 장시간 누수량 측정으로 해석하지 않는다.

별도 심볼 포함 GPU 진단은 debugger 아래 312 frames / 936 전체 이미지와 18개 SDK snapshot을
검증하고 정상 종료했다. second-chance 예외는 없었다. 이는 schema 3와 기존 수집기의 호환성
검사이며 이전 Intel 액세스 위반의 원인을 해결하거나 장시간 검증을 대신하지 않는다.
공유 helper의 native CPU 1초 및 public SDK 2-generation/32-frame lifetime 실행도 통과했다.

Core CTest **28/28**, Python **158/158**가 통과했다. 새 11개 회귀는 네 필드 mapping,
누락·identity·segment·오류·범위, 변동 bracket, 0/over-budget, unavailable null/code, SDK 양성
대조, guard와 schema 2 executable 거부를 검사한다. 합성 GPU 자료는 실제 측정과 구분한다.
빌드 retry는 없었다. 이번 작업에서 common 72/72·창 7/7 전체 실행을 다시 수행한 것은 아니며,
기존 inventory/package 해시 일치만 확인했다.

## 배포와 근거

새 [stress 25/1 inventory](../licenses/generated/windows-sdk-budget-campaign-release/inventory.json)와
[crash 27/1 inventory](../licenses/generated/windows-sdk-budget-campaign-crash-release/inventory.json)는
각각 다섯/여덟 executable의 실제 linker closure를 검사한다. 두 create/check와 기존 SDK·공통·
budget 검증 inventory/package의 check가 통과했다. 추가 외부 dependency는 없으며 기존 SDK
public source/library·설치본과 8개 SDK ZIP·역사적 raw evidence·inventory를 보존했다.
새 SDK binary ZIP은 만들지 않는다. 새 source archive에는 자체 소스와 설정·문서만 포함한다.
실행 파일·library·PDB·minidump·외부 runtime binary는 추가하지 않는다.

[사용 명령](../tools/memory_validation_probe/README.md#exact-adapter-resource-churn-campaign),
[직렬 검증 원본](../benchmarks/reports/validation/2026-10-22-phase2-budget-campaign/),
[요약·해시·source archive](../benchmarks/reports/2026-10-22-phase2-budget-campaign/)를 함께 보존한다.
원래 장시간 GBV 메모리 gate, 이전 Intel crash 원인, 육안 표시/실제 DPI/예상치 못한 TDR·
hot-unplug와 Vulkan/Linux/Metal/macOS는 남아 있다. Phase 2 완료나 성능 우위를 주장하지 않는다.
