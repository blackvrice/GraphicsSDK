# Phase 3 — Vulkan core/synchronization validation

2026-10-23, Windows x64 / MSVC 19.44 / Windows SDK 10.0.26100.0.

이전 버퍼 전송 구현의 미완료 항목인 실제 Khronos validation 실행을 완료했다.
SDK `validation=true`는 core validation에 synchronization validation과 submit-time
checking을 명시적으로 추가한다. 공유 private 설정/atomic 콜백을 SDK와 독립 native
대조가 함께 사용한다. 필요한 layer/extension이 없으면 `Unsupported`이며 fallback하지 않는다.
외부 환경/설정 파일은 API 설정을 덮어쓸 수 있으므로 검증 실행은 해당 입력을 통제한다.

## 구현과 로컬 runtime

`tools/prepare_vulkan_validation.py`는
[`vulkan-validation.lock.json`](../tools/vulkan-validation.lock.json)의 정확한 여섯 commit을
확인하고 원본을 수정하지 않은 채 로컬 runtime을 순차 빌드한다. VVL은 `v1.3.290`,
`79f4759c2c492c1be37e18c0318330f2959327cb`이며 나머지는 해당 버전의 known-good
Vulkan-Headers/Utility Libraries/SPIRV-Headers/SPIRV-Tools/robin-hood다.
시스템 설치·레지스트리·드라이버·영구 환경 설정은 변경하지 않았다.

로컬 도구는 mimalloc/upstream tests/SPIR-V executables를 끄고 `/MP1`을 적용한다.
SPIRV-Tools의 첫 compiler access violation 후 원본 변경 없이 재시도가 성공했다.
VVL 최적화 빌드는 MSVC C1001로 실패하여 레이어만 `/MD /Od /Ob0 /DNDEBUG`로 빌드했다.
성공한 구성과 실패 원본 로그를 함께 보존한다. SDK Release 최적화는 변경하지 않았으며,
이 관측으로 하드웨어나 과거 Intel AV 원인을 확정하지 않는다. 레이어 성능 측정도 아니다.

`VK_LAYER_PATH`는 기록된 JSON/DLL 폴더, `VK_LAYER_SETTINGS_PATH`는 빈 로컬 설정으로
지정한다. 상속된 Vulkan/VVL 설정을 제거하고 implicit layer 비활성화를 요청한다.
Native probe는 실제 로드 DLL 경로와 예상 파일의 동일성을 검사한다. Runtime SHA는
실행 전후마다 검사하며 SDK/source ZIP에는 이 외부 DLL/PDB/LIB/vendor source를 넣지 않는다.

## 실제 검증

| Windows 구성 | 결과 |
|---|---|
| D3D12 + Vulkan + required validation | 37/37 CTest 통과, skip 없음 |
| Vulkan only + required validation | 9/9 CTest 통과, skip 없음 |
| 두 backend OFF | 6/6 CPU CTest 통과, skip 없음 |
| 설치된 public SDK 소비자 + validation | NVIDIA/Intel 모두 통과 |
| Python 기존 도구 계약 | 208/208 통과 |

정상 SDK validation suite는 실행당 717 assertion, 48회 upload→device→readback,
57,042,546바이트의 완전 비교를 통과했다. 통합·Vulkan-only·설치 소비자의 validation
실행 합계는 **144회/171,127,638바이트**다. Driver-only 대조 두 회를 더하면
240회/285,212,730바이트다. 정상 SDK 작업에서는 validation ERROR가 0이었다.
1~8,388,611바이트의 홀수 크기, 상태/버전/거부 원자성, wrapper/native 수명,
펜스·스레드·장치 종료 계약을 기존 suite 그대로 검사했다.

별도 native probe는 실제 두 GPU에서 실행당 **274 assertion**을 통과했다.

- 크기 0 buffer 생성의 같은 VUID를 GPU당 32회, 총 64회 검출했다. 콜백은 `VK_TRUE`로
  잘못된 호출을 driver dispatch 전에 중단한다. 반복 오류가 기본 10회 한도를 넘어 집계된다.
- Barrier 없는 두 복사의 WAW hazard를 synchronization ON에서 GPU당 1회 검출하고,
  OFF 대조에서는 검출되지 않음을 확인했다. 이 명령 버퍼는 어느 경우에도 제출하지 않았다.
- GPU당 네 worker의 1,024 synthetic ERROR와 별도 인스턴스 1개 ERROR를 집계했다.
  총 2,050 synthetic ERROR이며 warning/info는 제외된다. 실제 callback 실행의 병렬성을
  계측한 것은 아니며 네 호출 스레드에서의 손실 없는 집계를 확인한 것이다.
- 인스턴스 카운터 독립성과 종료 후 추가 예상 밖 오류가 없음을 확인했다.

이 의도적 오류는 정상 SDK suite와 다른 프로세스에서 실행하며, 정상 작업의 오류로
합산하거나 숨기지 않는다. Missing-layer 요청과 오타/추가 CLI 인수는 exit 1로 거부한다.
`GFX_VULKAN_VALIDATION_TESTS=ON`은 required tests이며 누락 레이어를 skip하지 않는다.

## 배포 근거와 남은 범위

[명령/원본 로그](../benchmarks/reports/validation/2026-10-23-phase3-vulkan-validation/commands.json),
[요약](../benchmarks/reports/2026-10-23-phase3-vulkan-validation/summary.json),
[소스 ZIP·해시 manifest](../benchmarks/reports/2026-10-23-phase3-vulkan-validation/evidence.json)를 보존한다.
SDK fingerprint는 `7d47b43102131c83bda685ef152f738ee6dfe4f940760d79b202cbb00bd4204f`다.

새 SDK ZIP은 `out/GraphicsSDK-0.0.1-dev-windows-x64-vulkan-validation.zip`이다.
[별도 감사](../licenses/generated/windows-sdk-vulkan-validation-release/inventory.json)는
60개 compiler input/2개 component/16개 compiler log/15개 linker log와 정확한 SDK
payload를 확인한다. SDK 자체 MIT와 Vulkan-Headers Apache-2.0 고지를 보존한다.
외부 local-only validation runtime은 별도 provenance/원문 라이선스 해시를 기록하며,
그 runtime 전체의 재배포 허가나 symbol reachability 감사로 표현하지 않는다.
이전 근거와 로컬 SDK ZIP 13개를 변경하지 않았다.

GPU-assisted validation, Linux native 실행, 소프트웨어 Vulkan, noncoherent 메모리 분기의
실기 검증과 실제 device loss는 이번 gate 밖이다. Texture/rendering/SPIR-V/bindings/
timestamps/presentation 및 Vulkan 경쟁 비교도 후속이다. Phase 3 전체 완료가 아니며,
보류한 D3D12 장시간 메모리/Intel 원인과 interactive/physical acceptance는 계속 미해결이다.

설정/대조는 Khronos의
[validation features](https://docs.vulkan.org/refpages/latest/refpages/source/VkValidationFeaturesEXT.html),
[layer settings](https://docs.vulkan.org/refpages/latest/refpages/source/VkLayerSettingsCreateInfoEXT.html),
[debug message injection](https://docs.vulkan.org/refpages/latest/refpages/source/vkSubmitDebugUtilsMessageEXT.html)
정의를 따른다. 환경·파일·API의 우선순위는 고정 Utility Libraries의 실제 설정 코드도 확인했다.
