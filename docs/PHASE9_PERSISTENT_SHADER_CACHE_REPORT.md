# Phase 9 영속 Shader/Pipeline Cache · Variant · Prewarming · Hot Reload 보고서 (`P9-SHD-02`)

- 작성일: 2026-11-22
- 관련 작업 ID: `P9-SHD-02` (완료 — API·D3D12·Win Vulkan 구현 및 cold vs warm 실측), `P9-SHD-01` (대기 — `P5-SHD-08` 선행 수용 및 Linux Vulkan / macOS Metal 최종 수용)
- 기준 산출물: `out/p9-shader-cache-install/lib/GraphicsSDKCore.lib` (SHA-256: `FA2FDDBC1BAA50B8F07C4CE09D1670D11BF14E6D50F648873A6C7D3462F32917`)

---

## 1. 공개 API 및 디스크 포맷 설계

기존 13개 공개 헤더와 5개 `CORE` 번역 단위, 그리고 `Device::createCachedGraphicsPipeline` 8-entry LRU 계약을 유지하면서 디스크 영속 캐시와 변형 관리 계층을 추가했다.

- **지문·결과 타입 (`include/graphics/Types.hpp`)**:
  - `PipelineCacheFingerprint`: `sdkVersion`, `backend`, `vendorId`, `deviceId`, `adapterName`, `driverFingerprint`.
  - `PipelineCacheLoadResult`: `loaded`, `invalidated`, `recoveredFromCorruption`, `bytesLoaded`.
  - `ShaderDefine`, `ShaderVariantKey` (`name`, 정규화 정렬 `defines`, `sourceHash`), `PipelinePrewarmStats`.
- **`Device` 영속 캐시 API (`include/graphics/Device.hpp`, `src/foundation/Backend.cpp`)**:
  - `pipelineCacheFingerprint()`, `serializePipelineCache()`, `loadPipelineCacheData(span)`, `savePipelineCacheToFile(path)`, `loadPipelineCacheFromFile(path)`.
  - **디스크 포맷 (`GFXPC001` v1)**: 8B 매직 + 4B 버전 + 8B FNV-1a 64-bit 체크섬 + 지문 메타데이터 + 네이티브 페이로드. 임시 파일(`.tmp`) 기록 후 원자적 교체.
  - **무효화·복구 정책**: 파일 부재 시 clean no-op(`loaded=false`), 절단/매직 불일치/체크섬 불일치/드라이버 거부 시 크래시 없이 `recoveredFromCorruption=true`, 지문 불일치 시 `invalidated=true`로 안전 무시.
- **`ShaderVariantCache` (`include/graphics/Device.hpp`, `src/foundation/Backend.cpp`)**:
  - `getOrCompile`, `prewarm`, `hotReload`(새 파이프라인 먼저 컴파일 후 성공 시에만 교체 — 실패 시 기존 정상 파이프라인 무손실 보존), `find`, `saveToDisk`, `loadFromDisk`.

---

## 2. 백엔드 네이티브 캐시 구현

- **Vulkan (`src/backends/vulkan/Vulkan.cpp`)**:
  - 장치별 `VkPipelineCache`를 생성해 `vkCreateGraphicsPipelines`·`vkCreateComputePipelines`에 전달.
  - `serializePipelineCache()`는 `vkGetPipelineCacheData`로 드라이버 바이너리 추출, `loadPipelineCacheData()`는 `vkCreatePipelineCache(initialData)` + `vkMergePipelineCaches`로 병합.
  - `pipelineCacheFingerprint()`는 `VkPhysicalDeviceProperties2::{apiVersion, driverVersion, vendorID, deviceID, pipelineCacheUUID}` 기반 FNV-1a 해시.
- **D3D12 (`src/backends/d3d12/D3D12.cpp`)**:
  - `ID3D12PipelineState::GetCachedBlob`으로 PSO 생성 후 네이티브 드라이버 블롭을 수집해 키별 저장하고, 재생성 시 `CachedPSO`로 전달(드라이버 버전 불일치 등으로 실패 시 `CachedPSO={}` 자동 폴백 재컴파일).
  - `pipelineCacheFingerprint()`는 어댑터 LUID + `DXGI_ADAPTER_DESC1::{VendorId, DeviceId, SubSysId, Revision}` 기반 FNV-1a 해시.

