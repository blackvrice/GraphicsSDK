# Phase 3 — Vulkan color attachment passes

2026-10-23, Windows x64 / MSVC 19.44 / Windows SDK 10.0.26100.0.

Vulkan backend에 색상 Clear/Load/Store 렌더 패스와 혼합 포맷 첨부를 구현했다.
공개 API는 추가하지 않았으며 기존 `RenderPassDesc`와 `TextureDesc.renderTarget`을
사용한다. 실제 shader drawing/graphics pipeline/SPIR-V는 후속이며 Phase 3 전체 완료가 아니다.

## 구현과 계약

- RGBA8/RGBA16F render-target image에 COLOR_ATTACHMENT usage와 image view를 만든다.
  실제 adapter의 image-format/extent/sample 지원과 framebuffer 크기 한계를 확인한다.
  1..min(8, adapter maxColorAttachments)개, 동일 크기, 서로 다른 색상 첨부를 허용한다.
  `multipleRenderTargets`는 둘 이상의 첨부 지원을 나타내며 shader/depth 지원을 뜻하지 않는다.
- 명시적 RenderTarget transition이 필요하다. 전체 image에 Clear 또는 Load와 Store를
  적용한다. 두 포맷의 혼합, 단일 첨부 convenience API와 descriptor API를 함께 지원한다.
- Native layout 초기화와 contents 초기화를 별도로 기록한다. 전환만 한 새 image는 Load할
  수 없다. Clear나 전체 업로드는 contents를 초기화하고, 같은 recording 또는 선행 성공
  submission의 초기화가 Load를 허용한다. 거부된 다중 첨부 pass는 앞선 Clear를 실행하거나
  새 native 참조/관측 version을 남기지 않는다.
- Subpass 외부 dependency로 load/store와 선행/후행 transfer를 연결한다. Subpass dependency의
  ALL_COMMANDS가 ALL_GRAPHICS로 해석되는 규칙에 맞춰 TRANSFER를 명시한다. 같은 layout에서
  인접한 pass와 upload→Load→readback도 동기화한다.
- 중첩 pass, pass가 열린 recording의 end/submit, pass 내부 buffer/image transition/copy는
  InvalidState이며 열린 pass를 유지한다. endRenderPass는 열린 pass에서만 가능하다.
- Recording이 render pass/framebuffer와 native attachment allocation을 보유한다. Command pool,
  framebuffer/render pass, image view/image/memory 순으로 회수한다. 제출 전 wrapper 파괴,
  미제출 recording 폐기와 device retire에도 같은 수명 순서를 적용한다.
  Pending reference 통계는 보유한 pass 수도 포함한다. Cache/allocator 최적화 주장은 없다.

## 실제 결과와 보류 회귀

고정 VVL v1.3.290 runtime의 기존 DLL/JSON/lock 해시를 확인해 재사용했다. 상속된
Vulkan/VVL 설정을 제거한 child 환경에 layer 경로, 빈 settings와 implicit layer
비활성화 요청을 지정했다. Registry/driver/system install은 변경하지 않았다.
레이어의 이전 `/Od /Ob0` 진단 빌드와 SDK의 정상 Release 최적화를 구분한다.

최초 통합 CTest는 **40/41 통과**였으며, 기존 D3D12
`memory_reclamation_validation` process가 **SegFault**로 실패했다. 모든 Vulkan suite는
이 실행에서도 통과했다. 실패 실행의 명령/CTest XML/원본 로그와 해당 native 바이너리의
로컬 사본·해시를 [first-attempt](../benchmarks/reports/validation/2026-10-23-phase3-vulkan-color-pass/first-attempt/receipt.json)에 보존했다.
이 결과로 충돌 adapter/모듈/하드웨어 원인을 추정하거나 해결을 주장하지 않는다.

사용자의 기존 메모리/충돌 조사 보류 지시에 따라 최종 통합 실행에서는
`-E ^memory_reclamation_`로 해당 메모리 검사 3개를 명시적으로 제외했다.
CMake 등록과 memory policy는 바꾸지 않았다. D3D12 backend 소스도 변경하지 않았으며
메모리 검사를 성공할 때까지 재시도하지 않았다. 최종 SDK는 별도 검토 중 추가한 adapter
framebuffer 크기 거부 검사까지 포함한다. 최초 빌드 fingerprint는 `77e68ea2b9225ec12f8b0778ec8aefc29f3e9e12fb5517162b88439f02a8ee4d`,
최종은 `048ae963da0f66aedcc87026d2644c4cfc9b99c9bcc9b6e59823227dc00dbc1c`다.

