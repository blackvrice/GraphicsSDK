# AI 협업 작업 기록 (AI Work Log)

여러 AI 세션이 이전 작업을 보존하며 일관되게 개발을 이어가기 위한 시간순 기록이다.
실제 수행·검증한 사실만 남기며 미실행 항목을 완료로 기재하지 않는다.
현재 세션 인계 요약은 [AI_HANDOFF.md](AI_HANDOFF.md)를 확인한다.

---

## 2026-11-20 — 기준선 인수 (이전 AI 누적 완료분)

- **확인된 기준 HEAD**: `14dfba6` (`Document AI development instructions and handoff`), 직전 기능 커밋 `f5caaf9` (`Verify offscreen GPU workloads from Windows release ZIPs`)
- **누적 완료 상태**: `ROADMAP_TASKS.json` Phase 0~7 총 160개 ID 중 106개 완료, 54개 미완료 (최근 완료: `P7-PKG-09`~`15` 6개 프로필 배포 후보·DWARF 분리·공통 무결성 감사·Windows GPU 설치 소비자 smoke)
- **인수 시 확인된 사용자 미커밋 변경**: `M tests/VulkanAllocationBenchmarkTests.cpp` (LF/CRLF 차이), `?? .idea/editor.xml` — 소유권 보존 대상

---

## 2026-11-20 — 종합 고도화 로드맵 반영, 문서 정비 및 `copyBufferRegion` 구현·실기 검증

- **작업 목적**:
  1. 기존 AI 지시사항(`AGENTS.md`, `docs/AI_HANDOFF.md`), Git 상태, `docs/ROADMAP_TASKS.json`, 실제 소스·CI 제약 및 Sandbox RHI 참고 사본을 전수 감사.
  2. 기존 Phase 0~7(160개 ID)과 `tools/roadmap.py` 계약을 보존하면서 종합 고도화 요구사항을 Phase 8~12(30개 세부 ID)로 확장 등록.
  3. 사람이 빠르게 현황을 파악할 수 있도록 `README.md` 요약표, `docs/DEVELOPMENT_DASHBOARD.md`, `docs/AI_WORK_LOG.md`, `docs/AI_HANDOFF.md` 정리.
  4. 현재 환경에서 바로 구현·실기 검증 가능한 최우선 기능으로 오프셋 기반 버퍼 복사(`CommandList::copyBufferRegion`)를 D3D12·Vulkan·Metal 세 백엔드에 구현하고 Windows 실기 GPU·Validation·설치 소비자 검증 완료.
- **수행 내용**:
  - `include/graphics/CommandList.hpp`: `copyBufferRegion(source, sourceOffset, destination, destinationOffset, bytes)` 추가 (기존 `copyBuffer` 및 13개 공개 헤더 구성 유지).
  - `src/backends/d3d12/D3D12.cpp`: 오버플로 안전 범위·메모리·상태 사전 검증, 원자적 거부 후 `CopyBufferRegion` 기록.
  - `src/backends/vulkan/Vulkan.cpp`: 오버플로 안전 범위·메모리·상태 사전 검증, `uses.reserve` 원자적 거부, `VkBufferCopy{sourceOffset, destinationOffset, bytes}` 및 배리어 기록.
  - `src/backends/metal/Metal.cpp`: 4바이트 정렬 오프셋·크기는 `MTLBlitCommandEncoder`, 비정렬 오프셋/꼬리 바이트는 `gfx_copy_tail` 컴퓨트 커널(`srcOffset`, `dstOffset`, `count`)로 주변 바이트를 보존하며 복사하도록 구현.
  - `tests/BufferRegionTests.cpp`, `CMakeLists.txt`: 정렬·비정렬 오프셋, 가드 바이트 보존, upload-ring 스타일 8슬라이스 패킹 복사, 경계/오버플로/별칭/메모리 방향/상태/수명/원자적 거부/stale upload 테스트 추가 및 `if(GFX_ENABLE_VULKAN OR GFX_ENABLE_D3D12)` 블록에 등록(기존 `portable-core` 및 `macos-core.yml`의 11개 CPU 테스트/5개 CORE 유닛 목록과 충돌 없음 확인).
  - `tools/roadmap.py`, `docs/ROADMAP_TASKS.json`, `docs/ROADMAP.md`: Phase 8~12(30개 ID, 총 190개 ID) 추가 및 `P8-XFER-01/02/03` 완료 반영 (`P8-XFER-04`는 Linux/macOS 실행 대기로 `todo` 유지).
  - `README.md`, `docs/PUBLIC_API.md`, `docs/PHASE8_BUFFER_REGION_REPORT.md`, `docs/DEVELOPMENT_DASHBOARD.md`, `docs/AI_HANDOFF.md`, `docs/AI_WORK_LOG.md` 갱신.
- **설계 결정 및 주의사항**:
  - `tools/macos_metal_package.py`의 `HEADERS`(13개 공개 헤더)와 `tools/macos_core.py`의 `CORE`(5개 `.cpp`)·`TESTS`(11개 CPU 테스트)가 과거 검증 재현(`verify.py`)과 `macos-core.yml`에서 엄격히 검사되므로 새 공개 헤더 파일이나 새 `CORE` 번역 단위를 늘리지 않고 기존 헤더/백엔드 구조 안에서 확장했다.
  - 기존 사용자 미커밋 변경(`tests/VulkanAllocationBenchmarkTests.cpp`, `.idea/editor.xml`)은 건드리지 않고 보존했다.
- **테스트 내용과 결과** (격리 빌드 `out/p8-buffer-region`, 설치 `out/p8-buffer-region-install`, SDK 지문 `2316932de6cee5da52c8ed653ab48fd93b09e705b20ca2acb5400bd4f92e5a6a`):
  - MSVC 19.44.35228.0 / Windows SDK 10.0.26100.0 Release 빌드: 경고 0, 오류 0.
  - `gfx_buffer_region_tests --d3d12` (물리 GPU 2종: GTX 1660 SUPER, Intel UHD 770): 통과 (98 assertions, 48 copies, 38 rejections, 193,282 bytes).
  - `gfx_buffer_region_tests --d3d12 --validation` (Debug + GBV, 물리 GPU 2종): 통과 (100 assertions, validation 오류 0).
  - `gfx_buffer_region_tests --d3d12 --warp` (WARP 1종): 통과 (50 assertions, 24 copies, 19 rejections, 96,641 bytes).
  - `gfx_buffer_region_tests` (Windows Vulkan 물리 GPU 2종): 통과 (98 assertions, 48 copies, 38 rejections, 193,282 bytes).
  - `gfx_buffer_region_tests --validation` (고정 VVL 1.3.290 core+sync, 물리 GPU 2종): 통과 (100 assertions, validation 오류 0).
  - 독립 설치 소비자(`find_package(GraphicsSDK 0.0.1 EXACT CONFIG REQUIRED)`): D3D12 일반/Validation 및 Vulkan 일반/고정 VVL 모두 통과.
  - CTest 16종 회귀(`core_contracts`, `copy_atomicity_*`, `d3d12_*`, `vulkan_transfer`, `bgra_*`, `buffer_region_*` 등): 16/16 통과.
  - 기존 `out/core` 기준선 확인 시 기존에 알려진 로컬 `fence_wait_gpu`/`fence_wait_validation` 예외 2건은 이전 기록과 동일하게 관찰됨.
  - `tools/roadmap.py --check`: PASS (190 task IDs, 이력 SHA-256 일치).
  - `tools/phase3_comparison_report.py --check` 및 `benchmarks/reports/validation/2026-11-04-metal-package/verify.py`: 모두 PASS.
  - 전체 Python 계약 테스트 (`tools/check_python_contracts.py --output out/p8-python-contracts`): **1,546 tests · 4,005 subtests 통과** (실패·오류·건너뜀 0, `inputs_unchanged=true`).
