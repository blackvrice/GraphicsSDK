# Phase 11 외부 AI 연산 연계 설계 및 검증 보고서 (`P11-AI-01`)

- 작성일: 2026-11-22
- 관련 작업 ID: `P11-AI-01` (완료 — 소유권·동기화 경계 설계, 비침투 브리지 예제 및 Windows D3D12/Vulkan/Cross-API 프로토타입 검증), `P11-AI-02` (대기 — Linux Vulkan / macOS Metal 실기·CI 검증)
- 독립성 원칙: **SDK 공개 헤더(13개) 및 코어 번역 단위(5개 `CORE` `.cpp`) 무수정**, CUDA / DirectML / ONNX Runtime 빌드·링크 의존성 0.
- 로컬 환경 탐지 결과: Windows 시스템에 `C:\Windows\System32\DirectML.dll` 및 `C:\Windows\System32\nvcuda.dll` 존재 확인(단, SDK 코어는 이들을 직접 링크하지 않고 공개 Native/External Interop 계약으로만 연계).

---

## 1. 소유권 및 동기화 경계 설계 (`examples/ai_compute_interop/AiTensorInteropBridge.hpp`)

GraphicsSDK는 AI 추론 엔진을 내장하지 않고, 다음 3가지 표준 연계 모드를 `<graphics/Graphics.hpp>` 공개 계약만으로 제공한다.

| 연계 모드 | 대상 AI 런타임 | 핸들/메모리 전달 방식 | 동기화 프로토콜 | 소유권·수명 경계 |
| --- | --- | --- | --- | --- |
| **In-Process NativeHandle** (`InProcessNativeBinding`) | DirectML, ONNX Runtime D3D12 EP | `nativeDeviceHandle()`, `nativeBufferHandle()`, `nativeTimelineFenceHandle()` 비소유 포인터 전달 (`ID3D12Device*`, `ID3D12Resource*`, `ID3D12Fence*`) | 단조 증가 `TimelineFence` GPU-to-GPU 핑퐁 (`Graphics -> AI`: 홀수, `AI -> Graphics`: 짝수) | GraphicsSDK가 `BufferPtr`/`TimelineFencePtr` 소유; 외부 AI 작업 완료 전 파괴 금지; 타 장치 핸들 조회 시 `WrongDevice` 거부 |
| **External-Handle Zero-Copy** (`ExportedInteropHandles`) | CUDA (`cudaImportExternalMemory` / `cudaImportExternalSemaphore`), ONNX Runtime CUDA/TensorRT EP, 별도 Vulkan/D3D12 AI 프로세스 | `BufferDesc{bytes, Memory::Device, storage=true, shared=true}` + `exportBufferHandle()` + `createSharedTimelineFence()` + `exportTimelineFenceHandle()` (Win32 NT `D3D12Resource`/`D3D12Fence`/`OpaqueWin32Nt`, Linux `OpaqueFd`) | 양측 장치 `CommandQueue::submit(SubmitDesc{waits, signals})`로 CPU 대기 없는 GPU-to-GPU 타임라인 핑퐁 | OS 커널 핸들 참조 카운트로 양측 독립 수명 보장; `ExternalHandle` RAII 소멸 시 자동 `CloseHandle`/`close` |
| **Safe-Copy Host-Staging Fallback** (`HostStagingFallback`) | WARP, CPU-only ONNX Runtime EP, `externalMemoryInterop == false` 환경 | `Memory::Upload` / `Memory::Readback` 스테이징 버퍼 경유 복사 | 호스트 `queue().wait()` 후 CPU 추론 및 결과 재업로드 | Zero-Copy 불가 환경에서도 동일한 전처리/후처리 Compute 파이프라인과 텐서 형상(`TensorShape`) 유지 |

