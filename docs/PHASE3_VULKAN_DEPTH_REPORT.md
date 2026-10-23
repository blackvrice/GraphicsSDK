# Phase 3 — Vulkan D32 깊이 버퍼와 깊이 테스트

2026-10-23, Windows x64 / MSVC 19.44 / Windows SDK 10.0.26100.0.

Vulkan에 D32Float 전송·깊이 attachment와 모든 비교/쓰기 mask, depth-only/read-only
패스 및 색상 MRT와의 결합을 추가했다. 공개 API 형태/ABI 변경 없이 SDK와 해당 소비자를
모두 재빌드했다. Timestamps·presentation과 Linux native를 포함한 Phase 3 전체 완료는 아니다.

## 구현과 계약

D32 image는 한 mip/layer/sample, depth attachment/transfer usage와 depth view를 사용한다.
실제 adapter의 native format/usage/extent/sample 지원을 조회하며 depthAttachment capability는
이 D32 attachment/transfer 조합의 지원 여부다. Footprint는 float32 texel당 4바이트와
256바이트 정렬 rowPitch, 마지막 행 padding을 포함한다. Upload/Device 입력과
Readback/Device 출력의 전체 image copy가 depth aspect를 사용한다.
Payload는 제공된 바이트 그대로이며 호출자가 렌더링에 적합한 깊이 데이터를 공급한다.

DepthWrite는 native DEPTH_STENCIL_ATTACHMENT_OPTIMAL 및 depth read/write 접근,
DepthRead는 DEPTH_STENCIL_READ_ONLY_OPTIMAL 및 read 접근에 대응한다. 전환은 pass 밖에서
수행한다. Color는 depth state를, depth는 RenderTarget/ShaderRead/Present를 거부한다.
깊이 sampling은 Unsupported다. Stencil·MSAA·depth bounds·fragment depth export는 지원하지 않는다.

Pass는 색상 0..8개와 선택형 D32를 받고 attachment 하나 이상 및 동일 extent를 요구한다.
Depth Clear는 유한 0..1, Load는 초기화된 내용을 요구한다. Read-only는 DepthRead+Load,
writable은 DepthWrite를 사용하고 모든 attachment는 Store한다. 전체 검증이 끝난 뒤에만
native pass·clear 및 resource/version capture를 기록한다. 유효한 색상 뒤의 잘못된 depth도
앞선 색상을 지우거나 보유하지 않는다.

DepthState는 Never/Less/Equal/LessEqual/Greater/NotEqual/GreaterEqual/Always와 test/write
mask를 지원한다. Write는 test가 필요하고 depth state에는 depth layout이 필요하다.
Depth-only pipeline은 fragment bytes를 생략할 수 있다. 색상 출력은 fragment와 일치해야 한다.
Pipeline/pass의 depth 유무와 color 형식/수가 같아야 하며 read-only pass의 writing pipeline은
draw/drawIndexed에서 거부한다. 깊이는 vertex clip Position Z에서 공급한다.

Pass 외부 의존성은 color/transfer와 early/late fragment depth test 접근을 포함한다.
Framebuffer/pass/view/pipeline/descriptor와 native allocation은 wrapper 해제 뒤에도 보유되고
recording/submission 완료나 device retire 순서에 따라 해제된다. SDK resource version 검사와
creating-thread 계약을 유지한다. Render-pass cache/allocator 최적화나 성능 향상은 주장하지 않는다.

기존 고정 SPIRV-Tools local helper/library를 이전 receipt의 SHA로 확인해 재사용했다.
자체 vertex/color/2-MRT/8-MRT module 4개를 Vulkan 1.1용 SPIR-V 1.3으로 조립·검증하고,
잘못된 Uniform Block offset control은 validator가 exit 1로 거부했다.
SDK inspector는 기존 bounded interface 검사이며 전체 semantic validator가 아니다.
외부에서 검증한 신뢰할 수 있는 module이 필요하고 vendor tool/source/binary는 배포하지 않는다.
[도구 명령](../benchmarks/reports/validation/2026-10-23-phase3-vulkan-depth/shader-tools/commands.json)과
[별도 receipt](../benchmarks/reports/validation/2026-10-23-phase3-vulkan-depth/shader-tools/receipt.json)를 보존했다.

## 최종 검증

기존 pinned Khronos DLL/JSON/lock과 tool hash를 검증해 재사용했다. 상속 Vulkan/VVL 설정 제거,
명시적 layer 경로, 빈 설정 및 implicit layer 비활성화 요청은 child process에만 적용했다.
시스템 등록·레지스트리·드라이버는 변경하지 않았다.

