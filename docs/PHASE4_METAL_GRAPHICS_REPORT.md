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

실제 결과는 native CI 후 추가한다. 물리 GPU·성능/native validation·선행 Phase 3/P4
수용은 미완료이며 143개 roadmap 상태와 active Phase 3를 유지한다.

Apple의 [pipeline reflection](https://developer.apple.com/documentation/metal/mtlrenderpipelinereflection),
[indexed/base vertex draw](https://developer.apple.com/documentation/metal/mtlrendercommandencoder/drawindexedprimitives%28type%3Aindexcount%3Aindextype%3Aindexbuffer%3Aindexbufferoffset%3Ainstancecount%3Abasevertex%3Abaseinstance%3A%29),
[instance step](https://developer.apple.com/documentation/metal/mtlvertexbufferlayoutdescriptor/stepfunction) API를 사용한다.