- **남은 작업 / 다음 추천 작업**:
  - `P8-XFER-04`: Linux Vulkan 및 macOS Metal CI/실기에서 `copyBufferRegion` 검증.
  - `P8-XFER-05`: 부분 2D 텍스처 업로드/다운로드 (`copyBufferToTextureRegion` / `copyTextureToBufferRegion`).
  - `P8-CAP-01`: GPU 한도 및 포맷 지원 조회 API (`DeviceCaps` 확장·`queryFormatSupport`).
  - `P9-CMD-01`: 동적 `setViewport` 및 `CommandPool` 재사용.
  - 기존 Phase 7 흐름: 배포 후보 생성·설치 smoke CI 연동 (`P7-PKG-05/06/07` 준비).

---

## 2026-11-20 — `P8-XFER-05` 부분 2D 텍스처 업로드·다운로드 구현 및 실기 검증

- **기준 커밋**: `d58ecb5e` (`Add offset-aware copyBufferRegion and expand development roadmap and dashboard`)
- **작업 목적**:
  - 선행 충족 작업 `P8-XFER-05` 구현: 512바이트 정렬 `bufferOffset`, 256바이트 정렬 `bufferRowPitch` 및 2D 사각 영역 `(x, y, width, height)` 기반 부분 텍스처 복사 API(`TextureCopyRegion`, `computeTextureRegionFootprint`, `copyBufferToTextureRegion`, `copyTextureToBufferRegion`)를 세 백엔드에 구현하고 Windows 실기·Validation·설치 소비자로 검증.
  - macOS Metal 및 Linux Vulkan 실행 검증은 `P8-XFER-06`으로 정직하게 분리 유지.
- **수정 파일**:
  - `include/graphics/Types.hpp`, `include/graphics/RenderPass.hpp`, `include/graphics/CommandList.hpp`, `src/foundation/RenderPass.cpp`
  - `src/backends/d3d12/D3D12.cpp`, `src/backends/vulkan/Vulkan.cpp`, `src/backends/metal/Metal.cpp`
  - `tests/RenderPassTests.cpp`, `tests/TextureRegionTests.cpp`, `CMakeLists.txt`
  - `docs/PHASE8_TEXTURE_REGION_REPORT.md`, `docs/PUBLIC_API.md`, `docs/ROADMAP_TASKS.json`, `docs/ROADMAP.md`, `docs/DEVELOPMENT_DASHBOARD.md`, `README.md`, `docs/AI_HANDOFF.md`, `docs/AI_WORK_LOG.md`
- **테스트 내용과 결과** (격리 빌드 `out/p8-texture-region`, 설치 `out/p8-texture-region-install`, SDK 지문 `638800e78674a9bda385dfa04f11ae005dd226742a6944cf37881ae97fdb95e7`):
  - MSVC 19.44.35228.0 / Windows SDK 10.0.26100.0 Release 빌드 경고 0, 오류 0.
  - `gfx_texture_region_tests --d3d12` (물리 GPU 2종): 통과 (`assertions=340, region_uploads=20, region_downloads=14, rejections=52, verified_bytes=186400`).
  - `gfx_texture_region_tests --d3d12 --validation` (Debug + GBV, 2종): 통과 (`assertions=342`, validation 오류 `0`).
  - `gfx_texture_region_tests --d3d12 --warp` (WARP 1종): 통과 (`assertions=171, verified_bytes=93200`).
  - `gfx_texture_region_tests` (Windows Vulkan 2종): 통과 (`assertions=340, verified_bytes=186400`).
  - `gfx_texture_region_tests --validation` (고정 VVL 1.3.290, 2종): 통과 (`assertions=342`, validation 오류 `0`).
  - 설치 소비자(`find_package 0.0.1 EXACT`): D3D12 일반/GBV 및 Vulkan 일반/고정 VVL 4모드 모두 통과.
  - CTest 22종 회귀(`render_pass_contracts`, `render_pass_*`, `vulkan_texture_transfer`, `copy_atomicity_*`, `d3d12_*`, `bgra_*`, `buffer_region_*`, `texture_region_*` 등): 22/22 통과.
  - `tools/roadmap.py --check`: PASS (191 task IDs: 110 완료, 81 미완료).
- **다음 추천 작업**:
  - `P8-CAP-01`: GPU 한도 및 포맷 지원 조회 API (`DeviceCaps` 확장·`queryFormatSupport`).
  - `P9-CMD-01`: 동적 `setViewport` 및 `CommandPool` 재사용.
  - `P8-XFER-04`, `P8-XFER-06`: Linux Vulkan 및 macOS Metal CI/실기 검증.

---

## 2026-11-20 — `P8-CAP-01` GPU 한도 및 포맷 지원 조회 API 구현·실기 검증

- **기준 커밋**: `da18ac24` (`Add partial 2D texture region copy API and backend implementations`)
- **작업 목적**:
  - 선행 충족 작업 `P8-CAP-01` 구현: `DeviceCaps` 7개 유효 한도 필드 및 `FormatSupport` / `Device::queryFormatSupport(Format)`를 D3D12·Vulkan·Metal에 구현하고 Windows 실기·Validation·WARP·손실 복구·설치 소비자로 검증.
  - macOS Metal 및 Linux Vulkan 실행 검증은 `P8-CAP-02`로 정직하게 분리 유지.
- **수정 파일**:
  - `include/graphics/Types.hpp`, `include/graphics/Device.hpp`
  - `src/backends/d3d12/D3D12.cpp`, `src/backends/vulkan/Vulkan.cpp`, `src/backends/metal/Metal.cpp`
  - `tests/CapabilityTests.cpp`, `CMakeLists.txt`
  - `docs/PHASE8_CAPABILITY_REPORT.md`, `docs/PUBLIC_API.md`, `docs/ROADMAP_TASKS.json`, `docs/ROADMAP.md`, `docs/DEVELOPMENT_DASHBOARD.md`, `README.md`, `docs/AI_HANDOFF.md`, `docs/AI_WORK_LOG.md`
- **테스트 내용과 결과** (격리 빌드 `out/p8-capabilities`, 설치 `out/p8-capabilities-install`, SDK 지문 `0ccb4e60ce8c3ee5f2404509c8b310920fc019e01324b9170cd3eccd848094e2`):
  - MSVC 19.44.35228.0 / Windows SDK 10.0.26100.0 Release 빌드 경고 0, 오류 0.
  - `gfx_capability_tests --d3d12` (물리 GPU 2종): 통과 (`assertions=56, format_queries=8, rejections=4`).
  - `gfx_capability_tests --d3d12 --validation` (Debug + GBV, 2종): 통과 (`assertions=58`, validation 오류 `0`).
  - `gfx_capability_tests --d3d12 --warp` (WARP + `removeForTesting`/`recover`): 통과 (`assertions=32, format_queries=6, rejections=4`).
  - `gfx_capability_tests` (Windows Vulkan 2종): 통과 (`assertions=54, format_queries=8, rejections=4`).
  - `gfx_capability_tests --validation` (고정 VVL 1.3.290, 2종): 통과 (`assertions=56`, validation 오류 `0`).
  - 설치 소비자(`find_package 0.0.1 EXACT`): D3D12 하드웨어/GBV/WARP 및 Vulkan 하드웨어/고정 VVL 5모드 모두 통과.
  - CTest 20종 회귀(`core_contracts`, `adapter_*`, `d3d12_*`, `vulkan_transfer`, `buffer_region_*`, `texture_region_*`, `capabilities_*`): 20/20 통과.
  - `tools/roadmap.py --check`: PASS (192 task IDs: 111 완료, 81 미완료).
