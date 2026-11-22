# GraphicsSDK 전체 개발 대시보드

- 기준일: 2026-11-21
- 현재 버전: `0.0.1` (초기 개발 스냅샷 · `v0.0.2` 확장 슬라이스 진행 중)
- 현재 Active Phase: **Phase 3 (Vulkan)** (기존 필수 게이트 보존 + Phase 8~12 후속 고도화 병행)
- 상태 원본: [ROADMAP_TASKS.json](ROADMAP_TASKS.json) · [생성된 세부 로드맵](ROADMAP.md)

> [!NOTE]
> 필수(`required`), 조건부(`conditional`), 후속 확장(`followup`) 항목은 서로 다른 수용 성격을 가지므로 무분별하게 합산하지 않고 열을 분리해 집계한다.
> 또한 **컴파일 성공**, **소프트웨어/가상 GPU 실행**, **물리 GPU 실기 검증**, **사람의 화면 관측**을 구분한다.

---

## 1. 전체 개발 대시보드 (`ROADMAP_TASKS.json` 기준)

| Phase | 영역 | 전체 | 완료 | 필수 잔여 | 조건부 잔여 | 후속 확장 잔여 | 상태 |
| --- | --- | ---: | ---: | ---: | ---: | ---: | --- |
| Phase 0 | 조사·라이선스 기반 | 3 | 3 | 0 | 0 | 0 | 완료 (고정 스냅샷 기준) |
| Phase 1 | 공통 벤치마크 기반 | 6 | 6 | 0 | 0 | 0 | 완료 (Windows D3D12 subset) |
| Phase 2 | D3D12 Core·잔여 수용 | 14 | 10 | 4 | 0 | 0 | 핵심 완료 · 장시간 메모리/DPI/물리 TDR 잔여 |
| Phase 3 | Vulkan (Active Phase) | 79 | 60 | 11 | 8 | 0 | Windows 완료 · Linux 물리 GPU/실제 표시 잔여 |
| Phase 4 | Metal | 9 | 0 | 9 | 0 | 0 | 호스팅 ARM/Intel 구현·검증 누적 · 물리 Mac 및 P3 선행 잔여 |
| Phase 5 | Shader Pipeline | 8 | 0 | 8 | 0 | 0 | DXIL/SPIR-V/MSL/Reflection/Cache 구현 누적 · 선행 수용 잔여 |
| Phase 6 | GPU 자원 최적화 | 26 | 18 | 0 | 0 | 8 | 기준선·4슬롯 기각·링 모델 완료 · 후보/판정 잔여 |
| Phase 7 | 독립 SDK 배포 | 15 | 9 | 6 | 0 | 0 | 6프로필 후보·공통 감사·Windows GPU smoke 완료 · CI/최종 수용 잔여 |
| Phase 8 | GPU 자원·전송·Capability·Texture·Memory Pool 확장 | 20 | 11 | 0 | 0 | 9 | `copyBufferRegion`·부분 2D 텍스처 복사·GPU 한도/포맷 조회·`TextureView`(`P8-TEX-01`)·1D/3D/2DArray/Cubemap/Mipmap(`P8-TEX-02`)·MSAA/Resolve(`P8-TEX-03`)·압축 텍스처 BC/ETC2/ASTC(`P8-TEX-04`)·`UploadRing`/Pools(`P8-MEM-01`)·Fragmentation/Residency/Aliasing/Sparse(`P8-MEM-02`) **Phase 8 Win D3D12/Vulkan 전 항목 완료** · Linux/Metal 검증 9건 잔여 |
| Phase 9 | Command·멀티스레드·Shader·Compute·Render Graph | 15 | 7 | 0 | 0 | 8 | 동적 `setViewport`(`P9-CMD-04`)·명시적 `CommandPool`(`P9-CMD-01`)·병렬 기록/일괄 제출(`P9-CMD-02`)·다중 큐/Timeline Fence(`P9-CMD-03`)·Compute/UAV(`P9-CMP-01`)·Async Compute/Indirect/MDI/GPU Culling/Capability(`P9-CMP-02`)·선택적 Render Graph(`P9-RG-01`) D3D12/Win Vulkan 완료 · Linux/Metal 검증 및 영속 Shader Cache(`P9-SHD-01`) 잔여 |
| Phase 10 | Frame Pacing·HDR·Multi-Surface·Dirty Region | 8 | 4 | 0 | 0 | 4 | Frame Pacing(`P10-PRES-01`)·HDR/Color Space(`P10-HDR-01`)·Multi-Surface(`P10-SURF-01`)·Dirty Region(`P10-DIRTY-01`) **Phase 10 Win D3D12/Vulkan 전 항목 완료** · Linux/Metal 검증 4건 잔여 |
| Phase 11 | Native Interop·Zero-Copy Video·AI·Profiling/Debugging | 7 | 2 | 0 | 0 | 5 | GPU Profiling 확장(`P11-PROF-01`)·디버깅 고도화/Crash Breadcrumbs/Leak/Fuzz(`P11-DBG-01`) D3D12/Win Vulkan 완료 · Linux/Metal 검증(`P11-PROF-02`/`P11-DBG-02`) 및 Interop/Video/AI 대기 |
| Phase 12 | GraphicsUI·Sandbox RHI 호환성 | 4 | 2 | 0 | 0 | 2 | GraphicsUI 공통 기반(`P12-UI-01`)·Sandbox RHI 비침투 어댑터(`P12-SBX-01`) **Phase 12 Win D3D12/Vulkan 전 항목 완료** · Linux/Metal 검증 2건 잔여 |
| **합계** | **전체 13 Phases** | **214** | **132** | **38** | **8** | **36** | **진행 중** |

