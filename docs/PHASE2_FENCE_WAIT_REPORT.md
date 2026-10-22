# Phase 2 펜스 timeout 검증과 이벤트 재사용

2026-10-22. 공개 queue의 완료 여부와 무관한 timeout 검증, 이벤트 없는 0ms 조회,
반복 timeout에서의 native 등록 재사용을 구현했다. **실제 SDK 결함 수정이며, 별도 장시간
GBV texture-churn 메모리 gate를 해결하거나 Phase 2를 완료했다는 결과는 아니다.**
새 SDK fingerprint는
`07260dcfd152001342b801d0206bab594109b5d9046e58eff5694b4ba98a1fa6`이다.

## 수정 전 재현과 변경

기존 설치 SDK의 WARP에서 명령을 제출하고 완료한 뒤 `wait(fence, -1ms)`를 호출한 최소
소비자는 `completed_negative_timeout_accepted=1`, exit 2를 기록했다. 이전 구현은 완료된
경우 timeout 범위 검사를 건너뛰었다. 같은 소스를 새 설치 library에 다시 링크한 결과는
`completed_negative_timeout_accepted=0`, exit 0이다. 새 헤더에 Windows 타입은 노출하지 않는다.

기존 source에는 미완료 0ms 조회도 매번 이벤트 생성 → SetEventOnCompletion → 0ms wait →
CloseHandle을 거치는 경로가 있었다. 양수 timeout 뒤 재시도도 새 이벤트를 등록했다.
[Microsoft의 SetEventOnCompletion 계약](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12fence-seteventoncompletion)은
native 이벤트 저장 목록의 메모리가 부족하면 E_OUTOFMEMORY를 반환할 수 있음을 명시한다.
이번 작업은 불필요한 반복 등록 경로를 없애며, 기존 GBV texture-creation 증가의 원인이나
native kernel 메모리 회수량을 이 결함으로 단정하지 않는다.

private `FenceWaiter`는 timeout 범위를 먼저 검사하고, 완료된 펜스는 성공, 미완료 0ms는
이벤트 생성·등록 없이 Timeout을 반환한다. 유효 범위는 0..4,294,967,294ms이며 음수,
INFINITE 및 그보다 큰 값은 완료 펜스에서도 InvalidArgument다. 상태/thread·미제출·다른
장치/세대 검사의 기존 우선순위는 [공개 계약](PUBLIC_API.md)을 따른다.

양수 대기는 generation별 auto-reset 이벤트 하나를 재사용한다. 단일 증가 timeline에서
`completed + 1`을 등록하고, 그 값이 완료되기 전에는 같은 등록을 재사용한다. 큰 목표의
timeout 후 더 작은 미완료 목표를 기다려도 기존 등록이 그 목표보다 뒤에 있지 않는다.
목표값과 별도로 관리하는 이 기준을 넘은 경우에만 다음 등록으로 진행한다.
중간 완료나 오래된 이벤트가 깨워도 펜스를 다시 읽고 원래 deadline을 유지한다.
timeout 경계에서도 상태를 다시 확인해 DeviceLost를 Timeout으로 가리지 않는다.

Timeout은 작업 취소가 아니다. native batch 소유권과 실제 GPU 완료 뒤 정리 정책을 유지한다.
퇴역은 native fence 해제 뒤 이벤트를 닫고, 기존 안전 종료의 GPU drain도 유지한다.
이벤트 등록 수 제한은 단일 SDK timeline의 활성 대기 경로에 대한 계약이며 전체 driver/GBV
핸들 수나 전체 native 자원 수를 제한한다는 뜻은 아니다. 중간 fence point에서 추가 wake가
발생할 수 있다. hard real-time 지연 상한이나 성능 우위를 측정한 작업은 아니다.

## 결정적 native 대조와 공개 계약 검사

공유 private helper를 실제 native fence/queue로 실행한다. 두 CPU fence gate로 queue의
첫째/둘째 signal을 차단하고 scope 종료 시 항상 gate를 연다. 시간에 의존하는 무거운 draw로
미완료 상태를 추측하지 않는다. wrapping source는 실제 SetEventOnCompletion에 위임하면서
호출 수·threshold·이벤트 handle을 기록한다. SDK workload의 native 우회 경로는 추가하지 않는다.