---

## 3. 실측 검증 결과 (`tests/ShaderCacheTests.cpp`, 8-Variant Cold vs Warm)

동일 어댑터에서 8개 Graphics Pipeline 변형(`red`/`green`/`half_red` × `alphaBlend` × `RGBA8`/`BGRA8`)을 Cold 장치에서 프리워밍·디스크 저장 후, **완전히 새로운 Warm 장치 인스턴스**에서 디스크 캐시를 로드해 재프리워밍 시간·64×64 전 픽셀 일치·드라이버 지문 변조 무효화·3종 손상 복구(절단/매직/1비트 플립)·원자적 Hot Reload(빨강→초록→손상 바이트코드 실패 시 초록 보존)를 검증했다.

| 백엔드 / 어댑터 | 모드 | Cold 8-Variant | Warm 8-Variant | 가속 배수 | 디스크 파일 / 네이티브 캐시 | Validation 오류 | 결과 |
| --- | --- | ---: | ---: | ---: | ---: | ---: | --- |
| D3D12 / NVIDIA GeForce GTX 1660 SUPER | Release | 10.53 ms | 1.06 ms | **9.93x** | 39,918 B / 39,828 B | N/A | **PASS** |
| D3D12 / Intel(R) UHD Graphics 770 | Release | 16.20 ms | 0.63 ms | **25.71x** | 51,978 B / 51,892 B | N/A | **PASS** |
| D3D12 / NVIDIA GeForce GTX 1660 SUPER | Debug+GBV | 118.07 ms | 2.40 ms | **49.20x** | 39,918 B / 39,828 B | **0** | **PASS** |
| D3D12 / Intel(R) UHD Graphics 770 | Debug+GBV | 1.28 ms | 1.90 ms | 0.67x (OS 캐시 워밍 후) | 52,746 B / 52,660 B | **0** | **PASS** |
| D3D12 / Microsoft Basic Render Driver | WARP | 0.87 ms | 0.59 ms | **1.47x** | 29,262 B / 29,172 B | N/A | **PASS** |
| Vulkan / NVIDIA GeForce GTX 1660 SUPER | Release | 30.17 ms | 0.55 ms | **54.85x** | 33,311 B / 33,221 B | N/A | **PASS** |
| Vulkan / Intel(R) UHD Graphics 770 | Release | 12.95 ms | 0.35 ms | **37.00x** | 7,482 B / 7,396 B | N/A | **PASS** |
| Vulkan / NVIDIA GeForce GTX 1660 SUPER | VVL 1.3.290 | 8.70 ms | 4.17 ms | **2.09x** | 33,311 B / 33,221 B | **0** | **PASS** |
| Vulkan / Intel(R) UHD Graphics 770 | VVL 1.3.290 | 2.12 ms | 2.08 ms | 1.02x | 7,482 B / 7,396 B | **0** | **PASS** |

- **CTest (`shader_cache_*`)**: **5/5 PASS (`4.38s`)**.
- **독립 설치 소비자 (`tests/consumer`, `find_package(GraphicsSDK CONFIG REQUIRED)`)**: 빌드 및 `consumer.exe` 실행 **PASS** (`GraphicsSDK 0.0.1`).

---

## 4. 재현 절차 및 잔여 범위 (`P9-SHD-01`)

```powershell
$env:VK_LAYER_PATH = "D:\source\GraphicsSDK\out\vulkan-validation-runtime\build\Vulkan-ValidationLayers\layers\Release"
cmake -S . -B out/p9-shader-cache -DGFX_ENABLE_VULKAN=ON -DGFX_VULKAN_VALIDATION_TEST=ON
cmake --build out/p9-shader-cache --config Release --target gfx_shader_cache_tests -j 8
ctest --test-dir out/p9-shader-cache -C Release -R shader_cache --output-on-failure
```

- `P5-SHD-08` 선행 수용과 Linux Vulkan / macOS Metal 실기·CI 검증은 **`P9-SHD-01`**에서 추적한다.