- **다음 추천 작업**:
  - `P9-CMD-01`: 동적 `setViewport` 및 `CommandPool` 재사용.
  - `P8-TEX-01`: Texture View 및 Subresource View.
  - `P9-CMP-01`: Compute Shader·UAV·Dispatch.
  - `P8-XFER-04/06`, `P8-CAP-02`: Linux Vulkan 및 macOS Metal CI/실기 검증.

---

## 2026-11-20 — `P9-CMD-04` 동적 Viewport 설정(`setViewport`) 구현 및 실기 검증

- **기준 커밋**: `04d9e4dd` (`Add GPU limits to DeviceCaps and Device::queryFormatSupport API`)
- **작업 목적**:
  - `P9-CMD-01`에서 독립 완료 가능한 동적 Viewport 슬라이스를 `P9-CMD-04`로 분리 구현: `Viewport` 구조체, 이식성 경계 검증 `validateViewport`, `CommandList::setViewport(const Viewport&)`를 D3D12·Vulkan(1.1 음수 높이 Y-flip)·Metal(`Draw`별 `MTLViewport` 스냅샷)에 구현하고 Windows 실기·Validation·WARP·설치 소비자로 검증.
  - Linux Vulkan 및 macOS Metal 실행 검증은 `P9-CMD-05`로 정직하게 분리 유지 (`P9-CMD-01`은 명시적 Command Pool·재사용 범위로 유지).
- **수정 파일 (18개)**:
  - `include/graphics/Types.hpp`, `include/graphics/RenderPass.hpp`, `include/graphics/CommandList.hpp`, `src/foundation/RenderPass.cpp`
  - `src/backends/d3d12/D3D12.cpp`, `src/backends/vulkan/Vulkan.cpp`, `src/backends/metal/Metal.cpp`
  - `tests/RenderPassTests.cpp`, `tests/ViewportTests.cpp`, `CMakeLists.txt`
  - `docs/PHASE9_VIEWPORT_REPORT.md`, `docs/PUBLIC_API.md`, `docs/ROADMAP_TASKS.json`, `docs/ROADMAP.md`, `docs/DEVELOPMENT_DASHBOARD.md`, `README.md`, `docs/AI_HANDOFF.md`, `docs/AI_WORK_LOG.md`
- **테스트 내용과 결과** (격리 빌드 `out/p9-viewport`, 설치 `out/p9-viewport-install`, SDK 지문 `214f02ce490172a3d1ad39c9a360ee1924e528788d30d829658dc75a232811bd`):
  - MSVC 19.44.35228.0 / Windows SDK 10.0.26100.0 Release 빌드 경고 0, 오류 0.
  - `gfx_viewport_tests --d3d12` (물리 GPU 2종: GTX 1660 SUPER, Intel UHD 770): 통과 (`assertions=57366, images=8, rejections=18, errors=0`).
  - `gfx_viewport_tests --d3d12 --validation` (Debug + GBV, 2종): 통과 (`assertions=57368, images=8, rejections=18, errors=0`).
  - `gfx_viewport_tests --d3d12 --warp` (WARP 1종): 통과 (`assertions=28684, images=4, rejections=9, errors=0`).
  - `gfx_viewport_tests` (Windows Vulkan 2종): 통과 (`assertions=57366, images=8, rejections=18, errors=0`).
  - `gfx_viewport_tests --validation` (고정 VVL 1.3.290, 2종): 통과 (`assertions=57368, images=8, rejections=18, errors=0`).
  - 설치 소비자(`find_package 0.0.1 EXACT`): D3D12 하드웨어/GBV/WARP 및 Vulkan 하드웨어/고정 VVL 5모드 모두 통과.
  - CTest 18종 회귀(`core_contracts`, `render_pass_contracts`, `render_pass_*`, `vulkan_draw`, `vulkan_depth`, `d3d12_*`, `capabilities_*`, `viewport_*`): 18/18 통과 (13.49s).
  - `tools/roadmap.py --check`: PASS (194 task IDs: 112 완료, 82 미완료 [필수 38, 조건부 8, 후속 36]).
- **다음 추천 작업**:
  - `P8-TEX-01`: Texture View 및 Subresource View.
  - `P9-CMP-01`: Compute Shader·UAV·Dispatch.
  - `P9-CMD-01`: 명시적 Command Pool 및 CommandList 재사용.
  - `P8-XFER-04/06`, `P8-CAP-02`, `P9-CMD-05`: Linux Vulkan 및 macOS Metal CI/실기 검증.

---

## 2026-11-20 — `P9-CMD-01` 명시적 Command Pool 및 CommandList 재사용 구현·실기 검증

- **기준 커밋**: `7defb926` (`Add dynamic setViewport API and multi-backend implementations`)
- **작업 목적**:
  - 선행 충족 작업 `P9-CMD-01` 구현: `CommandPool` (`Device::createCommandPool`, `createCommandList`, `reset`) 및 epoch 기반 `CommandList` 재사용·in-flight/상태 거부·미제출 참조 회수·조기 파괴 수명 계약을 D3D12·Vulkan(`vkResetCommandPool`/`vkFreeCommandBuffers`)·Metal에 구현하고 Windows 실기·Validation·WARP·손실복구·설치 소비자로 검증.
  - Linux Vulkan 및 macOS Metal 실행 검증은 `P9-CMD-06`으로 정직하게 분리 유지.
- **수정 파일 (15개)**:
  - `include/graphics/CommandList.hpp`, `include/graphics/Device.hpp`
  - `src/backends/d3d12/D3D12.cpp`, `src/backends/vulkan/Vulkan.cpp`, `src/backends/metal/Metal.cpp`
  - `tests/CommandPoolTests.cpp`, `CMakeLists.txt`
  - `docs/PHASE9_COMMAND_POOL_REPORT.md`, `docs/PUBLIC_API.md`, `docs/ROADMAP_TASKS.json`, `docs/ROADMAP.md`, `docs/DEVELOPMENT_DASHBOARD.md`, `README.md`, `docs/AI_HANDOFF.md`, `docs/AI_WORK_LOG.md`
