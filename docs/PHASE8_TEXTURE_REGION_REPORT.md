# Phase 8 부분 2D 텍스처 업로드·다운로드(`copyBufferToTextureRegion` / `copyTextureToBufferRegion`) 검증 보고서

- 작성일: 2026-11-20
- 완료 작업 ID: `P8-XFER-05` (API·CPU footprint·D3D12·Windows Vulkan 구현/실기/Validation/설치 소비자 검증 및 Metal 코드 반영)
- 미완료 후속 ID: `P8-XFER-06` (Linux Vulkan 및 macOS Metal 실기/CI 검증 — 구현 반영 완료, 해당 OS 실행 대기)
- SDK 버전: `0.0.1` (`v0.0.2` 전송 확장 슬라이스)
- 검증한 SDK 소스 지문(`gfx::buildFingerprint()`): `638800e78674a9bda385dfa04f11ae005dd226742a6944cf37881ae97fdb95e7`

## 개발 목표와 변경 이유

기존 `copyBufferToTexture` / `copyTextureToBuffer`는 텍스처 전체 영역을 오프셋 `0` 버퍼로만 전송할 수 있었다.
GraphicsUI 폰트/텍스처 아틀라스 부분 갱신, Dirty Region 부분 복사, 텍스처 스트리밍, Sandbox RHI(`BufferTextureCopy`) 호환을 위해 2D 사각 영역 `(x, y, width, height)`과 512바이트 정렬 `bufferOffset`, 256바이트 정렬 `bufferRowPitch`를 지원하는 부분 텍스처 복사 API와 휴대용 footprint 계산기를 추가했다.

## 변경된 API 및 수정 파일

| 항목 | 경로 | 변경 내용 |
| --- | --- | --- |
| 공개 타입·API | [Types.hpp](../include/graphics/Types.hpp), [RenderPass.hpp](../include/graphics/RenderPass.hpp), [CommandList.hpp](../include/graphics/CommandList.hpp) | `TextureCopyRegion`, `computeTextureRegionFootprint`, `copyBufferToTextureRegion`, `copyTextureToBufferRegion` 추가 (기존 13개 공개 헤더 구성 유지) |
| 공통 Foundation | [RenderPass.cpp](../src/foundation/RenderPass.cpp) | 포맷별 texel 크기·256바이트 정렬 `rowPitch`·`rowBytes`·`size` 검증 및 계산 구현 |
| D3D12 백엔드 | [D3D12.cpp](../src/backends/d3d12/D3D12.cpp) | `D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT` + `D3D12_BOX` 기반 부분 업로드/다운로드, D32 부분 복사 `Unsupported`, 원자적 거부 구현 |
| Vulkan 백엔드 | [Vulkan.cpp](../src/backends/vulkan/Vulkan.cpp) | `VkBufferImageCopy{bufferOffset, bufferRowLength, imageOffset, imageExtent}` 기반 부분 업로드/다운로드, D32 부분 복사 `Unsupported`, `uses.reserve` 원자적 거부 구현 |
| Metal 백엔드 | [Metal.cpp](../src/backends/metal/Metal.cpp) | `MTLBlitCommandEncoder` `sourceOffset`/`destinationOffset`/`sourceOrigin`/`destinationOrigin`/`sourceBytesPerRow` 기반 부분 텍스처 복사 구현 (macOS 실행 검증은 `P8-XFER-06`) |
| CPU·GPU 테스트 | [RenderPassTests.cpp](../tests/RenderPassTests.cpp), [TextureRegionTests.cpp](../tests/TextureRegionTests.cpp), [CMakeLists.txt](../CMakeLists.txt) | CPU footprint 경계 테스트 및 RGBA8/BGRA8/RGBA16F 부분 복사·D32 전체/부분 거부·가드 바이트/원자적 거부 GPU 테스트 추가 |

## 검증 결과 (Windows x64 · MSVC 19.44.35228.0 · Windows SDK 10.0.26100.0)

격리 빌드 `out/p8-texture-region`과 설치 프리픽스 `out/p8-texture-region-install`에서 물리 GPU 2종(NVIDIA GeForce GTX 1660 SUPER, Intel UHD Graphics 770)과 WARP로 직렬 검증했다.

