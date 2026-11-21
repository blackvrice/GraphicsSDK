# Phase 12 Sandbox RHI 호환성 분석 및 비침투 어댑터 (`P12-SBX-01`) 보고서

- 작성일: 2026-11-21
- 대상 작업 ID: `P12-SBX-01` (완료), `P12-SBX-02` (Linux Vulkan 및 macOS Metal 실기/CI 실행 검증 분리 `todo`)
- 참고 스냅샷 (읽기 전용, 미수정): `.deps/references/sandbox` (`afe563557367a0f2b5cef88488708da36f578b70`, [SANDBOX_REFERENCE.md](SANDBOX_REFERENCE.md))
- 비침투 독립 어댑터 구현: [examples/sandbox_adapter/SandboxRhiAdapter.hpp](../examples/sandbox_adapter/SandboxRhiAdapter.hpp)
- 검증 테스트: [tests/SandboxAdapterTests.cpp](../tests/SandboxAdapterTests.cpp) (`gfx_sandbox_adapter_tests`)
- 격리 빌드/설치 경로: `out/p12-sandbox-adapter`, `out/p12-sandbox-adapter-install`, `out/p12-sandbox-adapter-consumer`
- 설치 `GraphicsSDKCore.lib` SHA-256: `B259BED4D12FEB51C7503B45924E0D3AF20108FA1CCC10117A5A014B988C54E6`

---

## 1. 독립성 및 경계 준수

- Sandbox 참고 저장소(`.deps/references/sandbox`)의 소스·설정·파일·Git 상태를 일절 변경하지 않았으며, Sandbox 코드를 복사하거나 빌드 의존성으로 연결하지 않았다.
- `GraphicsSDK` 공개 헤더(`include/graphics/`, 13개 헤더)와 코어 구현(`src/`, 5개 `CORE` `.cpp`)을 **단 1바이트도 수정하지 않고** 오직 설치 가능한 공개 `GraphicsSDK::Core` API만으로 어댑터 계약을 구현·검증했다.

---

## 2. Sandbox RHI ↔ GraphicsSDK 1:1 매핑 및 격차 분석

### 2.1 해소 완료 (1:1 직접 또는 어댑터 매핑)

| Sandbox RHI 항목 (`sbx::rhi`) | GraphicsSDK 대응 공개 API (`gfx`) | 매핑 방식 및 비고 |
| --- | --- | --- |
| `BackendType` (`D3D12`/`Vulkan`/`Metal`) · `backendCompiled` | `gfx::Backend` · `gfx::backendCompiled` | 1:1 매핑 |
| `DeviceDesc` (`debugLayer`/`warp`/`framesInFlight`/`uploadRingBytes`) | `gfx::DeviceDesc` (`validation`/`softwareAdapter`) + `gfx::UploadRingDesc` | 어댑터 `AdapterDevice::create`에서 조합 |
| `DeviceCaps` (`adapterName`, `dedicatedVideoMemory`, `timestampQueries`, `compute`, `maxTextureSize`, 정렬 한도 3종) | `gfx::DeviceCaps` (`P8-CAP-01`) + `gfx::enumerateAdapters()` | `dedicatedVideoMemory`는 어댑터 열거 정보로 보강 |
| `HandlePool<Tag, T>` (세대 기반 핸들) | 어댑터 측 `HandlePool<Tag, T>` (`GenerationalHandle`, non-zero generation) | `destroy()` 즉시 `alive()==false`, 슬롯 재할당 시 세대 증가로 stale 핸들 영구 거부 |
| `DeferredDestructionQueue` | 어댑터 `DeferredDestructionQueue` + SDK `shared_ptr` 명령/제출 retention | `destroy()` 시 제출 펜스로 지연 해제 예약 → `beginFrame()`/`waitIdle()`에서 완료 펜스 회수. 미제출 `CommandList` 참조도 SDK 소유권으로 안전 보호 |
| `UploadRing` (`allocateUpload`, `uploadRingDeferred`) | `Device::createUploadRing` (`P8-MEM-01`) + `UploadRing::allocate`/`upload`/`commit`/`reclaim` | 링 포화(`ErrorCode::OutOfMemory`) 시 `uploadRingDeferred` 증가 |
| `FrameContext` (`beginFrame` / `endFrame`, `framesInFlight = 2|3`) | 슬롯별 `CommandPool` + `CommandList` (`P9-CMD-01`) + `Fence` + `UploadRing::commit`/`reclaim` | 슬롯 순환 시 이전 펜스 대기 → `CommandPool::reset()` → 지연 해제·업로드 링·타임스탬프 회수 |
| `ICommandList::copyBuffer(BufferCopy)` | `CommandList::copyBufferRegion` (`P8-XFER-01`) | 오프셋 기반 버퍼 구간 복사 1:1 매핑 |
| `ICommandList::copyBufferToTexture` / `copyTextureToBuffer` | `CommandList::copyBufferToTextureRegion` / `copyTextureToBufferRegion` (`P8-XFER-05`) | `mip=0, layer=0` 임의 2D 사각 영역 1:1 매핑 |
| `ICommandList::setViewport` / `setScissor` / `beginRenderPass` | `CommandList::setViewport` (`P9-CMD-04`) / `setScissor` / `beginRenderPass(RenderPassDesc)` | 최대 8 color attachments(Sandbox 요구 4 초과) + `LoadOp::Clear/Load` |
| `ICommandList::barrier(span<ResourceBarrier>)` | `CommandList::transition(TexturePtr/BufferPtr, ResourceState)` | 명시적 상태 전이 1:1 매핑 |
| `ICommandList::beginDebugLabel` | `CommandList::debugMarker` | D3D12/Vulkan/Metal 마커 1:1 매핑 |
| `kMaxTimestampsPerFrame = 32` · `completedTimestamps()` | `CommandList::beginTimestampScope`/`endTimestampScope`/`resolveTimestampScopes`/`timestampScopes` (`P11-PROF-01`) | 최대 32개 중첩/순차 GPU 스코프(64슬롯)를 프레임 슬롯 순환 시 `completedTimestamps()`로 비동기 회수 |
| `DeviceStats` (`liveBuffers/Textures/Pipelines`, `pendingDestructions`, `releasedObjects`, `debugErrors`, `uploadRingUsed/Deferred`) | `HandlePool` + `DeferredDestructionQueue` + `UploadRing::statistics()` + `Device::resourceStatistics()` + `validationErrorCount()` | 1:1 집계 및 SDK 네이티브 해제 정합성 교차 검증 |

