# Phase 9 동적 Viewport 설정(`P9-CMD-04`) 검증 보고서

- 작성일: 2026-11-20
- 완료 작업 ID: `P9-CMD-04` (API·CPU 검증·D3D12·Windows Vulkan 구현/실기/Validation/WARP/설치 소비자 검증 및 Metal 코드 반영)
- 미완료 후속 ID: `P9-CMD-05` (Linux Vulkan 및 macOS Metal 실기/CI 검증 — 구현 반영 완료, 해당 OS 실행 대기), `P9-CMD-01` (명시적 Command Pool·재사용)
- SDK 버전: `0.0.1` (`v0.0.7` 명령 시스템 확장 선행 슬라이스)
- 검증한 SDK 소스 지문(`gfx::buildFingerprint()`): `214f02ce490172a3d1ad39c9a360ee1924e528788d30d829658dc75a232811bd`

## 개발 목표와 변경 이유

기존 `beginRenderPass`는 항상 첨부 전체 크기와 `[0, 1]` 깊이 범위로 뷰포트를 고정했다.
GraphicsUI 분할 영역 렌더링, Dirty Region 렌더링(`P10-DIRTY-01`), Sandbox RHI(`ICommandList::setViewport`) 호환을 위해 렌더 패스 내 동적 `CommandList::setViewport(Viewport)`와 휴대용 `validateViewport`를 추가했다.

## 변경된 API 및 수정 파일

| 항목 | 경로 | 변경 내용 |
| --- | --- | --- |
| 공개 타입·API | [Types.hpp](../include/graphics/Types.hpp), [RenderPass.hpp](../include/graphics/RenderPass.hpp), [CommandList.hpp](../include/graphics/CommandList.hpp) | `Viewport{x, y, width, height, minDepth, maxDepth}`, `validateViewport`, `CommandList::setViewport(const Viewport&)` 추가 (기존 13개 공개 헤더 유지) |
| 공통 Foundation | [RenderPass.cpp](../src/foundation/RenderPass.cpp) | 유한 값, 양수 크기, 타깃 경계 내 포함(`double` 합산), `0 <= minDepth <= maxDepth <= 1` 검증 구현 |
| D3D12 백엔드 | [D3D12.cpp](../src/backends/d3d12/D3D12.cpp) | 활성 렌더 패스 검증 후 `RSSetViewports` 호출 |
| Vulkan 백엔드 | [Vulkan.cpp](../src/backends/vulkan/Vulkan.cpp) | Vulkan 1.1 음수 높이 뷰포트(`y + height`, `-height`)로 D3D12·Metal과 동일한 좌상단 화면 좌표계 일치 보장 후 `vkCmdSetViewport` 호출 |
| Metal 백엔드 | [Metal.cpp](../src/backends/metal/Metal.cpp) | `beginRenderPass` 초기화, `setViewport` 검증 및 `Draw`별 `MTLViewport` 스냅샷·인코딩 구현 (macOS 실행 검증은 `P9-CMD-05`) |
| CPU·GPU 테스트 | [RenderPassTests.cpp](../tests/RenderPassTests.cpp), [ViewportTests.cpp](../tests/ViewportTests.cpp), [CMakeLists.txt](../CMakeLists.txt) | `validateViewport` CPU 경계 테스트 14건과 컬러·D32 깊이 사분면/인셋 렌더링·거부 시 기존 뷰포트 보존·새 패스 초기화·스레드/상태 거부 GPU 테스트 추가 |

## 검증 결과 (Windows x64 · MSVC 19.44.35228.0 · Windows SDK 10.0.26100.0)

격리 빌드 `out/p9-viewport` 및 설치 프리픽스 `out/p9-viewport-install`에서 물리 GPU 2종(NVIDIA GeForce GTX 1660 SUPER, Intel UHD Graphics 770)과 WARP로 직렬 검증했다.

| 검증 대상 | 환경 / 모드 | 어댑터 수 | Assertions | 전체 이미지(컬러+D32) | 거부 검증 | Validation 오류 | 결과 |
| --- | --- | ---: | ---: | ---: | ---: | ---: | --- |
| `render_pass_contracts` | CPU (`validateViewport`) | - | 전체 통과 | - | 11 | N/A | 통과 |
| `gfx_viewport_tests --d3d12` | Windows D3D12 하드웨어 | 2 | 57,366 | 8 | 18 | N/A | 통과 |
| `gfx_viewport_tests --d3d12 --validation` | Windows D3D12 Debug + GBV | 2 | 57,368 | 8 | 18 | 0 | 통과 |
| `gfx_viewport_tests --d3d12 --warp` | Windows D3D12 WARP | 1 | 28,684 | 4 | 9 | N/A | 통과 |
| `gfx_viewport_tests` | Windows Vulkan 하드웨어 | 2 | 57,366 | 8 | 18 | N/A | 통과 |
| `gfx_viewport_tests --validation` | Windows Vulkan + 고정 VVL 1.3.290 | 2 | 57,368 | 8 | 18 | 0 | 통과 |
| 설치 소비자(`find_package 0.0.1 EXACT`) | D3D12 하드웨어/GBV/WARP + Vulkan 하드웨어/VVL (5모드) | 1~2 | 28,684~57,368 | 4~8 | 9~18 | 0 | 5/5 통과 |
| 회귀 CTest 18종 | `core_contracts`, `render_pass_*`, `vulkan_draw`, `vulkan_depth`, `d3d12_*`, `capabilities_*`, `viewport_*` | 1~2 | 전체 통과 | - | - | 0 | 18/18 통과 |

## 재현 명령

```powershell
cmake -S . -B out/p9-viewport -G "Visual Studio 17 2022" -A x64 -DGFX_ENABLE_D3D12=ON -DGFX_ENABLE_VULKAN=ON -DGFX_BUILD_TESTS=ON -DGFX_BUILD_EXAMPLES=ON
cmake --build out/p9-viewport --config Release --parallel 1 --target GraphicsSDKCore gfx_viewport_tests gfx_render_tests gfx_vulkan_draw_tests gfx_vulkan_depth_tests gfx_tests gfx_capability_tests
ctest --test-dir out/p9-viewport -C Release --output-on-failure -R "^(core_contracts|render_pass_contracts|render_pass_gpu|render_pass_validation|render_pass_warp|d3d12_hardware|d3d12_validation|d3d12_warp|vulkan_draw|vulkan_depth|capabilities_.*|viewport_.*)$"
$env:VK_LAYER_PATH = "D:\source\GraphicsSDK\out\vulkan-validation-runtime\build\Vulkan-ValidationLayers\layers\Release"
.\out\p9-viewport\Release\gfx_viewport_tests.exe --validation
```

## 남은 조건과 다음 작업

- `P9-CMD-05`: Linux Vulkan 및 macOS Metal 실기/CI에서 `setViewport` 실행 검증 필요.
- 다음 선행 충족 확장 작업: `P8-TEX-01`(Texture View·Subresource View), `P9-CMP-01`(Compute Shader·UAV·Dispatch), `P9-CMD-01`(명시적 Command Pool·CommandList 재사용).
