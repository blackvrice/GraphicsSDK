# Phase 3 — Vulkan debug marker

2026-10-26 · `P3-CAP-01` · 구현 `7287a29`, 단일 adapter 검증기 수정 `f5e80f3`.

**공통 `debugMarker()`를 실제 Vulkan debug-utils label에 연결했다. Windows NVIDIA/Intel의 일반·검증 모드에서 label 24개, Linux software ICD에서 6개를 실제 관측·전달했고, 미지원·문자열·recording/thread·퇴역 계약을 검사해 필수 작업을 완료했다.** Public API/ABI와 D3D12 구현은 바꾸지 않았다. Linux software 성공은 물리 GPU 수용이 아니다.

[요약·해시](../benchmarks/reports/2026-10-26-phase3-vulkan-markers/summary.json), [Windows 원본](../benchmarks/reports/validation/2026-10-26-phase3-vulkan-markers/windows/campaign.json), [Linux 원본](../benchmarks/reports/validation/2026-10-26-phase3-vulkan-markers/linux/campaign.json), [CI 출처](../benchmarks/reports/validation/2026-10-26-phase3-vulkan-markers/ci/runs.json).

## 구현 계약

`VK_EXT_debug_utils`를 전역 instance 확장에서 찾으면 validation off에서도 활성화한다. Required validation 구성에서는 기존 필수 확장 목록과 중복 없이 결합한다. Instance 확장을 활성화한 경우에만 optional device 함수를 조회한다. 확장 또는 함수가 없으면 유효 recording의 API는 `Unsupported`이며 다른 device 기능을 막지 않는다. 별도 marker capability bool을 추가하지 않았다.

기존 `ready()`를 먼저 적용해 잘못된 thread/recording과 퇴역 상태를 거부한다. 정상 recording이면 render pass 안팎에서 label을 넣을 수 있다. 지원 시 NUL-free UTF-8만 받으며 빈 문자열은 허용하고, embedded NUL·잘못된 UTF-8·UINT32_MAX 초과 길이는 `InvalidArgument`로 거부한다. `string_view`의 부분 구간을 별도 null-terminated 문자열로 복사해 호출 동안 유지한다. 복사 allocation 실패는 `OutOfMemory`다. 미지원 함수는 문자열 검사보다 먼저 `Unsupported`를 반환한다.

