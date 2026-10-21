# Phase 2 vertex/index 입력

2026-10-21. 로드맵의 다음 항목에서 vertex/index 입력을 구현했다. 기본 vertex-ID draw 경로를
유지하면서 선언형 vertex layout, bounded buffer view, indexed/instanced draw를 추가했다.
**Phase 2 전체 완료는 아니다.** 일반화된 resource binding과 depth/MRT가 다음 항목이다.

## 구현

- `VertexLayout`: 32개 buffer slot/attribute, stride 최대 2048, Float/Float2/Float3/Float4/UNorm8x4.
- per-vertex/per-instance input, explicit semantic/index/offset. instance step은 1이다.
- `bindVertexBuffer`/`bindIndexBuffer`, UInt16/UInt32, byte view offset/size 및 alignment 검사.
- `draw`의 firstVertex/firstInstance와 `drawIndexed`의 firstIndex/signed baseVertex/firstInstance.
- VertexBuffer/IndexBuffer/VertexAndIndexBuffer 논리 상태, Device heap의 native barrier,
  Upload GENERIC_READ 유지, texture/readback의 부적절한 상태·binding 거부.
- pipeline 전환 후 요구 slot/stride 재검사, draw 직전 state 재검사, begin 시 binding 초기화.
- 기존 resource snapshot/version 검사와 native fence retention에 input buffer를 연결.

입력 구조 검증은 backend와 독립적인 `validateVertexLayout`에 둔다. GPU device 없이 잘못된
slot, enum, semantic, 중복, extent/alignment, stride를 검사한다. pipeline 생성도 같은 검사를
native PSO 생성 전에 실행한다. 지원 범위는 [public API](PUBLIC_API.md)에 정의했다.

native input element와 indexed draw 매개변수는 Microsoft의
[input element 계약](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_input_element_desc)과
[DrawIndexedInstanced 계약](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-drawindexedinstanced)에 맞춘다.
CPU가 아는 view/window·instance 범위는 검사하지만, GPU에서 공급되는 index 값과 baseVertex를
더한 결과의 유효성은 호출자 계약이다. 검사를 위해 GPU index data를 매 draw readback하지 않는다.
유효한 음수 baseVertex를 테스트했고 일반적인 signed offset을 임의로 금지하지 않았다.

## 실제 검증

| 검사 | 결과 |
|---|---|
| Core CTest | 8/8, skip 없음. 기존 4개와 portable layout/new input 4개 |
| 하드웨어 / debug+GBV / WARP | 각 8가지 입력 구성의 64×64 전체 CPU 이미지 참조 통과 |
| D3D12 OFF | Windows 별도 빌드 및 CPU CTest 2/2. Linux 실행으로 일반화하지 않음 |
| 공통 benchmark correctness | 새 SDK core를 연결해 72/72 통과 |
| Python | 전체 56/56 통과 |
| 설치 consumer | 새 layout 함수, capability와 vertex binding 사용·submit·wait 통과 |
| indexed-instancing 예제 | 실제 GPU 출력 256×256의 65,536개 RGB pixel이 별도 CPU 사각형 참조와 tolerance1로 일치 |

8가지 GPU 구성은 Upload/Device non-indexed, Upload UInt16/Device UInt32 양수 base,
Upload UInt32/Device UInt16 음수 base, 같은 Device buffer의 vertex+index view, 분리된
Float/Float2 입력이다. 나머지 구성의 Float3/Float4/UNorm8x4와 합쳐 모든 공개 형식을 검증한다.
non-contiguous slot3/7, 첫 vertex/index/instance offset, view offset, 중간 pipeline 전환도 포함한다.
두 instance가 화면을 정확히 반으로 나누어 적색·녹색으로 채우는 전체 CPU 참조를 사용한다.

잘못된 slot/stride/view, 정렬, 부족한 instance, index window/정수 overflow, 누락 binding,
pipeline stride 불일치, state 변경 후 draw, foreign device/thread를 거부했다. 정상 binding
뒤 실패한 rebind가 기존 상태를 훼손하지 않는 것도 실제 출력으로 확인했다. vertex 및 index
Upload buffer를 recording 후 수정하면 submit이 거부된다. 성공한 submit 뒤 buffer/pipeline/
target/command wrapper를 해제하고 fence 완료 후 전체 이미지를 읽어 native 수명을 검증했다.
debug/GBV의 ERROR/CORRUPTION은 0이었다.

[검증 로그·이미지·해시](../benchmarks/reports/validation/2026-10-21-phase2-vertex-input/)와
[재현 증거 목록](../benchmarks/reports/2026-10-21-phase2-vertex-input/evidence.json)을 보존한다.
기존 MSVC 병렬 compiler crash와 일회성 root-signature 초기화 실패의 원인 미확정 이력은
그대로다. 이번 serial build와 위 테스트에서 재현되지 않았다는 범위만 확인한다.

## 사용과 산출물

`examples/IndexedQuads.cpp`는 quad vertex 4개와 UInt16 index 6개, 별도 instance stream을 사용해
두 사각형을 한 indexed instanced draw로 렌더링하고 PPM으로 저장한다.

```powershell
cmake --preset windows
cmake --build --preset windows-release --parallel 1
ctest --test-dir out/core -C Release --output-on-failure
./out/core/Release/gfx_indexed_quads.exe out/indexed-quads.ppm
```

로컬 새 SDK ZIP은 `out/GraphicsSDK-0.0.1-dev-windows-x64-vertex-input-final.zip`이다. 새 헤더·정적
라이브러리·CMake package를 함께 설치하고 별도 consumer를 재빌드했다. 0.0.1 개발 API이므로
새 virtual 함수/descriptor에 대해 예전 바이너리 ABI 호환을 보장하지 않는다.
이전 SDK ZIP과 모든 Phase 1 측정·소스 archive는 덮어쓰지 않았다.

신규 외부 dependency는 없다. 새 SDK compiler-read inventory는 15개 프로젝트 입력/자체
구성요소 1개이며 package 검사와 `--check`가 통과했다. 공통 benchmark의 SDK core도 재빌드해
별도 inventory를 보존한다. inventory는 compiler-read closure이며 재배포의 법적 승인이 아니다.
변경된 SDK의 성능 우위를 새로 주장하지 않고, 이전 성능 자료는 당시 fingerprint의 역사 자료다.

이번에는 Vulkan/Metal, 일반화된 shader resource binding, depth/MRT, device-loss 복구,
장기 stress 및 interactive presentation acceptance를 완료하지 않았다.