### 2.2 확인된 핵심 계약 차이 (실증 검증됨)

1. **Upload 버퍼 `observedVersion` 스냅샷 규칙 (Stage-Before-Record)**:
   - Sandbox의 `UploadRing`은 영구 매핑 raw 포인터 기반이어서 한 프레임 내에서 `allocateUpload` → `copyBuffer` → `allocateUpload` → `copyBufferToTexture`를 인터리빙해도 오류가 발생하지 않는다.
   - 반면 GraphicsSDK는 기록된 `CommandList`가 참조한 버퍼가 제출 전에 CPU `write()`로 수정되면 `submit()` 시점에 `Resource changed after recording; re-record the command list`로 안전하게 거부한다.
   - **어댑터 계약**: 같은 `UploadRing` 세그먼트 버퍼에 대한 한 프레임의 모든 CPU 쓰기(`uploadData()`)는 해당 버퍼를 참조하는 복사 명령(`copyBufferRegion` / `copyBufferToTextureRegion`) 기록 **이전**에 배치(stage-before-record)해야 한다.
2. **영구 `map()` vs 범위 `write()`/`read()`**:
   - GraphicsSDK는 GPU in-flight 덮어쓰기 및 미완료 readback 레이스를 차단하기 위해 영구 raw 포인터 대신 fence-checked `Buffer::write`/`read` 및 `UploadRing::upload`를 제공한다. 어댑터는 이를 `writeBuffer`/`readBuffer`/`uploadData`로 안전하게 노출한다.

### 2.3 정직하게 명시한 잔여 격차 (어댑터에서 명시적 `ErrorCode::Unsupported` 거부)

| 잔여 격차 항목 | 현재 어댑터 동작 | 추적 로드맵 ID |
| --- | --- | --- |
| 확장 포맷 (`R8Unorm`, `RG8Unorm`, `RGBA8Srgb`, `BGRA8Srgb`, `R32Float`, `D24UnormS8Uint`, `R16Uint`) | `AdapterDevice::createTexture`에서 `ErrorCode::Unsupported` 반환 | `P8` 포맷 확장 후속 |
| 다중 Mipmap (`mips > 1`) 및 다중 레이어/배열 (`layers > 1`) | `createTexture` / `recordCopy*Texture`에서 `ErrorCode::Unsupported` 반환 | `P8-TEX-02` |
| Push Constants (≤128B), 동적 `SamplerDesc`, `Line`/`Point` Topology, `CullMode`/`Wireframe`, `Additive`/`PremultipliedAlpha` 블렌드, `StoreOp::DontCare` | 현재 SDK Core 범위 밖 — 상수 버퍼(`UploadRing`) 대체 또는 명시적 미지원 처리 | `P9` 파이프라인/바인딩 확장 |
| VRR / Tearing Present (`caps.tearing`) | `AdapterCaps::tearingSupported = false` 정직 보고 | `P10-PRES-01` |