- **테스트 내용과 결과** (격리 빌드 `out/p9-command-pool`, 설치 `out/p9-command-pool-install`, 설치 `GraphicsSDKCore.lib` SHA-256 `fe5576007709fa9ab5dc60c49c1e8698bcb0624ad554d22ea4feed79b67cb73d`):
  - MSVC 19.44.35228.0 / Windows SDK 10.0.26100.0 Release 빌드 경고 0, 오류 0.
  - `gfx_command_pool_tests --d3d12` (물리 GPU 2종): 통과 (`assertions=2390, epochs=64, submissions=140, rejections=216, inflight_reset_rejections=2, errors=0`).
  - `gfx_command_pool_tests --d3d12 --validation` (Debug + GBV, 2종): 통과 (`assertions=2624, epochs=64, submissions=144, rejections=214, errors=0`).
  - `gfx_command_pool_tests --d3d12 --warp` (WARP + `removeForTesting`/`recover`): 통과 (`assertions=1228, epochs=32, submissions=71, rejections=112, inflight_reset_rejections=1, errors=0`).
  - `gfx_command_pool_tests --vulkan` (Windows Vulkan 2종): 통과 (`assertions=2390, epochs=64, submissions=140, rejections=216, inflight_reset_rejections=2, errors=0`).
  - `gfx_command_pool_tests --vulkan --validation` (고정 VVL 1.3.290, 2종): 통과 (`assertions=2394, epochs=64, submissions=140, rejections=216, inflight_reset_rejections=2, errors=0`).
  - 설치 소비자(`find_package 0.0.1 EXACT`): D3D12 하드웨어/GBV/WARP 및 Vulkan 하드웨어/고정 VVL **5/5 모드 통과**.
  - CTest 34종 회귀(`core_contracts`, `render_pass_contracts`, `copy_atomicity_*`, `vulkan_transfer*`, `vulkan_draw*`, `vulkan_timestamps`, `buffer_region_*`, `texture_region_*`, `capabilities_*`, `viewport_*`, `command_pool_*`): **34/34 통과** (27.00s).
  - `tools/roadmap.py --check`: PASS (195 task IDs: 113 완료, 82 미완료 [필수 38, 조건부 8, 후속 36]).
- **다음 추천 작업**:
  - `P8-TEX-01`: Texture View 및 Subresource View.
  - `P9-CMP-01`: Compute Shader·UAV·Dispatch.
  - `P9-CMD-02`: 병렬 명령 기록·Thread-local Command Allocator 및 벤치마크.
  - `P8-XFER-04/06`, `P8-CAP-02`, `P9-CMD-05/06`: Linux Vulkan 및 macOS Metal CI/실기 검증.

---

## 2026-11-20 — `P8-TEX-01` Texture View 및 Subresource View (SRV/RTV/DSV) 구현·실기 검증

- **기준 커밋**: `60e8be8e` (`Add explicit CommandPool and reusable pooled CommandList API`)
- **작업 목적**:
  - 선행 충족 작업 `P8-TEX-01` 구현: `TextureViewUsage`, `TextureViewDesc`, `TextureView`, `validateTextureView`, `Device::createTextureView` 및 `ColorAttachment`/`DepthAttachment`/`TextureBinding`/`bindTextureView` 뷰 바인딩을 D3D12·Vulkan·Metal에 구현하고 CPU 계약·Windows 실기·Validation·WARP·손실복구·설치 소비자로 검증.
  - Linux Vulkan 및 macOS Metal 실행 검증은 `P8-TEX-05`로 정직하게 분리 유지.
- **수정 파일 (21개)**:
  - `include/graphics/Types.hpp`, `include/graphics/Resources.hpp`, `include/graphics/RenderPass.hpp`, `include/graphics/Bindings.hpp`, `include/graphics/CommandList.hpp`, `include/graphics/Device.hpp`
  - `src/foundation/RenderPass.cpp`, `src/backends/d3d12/D3D12.cpp`, `src/backends/vulkan/Vulkan.cpp`, `src/backends/metal/Metal.cpp`
  - `tests/RenderPassTests.cpp`, `tests/TextureViewTests.cpp`, `CMakeLists.txt`
  - `docs/PHASE8_TEXTURE_VIEW_REPORT.md`, `docs/PUBLIC_API.md`, `docs/ROADMAP_TASKS.json`, `docs/ROADMAP.md`, `docs/DEVELOPMENT_DASHBOARD.md`, `README.md`, `docs/AI_HANDOFF.md`, `docs/AI_WORK_LOG.md`
- **테스트 내용과 결과** (격리 빌드 `out/p8-texture-view`, 설치 `out/p8-texture-view-install`, 설치 `GraphicsSDKCore.lib` SHA-256 `c2341cba84da39505617e27aee9812a1f74cf634a32c20e8f0679590346fcda9`):
  - MSVC 19.44.35228.0 / Windows SDK 10.0.26100.0 Release 빌드 경고 0, 오류 0.
  - `gfx_render_tests --cpu`: 통과 (`65 assertions`).
  - `gfx_texture_view_tests --d3d12` (물리 GPU 2종): 통과 (`assertions=260, views=14, images=12, rejections=42, errors=0`).
  - `gfx_texture_view_tests --d3d12 --validation` (Debug + GBV, 2종): 통과 (`assertions=264, views=14, images=12, rejections=42, errors=0`).
  - `gfx_texture_view_tests --d3d12 --warp` (WARP + `removeForTesting`/`recover`): 통과 (`assertions=162, views=9, images=7, rejections=23, errors=0`).
  - `gfx_texture_view_tests --vulkan` (Windows Vulkan 2종): 통과 (`assertions=256, views=14, images=12, rejections=42, errors=0`).
  - `gfx_texture_view_tests --vulkan --validation` (고정 VVL 1.3.290, 2종): 통과 (`assertions=260, views=14, images=12, rejections=42, errors=0`).
  - 설치 소비자(`find_package 0.0.1 EXACT`): D3D12 하드웨어/GBV/WARP 및 Vulkan 하드웨어/고정 VVL **5/5 모드 통과**.
  - CTest 38종 회귀(`core_contracts`, `render_pass_contracts`, `vulkan_draw*`, `vulkan_resource_bindings`, `vulkan_binding_validation`, `vulkan_depth*`, `buffer_region_*`, `texture_region_*`, `capabilities_*`, `viewport_*`, `command_pool_*`, `texture_view_*`): **38/38 통과** (43.34s).
  - `tools/roadmap.py --check`: PASS (196 task IDs: 114 완료, 82 미완료 [필수 38, 조건부 8, 후속 36]).
- **다음 추천 작업**:
  - `P9-CMP-01`: Compute Shader·Compute Pipeline·Storage Buffer/Texture(UAV)·Dispatch.
  - `P8-TEX-02`: Texture1D/3D·Texture2DArray·Cubemap·Mipmap 지원.
  - `P9-CMD-02`: 병렬 명령 기록·Thread-local Command Allocator 및 벤치마크.
  - `P8-XFER-04/06`, `P8-CAP-02`, `P8-TEX-05`, `P9-CMD-05/06`: Linux Vulkan 및 macOS Metal CI/실기 검증.

---

## 2026-11-20 — `P9-CMP-01` Compute Shader·Compute Pipeline·Storage Buffer/Texture(UAV)·Dispatch 구현·실기 검증

- **기준 커밋**: `5383a566` (`Ignore .idea directory in .gitignore`, 직전 기능 `b309379e`)
- **작업 목적**:
  - 선행 충족 작업 `P9-CMP-01` 구현: `ShaderStage::Compute`(`cs_5_1`/`cs_6_0`/SPIR-V 1.3 `GLCompute`/MSL `gfx_compute`), `ComputePipelineDesc`, `Device::createComputePipeline`, `StorageBuffer`/`StorageTexture2D`(`TextureViewUsage::Storage` 활성화), `ResourceState::UnorderedAccess`, `setComputePipeline`/`bindComputeResources`/`dispatch`/`uavBarrier`, `FormatSupport::storage` 및 `caps.compute = true`를 D3D12·Vulkan·Metal에 구현하고 Windows 실기·Validation·WARP·손실복구·설치 소비자로 검증.
  - Linux Vulkan 및 macOS Metal 실행 검증은 `P9-CMP-03`으로 정직하게 분리 유지.
