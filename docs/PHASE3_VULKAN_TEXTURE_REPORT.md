# Phase 3 — Vulkan color-texture transfers

2026-10-23, Windows x64 / MSVC 19.44 / Windows SDK 10.0.26100.0.

Vulkan 1.1 backend에 RGBA8UNorm/RGBA16Float 2D 텍스처 생성과 전체 이미지
upload/readback을 추가했다. 기존 공개 API를 사용하며 외부 링크 의존성과 공개
헤더를 추가하지 않았다. 렌더링을 준비하는 전송 단계이며 Phase 3 전체 완료가 아니다.

## 구현 범위

- 공통 description 검증(1..16384 크기, format/usage 일관성) 후 실제 선택 장치의
  optimal-tiling format/extent/sample 지원을 조회한다. 각각 전용 device-local 메모리에
  하나의 mip/layer/sample을 가진 transfer-only VkImage를 만든다.
- Staging footprint는 `rowBytes=width*texelBytes`, 256바이트 정렬 `rowPitch`,
  `size=rowPitch*height`다. 마지막 행 패딩도 버퍼 크기 계약에 포함한다. 이는 native
  image allocation 크기가 아니다. Vulkan row length에는 texel 단위를 사용한다.
- 업로드는 Upload/Device 버퍼에서, 다운로드는 Readback/Device 버퍼로 가능하다.
  전체 이미지와 offset 0만 지원하며 footprint보다 작은 버퍼와 반대 host memory는 거부한다.
- Logical Common/native UNDEFINED로 시작해 첫 유효 transition에서 layout을 정한다.
  이후 제출된 layout은 유지하며 Common/CopyDestination/CopySource를 명시적으로 전환한다.
  같은 layout에서 반복하는 copy에도 image/buffer memory dependency를 적용한다.
- Buffer와 같은 allocation/version shadow 체계를 사용한다. 거부된 호출은 새 참조나
  관측 version을 남기지 않는다. CPU upload 변경과 선행 제출은 오래된 recording을 거부한다.
  Recording/submission은 public wrapper 대신 native allocation을 보유하며, device 파괴는
  대기·retire 후 남은 child의 CPU description/footprint만 유지한다.

## 실제 검증

기존 고정 Khronos VVL v1.3.290 DLL/JSON과 lock을 해시로 확인하여 재사용했다.
Child process의 상속 Vulkan/VVL 설정을 제거하고 명시된 layer 경로, 빈 settings,
implicit layer 비활성화 요청을 적용했다. 시스템 설치·레지스트리·드라이버는 변경하지 않았다.
레이어는 이전 `/Od /Ob0` 로컬 진단 빌드이며 SDK는 정상 Release 최적화를 유지한다.
이 구성의 한계와 독립 native counter 대조는 [이전 보고서](PHASE3_VULKAN_VALIDATION_REPORT.md)에 있다.

| Windows 구성 | 결과 |
|---|---|
| D3D12 + Vulkan + required validation | 39/39 CTest, skip 없음 |
| Vulkan only + required validation | 11/11 CTest, skip 없음 |
| 두 backend OFF | 6/6 CPU CTest, skip 없음 |
| 설치 SDK buffer·texture 소비자 | NVIDIA GTX 1660 SUPER / Intel UHD Graphics 770 모두 통과 |
| Python 회귀 | 208/208 통과 |

텍스처 suite는 실행당 **835 assertion / 74회 왕복 / 499,371,832 texel 바이트**를
검증했다. 두 포맷 × 1×1, 3×7, 63×5, 64×1, 65×17, 257×129, 1920×1080,
3840×2160 × 두 payload × 두 GPU의 경로와, GPU당 5회 변경 payload 재사용이다.
각 일반 경로는 upload→image→Device buffer→두 번째 image→readback을 통과한다.
모든 texel 바이트를 비교하고 40개 padded 경로에서 모든 행 및 마지막 행의 `0xd3`
sentinel이 보존됨을 확인했다. RGBA16F는 수치 연산 없이 저장된 바이트의 동일성을 검사한다.

통합·Vulkan-only·설치 소비자의 validation 실행은 합계 **222회 / 1,498,115,496바이트**,
driver-only 두 대조를 포함한 합계는 **370회 / 2,496,859,160바이트**다.
정상 SDK texture/buffer suite의 validation ERROR는 0이었다.
기존 buffer suite도 실행당 717 assertion/48 transfers/57,042,546바이트를 통과했다.

추가로 null/foreign-device/잘못된 format·extent·usage·layout·memory·범위 거부,
32회 반복 거부 후 native allocation 회수, 거부 후 버퍼 write/다른 recording 제출,
CPU write·sibling submission stale-version 거부, 기록 후 wrapper 파괴와 native 보유,
zero-time fence poll, 전체 회수, creating-thread와 device retirement를 검사했다.
Unknown/extra CLI 옵션은 GPU 작업 전에 실패하고 missing layer는 exit 1이며 skip하지 않는다.
별도 native probe의 의도적 core/sync 오류는 정상 SDK 작업과 다른 process에서 검증했다.

## 배포와 보존

SDK fingerprint는 `218bda2ede7ae2e136a06ecb7e794a5f0bc22938e561195a2a6b74cd86c25ffe`다.
새 SDK는 `out/GraphicsSDK-0.0.1-dev-windows-x64-vulkan-textures.zip`이며,
[감사 inventory](../licenses/generated/windows-sdk-vulkan-textures-release/inventory.json)는
61개 compiler input/2개 component/18개 compiler log/17개 linker log와 정확한 SDK
payload를 확인한다. Vulkan-Headers Apache-2.0 및 SDK MIT 고지를 보존한다.
Loader/ICD/driver/VVL runtime 및 외부 vendor source를 SDK/source ZIP에 넣지 않는다.
이전 근거와 로컬 SDK ZIP 14개, 장시간 메모리 policy를 그대로 보존했다.

[실행 명령](../benchmarks/reports/validation/2026-10-23-phase3-vulkan-textures/commands.json),
[요약](../benchmarks/reports/2026-10-23-phase3-vulkan-textures/summary.json),
[소스 ZIP·근거 manifest](../benchmarks/reports/2026-10-23-phase3-vulkan-textures/evidence.json)를 기록한다.

Render/depth attachment, sampling, SPIR-V/graphics pipeline/binding/timestamps/presentation,
mip/array/partial copy 및 Vulkan 경쟁 측정은 후속이다. Linux native 실행, GPU-assisted
validation, noncoherent branch 실기와 실제 device loss는 이번 증거 밖이다.
장시간 메모리/과거 Intel AV와 interactive/physical acceptance는 계속 미해결·보류 상태이며,
이 짧은 전송 검사로 하드웨어 원인을 확정하거나 해당 acceptance를 통과 처리하지 않는다.

주소·layout·format 지원 계약은 Khronos의
[VkBufferImageCopy](https://docs.vulkan.org/refpages/latest/refpages/source/VkBufferImageCopy.html),
[buffer-to-image copy](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdCopyBufferToImage.html),
[image barrier](https://docs.vulkan.org/refpages/latest/refpages/source/VkImageMemoryBarrier.html),
[format query](https://docs.vulkan.org/refpages/latest/refpages/source/vkGetPhysicalDeviceImageFormatProperties.html)를 따른다.
