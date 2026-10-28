# P3-CMP-03 LLGL Vulkan 업로드 정확성 검증

2026-10-28. **사용자가 승인한 명시적 패치 프로필로 P3-CMP-03을 완료했다.** 고정 원본의 sync 오류는 유지하며, `LLGL 649c0cc + transfer-src patch v1`은 네 입력의 일반·검증 실행을 모두 통과했다. [사용자 결정](PHASE3_LLGL_UPLOAD_SCOPE.md)과 [비교 프로필 계약](../benchmarks/phase3_comparison/competitors/llgl-transfer-src-profile.json)을 별도 기록했다. 성능 측정은 미완료다.

## 원본과 수정 후보 결과

| 대상 | 네 입력 / 전체 참조 | native 검증 | 현재 수용 |
|---|---|---|---|
| LLGL 원본 `649c0cc8fa608312c5e4b94dae409a5859fc7553` | odd 65×37 RGBA8, 1080p/4K RGBA8, 4K RGBA16F. 각 4회 변하는 전체 이미지. 일반 실행 네 경우 모두 일치 | 네 경우 모두 readback에서 `SYNC-HAZARD-READ-AFTER-WRITE` 재현 | 원본 correctness·성능 **수용 안 함** |
| `transfer-src-candidate` → 승인된 명시적 프로필 v1 | 같은 커밋에 아래 10줄만 추가. 별도 DLL/프로세스. 일반·검증 8회 프로세스에서 **32개 이미지·862,694,560바이트** 전체 비교 및 독립 CPU SHA-256 일치 | 정상 workload delivered ERROR 0. core/sync 양성 통제와 sync 비활성 음성 통제 통과 | **업로드 correctness 완료**. 사용자 결정 전 실행 이력은 candidate로 보존. 성능 수용 안 함 |

실제 장치는 두 대상 모두 NVIDIA GeForce GTX 1660 SUPER이며 native device UUID `be4ae3472ccd6deb68ccf2e33670e7bd`, driver UUID `61fefcd074265598aa87bdd69012bf6a`, driverVersion 2604679168, graphics queue family/index 0이다. 이름으로 추정하지 않고 LLGL native physical/logical device·queue를 조회했다. loader/device 지원 API는 1.4.357이며 instance 생성 인수는 가로채 관측하지 않았다. LLGL의 loader-max 요청은 고정 소스에서 유도한 정보로 구분한다.

## 원인과 검토할 변경

원본 `sources/Renderer/Vulkan/Command/VKCommandContext.cpp`의 `ImageMemoryBarrier`는 old/new layout switch에 `TRANSFER_SRC_OPTIMAL` 처리가 없다. image를 copy source로 바꾸면서 transfer-read 접근과 transfer stage를 지정하지 않아, 이어지는 image-to-buffer copy에서 layout transition에 대한 읽기 hazard가 검출됐다. 공개 `CopyBufferFromTexture`가 이 helper를 사용한다. `ReadTexture`도 같은 helper로 transfer-source 전환을 수행하므로 소스 수준에서 같은 제한을 가진다. 후자는 별도 실행 통과를 주장하지 않는다.

[검토할 patch](../benchmarks/reports/validation/2026-10-28-phase3-llgl-upload/patch/transfer-src.patch)는 해당 함수의 두 switch에 각 5줄을 추가한다. old layout에는 `srcAccessMask=TRANSFER_READ`, new layout에는 `dstAccessMask=TRANSFER_READ`, 두 경우 모두 transfer stage를 지정한다. [원본 파일](../benchmarks/reports/validation/2026-10-28-phase3-llgl-upload/patch/VKCommandContext.original.cpp)과 [수정 파일](../benchmarks/reports/validation/2026-10-28-phase3-llgl-upload/patch/VKCommandContext.cpp)을 함께 보존했다. 원래 BSD 고지와 루트 라이선스를 유지한다.

**`.deps/LLGL` 고정 checkout은 변경하지 않았다.** 별도 출력 경로의 source overlay 한 파일을 명시적으로 교체해 DLL을 새로 빌드했다. 다른 소스는 같은 고정 checkout이다. DLL·compiler/linker read log·overlay·patch 해시를 기록하며 consumer에도 `dependency_profile=transfer-src-candidate`를 출력한다. 수정 후보를 원본 LLGL 성능으로 표시하거나 원본의 실패를 숨기지 않는다.

## 업로드 계약과 오류 통제