이 동작은 [Khronos command 규격](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdInsertDebugUtilsLabelEXT.html)과 [UTF-8 label 구조체 계약](https://docs.vulkan.org/refpages/latest/refpages/source/VkDebugUtilsLabelEXT.html)을 따른다. Queue label·region begin/end·색상 설정 등 새 공개 API는 추가하지 않았다.

## 실제 관측

[Own observer](../tests/vulkan_marker/README.md)는 별도 explicit layer다. Controller가 정확한 layer library를 먼저 열어 고정하고, 실제 label 호출의 command handle·문자열·구조체·색상과 다음 Vulkan 함수로의 전달을 검사한다. Instance와 device proc lookup 양쪽을 관측한다. 이 중 instance lookup 관측이 없던 첫 구현은 관측 계약을 실패했고 원본을 보존했다. SDK success만으로 native 관측을 통과시키지 않았다.

| 실행 | 실제 adapter | 성공 label / 전달 | native 함수 부재 control | 검증 |
|---|---|---:|---|---|
| Windows 일반 | NVIDIA GTX 1660 SUPER + Intel UHD 770 | 12 / 12 | `Unsupported`, 0 native call | validation off |
| Windows required validation | 같은 두 GPU | 12 / 12 | `Unsupported`, 0 native call | 각 device native error 0 |
| Linux 일반 software | llvmpipe LLVM 20.1.2, CPU device | 6 / 6 | `Unsupported`, 0 native call | validation off; hardware 수용 아님 |

각 adapter에서 null terminator가 없는 substring `slice`, 호출 직후 변경한 임시 UTF-8/emoji 문자열, 빈 문자열, 200-byte 문자열, pass 내부와 command 재사용 label을 대조했다. Invalid UTF-8·NUL·초과 길이는 native call·새 resource reference를 만들지 않았고, begin 전/end 후/submitted/다른 thread/retired 명령도 거부했다. Pass와 제출을 마친 후 SDK 객체·자원·pending 계수, observer instance/device는 모두 0이다. Retired device 검사는 실제 TDR 유발 검사가 아니다.

함수 부재는 observer에서 조회를 차단한 **합성 control**이다. 실제 driver가 확장을 제공하지 않는 물리 장비의 측정으로 설명하지 않는다. Observer의 bounded 저장 한도는 32 labels×255 bytes이며 초과 시 실패한다. 이 결과는 API 호출·정확한 문자열·forwarding 관측이며 RenderDoc 화면, monitor 출력, GPU 성능 측정은 아니다.

Windows 관측 환경은 OS 10.0.26220, NVIDIA driver 32.0.16.2101, Intel driver 32.0.101.6129, system Vulkan loader 1.4.357.0이다. Required validation은 기존 고정 VVL 1.3.290을 사용한다. Layer 선택은 child process 환경에만 적용했고 전역 등록·설치나 driver 설정을 바꾸지 않았다. DLL/runtime은 배포하지 않았다.

## 회귀와 실패 보존

새 SDK의 로컬 native CTest **58/58**이 통과했다. 원래 장시간 메모리 수용과 별개인 기존 `memory_reclamation_gpu/validation` 두 smoke만 기존 CI와 같은 filter로 제외했다. 첫 전체 실행은 D3D12 fence 검사 두 개가 예외로 끝났으며 **56/58 실패 원본을 보존**했다. 같은 바이너리의 개별 두 검사와 동일 전체 batch 재검사는 모두 통과했다. 첫 예외의 원인은 규명되지 않았고 hardware·SDK로 단정하거나 원래 Intel AV의 해결로 표시하지 않는다.

첫 전체 build에서도 별도 `VulkanVertexTests.cpp`의 CL.exe가 -1073741819로 종료됐다. 관측한 tool transcript 사실과 성공한 직렬 재시도 로그를 보존했다. 완전한 첫 build stdout을 저장했다고 주장하지 않는다. D3D12/fence 코드를 수정하거나 수용 조건을 낮추지 않았다.

Linux 첫 marker workflow는 실제 6개 label을 전달하고 native exit 0이었지만, 검증기가 고정 최소 30 assertions를 요구해 1개 adapter의 정상 29 assertions를 거부했다. 초기 artifact·원래 검증기·실패를 그대로 보존하고, **8+21×adapter 수**의 계약과 29/28·50/49 경계 회귀를 추가해 새 CI에서 전체 positive/negative 두 사례를 다시 통과했다. 원래 실패 상태를 pass로 덮어쓰지 않았다.

최종 로컬 전체 Python **667개**, 새 Windows/Linux CI도 각각 **667개**가 오류·실패·건너뜀 0으로 통과했다. CI Windows 기본 CTest 37개와 Linux portable 11개는 해당 구성 범위다. Linux Vulkan compile, 기존 buffer/texture/draw/input/depth/timestamp·설치 payload 감사와 required core/sync validation도 새 SDK source에서 통과했다. Core/sync run은 SDK가 같은 `7287a29`, marker 검사·전체 Python은 검증기 수정 `f5e80f3`의 결과로 구분한다.

## 원본 소스와 완료 범위

```text
Windows 실행 바이트 SDK fingerprint: e4afbc3b39066425ce144e8b0171c4e4ac8ba893fc71419bfba2b9804d459634
Linux Git 바이트 SDK fingerprint:     1c08c10c0ee727495e9a1ffa2eee8ec509de482023ba7941d298091c907b2e4a
```

두 byte identity를 합치거나 옛 결과에 재할당하지 않았다. 각 source ZIP은 SDK 27개와 own 검사·observer·고정 prerequisite lock 등 총 39개 파일을 보존한다. Windows ZIP은 실행 바이트, Linux ZIP은 정확한 Git commit 바이트로 실제 Linux fingerprint와 일치한다. Compiled library/executable/observer 6개는 해시만 기록했고, 외부 header/runtime·compiled binary·dump는 source ZIP에 넣지 않았다. 이전 SDK/source ZIP과 모든 과거 실패 자료는 그대로 유지했다.

[검증 도구](../benchmarks/reports/validation/2026-10-26-phase3-vulkan-markers/verify_evidence.py)는 두 ZIP의 당시 코드로 native labels·원래 검증기 실패·수정 후 결과·CI 로그·원본 ZIP 해시를 대조한다.

```sh
python benchmarks/reports/validation/2026-10-26-phase3-vulkan-markers/verify_evidence.py
```

`P3-CAP-01`만 완료한다. 조건부 recovery/loss injection/budget/GPU-assisted, Linux 물리 GPU `P3-LNX-15`, Linux WSI, 경쟁 성능 및 최종 통합 gate는 미완료다. 사람 확인을 완료한 `P2-ACC-03`과 별개로 장시간 메모리·Intel AV·실제 DPI/물리 손실 상태도 유지한다.
