# Metal graphics·입력·binding 준비

P4-MTL-05 구현 준비다. Metal native graphics pipeline, vertex/index/instance 입력,
immutable binding snapshot과 실제 draw를 추가한다. ShaderFormat 끝에 MSL을 추가해 기존
DXBC=0/SPIRV=1 값을 유지한다. MSL은 UTF-8 source와 고정 gfx_vertex/gfx_fragment entry다.
HLSL 변환·metallib 배포·공개 MSL reflection/compute API는 추가하지 않는다. 공개 header의
struct/vtable는 유지하며 새로운 enum 값과 Metal mapping 주석만 추가한다.

## 계약

- Native MSL compiler·function stage·active attribute와 Metal 3 호환 render pipeline
  argument reflection을 검사한다. vertex attribute i는 attributes[i], stream slot 0..14는
  native buffer 16+slot이다. 상수는 buffer 0..13의 bounded flat numeric struct, texture는
  non-depth float texture2d, sampler는 static sampler를 사용한다. space는 0만 지원한다.
  잘못된/누락/초과/visibility/range/type interface는 거부한다. 중첩/포인터/argument buffer,
  writable graphics storage·sampler 배열 등 지원 밖 입력은 Unsupported다.
- BindingLayout texture slot+element, constant buffer slot+element, StaticSampler slot을
  각 vertex/fragment stage에 적용한다. texture 배열 및 Point/Linear·Clamp/Repeat를 지원한다.
  상수 view는 256 정렬·256..65536 size·reflection minimumBytes·range를 검사한다.
- Vertex Float/Float2/3/4/UNorm8x4, per-vertex/per-instance rate, 16/32 index, nonzero view
  offset·first vertex/index/instance·base vertex를 지원한다. CPU range/alignment/stride와
  상태/세대를 검사한다. indexed draw의 index 값+baseVertex가 vertex view 안에 있다는
  보장은 기존 공개 API처럼 caller 책임이며 shader source의 모든 메모리 의미를 검증하지 않는다.
- Pass format/depth와 pipeline을 일치시키고 read-only depth에 writing pipeline draw를
  거부한다. triangles·fixed full viewport·scissor, source-alpha blending, depth compare/write,
  depth-only와 MRT를 지원한다. winding cull은 하지 않는다.
- Draw는 native pipeline, resource snapshot, vertex/index views·scissor를 값으로 보유한다.
  이후 재바인딩과 실패한 binding 요청이 기존 draw를 바꾸지 않는다. accepted bind의
  observed version은 CPU write/다른 제출에 의해 stale이 되며 submit 전에 거부한다.
  native pipeline/sampler와 allocations는 wrapper와 별도로 recording/fence에 보유한다.
  Device 퇴역 시 native pipeline/depth/sampler도 해제한다.
- ShaderRead/ConstantBuffer/VertexBuffer/IndexBuffer/결합 input state를 추가하고 나머지
  texture 초기화·tracked hazard·managed flush/readback·반환/회수 계약은 유지한다.
  native validation/device recovery·timestamps·표시·공개 compute는 여전히 미지원이다.

## 검증 준비

reviewed `tests/fixtures/metal/graphics.metal`을 native compiler와 reflection으로 검사하고
18개 전체 이미지·row padding을 native CTest/직접/설치 소비자에서 비교한다. procedural draw,
UNorm vertex, UInt16/32 indexed base/first/view offset, instance/baseInstance, 좌우 불변 binding
snapshot, texture 배열·static sampler filter/address, read-only depth·MRT/BGRA·depth-only,
HDR·alpha blend/scissor를 포함한다. 재바인딩 실패·native compiler 오류·잘못된 stage/entry/
attribute/layout/space/view/range/세대/thread·stale submit도 검사한다.

recorded_draws=20은 참조 시나리오의 GPU 완료 draw 19개와 stale 제출 제어에서 수락된 CPU
기록 1개를 포함한다. 실제 완료 draw 수나 성능 카운터로 재사용하지 않는다. 원본 이미지와
CPU 참조가 실제 출력 근거다. 색은 UNorm 1.01/255·half .002, depth 1e-6과 finite 검사를
사용하고 모든 padding은 정확히 일치해야 한다. native fault/busy/timeout 강제 주입은 없다.