---

## 2. 세분화 버전 계획 (기존 `0.0.1` 이후 확장)

기존 `0.0.1` 스냅샷과 Phase 0~7 계약을 보존하면서 주요 기능이 검증될 때마다 점진적으로 버전을 올린다.

| 목표 버전 | 핵심 기능 범위 | 연결 작업 ID | 현재 상태 |
| --- | --- | --- | --- |
| `0.0.1` | Core RHI(D3D12/Vulkan/Metal), 버퍼/2D 텍스처, Depth/MRT, 바인딩/정점 입력, Timestamp, 스왑체인, 파이프라인 캐시, 6프로필 배포 후보 | `P0`~`P7` | 개발 스냅샷 진행 중 |
| `v0.0.2` | 오프셋 버퍼 복사(`copyBufferRegion`) · 부분 2D 텍스처 복사 | `P8-XFER-01`~`06` | `01~03, 05` 완료 · `04, 06`(Linux/Metal 실기) 잔여 |
| `v0.0.3` | GPU 한도 및 포맷 지원 조회(`DeviceCaps` 확장·`queryFormatSupport`) | `P8-CAP-01`, `P8-CAP-02` | `01` 완료 · `02`(Linux/Metal 실기) 잔여 |
| `v0.0.4` | Texture View·Subresource View·1D/3D·2DArray·Cubemap·Mipmap | `P8-TEX-01`, `P8-TEX-02`, `P8-TEX-05`, `P8-TEX-07` | `01, 02` 완료 · `05, 07`(Linux/Metal) 잔여 |
| `v0.0.5` | MSAA 텍스처·Resolve·압축 텍스처(BC1~BC7/ASTC/ETC2) | `P8-TEX-03`, `P8-TEX-04`, `P8-TEX-06`, `P8-TEX-08` | `03, 04` 완료 · `06, 08`(Linux/Metal) 잔여 |
| `v0.0.6` | 공개 Upload Ring·Staging/Readback/Transient Pool·VRAM Budget·Fragmentation/Residency/Aliasing/Sparse | `P8-MEM-01`~`04`, `P6-OPT-*` | `01, 02` 완료 (**Phase 8 Win D3D12/Vulkan 완료**) · `03, 04`(Linux/Metal) 잔여 |
| `v0.0.7` | 동적 Viewport·Command Pool·병렬 명령 기록·다중 큐·Timeline Fence | `P9-CMD-01`~`08` | `01, 02, 03, 04` 완료 · `05, 06, 07, 08`(Linux/Metal) 잔여 |
| `v0.0.8` | Compute Pipeline·Storage Buffer/Texture(UAV)·Dispatch·Async Compute GPU Culling·Indirect/MDI·Bindless/Mesh Cap | `P9-CMP-01`~`04` | `01, 02` 완료 · `03, 04`(Linux/Metal) 잔여 |
| `v0.0.9` | Frame Pacing·HDR/Color Space·Multi-Surface·Dirty Region 기반 | `P10-PRES-01`~`P10-DIRTY-02` | `01` 4종 완료 (**Phase 10 Win D3D12/Vulkan 완료**) · Linux/Metal 4건 잔여 |
| `v0.0.10` | 선택적 Render Graph·Native Interop·Zero-Copy Video·GPU Profiling/Diagnostics·GraphicsUI·Sandbox 어댑터 | `P9-RG-01/02`, `P11-*`, `P12-*` | `P9-RG-01`·`P11-PROF-01`·`P12-UI-01`·`P12-SBX-01` 완료 (**Phase 12 Win 완료**) · Linux/Metal 및 나머지 `P11` 잔여 |