NVIDIA GTX 1660 SUPER / Intel UHD 770 / WARP를 일반과 전체 GBV 두 모드에서 실행했다.
총 여섯 adapter 실행에서 **24,576개 0ms 조회와 384개 양수 timeout 재시도**를 검사했다.
각 adapter의 4,096개 0ms 조회는 등록 0회, 같은 미완료 지점의 64개 양수 재시도는 등록
1회였다. 첫 gate의 진행 뒤 둘째 지점을 기다리며 한 번 더 등록하고, timeout 경계의 손실
처리 대조에서 세 번째 native 등록을 사용했다. 총 native 등록은 18회다.

같은 이벤트 handle의 재사용, 요청 목표보다 이른 threshold, 더 이른 목표 재시도, 펜스 진행
없는 stale wake, 실제 gate 재개 후 성공, 명시적 reset 뒤 owned handle 폐기를 확인했다.
timeout 경계의 DeviceLost는 poll callback의 제어된 오류이며 저장된 native reason -17을
보존한다. 실제 SDK에서는 별도로 native RemoveDevice, 같은 펜스의 손실 우선 처리와 새
generation의 old-fence 거부를 검사한다. 이 둘을 예기치 않은 실제 TDR 증거로 합치지 않는다.

공개 SDK 검사는 4KiB 전체 buffer readback, 완료 펜스의 음수/INFINITE/overflow 거부,
최대 유한 timeout 성공, 미제출/다른 장치/다른 thread 거부, resource 회수와 복구를 포함한다.
전체 GBV에서 건강한 원래/새 generation의 validation error는 0이어야 한다.

초기 pilot의 일반 GPU 검사는 통과했으나 전체 GBV에서 'process 전체 handle은 정확히
하나만 늘어난다'는 단정이 실패했다. 이 값에는 validation/runtime의 다른 handle도 포함된다.
초기 source/실패 로그는 보존하고, 최종 검사는 실제 owned handle의 동일성·폐기와 실제 native
등록 수를 직접 확인한다. 전체 process handle 수는 그대로 기록하되 SDK 소유 수로 해석하지
않는다. 원래 장시간 메모리 policy는 수정하지 않았다.

## 회귀·배포·재현

Core CTest **30/30**, Windows D3D12 OFF **6/6**, 공통 benchmark correctness **72/72**,
창 예제 **7/7**, Python **159/159**와 기존 설치 소비자가 통과했다. 새 Python 회귀는 private
backend header 변경도 SDK fingerprint를 바꾸는지 확인한다. CMake/Python 계산에 private
`src/**/*.hpp`를 함께 포함해 helper 변경이 stale binary 검사를 우회하지 못하게 했다.
SDK가 변경되어 관련 library·benchmark·설치 소비자·진단/심볼 도구를 모두 재빌드했다.
창과 명시 adapter campaign은 기존 전체 backbuffer/복구 계약을 다시 검증한다.

새 SDK ZIP은 `out/GraphicsSDK-0.0.1-dev-windows-x64-fence-wait.zip`이며 기존 8개는 보존했다.
새 [SDK](../licenses/generated/windows-sdk-fence-wait-release/inventory.json),
[공통](../licenses/generated/windows-benchmarks-fence-wait-release/inventory.json),
[wait/설치/창](../licenses/generated/windows-sdk-fence-wait-validation-release/inventory.json),
[stress](../licenses/generated/windows-sdk-fence-wait-stress-release/inventory.json),
[crash](../licenses/generated/windows-sdk-fence-wait-crash-release/inventory.json) 감사는 각각
20/1, 978/22, 41/1, 26/1, 28/1 compiler inputs/components다.
`sdk-waits`는 기존 budget profile에 `tests/FenceWaitTests.cpp`만 추가 허용하고 총 7개 실제
executable linker closure를 검사한다. private helper는 자체 SDK source이며 새 외부 dependency,
runtime 재배포 또는 public native 접근 API를 추가하지 않는다. 다섯 create/check와 package
검사를 수행하며 자체 source archive에는 binary·PDB·dump를 넣지 않는다.

[직렬 검증 원본](../benchmarks/reports/validation/2026-10-22-phase2-fence-wait/)과
[요약·해시·source archive](../benchmarks/reports/2026-10-22-phase2-fence-wait/)를 보존한다.
이전 장시간 GBV gate 실패·Intel access violation 원인, 육안 표시/실제 DPI/예기치 않은
TDR·hot-unplug와 Vulkan/Linux/Metal/macOS 검증은 여전히 남아 있다.