| 최종 Windows 실행 범위 | 결과 |
|---|---|
| 통합 required validation, 메모리 3개 제외 | 선택 38/38 CTest, skip 없음 (등록은 41개) |
| Vulkan only + required validation | 13/13 CTest, skip 없음 |
| 두 backend OFF | 6/6 CPU CTest, skip 없음 |
| 설치 SDK buffer·texture·color-pass 소비자 | NVIDIA GTX 1660 SUPER / Intel UHD Graphics 770 모두 통과 |
| Python 회귀 | 208/208 통과 |

색상 pass suite는 실행당 **593 assertion / 128 pass / 138 full images /
747,063,512 texel 바이트 / 114 padded images**를 확인했다. RGBA8/RGBA16F의
1×1, 3×7, 65×17, 1080p, 4K에 대해 Clear→인접 Load, 제출 후 Load, 재Clear를
모든 texel의 CPU reference와 비교했다. RGBA16F의 음수/1 초과 clear 값도 검사했다.
1..8개 혼합 첨부마다 개별 clear/store를 확인하고, 패턴을 업로드한 image의 Load가
모든 texel 바이트를 보존함을 확인했다. Readback의 행/마지막 행 패딩 sentinel도 검사했다.

최종 통합·Vulkan-only·설치 소비자의 validation 합계는 **384 pass / 414 images /
2,241,190,536바이트**다. Driver-only 대조 두 회까지 포함하면 **640 pass / 690 images /
3,735,317,560바이트**다. 최초 실행은 이 합계에 넣지 않았다. 정상 SDK의 core/sync
validation ERROR는 0이었다. 기존 buffer suite(717 assertion/48 transfers)와 texture
suite(835 assertion/74 roundtrips)도 최종 세 구성에서 유지됐다.

추가로 새 contents의 Load 거부, 잘못된 다중 첨부/format/extent/usage·비유한 clear 값,
32회 반복 거부 후 native 자원 회수, 뒤쪽 Load 실패 후 앞쪽 Clear 미초기화,
열린 pass의 금지 명령 후 계속 사용, sibling version 거부, creating-thread,
wrapper 파괴와 미제출 framebuffer 폐기, 제출·미제출 pass의 device retire를 검사했다.
Unknown/extra CLI는 GPU 작업 전에 실패하고 missing validation layer는 exit 1이다.
독립 native 의도 오류 대조는 정상 SDK 실행과 별도 process에서 통과했다.

## 배포와 한계

새 SDK는 `out/GraphicsSDK-0.0.1-dev-windows-x64-vulkan-color-pass.zip`이다.
[독립 감사](../licenses/generated/windows-sdk-vulkan-color-pass-release/inventory.json)는
62개 compiler input/2 component/20 compiler log/19 linker log와 정확한 SDK payload를
확인한다. SDK MIT/Vulkan-Headers Apache-2.0 고지를 보존하며 새 외부 코드는 추가하지 않았다.
VVL/ICD/loader/driver/vendor source와 최초 실행 native 바이너리는 SDK/source ZIP에 넣지 않는다.
이전 모든 근거·로컬 SDK ZIP 15개·원래 메모리 policy를 보존했다.

[최종 명령](../benchmarks/reports/validation/2026-10-23-phase3-vulkan-color-pass/commands.json),
[요약](../benchmarks/reports/2026-10-23-phase3-vulkan-color-pass/summary.json),
[소스 ZIP·증거 manifest](../benchmarks/reports/2026-10-23-phase3-vulkan-color-pass/evidence.json)를 기록한다.

Depth attachment, shader drawing/SPIR-V/pipeline/binding/timestamps/presentation과 Vulkan 경쟁
측정은 후속이다. Linux native, GPU-assisted validation, actual device loss 및 상한이 낮은
다른 adapter의 실기 실행은 미검증이다. 최종 선택 범위의 성공은 전체 60개 등록 검사 통과,
장시간 메모리 acceptance, 과거 Intel AV 해결 또는 interactive/physical acceptance가 아니다.

Native 계약은 Khronos의 [attachment descriptions](https://docs.vulkan.org/refpages/latest/refpages/source/VkAttachmentDescription.html),
[subpass dependencies](https://docs.vulkan.org/refpages/latest/refpages/source/VkSubpassDependency.html),
[begin render pass](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdBeginRenderPass.html)를 따른다.
