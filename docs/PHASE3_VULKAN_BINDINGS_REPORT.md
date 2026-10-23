# Phase 3 — Vulkan 리소스 바인딩

2026-10-23, Windows x64 / MSVC 19.44 / Windows SDK 10.0.26100.0.

기존 공개 API로 Vulkan Texture2D·상수 버퍼 배열, 정적 sampler, 단계별 가시성과
변경되지 않는 바인딩 snapshot을 구현했다. VS 상수로 UV를 변환하고 FS에서 두 image와
상수 배열을 사용한 결과를 두 GPU의 전체 이미지로 검증했다. 공개 API 형태/ABI 변경은
없다. Depth·timestamps·presentation을 포함한 Phase 3 전체 완료는 아니다.

## 구현 범위

Vulkan은 set 0에서 layout entry i를 binding i에, static sampler i를 binding 32+i에
매핑한다. Register slot/space는 D3D12 shader 매핑으로 유지하고 Vulkan set 번호로
사용하지 않는다. 고정 descriptor array count와 shader visibility를 검사하며 공통
구조 검증 및 native per-stage/set 한도, fragment attachment 수를 적용한다.

지원 shader 자원은 별도 float32 sampled Texture2D, scalar sampler와 Uniform Block이다.
Block은 연속 float4 member와 0,16,32,... offset만 받으며 최대 64KiB다. VS/FS에서
필요한 최소 바이트의 최댓값을 binding에 적용한다. 일반 struct/matrix/array block,
combined image-sampler, storage/push/runtime array와 추가 capability는 지원하지 않는다.
외부에서 Vulkan 1.1용으로 검증한 신뢰할 수 있는 SPIR-V 1.3이 필요하다. SDK의 bounded
framing/interface 검사는 전체 semantic validator나 public general reflection이 아니다.

기존 고정 로컬 SPIRV-Tools helper와 library를 이전 receipt의 SHA로 확인하고 자체
module 5개를 조립·검증했다. 잘못된 Block offset control은 validator가 exit 1로
거부했다. 기존 vendor source/tool binary는 SDK나 소스 ZIP에 넣지 않는다.
[명령](../benchmarks/reports/validation/2026-10-23-phase3-vulkan-bindings/shader-tools/commands.json)과
[도구 receipt](../benchmarks/reports/validation/2026-10-23-phase3-vulkan-bindings/shader-tools/receipt.json)를 보존한다.

색상 image에는 sampled usage와 view를 생성한다. 업로드하거나 렌더한 이미지의
ShaderRead transition은 SHADER_READ_ONLY_OPTIMAL과 shader read barrier를 사용한다.
미초기화 image와 active render target의 sampling은 거부한다. Static sampler는
Point/Linear, Clamp/Repeat이며 한 mip, comparison/anisotropy 없음이다. Linear sampler가
있는 layout은 보수적으로 모든 texture의 native linear-filter feature를 요구한다.
Device/Upload는 ConstantBuffer state를, Readback은 copy destination만 지원한다.
상수 view는 256바이트 단위 offset/size, size 256..65536, shader minimum과 native
alignment/range를 모두 만족해야 한다.

각 bind는 전체 배열 원소를 정확히 한 번 요구하고 검증 완료 후에만 descriptor pool/set,
native 자원과 version을 보유한다. 새 set이 과거 draw의 descriptor를 바꾸지 않는다.
거부는 이전 set과 budget을 유지하며, 승인된 Upload의 CPU write는 stale submit으로
거부한다. Recording당 resource descriptor 256개와 snapshot 최대 256개를 허용하고
초과는 InvalidState다. 새 pass/setPipeline은 선택 snapshot을 초기화한다. 단일 fragment
texture shorthand sampledTexture/bindTexture도 지원하며 explicit layout과 함께 쓰지 않는다.
Set/pool·pipeline/immutable sampler·image view/allocation을 wrapper와 독립적으로 보유하고
device retire 시 native 의존 순서로 해제한다. Pool은 snapshot마다 별도 생성하는 초기
정확성 구현이며 allocator/descriptor pooling 최적화와 성능 주장은 후속이다.

## 최종 검증

Pinned Khronos runtime을 SHA 확인 후 재사용했다. 상속 Vulkan/VVL 설정 제거, 명시적
layer 경로, 빈 설정과 implicit layer 비활성화 요청은 child process에만 적용했다.
시스템 등록·레지스트리·드라이버는 변경하지 않았다.

