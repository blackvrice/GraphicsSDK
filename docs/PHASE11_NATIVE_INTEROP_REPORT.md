# Phase 11 Native GPU Interoperability 보고서 (`P11-INT-01`)

- 작성일: 2026-11-22
- 관련 작업 ID: `P11-INT-01` (완료 — API·Windows D3D12/Vulkan 구현 및 Cross-API 실기 검증), `P11-INT-02` (대기 — Linux Vulkan / macOS Metal 실기·CI 검증)
- 기준 산출물: `out/p11-native-interop-install/lib/GraphicsSDKCore.lib` (SHA-256: `4C307CE13C01BF18204D9CBC6F3143C9571401CE517FB74220C860F43460CC2C`)

---

## 1. 목적 및 설계 경계

공개 헤더(`include/graphics/`, 기존 13개 헤더 유지)에 OS·D3D12·Vulkan·Metal 타입이나 헤더를 노출하지 않으면서, 외부 엔진·코덱·AI 런타임 또는 동일 GPU 상의 다른 장치·API 간에 GPU 메모리와 동기화 프리미티브를 Zero-Copy로 공유하고 비소유 네이티브 핸들을 안전하게 조회하는 계약을 구축했다.

### 공개 API 추가 (`include/graphics/Types.hpp`, `include/graphics/Device.hpp`)

- **능력 조회 (`DeviceCaps`)**: `externalMemoryInterop`, `externalFenceInterop`.
- **공유 자원 요청**: `BufferDesc::shared`, `TextureDesc::shared` (기본값 `false`, 기존 호출자 100% 하위 호환).
- **비소유 네이티브 핸들 (`NativeObjectType`, `NativeHandle`)**:
  - `Device::{nativeDeviceHandle, nativeBufferHandle, nativeTextureHandle, nativeTimelineFenceHandle}`
  - D3D12(`ID3D12Device*`, `ID3D12Resource*`, `ID3D12Fence*`), Vulkan(`VkDevice`, `VkBuffer`, `VkImage`, `VkSemaphore` — 64비트 불투명 핸들), Metal(`id<MTLDevice>`, `id<MTLBuffer>`, `id<MTLTexture>`, `id<MTLSharedEvent>` — `__bridge void*`)을 `uint64_t value`로 비소유 반환.
  - 장치 불일치 시 `WrongDevice`, 잘못된 스레드 시 `InvalidState`, 손실 장치 시 `DeviceLost`.
- **소유권 이전 외부 핸들 (`ExternalHandleType`, `ExternalHandle`)**:
  - `ExternalHandleType::{OpaqueWin32Nt, D3D12Resource, D3D12Fence, OpaqueFd, MetalSharedEvent}`.
  - RAII `ExternalHandle`(move-only, 소멸 시 `ExternalHandle::reset()`이 비공개 구현 `src/foundation/Backend.cpp`에서 Win32 `CloseHandle` / POSIX `close` 수행).
  - `Device::{exportBufferHandle, importBufferHandle, exportTextureHandle, importTextureHandle, createSharedTimelineFence, exportTimelineFenceHandle, importTimelineFenceHandle}`.

---

## 2. 백엔드별 구현 및 규격 반영 사항

### Windows D3D12 (`src/backends/d3d12/D3D12.cpp`)

- `BufferDesc::shared == true` / `TextureDesc::shared == true`: `D3D12_HEAP_FLAG_SHARED`로 커밋드 리소스 생성(비-깊이 단일 샘플 공유 텍스처는 `D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS` 포함).
- `createSharedTimelineFence(initialValue)`: `D3D12_FENCE_FLAG_SHARED`로 타임라인 펜스 생성.
- `export*Handle`: `ID3D12Device::CreateSharedHandle(..., GENERIC_ALL, nullptr, &ntHandle)`로 Win32 NT 핸들 발급(`ExternalHandleType::D3D12Resource`, `ExternalHandleType::D3D12Fence`).
- `import*Handle`: `ID3D12Device::OpenSharedHandle`로 `D3D12Resource` / `D3D12Fence` / `OpaqueWin32Nt` NT 핸들을 가져오며, 리소스 차원·크기·포맷 불일치 시 즉시 `InvalidArgument`로 거부.
- `caps().externalMemoryInterop = true`, `caps().externalFenceInterop = true`.

### Windows Vulkan (`src/backends/vulkan/Vulkan.cpp`)

- 선택 확장 `VK_KHR_external_memory_win32`, `VK_KHR_external_semaphore_win32` 활성화 및 함수 포인터(`vkGetMemoryWin32HandleKHR`, `vkGetMemoryWin32HandlePropertiesKHR`, `vkGetSemaphoreWin32HandleKHR`, `vkImportSemaphoreWin32HandleKHR`) 로드.
- **명시적 `ExternalHandleType` 분기 및 Vulkan 스펙 준수**:
  - Vulkan-export 버퍼/텍스처는 `VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT` + `VkMemoryDedicatedAllocateInfo`로 할당·내보내기(`ExternalHandleType::OpaqueWin32Nt`).
  - Vulkan-import 시 `ExternalHandleType::D3D12Resource`는 `VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT`로 `vkGetMemoryWin32HandlePropertiesKHR`를 조회해 메모리 타입을 교집합으로 선택한다.
  - `ExternalHandleType::OpaqueWin32Nt`는 Vulkan 규격 `VUID-vkGetMemoryWin32HandlePropertiesKHR-handleType-00666`(non-opaque 핸들 타입에만 호출 허용)에 따라 `vkGetMemoryWin32HandlePropertiesKHR`를 호출하지 않고 `req.memoryTypeBits`를 직접 사용한다.
  - 공유 타임라인 세마포어는 `ExternalHandleType::D3D12Fence`에 `VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE_BIT`, `ExternalHandleType::OpaqueWin32Nt`에 `VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT`를 정확히 매핑해 Intel UHD 770 드라이버에서 타입 추정(probing) 시 발생하던 타임라인 대기 불일치를 근본 제거했다.