---

## 3. 실기 및 설치 소비자 검증 결과

### 3.1 `gfx_sandbox_adapter_tests` 5개 실행 모드
세대 핸들 재사용/stale 거부, 미지원 포맷 5종·multi-mip·multi-layer `Unsupported` 거부, `framesInFlight=2` 4프레임 루프(`UploadRing` 오프셋 버퍼 복사 + 8×8 부분 2D 아틀라스 타일 업로드 + 동적 `setViewport`/`setScissor` + 프레임 중 `destroy()` 지연 해제 + `completedTimestamps()` 비동기 슬롯 회수 + 전체 32×32 픽셀 및 1,024B 바이트 일치), `UploadRing` 단일 세그먼트 초과(`InvalidArgument`) 및 전 세그먼트 포화(`OutOfMemory` → `uploadRingDeferred` 증가), 사용자 리소스 4개 완전 회수를 검증했다.

| 모드 | 어댑터 | Validation 오류 | 프레임 | 거부 | 결과 |
| --- | --- | ---: | ---: | ---: | --- |
| `--d3d12` | NVIDIA GeForce GTX 1660 SUPER | 0 (off) | 4 | 10 | **PASS (`assertions=156`)** |
| `--d3d12 --validation` | NVIDIA GeForce GTX 1660 SUPER | **0** (Debug+GBV) | 4 | 10 | **PASS (`assertions=158`)** |
| `--d3d12 --warp` | Microsoft Basic Render Driver | 0 (off) | 4 | 11 (`DeviceLost` 포함) | **PASS (`assertions=158`)** |
| `--vulkan` | NVIDIA GeForce GTX 1660 SUPER | 0 (off) | 4 | 10 | **PASS (`assertions=156`)** |
| `--vulkan --validation` | NVIDIA GeForce GTX 1660 SUPER | **0** (Pinned VVL 1.3.290) | 4 | 10 | **PASS (`assertions=158`)** |

### 3.2 독립 설치 소비자 (`find_package(GraphicsSDK 0.0.1 EXACT CONFIG REQUIRED)` + `SandboxRhiAdapter.hpp`)
`out/p12-sandbox-adapter-consumer`에서 설치된 `GraphicsSDK::Core`와 `SandboxRhiAdapter.hpp`만으로 3프레임 순환·전체 픽셀 일치·지연 해제·타임스탬프 회수·`val_errors == 0`을 열거된 모든 어댑터에서 실행했다.

| 백엔드 | 어댑터 | 비검증 | 검증 (`val_errors`) |
| --- | --- | --- | --- |
| D3D12 | NVIDIA GeForce GTX 1660 SUPER | **PASS (`released=2, ts_valid=yes`)** | **PASS (`0`, Debug+GBV)** |
| D3D12 | Intel(R) UHD Graphics 770 | **PASS (`released=2, ts_valid=yes`)** | **PASS (`0`, Debug+GBV)** |
| D3D12 | Microsoft Basic Render Driver (WARP) | **PASS (`released=2, ts_valid=yes`)** | - (SW 비검증 경로) |
| Vulkan | NVIDIA GeForce GTX 1660 SUPER | **PASS (`released=2, ts_valid=yes`)** | **PASS (`0`, VVL 1.3.290)** |
| Vulkan | Intel(R) UHD Graphics 770 | **PASS (`released=2, ts_valid=yes`)** | **PASS (`0`, VVL 1.3.290)** |

### 3.3 CTest 실행
- `ctest --test-dir out/p12-sandbox-adapter -C Release -R "sandbox_adapter" --output-on-failure` (고정 VVL 1.3.290 `VK_LAYER_PATH`): **5 / 5 통과 (`1.88s`)**.
- SDK 코어(`include/graphics/`, `src/`) 미수정으로 직전 `P11-PROF-01`의 전체 132/132 회귀 통과 상태가 그대로 유지된다.

---

## 4. 미실행 범위

- Linux Vulkan 및 macOS Metal 환경에서의 `gfx_sandbox_adapter_tests` 실기/CI 실행은 Windows 호스트에서 수행하지 않았으며 `P12-SBX-02`(`todo`)로 분리 유지한다.