| Windows 구성 | 결과 |
|---|---|
| 통합 required validation | 선택 47/47, skip 없음 (등록 50개, 보류 메모리 3개 제외) |
| Vulkan-only required validation | 22/22, skip 없음 |
| 두 backend OFF | CPU 9/9, skip 없음 |
| 설치 SDK의 GPU suite 6개 | NVIDIA GTX 1660 SUPER / Intel UHD Graphics 770 모두 통과 |
| Python 회귀 | 208/208 통과 |

새 CPU resource interface suite는 **43 assertion / GPU calls 0**을 통과했다. 기존
181개 SPIR-V interface 및 61개 typed input/varying assertion을 유지한다.
바인딩 GPU suite는 실행당 두 GPU 합계 **573 assertion / 156 draws / 258 snapshots /
80 full images / 249,889,536 texel bytes / 72 padded images**를 비교했다.

- RGBA8/RGBA16F 입력과 출력의 모든 조합, Point/Linear·Clamp/Repeat의 두 sampler,
  Upload/Device 상수, texture/constant 배열, VS/FS register 재사용과 nonzero space,
  32/272바이트 shader block을 검사했다. CPU가 sampling·보간·wrap/clamp·가중 합을
  독립 계산하고 모든 픽셀과 마지막 행까지 sentinel padding을 대조했다.
- 좌우 scissor draw 사이에 image 배열을 교체해 이전 set의 불변성을 검사했다.
  Legacy sampling, rendered image postprocess, 1080p/4K 전체 이미지도 비교했다.
- 완전성·중복·타입·array/visibility·view minimum/alignment/range·null/foreign/readback,
  초기화/state·creating thread를 거부했다. 32회 반복 거부의 미보유, late rejection의
  미관측 version, budget 소진 뒤 기존 set 보존, 승인 뒤 stale write, wrapper 파괴·
  full reclamation·제출 중 device retire를 검사했다.

통합·Vulkan-only·설치 소비자의 validation 합계는 **468 draws / 774 snapshots /
240 images / 749,668,608 texel bytes**다. 두 driver-only 대조를 포함한 최종 합계는
**780 draws / 1,290 snapshots / 400 images / 1,249,447,680 texel bytes**다.
정상 SDK core/sync validation ERROR는 0이다. 기존 다섯 GPU suite와 의도 오류 probe도
유지했다. 각 suite의 unknown/extra 인수와 missing-layer 실패 control을 통과했다.

최초 통합은 45/47 통과였으며 기존 draw 테스트 2개가 "Bindings await next gate"라는
과거 기대값 때문에 실패했다. 원본 명령·로그·XML·테스트 소스를 별도 보존했다.
SDK source 변경 없이 해당 테스트를 legacy/explicit layout 충돌 거부 계약으로 바꿔
재빌드했고 최종 matrix를 통과했다. 초기 실패 결과는 최종 합계에 넣지 않는다.

## 배포와 후속

SDK fingerprint는 `702ad51ad296355c71a73ff7e08d845442612de3af1166dc43692da32b13757b`다.
새 패키지는 `out/GraphicsSDK-0.0.1-dev-windows-x64-vulkan-bindings.zip`이다.
[독립 감사](../licenses/generated/windows-sdk-vulkan-bindings-release/inventory.json)는
74개 compiler input/2개 component/29개 compiler log/28개 linker log, 설치 header/library
일치와 정확한 SDK ZIP payload를 확인한다. 누락 고지·변경 library·추가 library도 거부한다.
MIT/Apache-2.0 고지를 유지하고 기존 SDK ZIP 18개와 과거 근거·D3D12 source·memory policy를 보존한다.

[최종 명령](../benchmarks/reports/validation/2026-10-23-phase3-vulkan-bindings/commands.json),
[요약](../benchmarks/reports/2026-10-23-phase3-vulkan-bindings/summary.json),
[소스 ZIP·해시 manifest](../benchmarks/reports/2026-10-23-phase3-vulkan-bindings/evidence.json)를 기록한다.
보류 메모리 3개는 처음부터 제외했고 등록·acceptance policy는 그대로다. 장시간 메모리
acceptance와 Intel AV 원인 규명은 보류이며 전체 등록 81개 통과나 원인 해결을 주장하지 않는다.
Vulkan depth, timestamps, presentation과 Linux native/경쟁 측정은 후속이다.

Native 계약은 Khronos의 [descriptor layout](https://docs.vulkan.org/refpages/latest/refpages/source/VkDescriptorSetLayoutBinding.html),
[bind](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdBindDescriptorSets.html),
[limits](https://docs.vulkan.org/refpages/latest/refpages/source/VkPhysicalDeviceLimits.html)를 따른다.