CPU payload 함수 본문은 기존 SDK/SDL upload 본문과 일치하도록 검사한다. RGBA8 uint32와 유한 RGBA16F 패턴, seed 1~4를 그대로 유지하고 독립 Python stdlib 구현으로 전체 이미지 SHA-256을 만든다. 단순 pixel/일부 행 검사나 이전 값의 재사용으로 통과시키지 않는다.

전송은 LLGL 공개 `MapBuffer`/`UnmapBuffer` → `CopyTextureFromBuffer` → command/fence Submit → WaitFence다. 읽기는 공개 `CopyBufferFromTexture` → fence 완료 → `ReadBuffer`다. row/layer stride 0은 tight layout이며 SDK의 padded staging과 다르다. LLGL은 CPU map용 내부 staging을 가진 device buffer를 사용해 Unmap 시 내부 GPU buffer 복사가 추가된다. 이 차이를 향후 total CPU 경계에 포함해야 한다. 이번에는 시각·시간 표본을 만들지 않았다(`samples_ms=[]`, `performance_samples=0`, `gpu_ms=null`).

필수 고정 VVL DLL의 실제 로드와 child 환경·설정 파일을 보존했다. core validation은 잘못된 properties 구조의 sType 조회를 잡는 VUID로, sync validation은 4KiB buffer의 write-after-write를 **record만 하고 제출하지 않는** 별도 프로세스로 확인했다. ERROR callback은 잘못된 call의 driver 전달 중단을 요청한다. 이 native 호출은 검증 통제에만 사용하며 실제 업로드를 native API로 우회하지 않는다.

수정 후보 matrix는 정상 업로드 8개, core/sync 진단 통제 2개, missing-layer·wrong-target·readback 마지막 byte 변조·sync-disabled 거부 4개, 총 14개 프로세스다. sync를 끄면 의도한 sync diagnostic을 얻지 못해 실패해야 한다. 검증 설정 이름만 지정하고 성공으로 처리하지 않는다. 원본에서도 core/sync 진단과 sync-disabled 통제를 별도로 실행했다.

## 기록과 재검사

[summary.json](../benchmarks/reports/validation/2026-10-28-phase3-llgl-upload/summary.json), [원본 결과](../benchmarks/reports/validation/2026-10-28-phase3-llgl-upload/upstream/receipt.json), [수정 후보 결과](../benchmarks/reports/validation/2026-10-28-phase3-llgl-upload/candidate/receipt.json), [독립 재검사](../benchmarks/reports/validation/2026-10-28-phase3-llgl-upload/verify.py)를 보존한다. 원본 실행 당시의 별도 source ZIP과 수정 후보 source ZIP으로 각 fingerprint를 확인한다. 외부 DLL/EXE/driver/loader는 재배포하지 않는다.

```text
python -m unittest discover -s tests -p test_llgl_vulkan_upload.py -v
python benchmarks/reports/validation/2026-10-28-phase3-llgl-upload/verify.py
python tools/roadmap.py --check
```

offline 검사는 저장된 metadata·raw output·해시·원본/수정 후보 구분을 확인한다. GPU 실행이나 원본 dependency 획득을 재현하지 않으며 metadata 해시는 외부 신뢰 서명이 아니다. native 재실행은 `tools/prepare_llgl_transfer_src_candidate.py`와 `tools/llgl_vulkan_upload.py --candidate-build ...`를 명시적으로 사용해야 한다. 기본 runner는 원본 DLL을 요구하고 원본의 검증 실패를 거부한다.

새 회귀 테스트 30개와 전체 Python 테스트 956개가 통과했다. 원본/수정 프로필 혼동, 전체 이미지 누락·변조, validation 통제 누락, 성능 수용 위조, 사용자 결정·patch 해시 변경도 거부한다. CI에서도 저장된 원본 오류와 명시적 수정 프로필을 독립 재검사한다.

사용자는 “명시적 패치 프로필을 허용해 계속 진행”을 선택했다. 이에 따라 고정 목록은 그대로 두고 고정 커밋+patch 해시를 새로운 명시적 비교 입력으로 연결했다. 실험 당시 candidate receipt의 `adopted_as_comparison_baseline=false`는 결정 전 이력이므로 바꾸지 않는다. 이후 채택 상태와 표시 이름은 별도 프로필 계약에서 검증한다. 다음은 `P3-CMP-04` draw/instancing correctness다. P3-CMP-07의 실제 세 SDK 비교와 다른 필수 gate를 자동 축소하지 않는다. 메모리/Intel 충돌 조사는 재개하지 않았다.