---

## 3. 플랫폼별 기능 및 실제 검증 매트릭스

표기 기준:
- **물리 GPU 검증**: 실제 하드웨어 GPU에서 전체 바이트/픽셀 readback 검증 완료
- **SW/가상 검증**: WARP, Linux lavapipe(Xvfb), GitHub 호스팅 macOS Paravirtual Metal 검증
- **구현(미실행)**: 소스 구현 반영, 해당 OS/하드웨어 실행 미완료
- **미구현 / 미지원**: 공개 API 또는 백엔드 미구현 (`ErrorCode::Unsupported`)

| 기능 영역 | Windows D3D12 | Windows Vulkan | Linux Vulkan | macOS Metal |
| --- | --- | --- | --- | --- |
| 어댑터 열거·정확한 선택 | 물리 GPU 검증 | 물리 GPU 검증 | SW 검증 (물리 `P3-LNX-15` 대기) | 호스팅 가상 검증 |
| 버퍼 전송·수명·원자적 거부 | 물리 GPU + GBV + WARP | 물리 GPU + 고정 VVL | SW + 고정 VVL | 호스팅 가상 + Validation |
| 오프셋 버퍼 복사 (`copyBufferRegion`) | **물리 GPU + GBV + WARP** | **물리 GPU + 고정 VVL** | 구현(Linux CI 대기, `P8-XFER-04`) | 구현(macOS CI 대기, `P8-XFER-04`) |
| GPU 한도·포맷 지원 조회 (`DeviceCaps`·`queryFormatSupport`) | **물리 GPU + GBV + WARP** | **물리 GPU + 고정 VVL** | 구현(Linux CI 대기, `P8-CAP-02`) | 구현(macOS CI 대기, `P8-CAP-02`) |
| 2D 텍스처 전체 전송 (RGBA8/BGRA8/RGBA16F/D32) | 물리 GPU + GBV + WARP | 물리 GPU + 고정 VVL | SW + 고정 VVL | 호스팅 가상 + Validation |
| 부분 2D 텍스처 복사 (`copyBufferToTextureRegion` / `copyTextureToBufferRegion`) | **물리 GPU + GBV + WARP** | **물리 GPU + 고정 VVL** | 구현(Linux CI 대기, `P8-XFER-06`) | 구현(macOS CI 대기, `P8-XFER-06`) |
| Texture View (`createTextureView` SRV/RTV/DSV/UAV) | **물리 GPU + GBV + WARP** | **물리 GPU + 고정 VVL** | 구현(Linux CI 대기, `P8-TEX-05`) | 구현(macOS CI 대기, `P8-TEX-05`) |
| 1D/3D·2DArray·Cubemap·다중 Mipmap (`TextureDimension`) | **물리 GPU + GBV + WARP (`P8-TEX-02`)** | **물리 GPU + 고정 VVL (`P8-TEX-02`)** | 구현(Linux CI 대기, `P8-TEX-07`) | 구현(macOS CI 대기, `P8-TEX-07`) |
| 블록 압축 텍스처 (BC1~BC7·ETC2/EAC·ASTC) | **물리 GPU + GBV + WARP (BC7종, ETC2/ASTC 거부, `P8-TEX-04`)** | **물리 GPU + 고정 VVL (GTX BC7종·Intel 14종 전체, `P8-TEX-04`)** | 구현(Linux CI 대기, `P8-TEX-08`) | 구현(macOS CI 대기, `P8-TEX-08`) |
| MSAA 텍스처(2x/4x/8x)·`resolveTexture` | **물리 GPU + GBV + WARP** | **물리 GPU + 고정 VVL** | 구현(Linux CI 대기, `P8-TEX-06`) | 구현(macOS CI 대기, `P8-TEX-06`) |
| 공개 `UploadRing`·`ReadbackPool`·`TransientResourcePool` | **물리 GPU + GBV + WARP (13.68x)** | **물리 GPU + 고정 VVL (9.91x)** | 구현(Linux CI 대기, `P8-MEM-03`) | 구현(macOS CI 대기, `P8-MEM-03`) |
| `MemoryHeap` Fragmentation·Residency·Aliasing·Sparse 2D | **물리 GPU + GBV + WARP (50% 절감·Evict/MakeResident, `P8-MEM-02`)** | **물리 GPU + 고정 VVL (50% 가상 절감·Sparse 128², `P8-MEM-02`)** | 구현(Linux CI 대기, `P8-MEM-04`) | 구현(macOS CI 대기, `P8-MEM-04`) |
| 동적 Viewport (`setViewport`) | **물리 GPU + GBV + WARP** | **물리 GPU + 고정 VVL** | 구현(Linux CI 대기, `P9-CMD-05`) | 구현(macOS CI 대기, `P9-CMD-05`) |
| 명시적 Command Pool·재사용 (`CommandPool`) | **물리 GPU + GBV + WARP** | **물리 GPU + 고정 VVL** | 구현(Linux CI 대기, `P9-CMD-06`) | 구현(macOS CI 대기, `P9-CMD-06`) |
| 병렬 명령 기록·일괄 제출 (`bindRecordingThread`·`submit(span)`) | **물리 GPU + GBV + WARP (4T 3.86x)** | **물리 GPU + 고정 VVL (4T 3.55x)** | 구현(Linux CI 대기, `P9-CMD-07`) | 구현(macOS CI 대기, `P9-CMD-07`) |
| Graphics/Compute/Copy 다중 큐·`TimelineFence`·Async 전송 | **물리 GPU + GBV + WARP (1.33x)** | **물리 GPU + 고정 VVL (1.23x)** | 구현(Linux CI 대기, `P9-CMD-08`) | 구현(macOS CI 대기, `P9-CMD-08`) |
| Depth/MRT·Clear/Load·Scissor·Alpha Blend | 물리 GPU + GBV + WARP | 물리 GPU + 고정 VVL | SW + 고정 VVL | 호스팅 가상 + Validation |
| Vertex/Index/Instance 입력·Binding Layout | 물리 GPU + GBV + WARP | 물리 GPU + 고정 VVL | SW + 고정 VVL | 호스팅 가상 + Validation |
| GPU Timestamp (2-slot + 다중 32-Scope·ON/OFF 프로파일링) | **물리 GPU + GBV + WARP (`P11-PROF-01`)** | **물리 GPU + 고정 VVL (`P11-PROF-01`)** | 2-slot SW 검증 · 다중 스코프 구현(`P11-PROF-02`) | GPU 스코프 `Unsupported` · 카운터 구현(`P11-PROF-02`) |
| Pipeline Cache (장치별 8-entry LRU) | SW(WARP) 검증 | SW/설치 소비자 검증 | SW 검증 | 호스팅 가상 검증 |
| OS Memory Budget 조회 (`queryMemoryBudget`) | 물리 GPU + GBV | 미지원 (`P3-CAP-04` 조건부) | 미지원 (`P3-CAP-04`) | 미지원 (`Unsupported`) |
| 장치 손실 복구 (`recover`) | 통제 RemoveDevice 검증 | 합성 손실 레이어 검증 | SW 합성 손실 검증 | 미지원 (`Unsupported`) |
| SwapChain / 화면 출력 | NVIDIA 수동 표시 수용 (DPI/TDR 대기) | NVIDIA 자동 검증 (Intel 미지원) | XCB Xvfb SW 검증 (수동 `P3-WSI-08` 대기) | CAMetalLayer 호스팅 검증 (물리 Mac 대기) |
| Compute Pipeline·Storage Buffer/Texture(UAV)·Dispatch | **물리 GPU + GBV + WARP** | **물리 GPU + 고정 VVL** | 구현(Linux CI 대기, `P9-CMP-03`) | 구현(macOS CI 대기, `P9-CMP-03`) |
| 선택적 Render Graph (`RenderGraph`·DAG·자동 배리어·Aliasing·Culling·진단) | **물리 GPU + GBV + WARP (`-3.08%`)** | **물리 GPU + 고정 VVL (`-2.02%`)** | 구현(Linux CI 대기, `P9-RG-02`) | 구현(macOS CI 대기, `P9-RG-02`) |
| Async Compute GPU Culling·Indirect/MDI/`dispatchIndirect`·Bindless/Mesh Cap | **물리 GPU + GBV + WARP (GTX `mesh=yes`/Intel `no`, `P9-CMP-02`)** | **물리 GPU + 고정 VVL (GTX `mesh=yes`/Intel `no`, `P9-CMP-02`)** | 구현(Linux CI 대기, `P9-CMP-04`) | 구현(macOS CI 대기, `P9-CMP-04`) |
| Frame Pacing·Present Mode(Fifo/Immediate/Mailbox/Tearing)·최대 지연·GPU Stall | **물리 GPU + GBV + WARP (Fifo/Imm/Tearing·Waitable, `P10-PRES-01`)** | **물리 GPU + 고정 VVL (GTX 4모드·Intel 거부, `P10-PRES-01`)** | 구현(Linux CI 대기, `P10-PRES-02`) | 구현(macOS CI 대기, `P10-PRES-02`) |
| HDR·Color Space·10-bit·sRGB (`RGBA8SRGB`/`BGRA8SRGB`/`RGB10A2UNorm`·HDR10·scRGB·메타데이터) | **물리 GPU + GBV + WARP (8/8 쌍·HDR10 메타데이터, `P10-HDR-01`)** | **물리 GPU + 고정 VVL (GTX 8/8 쌍·`VK_EXT_hdr_metadata`, `P10-HDR-01`)** | 구현(Linux CI 대기, `P10-HDR-02`) | 구현(macOS CI 대기, `P10-HDR-02`) |
| Multi-Surface·다중 스왑체인·DPI·최소화/복원 (`SurfaceMetrics`·`syncExtent`·`presentSwapChains`) | **물리 GPU + GBV + WARP (3중 창 동시/복원, `P10-SURF-01`)** | **물리 GPU + 고정 VVL (GTX 3중 창·Intel 거부, `P10-SURF-01`)** | 구현(Linux CI 대기, `P10-SURF-02`) | 구현(macOS CI 대기, `P10-SURF-02`) |
| Dirty Region Rendering·Partial Present (`LoadOp::Load`·`presentDirtyRects`·실측 비교) | **물리 GPU + GBV + WARP (`Present1`·46.88x~99.45x GPU 가속, `P10-DIRTY-01`)** | **물리 GPU + 고정 VVL (`32.14x~98.77x` GPU 가속·미노출 거부, `P10-DIRTY-01`)** | 구현(Linux CI 대기, `P10-DIRTY-02`) | 구현(macOS CI 대기, `P10-DIRTY-02`) |
| Crash Breadcrumbs·Leak Detection·Fault Injection·Input Fuzz (`diagnosticReport`) | **물리 GPU + GBV + WARP (4,500 Fuzz·1 TiB 방어, `P11-DBG-01`)** | **물리 GPU + 고정 VVL (1,000 Fuzz·1 TiB 방어, `P11-DBG-01`)** | 구현(Linux CI 대기, `P11-DBG-02`) | 구현(macOS CI 대기, `P11-DBG-02`) |
| Native Interop / Zero-Copy Video / AI 연계 | 미구현 (`P11`) | 미구현 (`P11`) | 미구현 (`P11`) | 미구현 (`P11`) |

