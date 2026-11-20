# Phase 8 GPU 한도 및 포맷 지원 조회(`P8-CAP-01`) 검증 보고서

- 작성일: 2026-11-20
- 완료 작업 ID: `P8-CAP-01` (API·D3D12·Windows Vulkan 구현/실기/Validation/WARP/설치 소비자 검증 및 Metal 코드 반영)
- 미완료 후속 ID: `P8-CAP-02` (Linux Vulkan 및 macOS Metal 실기/CI 검증 — 구현 반영 완료, 해당 OS 실행 대기)
- SDK 버전: `0.0.1` (`v0.0.3` Capability 확장 슬라이스)
- 검증한 SDK 소스 지문(`gfx::buildFingerprint()`): `0ccb4e60ce8c3ee5f2404509c8b310920fc019e01324b9170cd3eccd848094e2`

## 개발 목표와 변경 이유

기존 `DeviceCaps`는 boolean 기능 플래그만 제공하고 최대 텍스처 크기·최대 color attachment 수·버퍼/텍스처 정렬 한도·정점 스트림 수·포맷별 지원 여부(`FormatSupport`)를 노출하지 않았다.
압축 텍스처(`P8-TEX-04`), Texture View(`P8-TEX-01`), Compute(`P9-CMP-01`), Frame Pacing(`P10-PRES-01`), Sandbox RHI 호환을 위해 실제 어댑터와 SDK 유효 한도를 조회하는 API를 추가했다.

## 변경된 API 및 수정 파일

| 항목 | 경로 | 변경 내용 |
| --- | --- | --- |
| 공개 타입·API | [Types.hpp](../include/graphics/Types.hpp), [Device.hpp](../include/graphics/Device.hpp) | `FormatSupport` 구조체, `DeviceCaps` 7개 한도 필드(`maxTextureDimension2D`, `maxColorAttachments`, `constantBufferAlignment`, `textureCopyRowAlignment`, `textureCopyOffsetAlignment`, `maxVertexStreams`, `maxConstantBufferSize`), `Device::queryFormatSupport(Format)` 추가 (기존 13개 공개 헤더 유지) |
| D3D12 백엔드 | [D3D12.cpp](../src/backends/d3d12/D3D12.cpp) | `ID3D12Device::CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT)` 기반 실제 포맷 조회 및 D3D12 아키텍처/SDK 한도 채우기 |
| Vulkan 백엔드 | [Vulkan.cpp](../src/backends/vulkan/Vulkan.cpp) | `vkGetPhysicalDeviceFormatProperties` 기반 실제 optimal-tiling 포맷 조회 및 `VkPhysicalDeviceLimits` 기반 유효 한도 채우기 |
| Metal 백엔드 | [Metal.cpp](../src/backends/metal/Metal.cpp) | macOS Metal 포맷 지원 조회 및 유효 한도(`maxVertexStreams=15` 등) 채우기 (macOS 실행 검증은 `P8-CAP-02`) |
| GPU 테스트 | [CapabilityTests.cpp](../tests/CapabilityTests.cpp), [CMakeLists.txt](../CMakeLists.txt) | 7개 한도 계약, 4개 포맷 조회·실제 텍스처 교차 생성, 알 수 없는 포맷/워커 스레드/장치 손실·복구 거부 및 자원 무할당 검증 |

## 검증 결과 (Windows x64 · MSVC 19.44.35228.0 · Windows SDK 10.0.26100.0)

격리 빌드 `out/p8-capabilities` 및 설치 프리픽스 `out/p8-capabilities-install`에서 물리 GPU 2종(NVIDIA GeForce GTX 1660 SUPER, Intel UHD Graphics 770)과 WARP로 직렬 검증했다.

| 검증 대상 | 환경 / 모드 | 어댑터 수 | Assertions | 포맷 조회 | 거부 검증 | Validation 오류 | 결과 |
| --- | --- | ---: | ---: | ---: | ---: | ---: | --- |
| `gfx_capability_tests --d3d12` | Windows D3D12 하드웨어 | 2 | 56 | 8 | 4 | N/A | 통과 |
| `gfx_capability_tests --d3d12 --validation` | Windows D3D12 Debug + GBV | 2 | 58 | 8 | 4 | 0 | 통과 |
| `gfx_capability_tests --d3d12 --warp` | Windows D3D12 WARP + 손실/복구 | 1 | 32 | 6 | 4 | N/A | 통과 |
| `gfx_capability_tests` | Windows Vulkan 하드웨어 | 2 | 54 | 8 | 4 | N/A | 통과 |
| `gfx_capability_tests --validation` | Windows Vulkan + 고정 VVL 1.3.290 | 2 | 56 | 8 | 4 | 0 | 통과 |
| 설치 소비자(`find_package 0.0.1 EXACT`) | D3D12 하드웨어/GBV/WARP + Vulkan 하드웨어/VVL (5모드) | 1~2 | 32~58 | 6~8 | 4 | 0 | 5/5 통과 |
| 회귀 CTest 20종 | `core_contracts`, `adapter_*`, `d3d12_*`, `vulkan_transfer`, `buffer_region_*`, `texture_region_*`, `capabilities_*` | 1~2 | 전체 통과 | - | - | 0 | 20/20 통과 |

## 재현 명령

```powershell
cmake -S . -B out/p8-capabilities -G "Visual Studio 17 2022" -A x64 -DGFX_ENABLE_D3D12=ON -DGFX_ENABLE_VULKAN=ON -DGFX_BUILD_TESTS=ON -DGFX_BUILD_EXAMPLES=ON
cmake --build out/p8-capabilities --config Release --parallel 1 --target GraphicsSDKCore gfx_capability_tests gfx_adapter_tests gfx_tests gfx_vulkan_tests gfx_buffer_region_tests gfx_texture_region_tests
ctest --test-dir out/p8-capabilities -C Release --output-on-failure -R "^(core_contracts|adapter_contracts|adapter_selection_gpu|adapter_selection_validation|d3d12_hardware|d3d12_validation|d3d12_warp|vulkan_transfer|buffer_region_.*|texture_region_.*|capabilities_.*)$"
$env:VK_LAYER_PATH = "D:\source\GraphicsSDK\out\vulkan-validation-runtime\build\Vulkan-ValidationLayers\layers\Release"
.\out\p8-capabilities\Release\gfx_capability_tests.exe --validation
```

## 남은 조건과 다음 작업

- `P8-CAP-02`: Linux Vulkan 및 macOS Metal 실기/CI에서 `DeviceCaps` 한도와 `queryFormatSupport` 실행 검증 필요.
- 다음 선행 충족 확장 작업: `P9-CMD-01`(동적 `setViewport` 및 `CommandPool` 재사용), `P8-TEX-01`(Texture View·Subresource View), `P9-CMP-01`(Compute Shader·UAV·Dispatch).