| 검증 대상 | 환경 / 모드 | 어댑터 수 | Assertions | Region 업로드 | Region 다운로드 | 거부 검증 | 검증 바이트 | Validation 오류 | 결과 |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `render_pass_contracts` | CPU (`computeTextureRegionFootprint`) | - | 전체 통과 | - | - | 6 | - | N/A | 통과 |
| `gfx_texture_region_tests --d3d12` | Windows D3D12 하드웨어 | 2 | 340 | 20 | 14 | 52 | 186,400 | N/A | 통과 |
| `gfx_texture_region_tests --d3d12 --validation` | Windows D3D12 Debug + GBV | 2 | 342 | 20 | 14 | 52 | 186,400 | 0 | 통과 |
| `gfx_texture_region_tests --d3d12 --warp` | Windows D3D12 WARP | 1 | 171 | 10 | 7 | 26 | 93,200 | N/A | 통과 |
| `gfx_texture_region_tests` | Windows Vulkan 하드웨어 | 2 | 340 | 20 | 14 | 52 | 186,400 | N/A | 통과 |
| `gfx_texture_region_tests --validation` | Windows Vulkan + 고정 VVL 1.3.290 | 2 | 342 | 20 | 14 | 52 | 186,400 | 0 | 통과 |
| 설치 소비자(`find_package 0.0.1 EXACT`) | D3D12 하드웨어 / Debug+GBV | 2 / 2 | 340 / 342 | 20 / 20 | 14 / 14 | 52 / 52 | 186,400 | 0 | 통과 |
| 설치 소비자(`find_package 0.0.1 EXACT`) | Vulkan 하드웨어 / 고정 VVL | 2 / 2 | 340 / 342 | 20 / 20 | 14 / 14 | 52 / 52 | 186,400 | 0 | 통과 |
| 회귀 CTest 22종 | `core_contracts`, `render_pass_*`, `vulkan_texture_transfer`, `copy_atomicity_*`, `d3d12_*`, `bgra_*`, `buffer_region_*`, `texture_region_*` | 1~2 | 전체 통과 | - | - | - | - | 0 | 22/22 통과 |

## 검증한 핵심 계약

1. **전 컬러 포맷(`RGBA8UNorm`, `BGRA8UNorm`, `RGBA16Float`) 부분 업로드·비갱신 텍셀 보존**: 홀수 크기 `67×43` 텍스처에 전체 패턴을 올린 뒤 1×1 모서리, `19×13` 중앙(기본 피치, `bufferOffset=512`), `17×12` 우하단(커스텀 피치 `512`, `bufferOffset` 비영) 3개 패치를 단일 Upload 버퍼에서 부분 업로드하고 전체 `67×43` 텍셀을 대조.
2. **부분 다운로드·버퍼 가드/패딩 보존**: `0x6d` 센티넬로 초기화한 Readback 버퍼의 서로 다른 512바이트 정렬 오프셋에 2개 사각 영역(기본/커스텀 피치)을 내려받아 유효 텍셀과 모든 앞/사이/뒤/행 패딩 가드 바이트 일치 확인.
3. **D32Float 경계 계약**: `D32Float`의 전체 영역 + 비영 `bufferOffset`/커스텀 `rowPitch` 업로드·다운로드는 통과하고, 부분 사각 영역(`(1,0,w-1,h)` 등)은 D3D12/Vulkan/Metal 공통으로 `ErrorCode::Unsupported` 반환 확인.
4. **원자적 거부·수명·Stale Upload**: 512바이트 미정렬 오프셋, 256바이트 미정렬/과소 피치, 오버플로, 메모리 방향 오류, 잘못된 상태, 렌더 패스 내부 호출, 외부 장치 리소스를 자원 누수·버전 오염 없이 거부하고, 수락된 부분 업로드 이후 Upload 수정 시 `submit()` `InvalidState` 거부 확인.

## 재현 명령

```powershell
cmake -S . -B out/p8-texture-region -G "Visual Studio 17 2022" -A x64 -DGFX_ENABLE_D3D12=ON -DGFX_ENABLE_VULKAN=ON -DGFX_BUILD_TESTS=ON -DGFX_BUILD_EXAMPLES=ON
cmake --build out/p8-texture-region --config Release --parallel 1 --target GraphicsSDKCore gfx_texture_region_tests gfx_render_tests gfx_buffer_region_tests gfx_copy_tests gfx_vulkan_texture_tests gfx_bgra_tests gfx_tests
ctest --test-dir out/p8-texture-region -C Release --output-on-failure -R "^(core_contracts|render_pass_contracts|render_pass_gpu|render_pass_validation|render_pass_warp|copy_atomicity_gpu|copy_atomicity_validation|d3d12_hardware|d3d12_validation|d3d12_warp|vulkan_texture_transfer|bgra_d3d12|bgra_warp|bgra_vulkan|buffer_region_.*|texture_region_.*)$"
$env:VK_LAYER_PATH = "D:\source\GraphicsSDK\out\vulkan-validation-runtime\build\Vulkan-ValidationLayers\layers\Release"
.\out\p8-texture-region\Release\gfx_texture_region_tests.exe --validation
```

## 남은 조건과 다음 작업

- `P8-XFER-06`: Linux Vulkan 및 macOS Metal 실기/CI에서 `copyBufferToTextureRegion` / `copyTextureToBufferRegion` 실행 검증 필요.
- 다음 선행 충족 확장 작업: `P8-CAP-01`(GPU 한도·포맷 지원 조회 API), `P9-CMD-01`(동적 `setViewport` 및 `CommandPool`).