---

## 4. Sandbox RHI 및 GraphicsUI 호환성 대조

Sandbox 참고 기준: 읽기 전용 스냅샷 `.deps/references/sandbox` (`afe56355`, [SANDBOX_REFERENCE.md](SANDBOX_REFERENCE.md)).

| 요구 기능 | Sandbox RHI (`sbx::rhi`) | GraphicsUI 요구 | GraphicsSDK 현재 상태 | 연결 작업 ID |
| --- | --- | --- | --- | --- |
| Device / Adapter / Queue / Fence | `IRenderDevice`, `ICommandQueue` | 필수 | 구현·검증 완료 | `P2`, `P3`, `P4` |
| Buffer / Texture / Handle 수명 | `HandlePool` + `DeferredDestruction` | 필수 | `shared_ptr` + Fence 배치 보유·weak 퇴역 | `P2`, `P3`, `P6-OPT-12` |
| 오프셋 버퍼 복사 | `BufferCopy{srcOffset, dstOffset, size}` | 동적 VB/IB 업로드 | **`copyBufferRegion` 구현·Win D3D12/Vulkan 검증 완료** | `P8-XFER-01`~`04` |
| 부분 텍스처 복사 | `BufferTextureCopy{x, y, w, h, mip, layer}` | 폰트/텍스처 아틀라스·Dirty Region | **2D 부분 복사(`P8-XFER-05`) Win D3D12/Vulkan 완료** · mip/layer(`P8-TEX-02`) 대기 | `P8-XFER-05/06`, `P8-TEX-02` |
| Viewport / Scissor / Alpha Blend | `setViewport`, `setScissor`, `BlendMode` | 2D UI 핵심 | **`setViewport`(`P9-CMD-04`)·`setScissor`·`alphaBlend` Win D3D12/Vulkan 완료** | `P9-CMD-04/05` |
| Upload Ring / Frame Context | `UploadRing`, `beginFrame`/`endFrame` | 저오버헤드 프레임 업로드 | **공개 `UploadRing`·`ReadbackPool`·`TransientResourcePool`(`P8-MEM-01`) Win D3D12/Vulkan 완료** | `P8-MEM-01/03`, `P6-OPT-08` |
| GPU Timestamp (다중 구간) | `kMaxTimestampsPerFrame = 32` | GPU 프로파일링 | **2-slot + 다중 32-Scope·ON/OFF 프로파일링(`P11-PROF-01`) Win D3D12/Vulkan 완료** | `P11-PROF-01/02` |
| 포맷 확장 (`R8`, `RG8`, `sRGB`, `R16Uint`, `D24S8`) | `Format` 16종 + `formatInfo` | 폰트 마스크(R8)·sRGB | 4종(RGBA8/BGRA8/RGBA16F/D32F) | `P8-CAP-01`, `P8-TEX-01` |
| Multi-Surface / Low Latency | `ISwapChain` + `tearing` | 다중 독립 창·저지연 | **Present Mode(Fifo/Immediate/Mailbox/Tearing)·Frame Pacing·Waitable Latency(`P10-PRES-01`) Win D3D12/Vulkan 완료** · Multi-Surface(`P10-SURF-01`) 대기 | `P10-PRES-01/02`, `P10-SURF-01` |

