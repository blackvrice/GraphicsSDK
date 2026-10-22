# Phase 2 공개 OS 메모리 예산 조회

2026-10-22. `Device::queryMemoryBudget(MemorySegment)`를 구현했다. 앱은 선택한 GPU의
동적인 process 사용량·OS 예산·예약 정보를 SDK API로 조회할 수 있다. **기능 검증은 통과했으며
원래 장시간 GBV 메모리 gate와 이전 Intel AV 원인은 계속 미해결이다.**

새 SDK fingerprint는
`9f3235c37c9a907b1156fc5d3577f6d5e867529a78d1147800c96b1b3e00c7f3`이다.

## API와 backend 계약

공개 헤더는 표준 C++ 타입만 사용한다. `MemorySegment::Local`/`NonLocal`을 각각 조회하며
`MemoryBudget`은 실제 `adapterId`, 요청 `segment`와 `budgetBytes`, `usageBytes`,
`availableForReservationBytes`, `reservationBytes`를 반환한다. D3D12 구현은 native device의
LUID로 [동일 adapter](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_4/nf-dxgi1_4-idxgifactory4-enumadapterbyluid)를
찾고 node 0에서 [QueryVideoMemoryInfo](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_4/nf-dxgi1_4-idxgiadapter3-queryvideomemoryinfo)를
호출한다. 값은 clamp·차감·0 합성 없이 native 네 필드에서 매핑한다.

OS process/adapter 관측에는 다른 device/native owner의 비용도 포함될 수 있다. SDK 고유
resource allocation 합, 정적 adapter capacity, process private commit, 총량/free VRAM 또는
연속 peak와 다르다. usage가 budget보다 클 수 있다. available-for-reservation을 free memory로
계산하지 않고, 조회가 예약량·GPU 자원·residency 정책을 바꾸지도 않는다. 두 segment는 다른
시점의 관측이며 OS budget은 즉시 바뀔 수 있다. 자세한 필드는
[native 구조 계약](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_4/ns-dxgi1_4-dxgi_query_video_memory_info)을 따른다.

creating thread 전용이고 GPU wait/fence signal이나 SDK 회수 traversal을 실행하지 않는다.
건강한 장치의 잘못된 segment는 InvalidArgument, 다른 thread는 InvalidState다. 기존 상태
guard가 먼저 실행돼 손실·퇴역 세대는 native 참조에 접근하기 전에 저장된 DeviceLost 원인을
반환한다. 같은 ID로 복구한 새 device는 다시 조회할 수 있다. 미지원 interface/query는
Unsupported/native code, 그 밖의 실패는 기존 오류/loss 경로로 보존한다.

## 실제 검증

새 C++ 테스트는 gpu와 full-GBV mode의 별도 process에서 NVIDIA GTX 1660 SUPER,
Intel UHD 770, WARP를 명시적으로 선택한다. adapter ID는 이번 세션에서 각각
96576 / 106703 / 106596이다. 독립 DXGI adapter도 요청 LUID로 찾는다.

native-before → SDK → native-after의 네 필드가 안정된 구간에서만 직접 매핑을 비교한다.
관측값이 움직이면 최대 20회 재시도하고 안정된 bracket이 없으면 실패한다. 임의 오차를
붙이거나 실패한 값을 0으로 바꾸지 않는다. baseline/held/released/recovered × 두 segment ×
세 adapter × 두 mode의 **48개 snapshot**이 일치했다. WARP와 Intel의 성공한 nonlocal 0도
native 0과 일치했다. 이 호스트에서 query 미지원 오류가 실제로 발생했다는 주장은 하지 않는다.

