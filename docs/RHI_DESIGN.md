# RHI contracts

* `createDevice`는 요청 backend가 빌드에 없으면 Unsupported를 반환한다.
  하드웨어 선택 실패 시 WARP로 조용히 대체하지 않는다. softwareAdapter를 명시해야 한다.
* `Result<T>`는 `std::expected<T, Error>`다. InvalidArgument, InvalidState,
  WrongDevice, Unsupported, OutOfMemory, DeviceLost, Timeout, BackendFailure를 구분한다.
  native 실패에는 HRESULT가 포함된다. C++ allocator의 bad_alloc은 예외로 전파될 수 있다.
* Buffer/Texture/Pipeline은 장치에 소속된다. 다른 장치의 자원/목록/펜스는 거부한다.
* Buffer/Texture의 메모리는 초기화되지 않는다. upload/render clear 후 사용한다.
* 명령 목록은 begin → 기록 → end → submit 한 번의 상태 머신이다. 다시 begin하려면
  이전 제출 fence가 완료되어야 한다. 열린 render pass를 둔 end와 pass 밖 draw는 거부한다.
* transition은 명령 목록 로컬 상태만 바꾼다. 폐기된 목록은 전역 상태를 바꾸지 않는다.
  submit은 기록 시 version을 검증하고 성공 경로에서 최종 상태를 commit한다.
  다른 목록 제출이나 CPU write로 snapshot이 바뀌면 재기록을 요구한다.
  읽기 전용 공유도 보수적으로 version을 증가시키므로 일부 독립 병렬 사용은 거부한다.
* Upload는 GENERIC_READ, Readback은 COPY_DEST를 유지한다. CPU write/read는
  마지막 사용 fence가 완료되지 않았다면 거부한다. 기록 후 upload write도 stale submit으로 거부한다.
* 자원 wrapper를 제출 전/후 파괴해도 기록 목록과 제출 batch가 native 객체를 유지한다.
  descriptor heap과 query readback, allocator, PSO도 함께 보호한다.
* 큐 fence는 장치 ID와 단조 증가 값이다. wait는 이미 제출한 값만 허용한다.
  waitIdle은 새 signal을 넣어 DXGI Present가 추가한 큐 작업까지 기다린다.
* SwapChain resize는 acquired wrapper 및 기록 목록이 참조하는 backbuffer를 거부한다.
  참조를 해제한 뒤 큐를 기다리고 resize한다. 명시적 HWND 표면만 지원한다.

첫 graphics pipeline은 vertex-ID 기반 triangle list, 단일 color target,
RGBA8/RGBA16F, 선택적 alpha blend 및 fragment t0/s0 한 개를 지원한다.
Vertex/index buffer binding, depth/MRT, 범용 descriptor layout, compute는 아직 제공하지 않는다.
shader의 입력/바인딩 계약과 public API의 제한을 지켜야 한다.