---

## 5. 주요 벤치마크 및 검증 보고서 연결

실측하지 않은 값은 기록하지 않으며, 모든 원자료는 동일 환경·동일 워크로드 조건을 유지한다.

| 범주 | 대표 보고서 |
| --- | --- |
| D3D12 경쟁 비교 (LLGL·SDL3·Diligent·bgfx·The Forge) | [BENCHMARK_REPORT.md](BENCHMARK_REPORT.md), [PHASE1_COMPLETION_REPORT.md](PHASE1_COMPLETION_REPORT.md), [GPU_TIMESTAMP_BENCHMARK_REPORT.md](GPU_TIMESTAMP_BENCHMARK_REPORT.md) |
| Windows Vulkan 3-SDK 비교 (GraphicsSDK·SDL3·LLGL) | [PHASE3_COMPARISON_REPORT.md](PHASE3_COMPARISON_REPORT.md), [PHASE3_DRAW_GPU_TIMESTAMP_REPORT.md](PHASE3_DRAW_GPU_TIMESTAMP_REPORT.md) |
| macOS Metal 3-SDK 비교 | [PHASE4_METAL_COMPARISON_REPORT.md](PHASE4_METAL_COMPARISON_REPORT.md) |
| Shader / PSO 캐시 타이밍 | [PHASE5_SHADER_PSO_TIMING_REPORT.md](PHASE5_SHADER_PSO_TIMING_REPORT.md) |
| Vulkan 자원 최적화 기준선 및 4슬롯 풀 판정 | [PHASE6_VULKAN_POOL_DECISION.md](PHASE6_VULKAN_POOL_DECISION.md), [PHASE6_VULKAN_UPLOAD_STAGING_BASELINE.md](PHASE6_VULKAN_UPLOAD_STAGING_BASELINE.md), [PHASE6_VULKAN_SUBMIT_BASELINE.md](PHASE6_VULKAN_SUBMIT_BASELINE.md) |
| 6프로필 배포 후보 무결성 및 Windows GPU Smoke | [PHASE7_COMMON_CANDIDATE_AUDIT.md](PHASE7_COMMON_CANDIDATE_AUDIT.md), [PHASE7_WINDOWS_CANDIDATE_GPU.md](PHASE7_WINDOWS_CANDIDATE_GPU.md) |
| Phase 8·9·11·12 전송·Capability·View·MSAA·Memory Pool·Viewport·CommandPool·병렬 기록·다중 큐·Compute·Render Graph·GPU Profiling·Sandbox 어댑터 | [PHASE8_BUFFER_REGION_REPORT.md](PHASE8_BUFFER_REGION_REPORT.md), [PHASE8_TEXTURE_REGION_REPORT.md](PHASE8_TEXTURE_REGION_REPORT.md), [PHASE8_CAPABILITY_REPORT.md](PHASE8_CAPABILITY_REPORT.md), [PHASE8_TEXTURE_VIEW_REPORT.md](PHASE8_TEXTURE_VIEW_REPORT.md), [PHASE8_MSAA_REPORT.md](PHASE8_MSAA_REPORT.md), [PHASE8_MEMORY_POOL_REPORT.md](PHASE8_MEMORY_POOL_REPORT.md), [PHASE9_VIEWPORT_REPORT.md](PHASE9_VIEWPORT_REPORT.md), [PHASE9_COMMAND_POOL_REPORT.md](PHASE9_COMMAND_POOL_REPORT.md), [PHASE9_PARALLEL_RECORDING_REPORT.md](PHASE9_PARALLEL_RECORDING_REPORT.md), [PHASE9_MULTI_QUEUE_REPORT.md](PHASE9_MULTI_QUEUE_REPORT.md), [PHASE9_COMPUTE_REPORT.md](PHASE9_COMPUTE_REPORT.md), [PHASE9_RENDER_GRAPH_REPORT.md](PHASE9_RENDER_GRAPH_REPORT.md), [PHASE11_PROFILING_REPORT.md](PHASE11_PROFILING_REPORT.md), [PHASE12_SANDBOX_ADAPTER_REPORT.md](PHASE12_SANDBOX_ADAPTER_REPORT.md) |
