# Phase 3 — Linux core/synchronization validation 수용

2026-10-25 · `P3-LNX-11` · 검증 커밋 `051efe6`.

실제 Ubuntu에서 고정 VVL 1.3.290을 빌드해 **공통 suite 8개를 in-tree와 설치 SDK에서 각각 실행하고, 정상 validation 오류 계수 0과 의도된 오류 검출을 확인**했다. 수용 범위는 headless Linux의 Mesa software ICD다. 관측 장치는 CPU 종류이며 hardware adapter는 0개다. 물리 GPU의 같은 공통 suite/validation 수용은 최종 gate의 필수 의존성 `P3-LNX-15`에 유지한다.

[전용 CI](https://github.com/blackvrice/GraphicsSDK/actions/runs/40184124754)와 [job](https://github.com/blackvrice/GraphicsSDK/actions/runs/40184124754/job/121141188825)이 성공했다. SDK C++·공개 API·root `CMakeLists.txt`는 변경하지 않았다. 기존 native controls에는 Linux에서 실제 로드한 shared library를 확인하는 검사만 추가했다. [요약](../benchmarks/reports/2026-10-25-phase3-linux-validation/summary.json), [원본 receipt](../benchmarks/reports/validation/2026-10-25-phase3-linux-validation/native/receipt.json), [job log](../benchmarks/reports/validation/2026-10-25-phase3-linux-validation/job.log)에 결과를 보존했다.

## 정상 required validation

| 공통 suite | in-tree assertion | 설치 소비자 assertion | Validation 오류 계수 |
|---|---:|---:|---:|
| Buffer·queue/fence·수명 | 361 | 361 | 0 |
| Texture 전송·texel/padding | 418 | 418 | 0 |
| 컬러 Clear/Load/Store·MRT | 297 | 297 | 0 |
| 삼각형·blend/scissor·instancing·MRT | 319 | 319 | 0 |
| Vertex/index/instance 입력 | 386 | 386 | 0 |
| Texture/constant 배열·sampler·snapshot | 287 | 287 | 0 |
| D32·depth-only/read-only·비교/쓰기 mask | 442 | 442 | 0 |
| Timestamp query·availability·수명 | 156 | 156 | 0 |
| 합계 | **2,666** | **2,666** | **정상 16회 모두 0** |

정상 실행은 모두 `validation=1`이다. 기존 suite의 `validationErrorCount()` 검사 통과를 확인했다. 오류 0은 이 SDK callback 계수의 검사 시점 기준이며 전체 OS/driver 로그 누적값을 뜻하지 않는다. 앞선 driver-only 실행의 0을 validation 성공으로 재해석하지 않았다.

`GFX_VULKAN_VALIDATION_TESTS=ON`인 별도 Linux preset에서 required CTest 8개를 하나씩 정확히 선택했다. 각 JUnit은 testcase 1개, skip/failure/error 0이다. 설치된 `GraphicsSDK::Core`의 별도 소비자 8개도 `--validation`으로 실행했다. 전체 buffer/texel/color/depth 참조·padding 및 각 경계/foreign/thread/version/lifetime 계약은 기존 suite 그대로 검사했다. 이번 in-tree/설치 library의 bytes도 일치했다.

누락 layer 대조는 각 설치 소비자에서 빈 `VK_LAYER_PATH`로 `--validation`을 요청했다. **8회 모두 exit 1·`VK_LAYER_KHRONOS_validation is not installed`로 명시적 실패**했고 PASS 출력은 없었다. 누락 layer를 skip·driver-only fallback으로 통과시키지 않는다.

## 실제 오류 검출 controls

정상 workload와 다른 process인 `vulkan_validation_controls`는 **137 assertion**을 통과했다.

| Control | 실제 결과 |
|---|---|
| Native core VUID | 크기 0 buffer 생성의 `VUID-VkBufferCreateInfo-size-00912` **32회**. callback이 invalid call을 driver dispatch 전에 중단하고 반환값/빈 handle 확인 |
| Native sync hazard | barrier 없는 두 copy의 **WAW hazard 1회**, `SYNC-HAZARD-WRITE-AFTER-WRITE` 검출 |
| Sync OFF 대조 | synchronization feature를 끈 별도 instance에서 같은 recording의 sync hazard 계수 0, 그 외 예상 밖 오류도 0 |
| Synthetic callback | 네 호출 thread의 ERROR 1,024회와 독립 instance의 ERROR 1회, 합계 **1,025회** 손실 없이 집계 |
| 계수·종료 | warning/info 제외, instance별 counter 독립성, native device/instance 종료 뒤 예상 밖 오류 0 |
| Invalid GPU submission | **0회**. controls의 command buffer는 어느 경우에도 제출하지 않음 |

Primary counter 1,057(32 core+1 sync+1,024 synthetic), 독립 counter 1, sync-OFF counter 0을 각각 확인했다. 기본 반복 메시지 제한보다 많은 32개 동일 VUID가 모두 전달돼 duplicate limit 해제도 검증됐다. Synthetic 메시지를 native VUID나 정상 오류로 합산하지 않는다. 네 호출 thread에서의 손실 없는 집계이며 callback 내부가 동시에 실행됐다는 계측은 아니다.

공유 `ValidationConfiguration`은 core에 synchronization validation과 submit-time checking을 요청하고 `duplicate_message_limit=0`, 빈 `message_id_filter`를 설정한다. 이번 sync positive control은 recording 시 WAW 검출이다. Submit-time에서만 발생하는 별도 hazard를 양성 대조로 실행한 것은 아니다. GPU-assisted validation은 활성화하지 않았다.

## 고정 runtime·실제 로드 경로

Ubuntu noble의 [기본 package](https://packages.ubuntu.com/noble/vulkan-validationlayers)는 1.3.275.0-1이며, 해당 [upstream manifest](https://raw.githubusercontent.com/KhronosGroup/Vulkan-ValidationLayers/v1.3.275/layers/VkLayer_khronos_validation.json.in)에 SDK가 요구하는 `VK_EXT_layer_settings`가 없다. SDK 요구를 낮추지 않고 기존 고정 VVL 1.3.290을 Linux용으로 별도 빌드했다.

[`prepare_linux_validation.py`](../tools/prepare_linux_validation.py)는 기존 [source lock](../tools/vulkan-validation.lock.json)의 여섯 commit만 재사용하고, Windows build 설정은 적용하지 않는다. 새 [Linux profile](../tools/linux-vulkan-validation.json)은 g++-14/Ninja/Release, runtime과 prerequisite의 `-O0 -DNDEBUG`, prerequisite parallel 2·layer parallel 1, PIC를 사용한다. Mimalloc·upstream tests·SPIR-V executables·XCB/Xlib/Wayland WSI support는 OFF다. SDK의 정상 Release 최적화는 그대로다. 외부 runtime의 속도나 완전한 upstream test 통과를 주장하지 않는다.

| Component | 고정 commit |
|---|---|
| Vulkan-ValidationLayers v1.3.290 | `79f4759c2c492c1be37e18c0318330f2959327cb` |
| Vulkan-Headers | `b379292b2ab6df5771ba9870d53cf8b2c9295daf` |
| Vulkan-Utility-Libraries | `5f26cf65a18bc89a8e3d6569c14314b6fdac8d4d` |
| SPIRV-Headers | `2acb319af38d43be3ea76bfabf3998e5281d8d12` |
| SPIRV-Tools | `0cfe9e7219148716dfd30b37f4d21753f098707a` |
| robin-hood-hashing | `9145f963d80d6a02f0f96a47758050a89184a3ed` |

빌드 전후에 각 source의 정확한 commit과 tracked 변경 부재를 확인했고 원본을 수정하지 않았다. Runtime은 전용 `out/linux-vvl-runtime-build`에서 빌드하고 의존성 install도 그 아래 전용 prefix를 사용한다. CI의 compiler/loader/Mesa package 준비와 별개로 VVL을 시스템에 설치·등록하지 않았다.

정상 16개 실행에서는 child에만 `LD_DEBUG=libs`를 지정해 실제 로드 경로를 보존했다. CTest의 child 출력도 포함해 읽고 canonical 경로가 예상 `.so` 하나와 같음을 확인했다. Native controls의 세 instance에서도 `dl_iterate_phdr`와 파일 동등성 검사로 같은 shared library를 확인했다.

```text
/home/runner/work/GraphicsSDK/GraphicsSDK/out/linux-vvl-runtime-build/Vulkan-ValidationLayers/layers/libVkLayer_khronos_validation.so
```

Library는 **59,913,736바이트**, SHA256 `264f6326379cdb9d562037aa06a49c7d6ec477cc4df7c4d8f5b894c9b9f93e5d`다. Manifest는 95,948바이트, SHA256 `e7a747e87a6663097db54662a353efe25fa92061995b89db9f2693ffd219f4ef`다. Generated manifest의 target/API version을 확인했고 두 파일의 실행 전후 hash가 일치했다. [Runtime receipt](../benchmarks/reports/validation/2026-10-25-phase3-linux-validation/native/runtime/receipt.json)에 source/profile/빌드 로그·원문 license 파일 hash를 기록했다.

이 외부 `.so`·의존 library·vendor source를 SDK나 artifact ZIP에 넣지 않았다. 여기의 runtime provenance는 전체 compiler/linker 입력·라이선스 감사 또는 runtime 재배포 수용을 대신하지 않는다.

## 환경·재현·증거

GitHub-hosted Ubuntu 24.04.5 LTS x86_64, kernel `6.17.0-1022-azure`, `libvulkan1 1.3.275.0-1build1`, `mesa-vulkan-drivers 25.2.8-0ubuntu0.24.04.4`에서 실행했다. 기존 설치 SDK device probe가 native CPU 종류(4)·UUID와 SDK ID를 대조했다. `llvmpipe (LLVM 20.1.2, 256 bits)`, ID `5892941257450743403`이며 software 1/hardware 0이다.

[`run_linux_validation.py`](../tools/run_linux_validation.py)는 실제 Linux·clean source·새 SDK/install/consumer root와 준비된 runtime receipt를 요구한다. 관측한 lavapipe manifest 하나를 child의 `VK_DRIVER_FILES`/`VK_ICD_FILENAMES`에 같은 경로로 지정한다. 상속 VK/VVL/LD/GFX_EXPECT 입력을 제거하고 implicit layer 비활성화, 고정 `VK_LAYER_PATH`·`GFX_EXPECT_VULKAN_LAYER`, 빈 settings 파일의 `VK_LAYER_SETTINGS_PATH`, `LP_NUM_THREADS=2`, 0700 XDG runtime 경로를 적용했다. 실제 settings 파일은 실행 뒤에도 0바이트다.

전용 [workflow](../.github/workflows/linux-vulkan-validation.yml)는 Ubuntu runner에 필수 package를 준비한 뒤 아래를 실행한다. 준비와 검증 모두 기존 경로를 덮어쓰지 않고 거부한다.

```sh
python3 tools/prepare_linux_validation.py
python3 tools/run_linux_validation.py
```

[Artifact ZIP](../benchmarks/reports/validation/2026-10-25-phase3-linux-validation/artifact.zip)의 SHA256 `e84ddc7676f781fac7af44a0c083bc1ee23746ebae383a4d0676ba8ce94e8e88`은 GitHub artifact digest와 일치했다. 모든 내부 diagnostic 크기/hash, job log와 ZIP의 runtime/실행 receipt 일치, 검증 커밋의 runner/profile/lock/suite/support/fixture bytes를 대조했다. Native controls의 변경도 Linux loaded-library 검사 추가뿐임을 source 대조로 확인했다.

SDK 27개 소스의 native Git/LF fingerprint는 `ca69fbe86eb5fb231503e070927322b95a80fbde10a1a951133f640b0dec2830`, 보존한 Windows CRLF SDK fingerprint는 `2e511b8e53dbef4bbb2bf3d86ab4b19cf03e8765111de2b3dad4fe45fa551401`로 그대로다. 빌드/설치 SDK library는 모두 413,918바이트, SHA256 `c6870603fd53a581c3ac6a4fcb70e6a58220d0ff0433f17f51f2e8a045e38b32`다. 메모리 정책과 이전 Windows 실행의 귀속은 변경하지 않았다.

같은 커밋의 [Linux compile CI](https://github.com/blackvrice/GraphicsSDK/actions/runs/40184124705)도 두 preset의 compile·CPU 11개씩을 통과했다. 별도 [전체 CI](https://github.com/blackvrice/GraphicsSDK/actions/runs/40184124680)는 Python 614개에서 Linux errors 92·Windows errors 99로 실패했다. [Linux 원본](../benchmarks/reports/validation/2026-10-25-phase3-linux-validation/common-portable-job.log)과 [Windows 원본](../benchmarks/reports/validation/2026-10-25-phase3-linux-validation/common-windows-job.log)을 보존했다. Windows의 추가 7건은 fixed-memory identity 2건과 temp 경로의 `RUNNER~1`/`runneradmin` 불일치 5건이다. 이번 작업에서 수정하지 않았고 `P3-LNX-14`의 추가 관측 근거로 연결한다. 전용 validation 성공을 전체 CI 성공으로 표현하지 않는다.

완료 상태는 `P3-LNX-11`만 바꾼다. 물리 GPU `P3-LNX-15`는 최종 gate의 필수 의존성으로 남긴다. 전체 설치 소비자·compiler/linker 입력·라이선스 감사 `P3-LNX-12`, 전체 CI 정비 `P3-LNX-14`, Linux WSI와 최종 gate는 별도 잔여다. 장시간 메모리·Intel AV 조사 보류도 유지한다. 다음 Linux 항목은 `P3-LNX-12` 설치 소비자·입력 감사다.