| Windows 구성 | 결과 |
|---|---|
| 통합 required validation | 선택 49/49, skip 없음 (등록 52개, 보류 메모리 3개 제외) |
| Vulkan-only required validation | 24/24, skip 없음 |
| 두 backend OFF | CPU 9/9, skip 없음 |
| 설치 SDK GPU suite 7개 | NVIDIA GTX 1660 SUPER / Intel UHD Graphics 770 모두 통과 |
| Python 회귀 | 208/208 통과 |

깊이 suite는 실행당 두 GPU 합계 **883 assertion / 206 passes / 258 draws /
278 full images (126 D32) / 334,855,104 texel bytes / 262 padded images**를 검증했다.

- RGBA8/RGBA16F 각각에서 8개 비교 연산·쓰기 mask on/off를 검사했다. .25/.5/.75 깊이를
  .5 clear와 비교해 색과 D32 모두 CPU reference로 대조하고 depth-test disabled도 검사했다.
- 2/8 혼합 MRT와 depth에서 앞/뒤 draw 순서를 바꾸고 Load/read-only pass의 색상 갱신과
  깊이 보존을 검사했다. Depth-only fragment 생략, Clear 후 Load와 draw도 실행했다.
- D32의 1×1/3×7/65×37/1080p/4K, Upload/Device staging 왕복과 Device 중간 buffer의 양방향
  copy를 검사했다. 모든 texel과 마지막 행까지 sentinel row padding을 비교했다.
- Invalid clear/load/read-only/state·형식·extent·foreign 호환성 및 thread를 거부했다.
  Late depth 거부 뒤 initialized color의 픽셀 보존, 32회 반복 거부의 미보유, 미관측 version,
  승인 depth use의 stale submission, 전체 자원 회수와 제출 중 device retire를 확인했다.

통합·Vulkan-only·설치 소비자 validation의 깊이 suite 합계는 **618 passes / 774 draws /
834 images (378 D32) / 1,004,565,312 texel bytes**다. 두 driver-only 대조를 포함한 최종
합계는 **1,030 passes / 1,290 draws / 1,390 images (630 D32) / 1,674,275,520 texel bytes**다.
정상 SDK core/sync validation ERROR는 0이다. 기존 CPU SPIR-V 181/61/43 assertion,
여섯 GPU suite와 의도 오류 native probe도 유지했다. Unknown/extra 인수와 missing-layer
실패 control은 suite별로 통과했다. 과거 미구현 depth 거부 테스트는 malformed depth usage
거부로 바꿨고 capability 기대값을 실제 구현에 맞췄다.

## 배포와 후속

SDK fingerprint는 `a00cb717ba67d18c7dac35b0d2cd51e79d4a83d53036cba4e7fbb2b2564b8cfc`다.
새 패키지는 `out/GraphicsSDK-0.0.1-dev-windows-x64-vulkan-depth.zip`이다.
[독립 감사](../licenses/generated/windows-sdk-vulkan-depth-release/inventory.json)는
77개 compiler input/2개 component/31개 compiler log/30개 linker log, 설치 header/library와
정확한 SDK ZIP payload를 확인한다. 누락 고지·변경 library·추가 library도 거부한다.
MIT/Apache-2.0 고지를 유지하며 기존 SDK ZIP 19개, 과거 근거·D3D12 source·memory policy를 보존한다.

[최종 실행 명령](../benchmarks/reports/validation/2026-10-23-phase3-vulkan-depth/commands.json),
[요약](../benchmarks/reports/2026-10-23-phase3-vulkan-depth/summary.json),
[소스 ZIP·해시 manifest](../benchmarks/reports/2026-10-23-phase3-vulkan-depth/evidence.json)를 기록한다.
보류 메모리 3개는 처음부터 제외했고 CTest 등록·acceptance policy는 그대로다. 장시간 메모리
acceptance나 Intel AV 원인 규명, 전체 등록 85개 통과를 주장하지 않는다.
Vulkan timestamps, presentation, Linux native/동일 Vulkan 경쟁 측정과 GPU-assisted validation은 후속이다.

Native 계약은 Khronos의 [depth state](https://docs.vulkan.org/refpages/latest/refpages/source/VkPipelineDepthStencilStateCreateInfo.html),
[attachment load/store](https://docs.vulkan.org/refpages/latest/refpages/source/VkAttachmentDescription.html),
[buffer/image copies](https://docs.vulkan.org/refpages/latest/refpages/source/VkBufferImageCopy.html)를 따른다.