- **수정 파일 (30개)**:
  - `include/graphics/Types.hpp`, `include/graphics/Shader.hpp`, `include/graphics/Bindings.hpp`, `include/graphics/Pipeline.hpp`, `include/graphics/Device.hpp`, `include/graphics/CommandList.hpp`
  - `src/foundation/Bindings.cpp`, `src/foundation/RenderPass.cpp`, `src/shader/Shader.cpp`, `src/backends/vulkan/Spirv.hpp`
  - `src/backends/d3d12/D3D12.cpp`, `src/backends/vulkan/Vulkan.cpp`, `src/backends/metal/Metal.cpp`, `src/backends/metal/MetalGraphics.hpp`
  - `tests/fixtures/ComputeFixtures.hpp`, `tests/ComputeTests.cpp`, `tests/CoreTests.cpp`, `tests/VulkanTests.cpp`, `tests/MetalSubmissionTests.cpp`, `tests/RenderPassTests.cpp`, `tests/TextureViewTests.cpp`, `CMakeLists.txt`
  - `docs/PHASE9_COMPUTE_REPORT.md`, `docs/PUBLIC_API.md`, `docs/ROADMAP_TASKS.json`, `docs/ROADMAP.md`, `docs/DEVELOPMENT_DASHBOARD.md`, `README.md`, `docs/AI_HANDOFF.md`, `docs/AI_WORK_LOG.md`
- **테스트 내용과 결과** (격리 빌드 `out/p9-compute`, 설치 `out/p9-compute-install`, 설치 `GraphicsSDKCore.lib` SHA-256 `13099ff5ee4e774dc2c3e3973fdf3839415d2a1b30e0bd02a73ee30d171c19b5`):
  - SPIR-V 1.3 Compute 픽스처 2종 Khronos SPIRV-Tools Vulkan 1.1 검증 PASS (`buffer_scale_add` 195 words, `texture_generate` 237 words).
  - MSVC 19.44.35228.0 / Windows SDK 10.0.26100.0 Release 빌드 경고 0, 오류 0.
  - CPU 계약 5종(`core_contracts`, `render_pass_contracts`, `binding_layout_contracts`, `spirv_interface_contracts`, `spirv_binding_contracts`) PASS.
  - `gfx_compute_tests --d3d12` (물리 GPU 2종): 통과 (`assertions=234, dispatches=10, images=4, rejections=46, errors=0`).
  - `gfx_compute_tests --d3d12 --validation` (Debug + GBV, 2종): 통과 (`assertions=238, dispatches=10, images=4, rejections=46, errors=0`).
  - `gfx_compute_tests --d3d12 --warp` (WARP + `removeForTesting`/`recover`): 통과 (`assertions=156, dispatches=6, images=2, rejections=25, errors=0`).
  - `gfx_compute_tests --vulkan` (Windows Vulkan 2종): 통과 (`assertions=226, dispatches=10, images=4, rejections=46, errors=0`).
  - `gfx_compute_tests --vulkan --validation` (고정 VVL 1.3.290 core+sync, 2종): 통과 (`assertions=230, dispatches=10, images=4, rejections=46, errors=0`).
  - 설치 소비자(`find_package 0.0.1 EXACT`): D3D12 하드웨어/GBV/WARP 및 Vulkan 하드웨어/고정 VVL **5/5 모드 통과**.
  - CTest 34종 회귀: **34/34 통과** (33.62s).
  - `tools/roadmap.py --check`: PASS (197 task IDs: 115 완료, 82 미완료 [필수 38, 조건부 8, 후속 36]).
- **다음 추천 작업**:
  - `P8-TEX-02`: Texture1D/3D·Texture2DArray·Cubemap·Mipmap 지원.
  - `P9-CMD-02`: 병렬 명령 기록·Thread-local Command Allocator 및 벤치마크.
  - `P8-TEX-03`: MSAA 텍스처 및 Resolve 경로.
  - `P8-XFER-04/06`, `P8-CAP-02`, `P8-TEX-05`, `P9-CMD-05/06`, `P9-CMP-03`: Linux Vulkan 및 macOS Metal CI/실기 검증.

---

## 2026-11-20 — `P8-TEX-03` MSAA 텍스처 및 Resolve 경로 구현·실기 검증

- **기준 커밋**: `79f0b85d` (`Add Compute pipeline, UAV storage resources, and dispatch API`)
- **작업 목적**:
  - 선행 충족 작업 `P8-TEX-03` 구현: `FormatSupport::sampleCounts`, `TextureDesc::sampleCount`, `RenderTargetLayout::sampleCount`, `ResourceState::ResolveSource`/`ResolveDestination`, `CommandList::resolveTexture` 및 MSAA RTV/DSV `TextureView`를 D3D12·Vulkan·Metal에 구현하고 CPU 계약 및 Windows 실기·Validation·WARP 손실복구·설치 소비자로 검증.
  - Linux Vulkan 및 macOS Metal 실행 검증은 `P8-TEX-06`으로 정직하게 분리 유지.
- **수정 파일 (19개)**:
  - `include/graphics/Types.hpp`, `include/graphics/RenderPass.hpp`, `include/graphics/CommandList.hpp`, `src/foundation/RenderPass.cpp`
  - `src/backends/d3d12/D3D12.cpp`, `src/backends/vulkan/Vulkan.cpp`, `src/backends/metal/Metal.cpp`, `src/backends/metal/MetalGraphics.hpp`
  - `tests/RenderPassTests.cpp`, `tests/MsaaTests.cpp`, `CMakeLists.txt`
  - `docs/PHASE8_MSAA_REPORT.md`, `docs/PUBLIC_API.md`, `docs/ROADMAP_TASKS.json`, `docs/ROADMAP.md`, `docs/DEVELOPMENT_DASHBOARD.md`, `README.md`, `docs/AI_HANDOFF.md`, `docs/AI_WORK_LOG.md`
- **테스트 내용과 결과** (격리 빌드 `out/p8-msaa`, 설치 `out/p8-msaa-install`, 설치 `GraphicsSDKCore.lib` SHA-256 `3b66ca94594f06685f16722356b9f190d5302b8ba5c78e04d5cd3f6e79f5283a`):
  - MSVC 19.44.35228.0 / Windows SDK 10.0.26100.0 Release 빌드 경고 0, 오류 0.
  - CPU 계약 `render_pass_contracts`: PASS (`79 assertions`).
  - `gfx_msaa_tests --d3d12` (물리 GPU 2종): 통과 (`assertions=7856, resolves=14, images=16, rejections=58, errors=0`).
  - `gfx_msaa_tests --d3d12 --validation` (Debug + GBV, 2종): 통과 (`assertions=7858, resolves=14, images=16, rejections=58, errors=0`).
  - `gfx_msaa_tests --d3d12 --warp` (WARP + `removeForTesting`/`recover`): 통과 (`assertions=7859, resolves=14, images=16, rejections=60, errors=0`).
  - `gfx_msaa_tests --vulkan` (Windows Vulkan 2종): 통과 (`assertions=7854, resolves=14, images=16, rejections=58, errors=0`).
  - `gfx_msaa_tests --vulkan --validation` (고정 VVL 1.3.290 core+sync, 2종): 통과 (`assertions=7856, resolves=14, images=16, rejections=58, errors=0`).
  - 설치 소비자(`find_package 0.0.1 EXACT`): D3D12 하드웨어/GBV/WARP 및 Vulkan 하드웨어/고정 VVL **5/5 모드 통과** (`errors=0`).
  - CTest 47종 회귀(CPU 11종 + Phase 8/9 + Vulkan 코어): **47/47 통과** (`43.16s`).
  - `tools/roadmap.py --check`: PASS (198 task IDs: 116 완료, 82 미완료 [필수 38, 조건부 8, 후속 36]).
