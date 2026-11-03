# Metal textures·전송·clear/load pass 준비

이 문서는 해당 slice의 당시 범위와 원본 증거다. 이후 MSL pipeline·입력/binding·draw 지원은
[Metal graphics 보고서](PHASE4_METAL_GRAPHICS_REPORT.md)를 따른다.

P4-MTL-04의 native API 기능 준비다. opt-in macOS Metal backend에 RGBA8UNorm,
BGRA8UNorm, RGBA16Float, D32Float의 single-sample/mip 2D private texture를 추가한다.
공개 header는 바꾸지 않는다. shader/pipeline/draw·표시·timestamp·native validation은 미지원이다.

## 계약과 구현

- 공통 texture/pass descriptor 검증, 256-byte rowPitch와 최종 행 padding을 포함한
  footprint, 실제 native pixel format/extent/private storage/tracked hazard/allocatedSize를 확인한다.
  RenderTarget/Depth 사용과 상태를 일치시키고 생성 스레드·장치/세대·퇴역을 검사한다.
- Upload/Device buffer→texture→Device/Readback buffer의 전체 level/slice 0 전송을
  native blit으로 기록한다. footprint와 host memory/state를 검사하고 managed readback은
  synchronizeResource 뒤 완료를 기다린다. [Metal blit API](https://developer.apple.com/documentation/metal/mtlblitcommandencoder)는
  buffer row/image stride와 region을 명시한다. D32는 combined depth/stencil이 아니므로
  [depth/stencil plane 옵션](https://developer.apple.com/documentation/metal/mtlblitoption/depthfromdepthstencil?language=objc)을 사용하지 않는다.
- Common/CopySource/CopyDestination, color RenderTarget, DepthWrite/DepthRead를 지원한다.
  Present·ShaderRead 등 후속 상태는 Unsupported다. 읽기/Load는 초기화된 texture만 허용한다.
- 색 1..8개 및 depth-only/혼합 attachment에 Clear/Load와 Store를 지원한다. finite clear,
  중복·동일 extent·usage/state·read-only depth Load를 검사한다. 패스 안의 marker는 render
  encoder에 기록하고 transfer/transition·중첩 pass·열린 pass 상태 end는 거부한다.
  depthAttachment/multipleRenderTargets caps는 이 pass 기능을 나타내며 graphics draw는 지원하지 않는다.
- 초기화 여부는 논리 상태/버전과 함께 기록 시 shadow, 성공한 제출 시 commit한다. stale
  CPU write·sibling 제출을 거부하며 잘못된 copy/pass가 새 참조·관측·초기화 상태를 남기지 않는다.
- native encoder는 end에서 새 미제출 command buffer에 직렬로 구성한다. wrapper와 별도로
  recording/pending이 native texture/allocation을 보유한다. 완료 후 회수와 Device 퇴역 시
  native 해제·CPU descriptor/footprint 보존을 기존 buffer와 같은 registry에서 처리한다.
  allocatedSize는 SDK 소유 allocation 진단이며 OS memory budget/driver cache/residency가 아니다.

## 검증 준비

네 포맷 × 1×1/3×2/65×17/129×7의 16개 roundtrip은 texture 두 개와 private buffer를 거쳐
모든 texel과 sentinel row padding을 비교한다. HDR half/정상 depth float를 포함한다.
color clear→동일 제출의 다음 pass Load→다음 제출 Load→재clear 9개, depth clear/read-only Load 2개,
2/4/8 mixed-format MRT와 depth 17개를 합해 실행당 readback 44개를 검사한다.
Native 색 변환은 UNorm 1.01/255·half .001·depth 1e-6 허용차와 finite 검사를 사용하며
transfer와 padding은 정확한 byte 비교다. BGRA swizzle과 HDR 음수/1 초과 값을 구분한다.
receipt의 passes=17은 이미지 참조 시나리오의 pass 수이며 오류/상태 제어 경로의 추가
pass를 전체 실행 수로 합산한 값이 아니다.

잘못된 extent/format/usage·범위·foreign/null/state·미초기화 copy/Load·중복/MRT extent·
비유한 clear·중첩/열린 pass·pass 중 transfer·read-only clear·stale upload/texture·thread·
거부 원자성·native 수명/회수·퇴역을 검사한다. native fault/busy/timeout은 강제 주입하지 않는다.

macOS CI는 기존 CPU-only Core/availability 후 Metal CTest 14개와 세 직접/설치 소비자를
실행한다. texture 직접/설치 프로그램은 전체 readback 88개를 보관하고 현재 Python
checker가 모든 texel/row padding을 재계산한다. 새로운 7개 adversarial CPU 계약은
누락·NaN·swizzle/guard 손상·scope 승격·compiler graph·CTest 누락을 거부한다.

`macos-metal-textures-v3` receipt는 과거 submission v1/buffers v2와 분리하고 이전 증거를
현재 기능/수용으로 재귀속하지 않는다. native compiler/cache/graph·source ZIP·설치 header·
raw stdout/stderr·PID/직렬 시간·전체 payload와 Python source binding을 보관한다.
native executable/system SDK는 재배포하지 않는다.

Phase 3 및 P4 선행 gate·물리 GPU·native validation/성능 수용은 남아 있으며 143개 task
상태와 active Phase 3를 유지한다. 다음 shader/pipeline/draw 구현은 P4-MTL-05 범위다.

## 실제 실행과 보관 결과

소스 `a0aea46675f66fc1efe0b941e6e34970f59b112f`, SDK fingerprint
`e279fccbd543f78efdb85de91d2846b2ecbb000fc0dbeea9ac36f8a65c7fc4e1`의
[macOS arm64/Intel CI](https://github.com/blackvrice/GraphicsSDK/actions/runs/41621603438)가
모두 통과했다. 각 환경에서 CPU 11+Metal 3 CTest, 제출/buffer/texture 직접 실행과
세 설치 public SDK 소비자, 전체 Python 1,494개가 통과했다.

texture 직접/설치 실행은 각각 이미지 참조 44개·324,352바이트와 106,925개 assertion을
검사했다. 새 전송·pass 기능뿐 아니라 기존 제출/비정렬 buffer 회귀도 같은 소스로 수행했다.
두 환경의 texture readback 176개·1,297,408바이트를 보관하고 Windows의 현재 checker로
모든 texel·finite/HDR/swizzle·row padding을 독립 재검증했다. 전송/guard는 byte 일치,
render clear/load는 위의 포맷별 허용차를 사용한다. 두 환경 모두 Apple Paravirtual
장치를 관측했으며 physical GPU·표시·graphics draw/성능 수용으로 승격하지 않는다.

같은 소스의 [Windows/Linux 회귀 CI](https://github.com/blackvrice/GraphicsSDK/actions/runs/41621603502)와
[Linux Vulkan 컴파일·CPU 계약](https://github.com/blackvrice/GraphicsSDK/actions/runs/41621603572)도
통과했다. SDK/test compiler graph 20 units 중 ObjC++/ARC 1개, 설치 소비자 3 units,
public headers 13개와 helper/source ZIP 입력 10개를 기록했다. 원본 evidence 512개를
`benchmarks/reports/validation/2026-11-03-metal-textures/`에 inventory/summary/manifest와
보관했다. evidence ZIP은 1,215,184바이트다. native executable/library·system framework/
SDK는 보관하지 않으며 source/원본 로그/receipt·전체 readback 데이터와 binary hash/크기를
기록한다. manifest는 자체 hash 목록이고 외부 신뢰 서명은 아니다. archived source/native
code를 실행하지 않고 현재 checker로 재생한다.

```sh
python3 benchmarks/reports/validation/2026-11-03-metal-textures/verify.py
```

이 재생을 Windows/Linux/macOS CI에 연결하며 이전 Metal submission v1/buffers v2·
availability/Core·Linux 증거는 보존한다. 로드맵에는 P4-MTL-04 진행 근거만 추가하며
선행 gate·physical GPU·native validation/오류 주입/성능 수용은 계속 미완료다.