v4 receipt는 이전 v1/v2/v3 증거를 새 draw 검증으로 승격하지 않는다. 전체 Python suite와
동일 source/SDK fingerprint·compiler graph·header·fixture/source ZIP·원본 stdout/stderr·
PID/직렬 시간·binary hash/크기와 readback을 보관한다. 현재 checker가 archived code나
native executable을 실행하지 않고 모든 pixel을 독립 재검증한다. 기존 증거도 유지한다.

물리 GPU·성능/native validation·선행 Phase 3/P4 수용은 미완료이며
143개 roadmap 상태와 active Phase 3를 유지한다.

Apple의 [pipeline reflection](https://developer.apple.com/documentation/metal/mtlrenderpipelinereflection),
[indexed/base vertex draw](https://developer.apple.com/documentation/metal/mtlrendercommandencoder/drawindexedprimitives%28type%3Aindexcount%3Aindextype%3Aindexbuffer%3Aindexbufferoffset%3Ainstancecount%3Abasevertex%3Abaseinstance%3A%29),
[instance step](https://developer.apple.com/documentation/metal/mtlvertexbufferlayoutdescriptor/stepfunction) API를 사용한다.


## Native 실행과 독립 재생

실행 소스는 `a180fbba9d13b2334b29bbf412058bd44192d1b3`, SDK source fingerprint는
`dfdf8375c729400b32ec194aa1a0d973d964b9c161d17d547c54d20044841ac6`이다.
[macOS arm64·Intel 실행](https://github.com/blackvrice/GraphicsSDK/actions/runs/41628085547),
[Windows·Linux 회귀](https://github.com/blackvrice/GraphicsSDK/actions/runs/41628085502),
[Linux Vulkan compile](https://github.com/blackvrice/GraphicsSDK/actions/runs/41628085728)을 사용했다.

| 검사 | arm64 | x86_64 |
|---|---:|---:|
| Metal ON CPU+Metal CTest | 15/15 | 15/15 |
| 설치 public-only Metal 소비자 | 4/4 | 4/4 |
| 전체 Python 계약 | 1,501 | 1,501 |
| graphics 직접/설치 실행 assertions | 각각 73,300 | 각각 73,300 |
| graphics 전체 readback 파일 | 36 | 36 |
| graphics 전체 검증 bytes | 322,048 | 322,048 |

그래픽스 readback은 양쪽 합계 72개·644,096 bytes다. CTest 실행도 같은 18개 이미지를
비교하지만 별도 파일은 만들지 않는다. buffers v2의 44개·4,475,426 bytes와 textures v3의
88개·648,704 bytes도 각 아키텍처에서 다시 검사했다. CPU Core OFF 빌드·설치 소비자 및
장치/offline shader 조사도 별도로 통과했다.

일반 GitHub runner에서 관측한 장치는 양쪽 모두 `Apple Paravirtual device`다.
ARM은 unified memory, Intel은 managed host buffer 경로를 실행한다. macOS는 ARM 15.8.2,
Intel 15.7.9이며 Xcode 16.4/Apple clang 17, SDK 15.5, Release `-O2 -DNDEBUG`, LTO/unity OFF다.
실제 native API/draw 결과이며 물리 Mac GPU·Metal validation·성능 측정 수용으로 승격하지 않는다.

원본은 [graphics evidence](../benchmarks/reports/validation/2026-11-03-metal-graphics/summary.json)에
보관한다. source ZIP 14개 입력, SDK/test compile graph 21개 unit(그중 private ARC Objective-C++
1개), 설치 consumer graph 4개 unit, public header 13개, fixture·전체 pixel/padding·raw 명령 로그·
cache·receipt·binary hash/크기를 검증한다. native binary·system framework는 보관하지 않는다.
원본 600개 파일을 ZIP 1,305,065 bytes로 묶었다. manifest는 자체 hash 목록이며 외부 신뢰 서명이 아니다.

```sh
python3 benchmarks/reports/validation/2026-11-03-metal-graphics/verify.py
```

이 재생을 Windows/Linux/macOS CI에 연결한다. archived source·shader·native executable을
실행하지 않고 현재 검사기로 재생하며, 이전 Metal v1/v2/v3·availability/Core 및 Linux
증거를 유지한다. 로드맵에는 P4-MTL-05 진행 근거만 추가하고 선행 gate 상태는 유지한다.
