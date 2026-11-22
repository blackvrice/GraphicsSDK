# Phase 3 Vulkan GPU-Assisted Validation 보고서 (`P3-CAP-07`)

- 작성일: 2026-11-22
- 관련 작업 ID: `P3-CAP-07` (완료 — opt-in GPU-Assisted Validation 설정·능력·오류 검출 대조 Control 실기 검증)
- 기준 산출물: `out/p3-gpu-av-install/lib/GraphicsSDKCore.lib` (SHA-256: `9059363E4336CF57CE4AC5B66BE4AB1F4EB38F1C0F210CD4FC6102ED8AC7CBDE`)
- 검증 런타임: 고정 로컬 VVL 1.3.290 (`out/vulkan-validation-runtime/build/Vulkan-ValidationLayers/layers/Release`)

---

## 1. 기존 Core/Sync Validation과의 구분 및 공개 계약

- **공개 설정 (`DeviceDesc::gpuAssistedValidation`, 기본값 `false`)**:
  - `validation == false && gpuAssistedValidation == true`는 `createDevice()`에서 `ErrorCode::InvalidArgument`로 거부.
  - `validation == true && gpuAssistedValidation == false`는 기존과 동일하게 `VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT`만 활성화하고 `caps().gpuAssistedValidation == false`를 유지한다.
  - `validation == true && gpuAssistedValidation == true`는 `VulkanValidation.hpp`에서 `VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT` + `VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT` + `VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_RESERVE_BINDING_SLOT_EXT`를 활성화하고, Vulkan 1.2 인스턴스 및 장치 지원 범위의 `fragmentStoresAndAtomics`/`vertexPipelineStoresAndAtomics`/`shaderInt64`를 요청하며 `caps().gpuAssistedValidation == true`를 보고한다.
- **D3D12**: `validation == true`일 때 기존과 같이 `ID3D12Debug1::SetEnableGPUBasedValidation(TRUE)`(GBV)를 활성화하고 `caps().gpuAssistedValidation = true`를 보고한다.

---

## 2. 실기 대조 검증 결과 (`tests/VulkanGpuAssistedValidationTests.cpp`)

동일 물리 GPU에서 GPU 간접 버퍼에 `DispatchIndirectCommand{1, 1, 65537}`(`maxComputeWorkGroupCount[2]` 초과)을 기록하고 `dispatchIndirect()`를 실행해 CPU-side Sync Validation과 GPU-AV를 직접 대조했다(VVL GPU-AV의 GPU 출력 버퍼는 `queue().waitIdle()` 시점에 flush됨을 확인).

| 물리 GPU 어댑터 | Sync-only (`gpuAssistedValidation=false`) 간접 버퍼 Control | GPU-AV (`gpuAssistedValidation=true`) 정상 Draw(64²)+Compute | GPU-AV (`gpuAssistedValidation=true`) 간접 버퍼 Control (`VUID-VkDispatchIndirectCommand-z-00419`) | 결과 |
| --- | ---: | ---: | ---: | --- |
| NVIDIA GeForce GTX 1660 SUPER | `errors=0` (미검출, 예상 일치) | `errors=0` (전 픽셀·전 바이트 일치) | **`errors=1` 검출** | **PASS** |
| Intel(R) UHD Graphics 770 | `errors=0` (미검출, 예상 일치) | `errors=0` (전 픽셀·전 바이트 일치) | **`errors=1` 검출** | **PASS** |

- `gfx_vulkan_gpu_av_tests`: `PASS adapters=2 assertions=207 rejections=1`.
- CTest (`vulkan_gpu_assisted_validation`): **1/1 PASS (`1.11s`)**.
- 독립 설치 소비자 (`tests/consumer`, `find_package(GraphicsSDK CONFIG REQUIRED)`): 빌드 및 `consumer.exe` 실행 **PASS** (`GraphicsSDK 0.0.1`).