- **다음 추천 작업**:
  - `P9-CMD-02`: 병렬 명령 기록·Thread-local Command Allocator 및 벤치마크.
  - `P8-TEX-02`: Texture1D/3D·Texture2DArray·Cubemap·Mipmap 지원.
  - `P8-MEM-01`: 공개 Upload Ring·Staging/Readback/Transient Resource Pool.
  - `P8-XFER-04/06`, `P8-CAP-02`, `P8-TEX-05/06`, `P9-CMD-05/06`, `P9-CMP-03`: Linux Vulkan 및 macOS Metal CI/실기 검증.

---

## 2026-11-20 — `P9-CMD-02` 병렬 명령 기록·Thread-local Command Allocator 및 벤치마크 구현·실기 검증

- **기준 커밋**: `ffc0d00e` (`Add MSAA texture attachments and explicit resolveTexture API`)
- **작업 목적**:
  - 선행 충족 작업 `P9-CMD-02` 구현: `CommandPool::bindRecordingThread()` 스레드 로컬 기록 친화성과 `CommandQueue::submit(std::span<CommandList* const>)` 단일 일괄 제출 및 교차 리스트 읽기/변경 충돌 원자적 거부를 D3D12·Vulkan·Metal에 구현하고 Windows 실기·Validation·WARP 손실복구·설치 소비자 및 1/2/4스레드 CPU 기록 벤치마크로 검증.
  - Linux Vulkan 및 macOS Metal 실행 검증은 `P9-CMD-07`로 정직하게 분리 유지.
- **수정 파일 (15개)**:
  - `include/graphics/CommandList.hpp`, `include/graphics/CommandQueue.hpp`
  - `src/backends/d3d12/D3D12.cpp`, `src/backends/vulkan/Vulkan.cpp`, `src/backends/metal/Metal.cpp`
  - `tests/ParallelRecordingTests.cpp`, `CMakeLists.txt`
  - `docs/PHASE9_PARALLEL_RECORDING_REPORT.md`, `docs/PUBLIC_API.md`, `docs/ROADMAP_TASKS.json`, `docs/ROADMAP.md`, `docs/DEVELOPMENT_DASHBOARD.md`, `README.md`, `docs/AI_HANDOFF.md`, `docs/AI_WORK_LOG.md`
- **테스트 및 벤치마크 결과** (격리 빌드 `out/p9-parallel`, 설치 `out/p9-parallel-install`, 설치 `GraphicsSDKCore.lib` SHA-256 `cc60c58a4dcb4aaf4879372553bea53d634ba7d0a590cf711a76fdde1ae64440`):
  - MSVC 19.44.35228.0 / Windows SDK 10.0.26100.0 Release 빌드 경고 0, 오류 0.
  - `gfx_parallel_recording_tests --d3d12` (물리 GPU 2종): 통과 (`assertions=114, batches=16, images=48, rejections=14, errors=0`).
  - `gfx_parallel_recording_tests --d3d12 --validation` (Debug + GBV, 2종): 통과 (`assertions=116, batches=16, images=48, rejections=14, errors=0`).
  - `gfx_parallel_recording_tests --d3d12 --warp` (WARP + `removeForTesting`/`recover`): 통과 (`assertions=116, batches=16, images=48, rejections=15, errors=0`).
  - `gfx_parallel_recording_tests --vulkan` (Windows Vulkan 2종): 통과 (`assertions=114, batches=16, images=48, rejections=14, errors=0`).
  - `gfx_parallel_recording_tests --vulkan --validation` (고정 VVL 1.3.290 core+sync, 2종): 통과 (`assertions=116, batches=16, images=48, rejections=14, errors=0`).
  - 설치 소비자(`find_package 0.0.1 EXACT`): D3D12 하드웨어/GBV/WARP 및 Vulkan 하드웨어/고정 VVL **5/5 모드 통과** (`errors=0`).
  - CTest 52종 회귀(CPU 11종 + `command_pool_*` + Phase 8/9 + Vulkan 코어): **52/52 통과** (`51.22s`).
  - **1 vs 2 vs 4 스레드 실측 벤치마크** (GTX 1660 SUPER, 16 lists × 1,024 draws = 16,384 draws/frame, 10 frames, 16/16 타깃 전체 픽셀 검증):
    - D3D12 CPU 기록 p50: 1T `1.903 ms` → 2T `0.983 ms` (**1.936x**) → 4T `0.493 ms` (**3.858x**); submit+wait p50 `0.192~0.241 ms`.
    - Vulkan CPU 기록 p50: 1T `1.529 ms` → 2T `0.803 ms` (**1.903x**) → 4T `0.430 ms` (**3.552x**); submit+wait p50 `0.480~0.533 ms`.
  - `tools/roadmap.py --check`: PASS (199 task IDs: 117 완료, 82 미완료 [필수 38, 조건부 8, 후속 36]).
- **다음 추천 작업**:
  - `P8-TEX-02`: Texture1D/3D·Texture2DArray·Cubemap·Mipmap 지원.
  - `P9-CMD-03`: Graphics/Compute/Copy 다중 큐·Timeline Fence·Async 전송.
  - `P8-MEM-01`: 공개 Upload Ring·Staging/Readback/Transient Resource Pool.
  - `P8-XFER-04/06`, `P8-CAP-02`, `P8-TEX-05/06`, `P9-CMD-05/06/07`, `P9-CMP-03`: Linux Vulkan 및 macOS Metal CI/실기 검증.

---

## 2026-11-20 — `P9-CMD-03` Graphics/Compute/Copy 다중 큐·Timeline Fence·Async 전송 구현·실기 검증

- **기준 커밋**: `46919a13` (`Add parallel command recording and batch queue submission`)
- **작업 목적**:
  - 선행 충족 작업 `P9-CMD-03` 구현: `QueueType(Graphics/Compute/Copy)`, `DeviceCaps::{dedicatedComputeQueue,dedicatedCopyQueue,timelineFences}`, `Device::{queue(QueueType),createCommandList(QueueType),createCommandPool(QueueType),createTimelineFence}`, `TimelineFence(completedValue/signal/wait)`, `CommandQueue::submit(const SubmitDesc&)`를 D3D12·Vulkan·Metal에 구현하고 Windows 실기(GTX 1660 SUPER + Intel UHD 770)·Validation·WARP 손실복구·설치 소비자 및 Async Transfer + Compute Overlap 벤치마크로 검증.
  - Linux Vulkan 및 macOS Metal 실행 검증은 `P9-CMD-08`로 정직하게 분리 유지.
