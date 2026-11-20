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