각 adapter에서는 공개 SDK로 64MiB upload→device copy를 완료하고 그중 64KiB를 readback해
모든 byte를 비교했다. native command/upload/readback을 해제한 뒤 device buffer 하나만
보유하고 SDK allocation 통계가 조회 전후 바뀌지 않는지도 확인했다. 두 hardware의 local
usage 보유/해제 차이가 각각 최소 32MiB인 양성 대조를 두 mode에서 **4회** 통과했다.
WARP에는 물리 GPU residency 차이를 요구하지 않았다. 전체 확인한 readback은 **393,216 bytes**다.
이는 체크포인트 대조이며 전체 64MiB payload reference나 연속 peak 측정이 아니다.

wrong-thread, negative/unknown enum, 건강한 장치 유지, opt-in native RemoveDevice 뒤 두
segment 및 invalid enum의 동일 DeviceLost 원인, native 퇴역 뒤 반복 거부, 정확한 ID의 새
세대 조회와 healthy GBV 오류 0을 확인했다. query가 resource 보유를 바꾸지 않는 것도 검사했다.
native removal은 자기 process 장치의 통제된 손실이며 실제 driver TDR/hot-unplug가 아니다.

설치 public SDK만 쓰는 `budget_consumer`는 native header/handle 없이 normal/validation
두 mode에서 **24개 snapshot / 6회 같은-ID 복구**를 확인했다. 예약량의 nonzero 변경이나 실제
OS over-budget 상태를 강제하는 시험은 하지 않았다. observed reservation 0도 그대로 비교했다.

## 회귀·패키지·남은 범위

새 API는 개발 버전 virtual interface를 확장하므로 SDK와 모든 관련 소비자를 재빌드했다.
Core **28/28**, Windows D3D12-OFF **6/6**, 공통 correctness **72/72**, presentation **7/7**,
Python **147/147**이 통과했다. 새 core tests는 CPU 1개와 native mode 2개다. 이번 전체
직렬 build/검증에는 compiler retry가 필요하지 않았다. 이를 일반적인 compiler 안정성의
증명으로 표현하지는 않는다.

기존 설치 main/recovery/adapter 소비자, 창 예제·pacing·기본/명시 adapter campaign도 새 SDK로
검증했다. 두 window campaign은 별도로 총 **441개 full images / 67,393,539 RGBA pixels**를
비교했다. 등록 CTests의 이미지 수와 합쳐 중복 집계하지 않는다. stress/lifetime/runtime와
symbol-bearing 진단 도구도 새 SDK에 맞춰 재빌드했으며 과거 장시간 결과를 새 SDK의 실행으로
재분류하지 않았다. 실제 monitor/DPI/예상치 못한 손실 수용과 이전 Intel AV 원인은 남아 있다.

새 로컬 SDK ZIP은 `out/GraphicsSDK-0.0.1-dev-windows-x64-memory-budget.zip`이다.
SDK 19/1, 공통 977/22, budget validation 39/1, stress 25/1, crash 27/1의 새 inventory와
다섯 `--check`를 통과했다. budget profile은 6개 actual linker closure와 설치 header/library
byte 일치를 확인한다. 이전 SDK ZIP **7개**, 기존 raw/report/inventory/source archives는
그대로 보존한다. 기존 live `out/` 실행 파일은 새 SDK로 재빌드했음을 구분한다.
외부 dependency/runtime 배포나 Sandbox/competitor 변경은 없다.

[공개 계약·예제](PUBLIC_API.md#os-메모리-사용량예산-snapshot),
[실행 방법](BUILD.md), [최종 원본](../benchmarks/reports/validation/2026-10-22-phase2-memory-budget-final/),
[초기 native 대조](../benchmarks/reports/validation/2026-10-22-phase2-memory-budget-pilot/),
[요약·해시·source archive](../benchmarks/reports/2026-10-22-phase2-memory-budget/)를 함께 확인한다.
장시간 GBV gate, 실제 TDR/hot-unplug/육안 표시, native Vulkan/Linux/Metal/macOS와 Phase 2
전체 완료를 이번 API 기능 검증으로 수용하지 않는다. 새 성능 우위 주장도 없다.
