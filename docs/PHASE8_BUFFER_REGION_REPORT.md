# Phase 8 오프셋 기반 버퍼 복사(`copyBufferRegion`) 구현 및 검증 보고서

- 작성일: 2026-11-20
- 대상 작업 ID: `P8-XFER-01` (API 계약), `P8-XFER-02` (D3D12 구현·검증), `P8-XFER-03` (Windows Vulkan 구현·검증)
- 미완료 후속 ID: `P8-XFER-04` (macOS Metal 및 Linux Vulkan 실기/CI 검증 — 구현 반영 완료, 해당 OS 실행 대기)
- SDK 버전: `0.0.1` (`v0.0.2` 준비 슬라이스)
- 검증한 SDK 소스 지문(`gfx::buildFingerprint()`): `2316932de6cee5da52c8ed653ab48fd93b09e705b20ca2acb5400bd4f92e5a6a`

## 개발 목표와 변경 이유

기존 `CommandList::copyBuffer(source, destination, bytes)`는 항상 오프셋 `0`에서 시작하는 복사만 지원했다.
Upload Ring(`P6-OPT-08`), 서브할당 버퍼, GraphicsUI 동적 정점/인덱스 업로드, Sandbox RHI(`BufferCopy{src, srcOffset, dst, dstOffset, size}`) 호환 및 부분 버퍼 갱신을 위해 오프셋 기반 버퍼 복사 `CommandList::copyBufferRegion`을 추가했다.

## 변경된 API 및 수정 파일

| 항목 | 경로 | 변경 내용 |
| --- | --- | --- |
| 공개 API | [CommandList.hpp](../include/graphics/CommandList.hpp) | `CommandList::copyBufferRegion(source, sourceOffset, destination, destinationOffset, bytes)` 추가 (기존 `copyBuffer` 유지) |
| D3D12 백엔드 | [D3D12.cpp](../src/backends/d3d12/D3D12.cpp) | 오버플로 안전 범위·메모리·상태 사전 검증, 원자적 거부 후 `ID3D12GraphicsCommandList::CopyBufferRegion` 기록 |
| Vulkan 백엔드 | [Vulkan.cpp](../src/backends/vulkan/Vulkan.cpp) | 오버플로 안전 범위·메모리·상태 사전 검증, `uses.reserve` 기반 원자적 거부, `VkBufferCopy{sourceOffset, destinationOffset, bytes}` 및 배리어 기록 |
| Metal 백엔드 | [Metal.cpp](../src/backends/metal/Metal.cpp) | 4바이트 정렬 오프셋은 `MTLBlitCommandEncoder`, 비정렬 오프셋/꼬리 바이트는 `gfx_copy_tail` 컴퓨트 커널로 주변 바이트를 보존하며 복사 |
| GPU 테스트 | [BufferRegionTests.cpp](../tests/BufferRegionTests.cpp), [CMakeLists.txt](../CMakeLists.txt) | 정렬/비정렬 오프셋, 가드 바이트 보존, upload-ring 스타일 8슬라이스 패킹 복사, 경계/오버플로/별칭/상태/수명/원자적 거부 테스트 등록 |
| API 문서 | [PUBLIC_API.md](PUBLIC_API.md) | `copyBufferRegion` 계약 및 거부 순서 문서화 |

## 검증 결과 (Windows x64 · MSVC 19.44.35228.0 · Windows SDK 10.0.26100.0)

격리 빌드 디렉터리 `out/p8-buffer-region` 및 설치 프리픽스 `out/p8-buffer-region-install`에서 실행했다.
로컬 물리 어댑터 2종(NVIDIA GeForce GTX 1660 SUPER, Intel UHD Graphics 770)과 WARP에서 직렬 검증했다.