- **수정 파일 (17개)**:
  - `include/graphics/Types.hpp`, `include/graphics/CommandList.hpp`, `include/graphics/CommandQueue.hpp`, `include/graphics/Device.hpp`
  - `src/backends/d3d12/D3D12.cpp`, `src/backends/vulkan/Vulkan.cpp`, `src/backends/metal/Metal.cpp`
  - `tests/MultiQueueTests.cpp`, `CMakeLists.txt`
  - `docs/PHASE9_MULTI_QUEUE_REPORT.md`, `docs/PUBLIC_API.md`, `docs/ROADMAP_TASKS.json`, `docs/ROADMAP.md`, `docs/DEVELOPMENT_DASHBOARD.md`, `README.md`, `docs/AI_HANDOFF.md`, `docs/AI_WORK_LOG.md`
- **테스트 및 벤치마크 결과** (격리 빌드 `out/p9-multi-queue`, 설치 `out/p9-multi-queue-install`, 설치 `GraphicsSDKCore.lib` SHA-256 `6A4DD16C87BE7C08D70B846392525BE838893FDBA09AE60BD67F1B534B67B6FC`):
  - MSVC 19.44.35228.0 / Windows SDK 10.0.26100.0 Release 빌드 경고 0, 오류 0, 13개 공개 헤더 계약 유지.
  - `gfx_multi_queue_tests --d3d12 --benchmark` (GTX 1660 SUPER, `1/1/1`): 통과 (`assertions=5268, pipelines=3, submits=31, rejections=47`).
  - `gfx_multi_queue_tests --d3d12 --validation` (Debug + GBV, GTX 1660 SUPER): 통과 (`assertions=456, errors=0`; Copy 큐 `COMMON` 암묵적 승격/감쇠 반영으로 `#1334` 0건).
  - `gfx_multi_queue_tests --d3d12 --warp` (WARP + `removeForTesting`/`recover`): 통과 (`assertions=566, pipelines=5, submits=38, rejections=52`).
  - `gfx_multi_queue_tests --vulkan --benchmark` (GTX 1660 SUPER, 3개 패밀리 `CONCURRENT`, `1/1/1`): 통과 (`assertions=5259, pipelines=3, submits=31, rejections=47`).
  - `gfx_multi_queue_tests --vulkan --validation` (고정 VVL 1.3.290 core+sync, GTX 1660 SUPER): 통과 (`assertions=448, errors=0`).
  - 설치 소비자(`find_package 0.0.1 EXACT`): D3D12(GTX 1660 SUPER·Intel UHD 770·WARP) 및 Vulkan(GTX 1660 SUPER 3패밀리 `1/1/1`·Intel UHD 770 공유 폴백 `0/0/1`) 비검증·검증 별도 프로세스 전체 통과 (`errors=0`).
  - CTest 68종 회귀(CPU 11종 + Phase 8/9 전체 + `multi_queue_*` 5종 + Vulkan 코어/타임스탬프/검증): **68/68 통과** (`52.49s`).
  - **Async Transfer + Compute Overlap 실측 벤치마크** (GTX 1660 SUPER, 12 frames × 4 MiB = 48 MiB + 프레임당 4 Compute UAV 패스, 7회 중앙값):
    - D3D12: 순차 `10.925 ms` → Async Overlap `8.237 ms` (**1.326x**).
    - Vulkan: 순차 `9.400 ms` → Async Overlap `7.624 ms` (**1.233x**).
  - `tools/roadmap.py --check`: PASS (200 task IDs: 118 완료, 82 미완료 [필수 38, 조건부 8, 후속 36]).
- **다음 추천 작업**:
  - `P8-TEX-02`: Texture1D/3D·Texture2DArray·Cubemap·Mipmap 지원.
  - `P8-MEM-01`: 공개 Upload Ring·Staging/Readback/Transient Resource Pool.
  - `P9-CMP-02`: Async Compute·Indirect/MDI·Bindless·GPU Culling.
  - `P9-RG-01`: 선택적 Render Graph.
  - `P8-XFER-04/06`, `P8-CAP-02`, `P8-TEX-05/06`, `P9-CMD-05/06/07/08`, `P9-CMP-03`: Linux Vulkan 및 macOS Metal CI/실기 검증.

---

## 2026-11-20 — `P8-MEM-01` 공개 Upload Ring·Readback/Transient Resource Pool 구현·실기 검증

- **기준 커밋**: `c8942feb` (`Add multi-queue separation, TimelineFence, and async transfer support`)
- **작업 목적**:
  - 하위 4개 작업(`P8-MEM-02`, `P9-RG-01`, `P11-PROF-01`, `P12-SBX-01`)을 여는 `P8-MEM-01` 구현: 기존 13개 공개 헤더(`Resources.hpp`/`Device.hpp`)와 공통 `Backend.cpp`(5개 CORE 유닛 유지)에 Fence 회수형 `UploadRing`, `ReadbackPool`, `TransientResourcePool` + `BudgetPressurePolicy`를 구현하고 Windows 실기(GTX 1660 SUPER + Intel UHD 770)·Validation·WARP 소멸/손실복구·설치 소비자 및 24프레임 동일 조건 전후 실측으로 검증.
  - Linux Vulkan 및 macOS Metal 실행 검증은 `P8-MEM-03`으로 정직하게 분리 유지.
- **수정 파일 (14개)**:
  - `include/graphics/Resources.hpp`, `include/graphics/Device.hpp`
  - `src/foundation/Backend.cpp`, `src/backends/d3d12/D3D12.cpp`
  - `tests/MemoryPoolTests.cpp`, `CMakeLists.txt`
  - `docs/PHASE8_MEMORY_POOL_REPORT.md`, `docs/PUBLIC_API.md`, `docs/ROADMAP_TASKS.json`, `docs/ROADMAP.md`, `docs/DEVELOPMENT_DASHBOARD.md`, `README.md`, `docs/AI_HANDOFF.md`, `docs/AI_WORK_LOG.md`
- **테스트 및 벤치마크 결과** (격리 빌드 `out/p8-memory-pool`, 설치 `out/p8-memory-pool-install`, 설치 `GraphicsSDKCore.lib` SHA-256 `4F34C4C28073BE5007C44B1A553474895F49EA7A8CEA5FADEFADA66B69A8052E`):
  - MSVC 19.44.35228.0 / Windows SDK 10.0.26100.0 Release 빌드 경고 0, 오류 0, 13개 공개 헤더 계약 유지.
  - `gfx_memory_pool_tests --d3d12 --benchmark` (GTX 1660 SUPER): 통과 (`assertions=18129, frames=10, rejections=73`).
  - `gfx_memory_pool_tests --d3d12 --validation` (Debug + GBV, GTX 1660 SUPER): 통과 (`assertions=385, errors=0`; D3D12 Copy 큐 동일 리스트 재승격 보강 포함).
  - `gfx_memory_pool_tests --d3d12 --warp` (WARP 소멸 순서 + `removeForTesting`/`recover`): 통과 (`assertions=754, frames=20, rejections=137`).
  - `gfx_memory_pool_tests --vulkan --benchmark` (GTX 1660 SUPER): 통과 (`assertions=18129, frames=10, rejections=73`).
  - `gfx_memory_pool_tests --vulkan --validation` (고정 VVL 1.3.290 core+sync, GTX 1660 SUPER): 통과 (`assertions=385, errors=0`).
  - 설치 소비자(`find_package 0.0.1 EXACT`): D3D12(GTX 1660 SUPER·Intel UHD 770·WARP) 및 Vulkan(GTX 1660 SUPER·Intel UHD 770) 비검증·검증 전체 통과 (`errors=0`).
  - CTest 73종 회귀(CPU 11종 + Phase 8/9 전체 + `memory_pool_*` 5종 + `multi_queue_*` 5종 + Vulkan 코어/타임스탬프/검증): **73/73 통과** (`47.21s`).
  - **24프레임 동일 조건 전후 실측 비교** (GTX 1660 SUPER, 16×16 KiB 업로드 + 256×256 RT Clear/복사 + Readback, 5회 중앙값):
    - D3D12: Wall `107.112 ms` → **`7.833 ms`** (**13.675x**), GPU `0.932 vs 0.926 ms`, 피크 고유 자원 `20 → 7`, 풀 적중 `46/48`.
    - Vulkan: Wall `71.534 ms` → **`7.217 ms`** (**9.912x**), GPU `2.097 vs 2.067 ms`, 피크 고유 자원 `19 → 6`, 풀 적중 `46/48`.
  - `tools/roadmap.py --check`: PASS (201 task IDs: 119 완료, 82 미완료 [필수 38, 조건부 8, 후속 36]).
