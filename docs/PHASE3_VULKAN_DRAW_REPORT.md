# Phase 3 — Vulkan SPIR-V graphics pipelines and drawing

2026-10-23, Windows x64 / MSVC 19.44 / Windows SDK 10.0.26100.0.

Vulkan 1.1 backend에 SPIR-V graphics pipeline과 실제 triangle draw를 추가했다.
무바인딩 vertex/fragment, 시저, 알파 블렌딩, instancing과 혼합 색상 출력을 지원한다.
Depth·vertex buffer·binding·presentation을 포함한 Phase 3 전체 완료가 아니다.

## 입력 및 구현

`ShaderBinary` 끝에 `ShaderFormat`을 추가했다. 기본 DXBC는 기존 aggregate 초기화와
HLSL bootstrap 결과의 소스 호환성을 유지하지만 ABI는 바뀌므로 모든 소비자를 재빌드한다.
Vulkan은 명시적 SPIRV 형식과 **외부에서 Vulkan 1.1 환경으로 검증한 SPIR-V 1.3**을 요구한다.
Public `reflectShader`는 DXBC만 지원하고 SPIRV 요청은 Unsupported다.

새 private inspector는 4MiB 이하 byte/word/header/instruction framing과 지원 interface를
검사한다. Shader capability, Logical GLSL450 memory model, 하나의 `main`, standalone
float4 Position, 선택적 VertexIndex/InstanceIndex와 location 0..N-1의 float4 fragment
outputs를 받는다. OriginUpperLeft 외 execution mode, extension/import/member decoration,
일반 varying·descriptor/push/storage interface는 이 초기 path에서 거부한다.
**전체 SPIR-V semantic validator가 아니며**, 임의 binary의 의미적 유효성/안전성을 보증하지 않는다.
입력은 호출자가 신뢰할 수 있는 compiler/validator를 거친 유효 module이어야 한다.

이 저장소는 자체 작성 assembly 9개를 기존 고정 SPIRV-Tools
`0cfe9e7219148716dfd30b37f4d21753f098707a` 라이브러리에 연결한 로컬 전용 helper로
조립·검증했다. 잘못된 소스와 OriginUpperLeft가 빠진 의미 오류 module은 validator가
exit 1로 거부하며 GPU로 보내지 않았다. 이 helper/library/vendor code는 SDK에 링크·배포하지 않는다.
SDK 검사는 전체 validator로 표현하지 않고 CPU 결과에도 이를 명시한다.
[도구 명령](../benchmarks/reports/validation/2026-10-23-phase3-vulkan-draw/shader-tools/commands.json)과
[별도 provenance](../benchmarks/reports/validation/2026-10-23-phase3-vulkan-draw/shader-tools/receipt.json)를 기록했다.

Pipeline은 triangle-list, 무 vertex input/culling/depth/MSAA, 1..adapter 한도(공통 최대 8)
색상 출력을 사용한다. Native attachment/blend format 지원을 조회하고 활성 pass의
형식/순서/수를 대조한다. RGBA8/RGBA16F 및 혼합 출력이 동작한다.
RGB blending은 `src.rgb*src.a + dst.rgb*(1-src.a)`, alpha는 `src.a`로 기존 D3D12와 같다.
음수 viewport height로 clip +Y가 위를 향하도록 맞췄다.

Pass가 열려 있어야 setPipeline/setScissor/draw를 허용한다. 새 pass는 binding을 비우고
전체 viewport/scissor로 시작한다. Scissor는 비어 있지 않은 범위 안의 사각형이어야 하며,
draw count는 양수이고 firstVertex/firstInstance 합은 uint32 범위를 넘지 않아야 한다.
거부는 기존 pipeline/scissor를 유지한다. Shader module은 pipeline 생성 후 파괴하고,
native pipeline/layout/compatibility render pass는 recording/submission에 보유한다.
Wrapper 파괴와 미제출 recording 폐기, child가 남은 device retire도 검증했다.
Pipeline/driver cache 통제나 성능 향상은 주장하지 않는다.

## 실제 결과

기존 pinned Khronos runtime의 DLL/JSON/lock SHA를 실행 전후 확인해 재사용했다.
상속 Vulkan/VVL 설정 제거, 명시적 layer 경로, 빈 설정 파일, implicit layer 비활성화
요청은 child process에만 적용했다. Driver/registry/system install 변경은 없다.

| 최종 Windows 검증 범위 | 결과 |
|---|---|
| 통합 required validation | 선택 41/41 CTest, skip 없음 (등록 44개, 보류 메모리 3개 제외) |
| Vulkan only + required validation | 16/16 CTest, skip 없음 |
| 두 backend OFF | 7/7 CPU CTest, skip 없음 |
| 설치 SDK buffer·texture·color-pass·draw 소비자 | NVIDIA GTX 1660 SUPER / Intel UHD Graphics 770 모두 통과 |
| Python 회귀 | 208/208 통과 |

