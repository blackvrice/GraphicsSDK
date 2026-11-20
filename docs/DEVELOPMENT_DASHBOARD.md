# GraphicsSDK 전체 개발 대시보드

- 기준일: 2026-11-20
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
| Phase 8 | GPU 자원·전송·Capability·Texture 확장 | 15 | 6 | 0 | 0 | 9 | `copyBufferRegion`·부분 2D 텍스처 복사·GPU 한도/포맷 조회·`TextureView`(`P8-TEX-01`) D3D12/Win Vulkan 완료 · Linux/Metal 검증 및 다중 차원/MSAA/압축/Pool 잔여 |
| Phase 9 | Command·멀티스레드·Shader·Compute·Render Graph | 10 | 2 | 0 | 0 | 8 | 동적 `setViewport`(`P9-CMD-04`)·명시적 `CommandPool`(`P9-CMD-01`) D3D12/Win Vulkan 완료 · Linux/Metal 검증 및 병렬 기록·Compute·RG 대기 |
| Phase 10 | Frame Pacing·HDR·Multi-Surface·Dirty Region | 4 | 0 | 0 | 0 | 4 | 계획 등록 완료 · 순차 구현 대기 |
| Phase 11 | Native Interop·Zero-Copy Video·AI·Profiling/Debugging | 5 | 0 | 0 | 0 | 5 | 계획 등록 완료 · 순차 구현 대기 |
| Phase 12 | GraphicsUI·Sandbox RHI 호환성 | 2 | 0 | 0 | 0 | 2 | 계획 등록 완료 · 비교 완료 및 어댑터 대기 |
| **합계** | **전체 13 Phases** | **196** | **114** | **38** | **8** | **36** | **진행 중** |

---

## 2. 세분화 버전 계획 (기존 `0.0.1` 이후 확장)

기존 `0.0.1` 스냅샷과 Phase 0~7 계약을 보존하면서 주요 기능이 검증될 때마다 점진적으로 버전을 올린다.

