# Phase 2 복사·메모리 전환 거부의 기록 원자성

2026-10-22. 복사/전환 요청의 validation이 실패했는데도 native 참조와 observed version을
기록하던 결함을 수정했다. **SDK 기록·자원 수명의 수정이며 장시간 GBV 메모리 plateau의
합격이나 원인 해결을 의미하지 않는다.** 새 SDK fingerprint는
`50fe75fea32a5444531868da9f1a225a05573d78ba12c52bb622a1ea80d35edf`다.

## 수정 전후 재현

기존 설치 SDK `165288f2cd9e3f820d5e08333ea0d129533a5fcab201490dee41b1df1c8be8fa`의
WARP에서 Upload → Common 상태의 Device buffer 복사를 요청하면 InvalidState를 반환한다.
그 뒤 Upload를 write하고 두 caller wrapper를 해제해도 명령 목록에는 두 resource가 남았다.
`uniqueResources=3`은 두 인자와 명령 목록의 timestamp readback이다. GPU 복사는 기록되지
않았지만 Upload version 변경이 무관한 빈 submit까지 InvalidState로 실패시켰다.
최소 소비자는 `rejected_copy_poisoned_submit=1`, `retained_resources_after_drop=3`, exit 2였다.

같은 소비자를 새 설치 SDK에 재링크하면 `rejected_copy_poisoned_submit=0`, 보유 resource는
명령 목록 자체의 timestamp readback **1개**, exit 0이다. 빈 명령 제출과 fence 완료가 성공한다.
수정 전의 실패한 요청을 잘못된 native GPU 복사로 제출하지 않았으며 원본과 source를 보존했다.

## 변경과 계약

기존 copy 구현은 source/destination의 상태를 `use()`로 읽었다. 이 함수는 새 자원 참조와
version snapshot을 추가하므로, source가 정상이어도 destination 상태가 잘못되면 인자에 대한
보유가 먼저 생겼다. Upload/Readback의 허용하지 않는 transition도 `change(use(...))` 안에서
거부해 같은 문제가 있었다.

이제 세 copy API는 메모리/범위 및 양쪽 상태를 읽기 전용 `current()`로 모두 검증한 뒤
`use()`를 호출한다. `current()`는 앞서 같은 명령 목록에서 수락한 shadow state를 반영한다.
Upload/Readback heap의 transition 제약도 `use()` 전에 확인한다. 기존 오류 code와 상태
조건을 유지하며 validation 거부가 새 참조·snapshot·barrier/copy를 남기지 않도록 했다.
공개 virtual ABI나 native 접근 API는 추가하지 않았다.

이미 정상 기록에서 사용한 자원과 version은 보존한다. 따라서 거부된 인자의 변경은 무관한
명령을 실패시키지 않지만, 정상 수락된 transition/copy 뒤 Upload를 변경하면 기존 stale-submit
검사가 계속 거부한다. validation 거부의 원자성을 다룬 작업이며, allocation 예외/OOM 복구를
새로 구현하거나 모든 실패에 대한 transaction 계약을 추가한 것은 아니다.

## 실제 검사

NVIDIA GTX 1660 SUPER / Intel UHD 770 / WARP를 정확한 ID로 선택해 일반/전체 GBV에서
실행했다. 두 heap transition과 세 copy API의 source/destination 상태 거부, 총 여덟 case를
각각 64회 반복했다. 총 **48 case 실행과 3,072회 반복 validation 거부**다.

Upload 인자는 거부 뒤 반복 write하고, Texture source는 별도 정상 명령으로 transition해
version을 바꾼다. caller wrapper 해제 뒤 보유 resource는 timestamp readback 1개여야 하며,
빈 명령 submit/wait가 성공하고 명령 해제 뒤 모든 SDK 소유 통계가 0이어야 한다.
이는 개별 SDK 소유 수명 대조이며 process/driver/kernel 메모리 회수량과 다르다.

정상 기록과 섞은 대조는 4KiB Upload → Device → Readback 전체 payload를 확인한다.
RGBA8/RGBA16F 각각 63×7 texture의 padded footprint로 업로드·readback하고 유효 row의
모든 byte를 비교한다. 거부된 destination/unused Upload는 보유하지 않아야 하며 수락한
resource는 wrapper 해제 뒤에도 native 제출 소유권으로 유지한다. 원래 global state와 다른
shadow state를 사용하므로 `current()`가 수락 기록을 반영하는지도 실제 성공/실패로 확인한다.

기존 accepted-use stale-submit 대조까지 합쳐 **3,114개 거부 검사**, **12개 전체 texture
이미지 / 5,292 pixels / 총 56,328 buffer·texture payload bytes**를 검증했다. row padding은
GPU가 보존해야 할 payload로 세지 않는다. 전체 GBV의 unexpected validation error는 0이다.

Core CTest **34/34**, Windows D3D12 OFF **6/6**, 공통 correctness **72/72**, 창 예제
**7/7**, Python **159/159** 및 기존 설치 소비자가 통과했다. 새 SDK의 기존 창/명시 adapter
campaign도 441개 전체 이미지와 67,393,539 pixels를 다시 검증했다. 원래 장시간 메모리
실패와 policy는 보존하며 이번 실행을 장시간/연속 peak/성능 비교로 표현하지 않는다.

## 배포와 근거

새 SDK ZIP은 `out/GraphicsSDK-0.0.1-dev-windows-x64-copy-atomicity.zip`이며 기존 10개 ZIP을
보존한다. 관련 library·benchmark·설치 소비자·진단/심볼 도구를 새 SDK로 재빌드했다.
새 [SDK 20/1](../licenses/generated/windows-sdk-copy-atomicity-release/inventory.json),
[공통 978/22](../licenses/generated/windows-benchmarks-copy-atomicity-release/inventory.json),
[copy/설치/창 43/1](../licenses/generated/windows-sdk-copy-atomicity-validation-release/inventory.json),
[stress 26/1](../licenses/generated/windows-sdk-copy-atomicity-stress-release/inventory.json),
[crash 28/1](../licenses/generated/windows-sdk-copy-atomicity-crash-release/inventory.json) 감사를 수행한다.
`sdk-copy`는 기존 swaps profile에 자체 `tests/CopyAtomicityTests.cpp`만 추가 허용하고 총
9개 executable linker closure를 검사한다. 외부 dependency/runtime를 추가하지 않고 자체
source ZIP에 실행 파일·library·PDB·dump를 포함하지 않는다.

[직렬 검증 원본](../benchmarks/reports/validation/2026-10-22-phase2-copy-atomicity/)과
[요약·해시·source archive](../benchmarks/reports/2026-10-22-phase2-copy-atomicity/)를 보존한다.
원래 GBV 장시간 메모리 gate, Intel access violation 원인, 육안 표시/실제 DPI/예기치 않은
TDR·hot-unplug 및 Vulkan/Linux/Metal/macOS 검증은 남아 있다.