| 검증 대상 | 환경 / 모드 | 어댑터 수 | Assertions | Region 복사 | 거부 검증 | 검증 바이트 | Validation 오류 | 결과 |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `gfx_buffer_region_tests --d3d12` | Windows D3D12 하드웨어 | 2 | 98 | 48 | 38 | 193,282 | N/A | 통과 |
| `gfx_buffer_region_tests --d3d12 --validation` | Windows D3D12 Debug + GBV | 2 | 100 | 48 | 38 | 193,282 | 0 | 통과 |
| `gfx_buffer_region_tests --d3d12 --warp` | Windows D3D12 WARP | 1 | 50 | 24 | 19 | 96,641 | N/A | 통과 |
| `gfx_buffer_region_tests` | Windows Vulkan 하드웨어 | 2 | 98 | 48 | 38 | 193,282 | N/A | 통과 |
| `gfx_buffer_region_tests --validation` | Windows Vulkan + 고정 VVL 1.3.290 | 2 | 100 | 48 | 38 | 193,282 | 0 | 통과 |
| 설치 소비자(`find_package 0.0.1 EXACT`) | D3D12 하드웨어 / Validation | 2 / 2 | 98 / 100 | 48 / 48 | 38 / 38 | 193,282 | 0 | 통과 |
| 설치 소비자(`find_package 0.0.1 EXACT`) | Vulkan 하드웨어 / 고정 VVL | 2 / 2 | 98 / 100 | 48 / 48 | 38 / 38 | 193,282 | 0 | 통과 |
| 기존 회귀 CTest 16종 | `core_contracts`, `copy_atomicity_*`, `d3d12_*`, `vulkan_transfer`, `bgra_*`, `buffer_region_*` | 1~2 | 전체 통과 | - | - | - | 0 | 16/16 통과 |

## 검증한 핵심 계약

1. **가드 바이트 보존**: `destinationOffset` 앞뒤 모든 센티넬 바이트(`0xa5`, `0xd1`, `0x6b`)가 1바이트도 훼손되지 않음을 전체 readback으로 검증.
2. **Upload-ring 스타일 멀티 슬라이스**: 1/7/16/63/256/513/1024/4099바이트의 정렬·비정렬 8개 슬라이스를 단일 Upload 버퍼에서 Device 버퍼의 분산 오프셋으로 복사한 뒤 Readback 버퍼의 별도 오프셋으로 다시 모아 전체 10,240바이트 대조.
3. **수명 및 회수**: `submit()` 직후 `upload`/`device`/`CommandList` 래퍼를 즉시 해제하고 `wait(fence)` 후 전체 바이트 일치 및 `resourceStatistics()` 5개 항목 0 복귀 확인.
4. **원자적 거부**: 잘못된 오프셋·오버플로(`UINT64_MAX - 3 + 8`)·0바이트·동일 버퍼 별칭·Readback 소스/Upload 대상·잘못된 `ResourceState`·렌더 패스 내부 호출·외부 장치 버퍼 거부 시 자원 누수 및 Upload 버전 오염 0 확인.
5. **Stale Upload 감지**: 수락된 `copyBufferRegion` 이후 Upload 버퍼를 CPU에서 수정하면 `submit()`이 `InvalidState`로 거부됨을 확인.

## 재현 명령

```powershell
cmake -S . -B out/p8-buffer-region -G "Visual Studio 17 2022" -A x64 -DGFX_ENABLE_D3D12=ON -DGFX_ENABLE_VULKAN=ON -DGFX_BUILD_TESTS=ON -DGFX_BUILD_EXAMPLES=ON
cmake --build out/p8-buffer-region --config Release --parallel 1 --target GraphicsSDKCore gfx_buffer_region_tests gfx_tests gfx_copy_tests gfx_vulkan_tests gfx_bgra_tests gfx_vulkan_creation_tests gfx_vulkan_timestamp_math_tests
ctest --test-dir out/p8-buffer-region -C Release --output-on-failure -R "^(core_contracts|vulkan_timestamp_math|vulkan_creation_ownership|copy_atomicity_gpu|copy_atomicity_validation|d3d12_hardware|d3d12_validation|d3d12_warp|vulkan_transfer|bgra_d3d12|bgra_warp|bgra_vulkan|buffer_region_.*)$"
$env:VK_LAYER_PATH = "D:\source\GraphicsSDK\out\vulkan-validation-runtime\build\Vulkan-ValidationLayers\layers\Release"
.\out\p8-buffer-region\Release\gfx_buffer_region_tests.exe --validation
```

## 남은 조건과 다음 작업

- `P8-XFER-04`: `Metal.cpp`의 오프셋 복사(정렬 blit + 비정렬 `gfx_copy_tail` 컴퓨트 경로)와 Linux Vulkan 경로는 코드가 반영됐으나 macOS/Linux 실기 실행이 필요하므로 `todo`로 유지한다.
- 다음 권장 확장 작업: `P8-XFER-05`(부분 2D 텍스처 복사), `P8-CAP-01`(GPU 한도 및 포맷 지원 조회 API), `P9-CMD-01`(동적 `setViewport`).
