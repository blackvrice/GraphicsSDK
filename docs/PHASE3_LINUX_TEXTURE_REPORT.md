# Phase 3 — Linux 텍스처 전송·padding·수명 계약

2026-10-25 · `P3-LNX-06` · 검증 커밋 `fc30a6b`.

실제 Ubuntu에서 기존 `tests/VulkanTextureTests.cpp`를 변경 없이 빌드해 in-tree CTest와 설치 SDK 소비자로 실행했다. **Linux software ICD의 RGBA8UNorm/RGBA16Float 전송·전체 texel·padding·전이·수명 계약을 완료**한다. 관측 장치는 CPU 종류의 Mesa lavapipe이며 hardware adapter는 0개다. 물리 GPU 수용은 최종 gate의 필수 의존성인 `P3-LNX-15`에 유지한다.

[전용 CI](https://github.com/blackvrice/GraphicsSDK/actions/runs/40175221677)와 [job](https://github.com/blackvrice/GraphicsSDK/actions/runs/40175221677/job/121114319706)이 성공했다. SDK C++·공개 API·root `CMakeLists.txt`·기존 C++ suite는 변경하지 않았다. [요약](../benchmarks/reports/2026-10-25-phase3-linux-texture/summary.json), [원본 receipt](../benchmarks/reports/validation/2026-10-25-phase3-linux-texture/native/receipt.json), [job log](../benchmarks/reports/validation/2026-10-25-phase3-linux-texture/job.log)에 결과를 보존했다.

## 전체 참조 비교

| 실행 | Assertion | Roundtrip | 대조한 texel bytes | 비어 있지 않은 padding 검사 roundtrip |
|---|---:|---:|---:|---:|
| in-tree `vulkan_texture_transfer` CTest | 418 | 37 | 249,685,916 | 20 |
| 설치 SDK `vulkan_texture_consumer` | 418 | 37 | 249,685,916 | 20 |
| 합계 | **836** | **74** | **499,371,832** | **40** |

각 포맷의 크기는 `1×1`, `3×7`, `63×5`, `64×1`, `65×17`, `257×129`, `1920×1080`, `3840×2160`이며 크기마다 2개 패턴을 사용한다. 총 32개 주 roundtrip은 upload→image A→device staging→image B→readback으로 전송한다. 같은 layout에서 image upload와 readback copy를 반복하는 WAW 의존성, Common 경유 상태 전이도 실행했다. 추가 5회는 RGBA8UNorm `65×17` image의 내용·layout/version 재사용을 확인한다.

RGBA8UNorm은 texel당 4바이트, RGBA16Float는 8바이트다. **두 포맷의 모든 texel 바이트가 참조와 일치**했다. RGBA16Float 검사는 임의 바이트 패턴의 보존 검사이며 half-float 수치 연산·shader·sampling 정확성 검사는 아니다. 이번 지원 관측은 renderTarget/depthStencil이 아닌 전송 usage의 image 생성·복사다. rendering·sampling 수용은 후속 suite에서 별도로 검사한다.

Footprint는 `rowBytes=width×texelBytes`, rowPitch 256-byte 정렬, `size=rowPitch×height`를 확인했다. 주 roundtrip의 readback 전체를 `0xd3`로 먼저 채운 뒤 **매 행의 모든 texel 및 padding 바이트**를 검사했다. RGBA8/RGBA16F 모두 폭 1·3·63·65·257에서 padding이 생겨 포맷별 10회, 실행당 20회가 비어 있지 않은 padding 검사다. 폭 64·1920·3840에서는 row padding이 없다. 추가 재사용 5회는 texel만 대조하며 padding 검사 계수에 넣지 않는다.

`verified_texel_bytes`는 전체 대조한 texel 크기의 합이다. padding, 추가 계약용 복사, 여러 native copy 단계를 합산한 전체 트래픽이나 성능 측정값은 아니다. CTest는 `^vulkan_texture_transfer$` 하나를 선택하고 JUnit에서 정확히 1개 testcase의 통과와 skip/failure/error 부재를 확인했다. 설치 소비자는 별도 project에서 `find_package(GraphicsSDK CONFIG REQUIRED)`로 동일 suite를 빌드했다.

## 상태·거부·소유 수명

| 계약 | 실행한 검사 |
|---|---|
| description·경계 | 두 포맷과 크기·footprint 보존, zero/상한 초과 extent·알 수 없는 포맷·잘못된 depth/color usage 거부 |
| copy·transition | recording 밖 전이, null/foreign image·buffer, 잘못된 layout·Present/미지원 state, 부족한 upload/readback footprint·잘못된 buffer memory 종류 거부 |
| 거부 원자성 | image copy/transition 거부를 32회 반복해 wrapper와 native allocation/reference 통계 보존; 거부가 image/buffer version을 포착하지 않음 |
| stale recording | CPU upload write 이후 recording submit 거부, 성공한 image submit 이후 sibling recording 거부 |
| recording·submission 보유 | public image/buffer wrapper를 해제해도 ended recording이 6개 native allocation을 보유; submit 뒤 command list도 해제하고 wait/readback 성공 |
| 자원 회수 | 각 주 roundtrip 후 trackedObjects·uniqueResources·allocation bytes·pending submission/reference 0; 재사용/계약 종료도 회수 확인 |
| thread·퇴역 | 다른 thread의 image 생성 거부; public device 종료가 pending 작업을 drain/retire하고 image CPU description/footprint 보존, child native 연산은 `DeviceLost` |

zero-time poll은 완료 또는 Timeout을 허용한 뒤 유한 기본 wait를 사용했다. 실제 Timeout 발생 횟수는 계측하지 않았으며 매 실행 Timeout 발생을 보장하지 않는다. device 종료에 따른 통제된 퇴역은 예상 밖 native device loss/TDR 검증이 아니다. 자원 소유 계수는 OS/driver 전체 메모리나 장시간 메모리 회수 기준의 근거가 아니다. noncoherent 전용 분기 실행 여부도 별도 계측하지 않았다.

## 관측 환경과 재현

| 항목 | 값 |
|---|---|
| Host | GitHub-hosted Ubuntu 24.04.5 LTS, x86_64, kernel `6.17.0-1022-azure` |
| Loader / Mesa | `libvulkan1 1.3.275.0-1build1` / `mesa-vulkan-drivers 25.2.8-0ubuntu0.24.04.4` |
| Device | `llvmpipe (LLVM 20.1.2, 256 bits)`, native `VK_PHYSICAL_DEVICE_TYPE_CPU` (4), software 1 / hardware 0 |
| Adapter ID | `5892941257450743403` (JSON 문자열로 정밀도 보존) |
| Device / driver UUID | `6d65736132352e322e382d3075627500` / `6c6c766d706970655555494400000000` |
| Build | g++-14 / Ninja / Release, D3D12 OFF, Vulkan ON, tests ON, validation tests OFF |
| Headers | Vulkan-Headers `b379292b2ab6df5771ba9870d53cf8b2c9295daf` |
| Native SDK fingerprint | `ca69fbe86eb5fb231503e070927322b95a80fbde10a1a951133f640b0dec2830` |

같은 CI 실행에서 기존 설치 SDK device probe가 native UUID/FNV ID·종류·driver module을 확인했다. 두 텍스처 suite의 adapter 이름/ID는 그 software device와 일치했다. loader `/usr/lib/x86_64-linux-gnu/libvulkan.so.1.3.275`와 driver `/usr/lib/x86_64-linux-gnu/libvulkan_lvp.so`의 크기/hash, manifest 정보는 receipt에 보존했다. 외부 runtime binary는 artifact에 묶지 않았다.

[`tools/run_linux_texture.py`](../tools/run_linux_texture.py)는 실제 Linux, clean source/고정 headers, 새 build/install/evidence 경로를 요구한다. 관측한 lavapipe manifest 1개를 child의 `VK_DRIVER_FILES`와 `VK_ICD_FILENAMES`에 같은 절대 경로로 지정한다. 상속 VK/VVL 입력을 제거하고 implicit layer 비활성화·빈 explicit layer 경로·`LP_NUM_THREADS=2`·0700 XDG runtime 경로를 사용한다. 시스템 driver/layer를 교체하지 않는다. 전용 [workflow](../.github/workflows/linux-vulkan-texture.yml)가 Ubuntu runtime package를 준비하고 아래 명령을 실행한다.

```sh
python3 tools/run_linux_texture.py
```

CLI 오타/추가 인수 4회는 작업 전 exit 1·Usage로 거부했다. 빈 layer 경로에서 `--validation` 요청 2회도 exit 1·Khronos layer 부재 진단으로 거부됐다. 정상 suite는 `validation=0`이다. 이때 validation error 계수 0은 **정상 validation layer 실행 성공 근거가 아니다**. core/sync validation·오류 검출 controls는 `P3-LNX-11`에 남긴다. headless `vulkaninfo`의 surface skip도 WSI 성공으로 세지 않는다.

## 증거 검증과 남은 범위

[artifact ZIP](../benchmarks/reports/validation/2026-10-25-phase3-linux-texture/artifact.zip)의 SHA256 `b95b352cafe7426b1e40bd1d6c579a89de6cfe49fdc08f3d30f5707d8f163fad`은 GitHub artifact digest와 일치했다. 내부 diagnostic 크기/hash, job log와 ZIP의 receipt 일치, 검증 커밋의 runner·suite·probe·preset·workflow bytes 및 SDK 27개 소스 파일의 native fingerprint를 대조했다. 빌드와 설치 SDK static library는 모두 413,918바이트, SHA256 `c6870603fd53a581c3ac6a4fcb70e6a58220d0ff0433f17f51f2e8a045e38b32`로 일치했다.

Windows raw SDK fingerprint `2e511b8e53dbef4bbb2bf3d86ab4b19cf03e8765111de2b3dad4fe45fa551401`와 메모리 정책은 그대로다. Git/LF 기반 Linux fingerprint와 Windows CRLF 기반 fingerprint의 차이를 보존했으며 이전 Windows 측정을 새 Linux 실행으로 재귀속하지 않는다.

같은 커밋의 [기존 Build and correctness CI](https://github.com/blackvrice/GraphicsSDK/actions/runs/40175221822)에서는 portable C++ CTest 통과 뒤 Python 614개 중 기존 오류 92건이 재현됐다. [해당 로그](../benchmarks/reports/validation/2026-10-25-phase3-linux-texture/common-portable-job.log)를 보존하고 필수 `P3-LNX-14`는 미완료로 유지한다. 전용 텍스처 CI 성공을 전체 CI 성공으로 표현하지 않는다.

이번에는 `P3-LNX-06`만 software ICD 범위에서 완료한다. 물리 GPU `P3-LNX-15`, draw/binding/depth/timestamp, 정상 layer `P3-LNX-11`, 전체 설치 소비자·입력/라이선스 감사 `P3-LNX-12`, Linux WSI와 최종 gate는 별도 잔여다. 장시간 메모리·Intel AV 원인 조사 보류도 유지한다. 다음 Linux 작업은 `P3-LNX-07` 컬러 pass·기본 draw다.