### 자원 상태 전이 경계
- 외부 AI 런타임으로 넘기기 전(`signal(sigPre)`) 텐서 버퍼를 `ResourceState::Common`으로 해제하고, 외부 AI 완료 후(`wait(sigAi)`) GraphicsSDK에서 `ResourceState::UnorderedAccess`로 재획득한다.
- 동일 프레임에서 `GraphicsSDK 전처리 → 외부 AI 추론(Affine+ReLU) → GraphicsSDK 후처리`가 단일 공유 FP32 버퍼(256B 정렬)에서 복사 0회로 완결된다.

---

## 2. 실기 프로토타입 검증 결과 (`tests/AiComputeInteropTests.cpp`)

- **워크로드**: `1×1×64×64` (`4,096` FP32 = 16 KiB) 텐서, HLSL `cs_5_1` 및 순수 SPIR-V 1.3(`tests/fixtures/ComputeFixtures.hpp::ai_affine_relu_fp32`, `OpCapability Shader` 단일)로 3연속 프레임 핑퐁(`12,288` FP32 원소) 실행 후 독립 CPU FP32 참조(`|diff| <= 1e-4f`) 전 원소 검증.
- **검증 항목**:
  1. In-Process `NativeHandle` 비영 조회 및 타 장치 `WrongDevice` 거부 (4건/모드)
  2. External-Handle Zero-Copy 3-Frame GPU-to-GPU Ping-Pong (Same-API D3D12, Same-API Vulkan, **Cross-API D3D12 ↔ Vulkan**)
  3. Safe-Copy Host-Staging Fallback (물리 GPU 2종 + WARP)

| 실행 모드 | 검증 GPU 어댑터 | 검증 추론 수 | Assertions | Rejections | Validation 오류 | 결과 |
| --- | --- | ---: | ---: | ---: | ---: | --- |
| `--d3d12` | GTX 1660 SUPER, Intel UHD 770 (2) | 8 (Fallback 2 + Ping-Pong 6) | 128 | 4 | N/A | **PASS** |
| `--d3d12 --validation` (Debug+GBV) | GTX 1660 SUPER, Intel UHD 770 (2) | 8 | 136 | 4 | **0** | **PASS** |
| `--d3d12 --warp` | Microsoft Basic Render Driver (1) | 1 (Fallback) | 19 | 0 | N/A | **PASS** |
| `--vulkan` | GTX 1660 SUPER, Intel UHD 770 (2) | 8 (Fallback 2 + Ping-Pong 6) | 122 | 4 | N/A | **PASS** |
| `--vulkan --validation` (VVL 1.3.290) | GTX 1660 SUPER, Intel UHD 770 (2) | 8 | 130 | 4 | **0** | **PASS** |
| `--cross-api` (D3D12 ↔ Vulkan) | GTX 1660 SUPER, Intel UHD 770 (2) | 6 (3-Frame × 2 GPU) | 95 | 4 | N/A | **PASS** |
| `--cross-api --validation` | GTX 1660 SUPER, Intel UHD 770 (2) | 6 (3-Frame × 2 GPU) | 101 | 4 | **0** | **PASS** |

- **CTest (`ai_compute_interop_*`)**: **6/6 PASS (`5.18s`)**.

---

## 3. 재현 절차 및 잔여 범위 (`P11-AI-02`)

```powershell
$env:VK_LAYER_PATH = "D:\source\GraphicsSDK\out\vulkan-validation-runtime\build\Vulkan-ValidationLayers\layers\Release"
cmake -S . -B out/p11-ai-interop -DGFX_ENABLE_VULKAN=ON -DGFX_VULKAN_VALIDATION_TEST=ON
cmake --build out/p11-ai-interop --config Release --target gfx_ai_compute_interop_tests -j 8
ctest --test-dir out/p11-ai-interop -C Release -R ai_compute_interop --output-on-failure
```

- Linux Vulkan(`OpaqueFd`) 및 macOS Metal(`HostStagingFallback` + 비소유 `NativeHandle`)에서의 실기·CI 검증은 **`P11-AI-02`**로 분리해 추적한다.