| 목표 버전 | 핵심 기능 범위 | 연결 작업 ID | 현재 상태 |
| --- | --- | --- | --- |
| `0.0.1` | Core RHI(D3D12/Vulkan/Metal), 버퍼/2D 텍스처, Depth/MRT, 바인딩/정점 입력, Timestamp, 스왑체인, 파이프라인 캐시, 6프로필 배포 후보 | `P0`~`P7` | 개발 스냅샷 진행 중 |
| `v0.0.2` | 오프셋 버퍼 복사(`copyBufferRegion`) · 부분 2D 텍스처 복사 | `P8-XFER-01`~`06` | `01~03, 05` 완료 · `04, 06`(Linux/Metal 실기) 잔여 |
| `v0.0.3` | GPU 한도 및 포맷 지원 조회(`DeviceCaps` 확장·`queryFormatSupport`) | `P8-CAP-01`, `P8-CAP-02` | `01` 완료 · `02`(Linux/Metal 실기) 잔여 |
| `v0.0.4` | Texture View·Subresource View·Mipmap·Texture Array·Cubemap | `P8-TEX-01`, `P8-TEX-02`, `P8-TEX-05` | `01`(`TextureView`) 완료 · `05`(Linux/Metal) 및 `02` 잔여 |
| `v0.0.5` | MSAA 텍스처·압축 텍스처(BC1~BC7/ASTC/ETC2) | `P8-TEX-03`, `P8-TEX-04` | 미완료 |
| `v0.0.6` | 공개 Upload Ring·Staging/Readback/Transient Pool·VRAM Budget | `P8-MEM-01`, `P8-MEM-02`, `P6-OPT-*` | CPU 모델(`P6-OPT-10`) 완료 |
| `v0.0.7` | 동적 Viewport·Command Pool·병렬 명령 기록·다중 큐·Timeline Fence | `P9-CMD-01`~`06` | `01, 04` 완료 · `05, 06`(Linux/Metal) 및 `02, 03` 잔여 |
| `v0.0.8` | Compute Pipeline·Storage Buffer/Texture(UAV)·Dispatch·Indirect/Bindless | `P9-CMP-01`, `P9-CMP-02` | 미완료 |
| `v0.0.9` | Frame Pacing·HDR/Color Space·Multi-Surface·Dirty Region 기반 | `P10-PRES-01`~`P10-DIRTY-01` | 미완료 |
| `v0.0.10` | 선택적 Render Graph·Native Interop·Zero-Copy Video·GPU Profiling/Diagnostics | `P9-RG-01`, `P11-*`, `P12-*` | 미완료 |

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
| Texture View (`createTextureView` SRV/RTV/DSV) | **물리 GPU + GBV + WARP** | **물리 GPU + 고정 VVL** | 구현(Linux CI 대기, `P8-TEX-05`) | 구현(macOS CI 대기, `P8-TEX-05`) |
| 동적 Viewport (`setViewport`) | **물리 GPU + GBV + WARP** | **물리 GPU + 고정 VVL** | 구현(Linux CI 대기, `P9-CMD-05`) | 구현(macOS CI 대기, `P9-CMD-05`) |
| 명시적 Command Pool·재사용 (`CommandPool`) | **물리 GPU + GBV + WARP** | **물리 GPU + 고정 VVL** | 구현(Linux CI 대기, `P9-CMD-06`) | 구현(macOS CI 대기, `P9-CMD-06`) |
| Depth/MRT·Clear/Load·Scissor·Alpha Blend | 물리 GPU + GBV + WARP | 물리 GPU + 고정 VVL | SW + 고정 VVL | 호스팅 가상 + Validation |
| Vertex/Index/Instance 입력·Binding Layout | 물리 GPU + GBV + WARP | 물리 GPU + 고정 VVL | SW + 고정 VVL | 호스팅 가상 + Validation |
| GPU Timestamp (2-slot) | 물리 GPU 검증 | 물리 GPU + 고정 VVL | SW + 고정 VVL | 미지원 (`Unsupported`) |
| Pipeline Cache (장치별 8-entry LRU) | SW(WARP) 검증 | SW/설치 소비자 검증 | SW 검증 | 호스팅 가상 검증 |
| OS Memory Budget 조회 (`queryMemoryBudget`) | 물리 GPU + GBV | 미지원 (`P3-CAP-04` 조건부) | 미지원 (`P3-CAP-04`) | 미지원 (`Unsupported`) |
| 장치 손실 복구 (`recover`) | 통제 RemoveDevice 검증 | 합성 손실 레이어 검증 | SW 합성 손실 검증 | 미지원 (`Unsupported`) |
| SwapChain / 화면 출력 | NVIDIA 수동 표시 수용 (DPI/TDR 대기) | NVIDIA 자동 검증 (Intel 미지원) | XCB Xvfb SW 검증 (수동 `P3-WSI-08` 대기) | CAMetalLayer 호스팅 검증 (물리 Mac 대기) |
| Compute / UAV / Render Graph / HDR / Interop | 미구현 (`P9`~`P11`) | 미구현 (`P9`~`P11`) | 미구현 (`P9`~`P11`) | 미구현 (`P9`~`P11`) |

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
| Upload Ring / Frame Context | `UploadRing`, `beginFrame`/`endFrame` | 저오버헤드 프레임 업로드 | CPU 링 모델(`P6-OPT-10`)·슬롯 기준선(`P6-OPT-11`) 완료 | `P6-OPT-08`, `P8-MEM-01` |
| GPU Timestamp (다중 구간) | `kMaxTimestampsPerFrame = 32` | GPU 프로파일링 | 2슬롯(0/1) 지원 · 다중 Scope 확장 필요 | `P11-PROF-01` |
| 포맷 확장 (`R8`, `RG8`, `sRGB`, `R16Uint`, `D24S8`) | `Format` 16종 + `formatInfo` | 폰트 마스크(R8)·sRGB | 4종(RGBA8/BGRA8/RGBA16F/D32F) | `P8-CAP-01`, `P8-TEX-01` |
| Multi-Surface / Low Latency | `ISwapChain` + `tearing` | 다중 독립 창·저지연 | 단일/다중 스왑체인 기반 · VRR/Frame Latency 미구현 | `P10-PRES-01`, `P10-SURF-01` |

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
| Phase 8·9 전송·Capability·View·Viewport·CommandPool | [PHASE8_BUFFER_REGION_REPORT.md](PHASE8_BUFFER_REGION_REPORT.md), [PHASE8_TEXTURE_REGION_REPORT.md](PHASE8_TEXTURE_REGION_REPORT.md), [PHASE8_CAPABILITY_REPORT.md](PHASE8_CAPABILITY_REPORT.md), [PHASE8_TEXTURE_VIEW_REPORT.md](PHASE8_TEXTURE_VIEW_REPORT.md), [PHASE9_VIEWPORT_REPORT.md](PHASE9_VIEWPORT_REPORT.md), [PHASE9_COMMAND_POOL_REPORT.md](PHASE9_COMMAND_POOL_REPORT.md) |