CPU interface suite는 실행당 **181 assertion**, GPU call 0을 통과했다. 정상 module,
instruction 내부 truncation, stage/header/word count/version/entry/execution-mode와
output-location mismatch, 검증됐지만 지원하지 않는 PointSize builtin을 대조한다.

Draw suite는 실행당 **637 assertion / 62 draws / 66 full images /
249,491,416 texel 바이트 / 58 padded images**를 비교했다.

- 두 포맷의 1×1, 3×7, 65×37, 1080p, 4K oversized triangle과 odd-size 일반 triangle의
  모든 픽셀을 독립 CPU geometry/color reference와 비교했다. 일반 triangle은 edge 위에
  pixel center가 놓이지 않는 크기/좌표를 사용하며 화면 방향과 clear 영역도 검사했다.
- firstVertex=6, firstInstance=0/1, 1/2 instance를 실제 VertexIndex/InstanceIndex shader로
  처리했다. Scissor/pipeline switch와 거부된 rectangle 뒤의 기존 state 보존,
  후속 Load pass의 RGB/alpha blending도 전체 이미지로 대조했다.
- 2개/8개 혼합 포맷 출력의 각 색상, 모든 행/마지막 행의 sentinel padding을 검사했다.
  Null/foreign/incompatible pipeline, 잘못된 SPIRV framing/stage/output/format,
  32회 반복 count/range/scissor 거부, creating-thread와 native 수명도 검증했다.

최종 통합·Vulkan-only·설치 소비자의 validation 합계는 **186 draws / 198 images /
748,474,248바이트**다. 두 driver-only 대조를 더하면 **310 draws / 330 images /
1,247,457,080바이트**다. 정상 SDK core/sync validation ERROR는 0이었다.
기존 buffer(717 assertion), texture(835), color-pass(593) suite와 독립 의도 오류
counter 대조도 유지됐다. Unknown/extra 인수와 missing layer는 명시적으로 실패한다.

초기 빌드는 입력 검사가 1.0에 제한된 상태였고 도구 출력은 1.3이었다. 사용자 입력으로
CTest 완료 receipt 전 중단된 실행의 명령·로그·초기 소스를 별도 보존했고, 성공/실패나
최종 합계로 사용하지 않았다. 최종은 1.3 입력 계약 및 D3D12 호환 alpha 규칙으로 재빌드했다.
보류된 메모리 3개는 처음부터 제외했고, 이전 D3D12 충돌 근거와 memory policy를 바꾸지 않았다.

## 배포와 남은 범위

SDK fingerprint는 `26fde3c455c91ebddf46d2f1162f09563622173e6ba2d9ff7d0d601c4d033e26`다.
새 패키지는 `out/GraphicsSDK-0.0.1-dev-windows-x64-vulkan-draw.zip`이다.
[독립 감사](../licenses/generated/windows-sdk-vulkan-draw-release/inventory.json)는 66개 compiler
input/2개 component/23 compiler log/22 linker log와 정확한 SDK payload를 확인한다.
SDK MIT/Vulkan-Headers Apache-2.0 고지를 보존하고 기존 local SDK ZIP 16개를 변경하지 않았다.
로컬 fixture tool의 hash/provenance는 SDK의 전체 dependency/reachability 감사와 구분한다.

[최종 실행 명령](../benchmarks/reports/validation/2026-10-23-phase3-vulkan-draw/commands.json),
[요약](../benchmarks/reports/2026-10-23-phase3-vulkan-draw/summary.json),
[소스 ZIP·해시 manifest](../benchmarks/reports/2026-10-23-phase3-vulkan-draw/evidence.json)를 기록한다.

DXC/DXIL/MSL/general reflection/compiler cache는 Phase 5에 남는다. Vulkan의 descriptor
binding, vertex/index input, depth, timestamps, presentation과 경쟁 측정도 후속이다.
Linux native/GPU-assisted validation/실제 device loss 및 다른 adapter 실기는 미검증이다.
이번 성공은 전체 67개 등록 검사 통과, 장시간 메모리 acceptance, 과거 Intel AV 해결이나
interactive/physical acceptance를 뜻하지 않는다.

Native 계약은 Khronos의 [graphics pipeline](https://docs.vulkan.org/refpages/latest/refpages/source/VkGraphicsPipelineCreateInfo.html),
[valid shader module](https://docs.vulkan.org/refpages/latest/refpages/source/VkShaderModuleCreateInfo.html),
[viewport orientation](https://docs.vulkan.org/refpages/latest/refpages/source/VkViewport.html)과
[SPIRV-Tools](https://github.com/KhronosGroup/SPIRV-Tools)를 따른다.
