# 경쟁 SDK 분석

조사일 2026-10-20. 모든 판단은 아래 고정 커밋의 공식 README, 공개 API 및
라이선스에 근거한다. 원문 파일 목록과 SHA는
[`sources.lock.json`](../benchmarks/competitors/sources.lock.json)에 있다.
성능 설계는 측정된 성능 우위를 의미하지 않는다. 빌드 시간·바이너리 크기·CPU/GPU
시간은 실제 실행한 구성만 BENCHMARK_REPORT에 기록한다.

## 조사 기준

| SDK | 고정 커밋 | 조사한 공식 소스 |
|---|---|---|
| LLGL | `649c0cc8fa608312c5e4b94dae409a5859fc7553` | [RenderSystem/API](https://github.com/LukasBanana/LLGL/tree/649c0cc8fa608312c5e4b94dae409a5859fc7553/include/LLGL) |
| bgfx | `d8d6acd94b64e01da3c4d1bc869df2dc2a8eb1ba` | [API](https://github.com/bkaradzic/bgfx/blob/d8d6acd94b64e01da3c4d1bc869df2dc2a8eb1ba/include/bgfx/bgfx.h), [D3D12](https://github.com/bkaradzic/bgfx/blob/d8d6acd94b64e01da3c4d1bc869df2dc2a8eb1ba/src/renderer_d3d12.cpp) |
| Diligent Engine | `9c63c12ec2a9f541b6a42851d30d851605e6782a` | [README 및 라이선스](https://github.com/DiligentGraphics/DiligentEngine/tree/9c63c12ec2a9f541b6a42851d30d851605e6782a) |
| DiligentCore | `4678e4393cd0194f3c56e5d6611221d46497cc95` | [DeviceContext](https://github.com/DiligentGraphics/DiligentCore/blob/4678e4393cd0194f3c56e5d6611221d46497cc95/Graphics/GraphicsEngine/interface/DeviceContext.h) |
| The Forge | `cd5046893faba2dc7869243873bf01f02a6f0df9` | [IGraphics](https://github.com/ConfettiFX/The-Forge/blob/cd5046893faba2dc7869243873bf01f02a6f0df9/Common_3/Graphics/Interfaces/IGraphics.h) |
| Dawn | `f7b0358d022045bca67dda4127e5790d3fd1ae7b` | [README](https://github.com/google/dawn/blob/f7b0358d022045bca67dda4127e5790d3fd1ae7b/README.md), `src/dawn/native/Device.h` |
| NVRHI | `6b96fb03e07539f08327aea76c56d55f1de9d906` | [README/API](https://github.com/NVIDIA-RTX/NVRHI/tree/6b96fb03e07539f08327aea76c56d55f1de9d906) |
| SDL3 GPU | `3b2bc951500ca6a1f428af77e036e732d9a8afe3` | [SDL_gpu.h](https://github.com/libsdl-org/SDL/blob/3b2bc951500ca6a1f428af77e036e732d9a8afe3/include/SDL3/SDL_gpu.h) |

DiligentCore는 인터페이스 조사를 위한 별도 HEAD 스냅샷이다. 위 Engine 커밋의
서브모듈과 동일하다고 가정하지 않는다. Diligent 실행 비교 전에는 Engine이 지정한
서브모듈 트리를 재귀적으로 고정해야 한다.

## 우선 조사 대상

### LLGL

RenderSystem의 가상 인터페이스, 명시적 Buffer/Texture 객체와 Release, 별도
CommandBuffer/CommandQueue, PipelineState/ResourceHeap을 제공한다.
공개 객체 포인터와 명령 버퍼를 사용하므로 얇은 공통 계층을 설계할 때 참고하기 좋다.
명령 버퍼 제출, 펜스, query heap, compute와 native handle API가 있다.
파이프라인 생성과 캐시는 분리되어 있다. 셰이더는 백엔드별 소스/바이너리를 받는다.

장점은 비교적 직접적인 API와 다양한 작은 예제다. 비용은 백엔드별 기능 격차와
명시적 Release 책임이며, 기본 텍스처 초기화 정책도 할당 측정에 영향을 준다.
이번 할당 어댑터는 `MiscFlags::NoInitialData`와 mip 1을 명시한다.
스레드별 명령 버퍼는 백엔드의 thread safety 계약을 확인한 후 확장해야 한다.
Frame-in-flight/메모리 서브할당/descriptor 재사용의 상세 비용은 프로파일링 전 미측정이다.
독립 CMake 빌드에 D3D12만 선택할 수 있고 SDK 소비자에게 LLGL 의존성을 전파할 필요가 없다.
Render graph는 조사한 공개 코어 API에서 확인되지 않았다.

### bgfx

작은 정수형 리소스 핸들, view, Encoder와 `frame()` 중심이다. API/렌더 스레드와
멀티스레드 encoder 모델이 있으며 제출 데이터를 정렬하고 렌더 스레드가 실제 API
명령을 만든다. Program/Uniform/Texture 핸들과 shaderc 기반 셰이더 빌드가 특징이다.
백엔드에서 PSO/descriptor 캐시 및 frame 상태를 관리한다. compute와 native resource
override/interoperability 경로도 있다.

장점은 이식 가능한 사용 흐름과 예제다. 비용은 정렬·배칭·비동기 처리가 포함되어
`submit()` 시간만으로 GPU에 도달하는 전체 CPU 비용을 알 수 없다는 점이다.
Drawstress 예제의 FPS를 LLGL의 명령 기록 비용과 직접 비교하면 안 된다.
bx/bimg와 GENie 중심 빌드 도구를 포함한 의존성·툴체인 설정도 별도 비용이다.
공개 view 모델은 범용 의존성 render graph와 동일하지 않다.
GraphicsSDK에는 쉬운 초기화와 capability 조회를 참고하고 전역 단일 렌더 컨텍스트는
도입하지 않는다. bgfx 어댑터는 현재 미구현이며 성능 수치는 없다.

### Diligent Engine / Core

C/C++ 참조 카운트 인터페이스의 IRenderDevice/IDeviceContext와 PipelineState,
ShaderResourceBinding을 제공한다. static/mutable/dynamic 셰이더 변수 수명 구분이
바인딩 갱신 비용을 조절한다. 자동 transition/명시적 transition/검증 모드를 분리하고
deferred context로 병렬 기록한다. native D3D/Vulkan interop을 제공한다.

장점은 일관된 검증, 리소스 상태 모델과 풍부한 튜토리얼이다. 비용은 기능과 도구
의존성의 크기, 변수 수명 및 상태 전이 정책 학습이다. shader 변환·reflection과
PSO 캐시를 활용할 수 있지만 cold compilation과 runtime 바인딩은 별도로 재야 한다.
공개 Apache 코어의 Metal 포함 여부를 README의 플랫폼 표만 보고 판단하면 안 된다.
**Metal은 상용 클라이언트용 별도 모듈**이다. 공개 코어 빌드와 비교 시 Metal은 제외한다.
조사한 코어 인터페이스에서 일반 render graph는 확인하지 못했다.
GraphicsSDK에는 명시적 상태 검증과 오류 문맥을 참고한다.

### The Forge

C 스타일 Renderer/Queue/Cmd/Buffer/Texture/Pipeline/DescriptorSet API다.
루트 시그니처·descriptor set·명시적 resource barrier와 fence/semaphore를 사용한다.
FSL 및 플랫폼 compiler를 연결하고 리소스 로더/메모리 관리, 병렬 command recording을
고려한 프레임워크다. 네이티브 타입을 포함하는 플랫폼별 구조체도 있다.

장점은 폭넓은 실전 렌더링 사례와 명시적 저수준 제어다. 비용은 플랫폼 설정과
관련 모듈·툴의 통합 범위다. 단일 작은 설치형 라이브러리의 사용성과는 별도 평가한다.
공개 README는 1.62에서 Windows Vulkan 공식 런타임 지원 종료, 콘솔 상용 라이선스
정책, 1.64 이후 Codeberg 이동을 명시한다. 여기서는 사용자가 지정한 GitHub 스냅샷만
분석했으며 새 호스팅의 최신 상태까지 확인했다고 주장하지 않는다.
GPU work graph API는 범용 render graph와 구분한다.
GraphicsSDK에는 명시적 queue/fence와 리소스 로더의 비용 분리를 참고한다.

후속 [D3D12 측정 프로파일](FORGE_BENCHMARK_REPORT.md)은 고정 스냅샷의 8개 공통 작업을
실행했다. VS 2022 외부 설정·중복 GUID 호환, legacy NVAPI 제외, 측정 밖 native readback을
명시한 구성이다. 원본의 모든 third-party가 Apache-2.0이라는 뜻은 아니며
[추가 고지 감사](DEPENDENCY_LICENSES.md#the-forge-추가-profile)를 따로 기록했다.

## 추가 비교 대상

* **Dawn:** WebGPU C API와 C++ wrapper, native backend와 선택적 wire, WGSL/Tint,
  bind group/pipeline layout, 검증·캐시와 비동기 작업 모델. 표준화된 검증과 오류 범위가
  장점이며 raw native API와 다른 보장 및 제한 때문에 별도 비용 범주가 필요하다.
* **NVRHI:** 참조 카운트 handles, graphics/compute pipeline, binding layouts/sets,
  선택적 상태 추적, 자동 수명/지연 해제, 병렬 명령 목록과 다중 큐가 특징이다.
  명시적 native API 연동이 편리하지만 장치 생성은 애플리케이션 통합 책임이다.
  공개 지원은 D3D11/12 및 Vulkan 1.3 Windows/Linux이며 Metal backend는 없다.
* **SDL3 GPU:** C API opaque pointers, graphics/compute pipeline, render/copy pass,
  transfer buffers, fence와 자원 cycling. SDL 생태계와 간단한 사용 흐름이 장점이다.
  셰이더 바이너리는 외부에서 준비하고 지정된 binding 규칙을 지켜야 한다.
  자동 자원 상태·재사용 비용을 raw 명령 비용으로 해석하지 않는다.
  조사한 공개 API에는 GPU timestamp query와 일반 render graph가 없다.

## 기능과 라이선스

상세 표는 [FEATURE_MATRIX](FEATURE_MATRIX.md). 라이선스 원문은 각 SHA의 저장소에
연결한다. 다음은 루트 프로젝트 조건이며 하위 의존성을 포괄하지 않는다.

| SDK | 루트 라이선스 | 배포 시 확인할 조건 |
|---|---|---|
| LLGL | [BSD-3-Clause](https://github.com/LukasBanana/LLGL/blob/649c0cc8fa608312c5e4b94dae409a5859fc7553/LICENSE.txt) | 저작권·조건·면책 유지, endorsement 제한 |
| bgfx | [BSD-2-Clause](https://github.com/bkaradzic/bgfx/blob/d8d6acd94b64e01da3c4d1bc869df2dc2a8eb1ba/LICENSE) | 소스/바이너리 고지 유지; bx/bimg 별도 |
| Diligent | [Apache-2.0](https://github.com/DiligentGraphics/DiligentEngine/blob/9c63c12ec2a9f541b6a42851d30d851605e6782a/License.txt) | 라이선스·NOTICE·변경 고지 및 의존성; Metal 별도 계약 |
| The Forge | [Apache-2.0](https://github.com/ConfettiFX/The-Forge/blob/cd5046893faba2dc7869243873bf01f02a6f0df9/LICENSE) | 공개 코드와 콘솔/비공개 미들웨어 구분 |
| Dawn | [BSD-3-Clause](https://github.com/google/dawn/blob/f7b0358d022045bca67dda4127e5790d3fd1ae7b/LICENSE) | Tint/third-party 각각 검토 |
| NVRHI | [MIT](https://github.com/NVIDIA-RTX/NVRHI/blob/6b96fb03e07539f08327aea76c56d55f1de9d906/LICENSE.txt) | 저작권·면책 유지, 헤더 의존성 확인 |
| SDL | [zlib](https://github.com/libsdl-org/SDL/blob/3b2bc951500ca6a1f428af77e036e732d9a8afe3/LICENSE.txt) | 출처 오인 금지, 변경 표시, 고지 보존 |

코드 복제 없이 새 SDK를 작성한다. 비교 SDK는 선택적 벤치마크 의존성이다.
추가 상용 기능이나 third-party를 GraphicsSDK MIT 라이선스로 재라이선스하지 않는다.

## 적용 우선순위와 미검증 사항

1. 비용 경계가 명확한 명령 기록/제출/대기와 capability 조회.
2. 제출 전 참조까지 보호하는 리소스 소유권 및 완료 펜스.
3. 입력 오류, backend 미지원, device loss를 구분하는 Result.
4. 실제 필요 전에는 render graph·media·게임 계층 의존성을 추가하지 않기.
5. 설치 후 외부 소비자 빌드 검증과 선택적 벤치마크 의존성 분리.

학습 난도와 통합 난도는 API 검토에 따른 정성적 판단이다. 사용자 연구나 수치
순위가 아니다. descriptor hit rate, shader/PSO cold/warm 비용, 메모리 효율, build time,
binary size 및 각 플랫폼 런타임 안정성은 각각 측정/검증될 때까지 미확정이다.
