# Phase 2 Depth/MRT

2026-10-21. 순서상 다음 개발 항목인 D3D12 depth attachment와 multiple render targets를
구현하고 render-pass correctness 범위를 확장했다. **Phase 2 전체 완료는 아니다.**
device-loss 복구/주입, 장기 메모리 stress와 interactive presentation acceptance가 남아 있다.

## 구현

- D32Float texture와 depthStencil usage, DepthWrite/DepthRead 및 writable/read-only DSV.
- DepthState의 8종 compare와 test/write 제어. write는 test를 켠 상태에서 사용한다.
- 최대 8개 RGBA8/RGBA16Float mixed-format color attachment와 선택적 D32 depth.
- RenderTargetLayout, RenderPassDesc, ColorAttachment/DepthAttachment, Clear/Load.
- depth-only pass와 fragment bytecode를 생략하는 depth-only pipeline.
- 전체 pass extent로 viewport/scissor 초기화, end 뒤 attachment 저장 및 Load 내용 보존.
- pipeline/pass의 color count·순서별 format·depth format/access 대조, shader 출력 대조.
- 모든 attachment를 검증한 뒤 clear/bind를 기록하는 원자적 beginRenderPass.
- 기존 단일 color Clear 호출 유지, native 자원/RTV/DSV의 fence 수명 보존과 submit version 검사.
- D32 whole-subresource buffer copy/upload/readback과 portable 구조 검증 3종.

구조와 제한은 [공개 API](PUBLIC_API.md)에 정의했다. D32는 depthStencil=true, renderTarget=false로
생성하고 sampling은 제공하지 않는다. Load는 호출자가 이전 clear/draw/copy로 내용을
초기화했음을 요구한다. 깊이 값은 VS의 SV_Position에서 공급한다. 이번 초기 graphics profile은
Stencil, MSAA/resolve, fragment depth/coverage/stencil export, 개별 color target의 독립 blend/mask,
discard/store 선택 및 subpass를 제공하지 않는다. alphaBlend는 모든 color output에 적용한다.

native attachment binding은 Microsoft의
[OMSetRenderTargets 계약](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-omsetrendertargets),
깊이 비교·쓰기 제어는 [depth state 계약](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_depth_stencil_desc),
읽기 전용 view는 [DSV flags 계약](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ne-d3d12-d3d12_dsv_flags)을 따른다.

## 실제 검증

| 검사 | 결과 |
|---|---|
| SDK core CTest | 16/16, skip 없음 |
| D3D12 OFF | 별도 Windows 빌드와 CPU CTest 4/4 |
| hardware / debug+GBV / WARP | 각 61개 전체 color/depth 이미지 CPU 참조 통과 |
| debug+GBV | ERROR/CORRUPTION 0 |
| 공통 benchmark correctness | 최신 SDK core를 연결해 72/72 |
| Python | 56/56 |
| 설치 consumer | 새 layout·capability·depth texture·MRT pipeline·pass·draw·readback. 1,024 pixels 일치 |
| SDK package/의존성 | 새 ZIP 및 SDK/common benchmark inventories와 재검사 보존 |

render-pass executable은 hardware/WARP에서 각각 116,339 assertions, debug+GBV에서
116,340 assertions를 통과했다. portable executable은 25개 부정 계약 assertions를 통과했다.

각 GPU mode의 61개 이미지는 다음과 같다.

- 8종 compare × depth write ON/OFF. 48×16 화면을 incoming depth .25/.5/.75로 나누어
  clear depth .5와 대조하고 color 및 D32 전체를 읽는다: 32개 이미지.
- 혼합 RGBA8/RGBA16F MRT와 D32에서 near/far draw 순서를 바꿔도 같은 결과인지 검사한다.
  다음 read-only pass는 Load로 기존 color/depth를 유지하며 일부 scissor 영역만 바꾼다.
  두 color output을 ShaderRead로 바꿔 후속 postprocess 입력으로 읽는다: 순서별 7개, 총 14개.
- PS 없는 depth-only pass 뒤 read-only depth를 쓰는 color pass: color/depth 2개.
- 17×9 odd extent의 mixed-format 8 MRT를 서로 다른 CPU 색 참조와 대조하고, 별도 mixed MRT에서
  실제 alpha blend를 확인한다: 10개 color 이미지.
- 두 번째 attachment의 state가 잘못된 pass 요청이 첫 target을 부분 clear하지 않는지
  확인하는 color 2개와 padded D32 17×3 copy 이미지 1개: 총 3개.

GPU mode당 color pixels 25,032개와 depth samples 16,179개를 검사한다. RGBA8 색상은 최대
1 LSB, RGBA16F 색상은 0.001, D32 값은 0.000001 tolerance다. D32 upload roundtrip은 유효한
각 행의 bytes도 원본과 정확히 대조한다. padding을 이미지 내용으로 해석하지 않는다.

누락·중복·foreign attachment, 크기/usage/clear/load/state 오류, nested pass, pass 안의
transition 및 command end를 거부한다. MRT shader output 누락·초과, pass/pipeline의 color
format/count/depth 불일치, read-only depth의 write pipeline 및 다른 thread도 거부한다.
depth attachment만 사용하는 command도 stale-version 검사를 통과해야 한다. MRT/depth/postprocess
명령을 제출한 뒤 texture/pipeline/command wrapper를 해제하고 fence 완료 후 읽어 수명을 검증했다.

[원본 로그](../benchmarks/reports/validation/2026-10-21-phase2-depth-mrt/)와
[재현 소스·해시 목록](../benchmarks/reports/2026-10-21-phase2-depth-mrt/evidence.json)을 보존한다.
backend-disabled 검증은 Windows에서 수행했으며 native Linux/macOS 검증으로 일반화하지 않는다.

## 패키지와 남은 범위

새 로컬 SDK ZIP은 `out/GraphicsSDK-0.0.1-dev-windows-x64-depth-mrt.zip`이다. 최초 SDK,
vertex-input, bindings ZIP과 모든 이전 측정·소스 archive·inventory를 보존한다. 새 virtual 함수,
format/state/descriptor 확장이 있으므로 0.0.1 개발 버전 consumer는 헤더와 라이브러리를 함께
재빌드해야 한다. 새 외부 dependency와 competitor 변경은 없다.

새 감사 폴더는 `licenses/generated/windows-sdk-depth-mrt-release`,
`licenses/generated/windows-benchmarks-depth-mrt-release`다. SDK compiler-read 입력은
19개/자체 구성요소 1개이며 공통 benchmark 확장 closure는 977개/22개다. 새 SDK ZIP과
두 inventory의 `--check`가 통과했다. 이는 build/package 입력 확인이며 포괄적인 법적 재배포 승인이 아니다.

이번에는 새 성능 캠페인을 실행하지 않았다. 과거 수치는 당시 source fingerprint의 역사 자료이며
새 API의 성능 우위를 주장하지 않는다. GraphicsSDK의 F workload 경쟁 비교를 추가로 실행한 것도 아니다.
Vulkan/Metal/native Linux, compute/storage, 위에 명시한 미지원 렌더 기능, device-loss 복구,
장기 stress, interactive presentation 및 remote CI 실행은 이번 완료 범위에 포함하지 않는다.