- **다음 추천 작업**:
  - `P8-TEX-02`: Texture1D/3D·Texture2DArray·Cubemap·Mipmap 지원.
  - `P9-RG-01`: 선택적 Render Graph.
  - `P9-CMP-02`: Async Compute·Indirect/MDI·Bindless·GPU Culling.
  - `P11-PROF-01`: GPU Profiling 확장.
  - `P8-XFER-04/06`, `P8-CAP-02`, `P8-TEX-05/06`, `P8-MEM-03`, `P9-CMD-05/06/07/08`, `P9-CMP-03`: Linux Vulkan 및 macOS Metal CI/실기 검증.

---

## 2026-11-20 — `P9-RG-01` 선택적 Render Graph 구현·실기 검증 및 수동 배리어 대비 성능 비교

- **기준 커밋**: `e1c7f62f` (`Add UploadRing, ReadbackPool, and TransientResourcePool with budget pressure policy`)
- **작업 목적**:
  - 선행 충족 작업 `P9-RG-01` 구현: 기존 직접 `CommandList` API, 13개 공개 헤더(`Device.hpp`), 5개 `CORE` `.cpp`(`Backend.cpp`) 및 Vulkan 기본 전용 할당 계약을 유지한 채 상위 선택 모듈 `gfx::RenderGraph`를 구현하고 Windows 실기(GTX 1660 SUPER + Intel UHD 770)·Validation(D3D12 Debug+GBV / 고정 VVL 1.3.290 core+sync 오류 0)·WARP 손실복구·설치 소비자 전체 어댑터 및 24프레임 수동 배리어 대비 성능 비교로 검증.
  - Linux Vulkan 및 macOS Metal 실행 검증은 `P9-RG-02`로 정직하게 분리 유지.
- **수정 파일 (12개)**:
  - `include/graphics/Device.hpp`, `src/foundation/Backend.cpp`
  - `tests/RenderGraphTests.cpp`, `CMakeLists.txt`
  - `docs/PHASE9_RENDER_GRAPH_REPORT.md`, `docs/PUBLIC_API.md`, `docs/ROADMAP_TASKS.json`, `docs/ROADMAP.md`, `docs/DEVELOPMENT_DASHBOARD.md`, `README.md`, `docs/AI_HANDOFF.md`, `docs/AI_WORK_LOG.md`
- **테스트 및 벤치마크 결과** (격리 빌드 `out/p9-render-graph`, 설치 `out/p9-render-graph-install`, 설치 `GraphicsSDKCore.lib` SHA-256 `D206BD2E35C6478FEB3361845C477348F5CA178869B412CD2DA927EA32DF321E`):
  - MSVC 19.44.35228.0 / Windows SDK 10.0.26100.0 Release 전체 타겟 빌드 경고 0, 오류 0, 13개 공개 헤더 계약 유지.
  - `gfx_render_graph_tests --d3d12 --benchmark` (GTX 1660 SUPER): 통과 (`assertions=9116, graphs_compiled=6, graphs_executed=6, rejections=20`).
  - `gfx_render_graph_tests --d3d12 --validation` (Debug + GBV, GTX 1660 SUPER): 통과 (`assertions=175, val_errors=0`).
  - `gfx_render_graph_tests --d3d12 --warp` (WARP `removeForTesting` → `DeviceLost` → `recover()`): 통과 (`assertions=178, rejections=21`).
  - `gfx_render_graph_tests --vulkan --benchmark` (GTX 1660 SUPER): 통과 (`assertions=9109, graphs_compiled=6, graphs_executed=6, rejections=20`).
  - `gfx_render_graph_tests --vulkan --validation` (고정 VVL 1.3.290 core+sync, GTX 1660 SUPER): 통과 (`assertions=171, val_errors=0`; GPU-to-GPU 타임라인 `syncTimelines`와 큐별 호스트 완료 타임라인 `hostTimelines` 분리로 반복 프레임 SyncVal 호환성 확보).
  - 설치 소비자(`find_package(GraphicsSDK 0.0.1 EXACT CONFIG REQUIRED)`): D3D12(GTX 1660 SUPER·Intel UHD 770·WARP) 및 Vulkan(GTX 1660 SUPER·Intel UHD 770 공유 큐 폴백) 비검증·검증 9/9 전체 통과 (`val_errors=0`).
  - 전체 CTest 회귀: **127/127 통과** (`179.26s`, 실패·건너뜀 0).
  - **수동 배리어 대비 성능 비교** (GTX 1660 SUPER, 24프레임 4단계 복합 Copy+Compute+Graphics+Copy, Warmup 2 + Measured 5 중앙값, `TransientResourcePool` `hits=1005, misses=3`):
    - D3D12: 수동 `59.149 ms` vs RenderGraph precompiled **`57.328 ms`** (**`-3.08%`**) / 매프레임 재구축 `65.953 ms` (`+11.50%`), 평균 `compile()` **`9.473 µs`**.
    - Vulkan: 수동 `43.684 ms` vs RenderGraph precompiled **`42.800 ms`** (**`-2.02%`**) / 매프레임 재구축 `45.483 ms` (`+4.12%`), 평균 `compile()` **`7.857 µs`**.
  - `tools/roadmap.py --check`: PASS (202 task IDs: 120 완료, 82 미완료 [필수 38, 조건부 8, 후속 36]).
- **다음 추천 작업**:
  - `P8-TEX-02`: Texture1D/3D·Texture2DArray·Cubemap·Mipmap 지원.
  - `P11-PROF-01`: GPU Profiling 확장 (다중 Timestamp Scope·카운터·Stall 진단).
  - `P9-CMP-02`: Async Compute·Indirect/MDI·Bindless·GPU Culling.
  - `P10-PRES-01`: Frame Pacing·Present Mode·VRR.
  - `P8-XFER-04/06`, `P8-CAP-02`, `P8-TEX-05/06`, `P8-MEM-03`, `P9-CMD-05/06/07/08`, `P9-CMP-03`, `P9-RG-02`: Linux Vulkan 및 macOS Metal CI/실기 검증.
