# GPU memory and lifetime

현재 allocator는 D3D12 committed resources를 사용한다. Device/Upload/Readback은
각각 default/upload/readback heap이다. 이 선택은 단순한 정확성 기준점이며 최적화된
메모리 서브할당이라고 주장하지 않는다. upload ring, resource pool, transient aliasing,
budget 기반 eviction 및 다중 프레임 arena는 미구현이다.

texture는 2D, mip 1, array 1, sample 1의 RGBA8/RGBA16F다. footprint는
GetCopyableFootprints로 얻는다. 마지막 행 뒤 padding이 총 크기에 포함되지 않을 수
있으므로 `rowPitch*height`를 필요 바이트와 무조건 같다고 가정하지 않는다.
63×7 텍스처의 행별 readback 테스트가 이 차이를 확인한다.

자원 상태는 기록 목록에서 로컬 추적하고 submit 시 snapshot version을 검증한다.
제출 batch는 완료 fence까지 자원·PSO·root signature·descriptor·allocator·list·query를
native COM 참조로 유지한다. 목록이 아직 미제출인 동안에도 native 참조를 가진다.
이 정책은 CPU wrapper를 조기에 파괴한 buffer/texture copy 테스트로 검증한다.

Present는 렌더 제출 뒤 큐에 추가 작업을 넣을 수 있다. resize/파괴 전에는 최신
렌더 fence만 기다리지 않고 큐 tail에 새 fence를 signal하여 기다린다.
GPU Validation에서 발견한 조기 backbuffer 해제 문제의 수정 근거다.

장치 공유 상태와 pending batch의 순환 참조를 만들지 않는다. 장치 종료는 남은
제출 완료를 기다리고 device removal이면 종료한다. device-loss 실제 주입·장기 누수
stress 및 multi-queue 자원 수명은 아직 검증하지 않았다.

D3D12 buffer는 ExecuteCommandLists 경계에서 COMMON으로 decay할 수 있다. 첫 구현은
promotable before-state를 유지하는 전통적인 명시적 추적을 사용한다. 이 방식은
[공식 resource barrier 계약](https://learn.microsoft.com/en-us/windows/win32/direct3d12/using-resource-barriers-to-synchronize-resource-states-in-direct3d-12)에
따라 유효하지만 불필요한 barrier를 줄이는 후속 최적화 여지가 있다. default-heap buffer의
연속 upload/copy/readback batch를 hardware/WARP/GPU validation으로 검증한다.
