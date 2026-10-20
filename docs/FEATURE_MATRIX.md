# 기능 비교표

조사 기준 SHA와 공식 근거는 [COMPETITOR_ANALYSIS](COMPETITOR_ANALYSIS.md).
`있음`은 공개 API/문서에서 확인했다는 뜻이며 이 환경의 런타임 검증을 뜻하지 않는다.
`미확인`은 부재를 단정하지 않는다.

| SDK | D3D12 | Vulkan | Metal | Compute | Resource binding | 상태/동기화 | Render graph |
|---|---|---|---|---|---|---|---|
| LLGL | 있음 | 있음, 빌드 옵션 experimental | 있음 | 있음 | ResourceHeap/PipelineLayout | command queue/fence, backend 처리 | 공개 코어 미확인 |
| bgfx | 있음 | 있음 | 있음 | 있음 | uniform/texture handles | frame/encoder, 내부 동기화 | view는 범용 graph 아님 |
| Diligent Core | 있음 | 있음 | 별도 상용 | 있음 | SRB, static/mutable/dynamic | transition/verify/none, fences | 코어 미확인 |
| The Forge | 있음 | Linux/Android 등, Windows 공식 지원 종료 | 있음 | 있음 | descriptor sets/root signatures | 명시적 barriers/fence/semaphore | work graph와 구분, 미확인 |
| Dawn | 있음 | 있음 | 있음 | 있음 | bind groups/layout | WebGPU 검증과 queue completion | core 미확인 |
| NVRHI | 있음 | 있음 (1.3) | 없음 | 있음 | binding sets/layout | 선택적 상태 추적, 자동 수명 | core 미확인 |
| SDL3 GPU | 있음 | 있음 | 있음 | 있음 | 정해진 shader slots | copy/render pass, fences/cycling | core 미확인 |

| SDK | 공개 소유권 | 병렬 기록 | 셰이더 경로 | Native interop |
|---|---|---|---|---|
| LLGL | 객체 포인터/Release | backend 계약 확인 필요 | backend source/binary | native handle |
| bgfx | 정수 handle/destroy | Encoder | shaderc | platform data/override 경로 |
| Diligent | ref-count interface | deferred context | HLSL/번역/backend binary | API별 interface |
| The Forge | opaque/플랫폼 구조체 포인터 | 독립 Cmd/pool | FSL + platform compiler | 플랫폼별 구조체 |
| Dawn | ref-count WebGPU objects | encoder 및 API 계약 | WGSL/Tint | native extensions, 별도 검증 |
| NVRHI | ref-count handles | 독립 command list | 미리 컴파일한 binary | native device/object |
| SDL3 GPU | opaque pointer/release | thread safety 계약별 | DXBC/DXIL/SPIR-V/MSL/metallib | 표준 GPU 코어와 extension 구분 |

GraphicsSDK의 구현·빌드·실행 검증 상태는 [ROADMAP](ROADMAP.md)에서 별도로 추적한다.
경쟁 SDK의 전체 기능과 아직 작은 GraphicsSDK 구현을 동급 완성도로 취급하지 않는다.