### macOS Metal (`src/backends/metal/Metal.cpp`)

- `nativeDeviceHandle`, `nativeBufferHandle`, `nativeTextureHandle`, `nativeTimelineFenceHandle`: `__bridge void*` 비소유 조회 구현.
- 외부 핸들 export/import 및 `createSharedTimelineFence`: `caps().externalMemoryInterop = false`, `caps().externalFenceInterop = false`, 명시적 `ErrorCode::Unsupported` 반환.

---

## 3. 실측 검증 결과 (Windows 물리 GPU 2종 + Validation + 설치 소비자)

테스트 스위트: `tests/NativeInteropTests.cpp` (`gfx_native_interop_tests`).
- **Same-API Interop**: 동일 물리 GPU에 독립 장치 `devA`·`devB`를 생성해 64 KiB 공유 버퍼, 64×64 RGBA8 공유 텍스처, 공유 `TimelineFence`를 export → import하고, `devA` 쓰기·시그널 → `devB` 대기·검증·수정·시그널 → `devA` 대기·역검증 및 거부 경계(비공유 자원 export, 빈/불일치 핸들 import, 다른 장치 핸들 조회, 핸들 소유권 보존)를 전 바이트·전 픽셀 확인.
- **Cross-API Interop (`D3D12 <-> Vulkan`)**: 동일 물리 어댑터(NVIDIA GeForce GTX 1660 SUPER, Intel UHD Graphics 770)에 D3D12 장치와 Vulkan 장치를 동시 생성해 D3D12에서 64 KiB 공유 버퍼와 공유 타임라인 펜스를 export → Vulkan에서 `D3D12Resource`/`D3D12Fence`로 import해 D3D12 쓰기 → Vulkan 읽기·변환·시그널 → D3D12 대기·전 바이트 검증 수행.

| 실행 모드 | 검증 GPU 어댑터 | Assertions | Rejections | Validation 오류 | 결과 |
| --- | --- | ---: | ---: | ---: | --- |
| `--d3d12` | GTX 1660 SUPER, Intel UHD 770 (2) | 8,348 | 16 | N/A | **PASS** |
| `--d3d12 --validation` (Debug + GBV) | GTX 1660 SUPER, Intel UHD 770 (2) | 8,350 | 16 | **0** | **PASS** |
| `--vulkan` | GTX 1660 SUPER, Intel UHD 770 (2) | 8,348 | 16 | N/A | **PASS** |
| `--vulkan --validation` (고정 VVL 1.3.290) | GTX 1660 SUPER, Intel UHD 770 (2) | 8,350 | 16 | **0** | **PASS** |
| `--cross-api` (D3D12 ↔ Vulkan) | GTX 1660 SUPER, Intel UHD 770 (2) | 99 | 0 | N/A | **PASS** |
| `--cross-api --validation` (Debug+GBV & VVL 1.3.290) | GTX 1660 SUPER, Intel UHD 770 (2) | 101 | 0 | **0** | **PASS** |

- **CTest (`native_interop_*`)**: 5/5 PASS (`5.21s` — `native_interop_vulkan`, `native_interop_vulkan_validation`, `native_interop_d3d12`, `native_interop_d3d12_validation`, `native_interop_cross_api`).
- **기존 회귀 CTest (CPU 계약 + WARP + Validation)**: 44/44 PASS (`29.80s`).
- **독립 설치 소비자 (`tests/consumer`, `find_package(GraphicsSDK CONFIG REQUIRED)`)**: `out/p11-native-interop-install` 대상 빌드 및 `consumer.exe` 실행 PASS (`GraphicsSDK 0.0.1`, 64 MiB retain/release, 1,024픽셀 Depth/MRT).

---

## 4. 재현 절차

```powershell
$env:VK_LAYER_PATH = "D:\source\GraphicsSDK\out\vulkan-validation-runtime\build\Vulkan-ValidationLayers\layers\Release"
cmake -S . -B out/p11-native-interop -DGFX_ENABLE_VULKAN=ON
cmake --build out/p11-native-interop --config Release --target gfx_native_interop_tests -j 8
cmake --install out/p11-native-interop --config Release --prefix out/p11-native-interop-install

./out/p11-native-interop/Release/gfx_native_interop_tests.exe --d3d12 --validation
./out/p11-native-interop/Release/gfx_native_interop_tests.exe --vulkan --validation
./out/p11-native-interop/Release/gfx_native_interop_tests.exe --cross-api --validation
ctest --test-dir out/p11-native-interop -C Release -R native_interop --output-on-failure
```

---

## 5. 잔여 범위 (`P11-INT-02`)

- 본 완료(`P11-INT-01`)는 Windows D3D12·Windows Vulkan 및 D3D12 ↔ Vulkan Cross-API 실기 검증까지다.
- Linux Vulkan(`VK_KHR_external_memory_fd`/`VK_KHR_external_semaphore_fd` 기반 `OpaqueFd` 경로) 및 macOS Metal(`native*Handle` 비소유 조회·외부 핸들 `Unsupported` 경계)의 실제 빌드·실행 검증은 **`P11-INT-02`**로 분리해 추적한다.
