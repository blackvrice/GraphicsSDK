# Phase 3 — Linux loader·adapter·device 실행: software ICD

2026-10-25 · `P3-LNX-04` · 검증 커밋 `3558b82`.

실제 Ubuntu에서 Vulkan loader와 Mesa lavapipe를 사용해 `VkDevice` 생성·exact adapter 선택·오류 경로를 검증했다. **수용 범위는 native Linux의 software ICD 장치/API 경로**다. 관측 장치는 Vulkan의 CPU 종류이며 hardware adapter는 0개다. 물리 GPU·공통 GPU suite 수용은 새 필수 잔여 `P3-LNX-15`로 분리해 최종 gate 앞에 유지한다. 원래 물리 GPU 검증 요구를 제거하거나 software 성공으로 통과 처리하지 않는다.

성공한 [runtime workflow](https://github.com/blackvrice/GraphicsSDK/actions/runs/40163549084)와 [job](https://github.com/blackvrice/GraphicsSDK/actions/runs/40163549084/job/121079881822)을 근거로 한다. SDK C++·공개 API·root `CMakeLists.txt`는 변경하지 않았다. 별도 probe·runner·CI를 추가했다. [요약](../benchmarks/reports/2026-10-25-phase3-linux-device/summary.json), [원본 receipt](../benchmarks/reports/validation/2026-10-25-phase3-linux-device/native/receipt.json), [job log](../benchmarks/reports/validation/2026-10-25-phase3-linux-device/job.log)에 연결한다.

## 관측 환경·식별

| 항목 | 실제 값 |
|---|---|
| Linux | GitHub-hosted Ubuntu 24.04.5 LTS VM, x86_64, kernel `6.17.0-1022-azure` |
| Loader package | `libvulkan1 1.3.275.0-1build1` |
| Mesa package | `mesa-vulkan-drivers 25.2.8-0ubuntu0.24.04.4` |
| Vulkan tools | `1.3.275.0+dfsg1-1` |
| 선택 ICD | 관측한 `lvp_icd*.json` 1개를 절대 경로로 지정 |
| Device | `llvmpipe (LLVM 20.1.2, 256 bits)`, native `VK_PHYSICAL_DEVICE_TYPE_CPU` (4) |
| Adapter ID | `5892941257450743403`, 2개 새 process·여러 instance에서 일치 |
| Device UUID | `6d65736132352e322e382d3075627500` |
| Driver UUID | `6c6c766d706970655555494400000000` |
| Software / hardware count | 정상 process마다 **1 / 0** |
| SDK native fingerprint | `ca69fbe86eb5fb231503e070927322b95a80fbde10a1a951133f640b0dec2830` |

UUID/ID는 이 driver·실행 환경의 관측값이다. 다른 driver·OS·hardware나 재부팅 뒤의 영구 식별자로 보장하지 않는다. `VkPhysicalDevice`라는 API 타입 이름을 물리 GPU 탑재의 증거로 사용하지 않고 실제 CPU 종류와 software flag를 기록한다.

실제 로드한 loader는 `/usr/lib/x86_64-linux-gnu/libvulkan.so.1.3.275`, driver는 `/usr/lib/x86_64-linux-gnu/libvulkan_lvp.so`다. loader 진입점의 `dladdr`와 같은 process의 `/proc/self/maps`에서 해당 module 경로를 읽고 runner가 크기·SHA256을 기록했다. 전체 transitive runtime 입력·라이선스 감사를 완료한 것은 아니다. loader/ICD binary는 artifact에 넣지 않고 manifest 정보·hash·로그를 보존했다.

## 정상 선택과 negative controls

| Process | 결과 |
|---|---|
| `selection-1`, `selection-2` | 각각 32 assertion, 실제 SDK device 3개, 기대한 거부 응답 7개. 합계 **64 assertion / 6 device** |
| `no-driver` | 4 assertion. 열거/생성이 `BackendFailure`, native `VK_ERROR_INCOMPATIBLE_DRIVER` (-9). 다른 ICD로 fallback하지 않음 |
| `no-loader` | 4 assertion. 열거/생성이 `Unsupported`. child에만 로드할 수 없는 loader fixture를 지정해 실패 확인 |
| `missing-layer` | 3 assertion. validation 요청이 `Unsupported`, Khronos layer 부재 명시. 정상 validation 실행 성공은 아님 |
| 잘못된 mode·추가 인수 | 2개 process 모두 exit 1, Usage 진단으로 실행 전 거부 |

합계는 **75 assertion, 기대한 거부 응답 19개, CLI 거부 2회**다. 정상 경로는 독립 native API로 16-byte device UUID, driver UUID, native 종류·vendor/device·graphics queue를 읽어 SDK의 ID·metadata와 대조했다. 새 instance의 열거, 같은 ID의 두 device 생성, 명시적인 software default 선택을 확인했다. ID는 JSON 문자열로 보존해 64-bit 정수 정밀도를 유지한다.

선택 거부는 software-kind mismatch, 다른 backend ID, 없는 exact ID, zero ID, hardware default의 software 암묵 fallback, 미빌드 backend 열거, 미지원 loss opt-in을 포함한다. loss opt-in은 거부만 검사했으며 native device removal이나 TDR을 실행하지 않았다.

probe는 buffer/image/command list를 만들지 않으며 전송·draw·timestamp·WSI를 실행하지 않는다. 빈 device의 SDK 소유 resource 0은 확인했지만 driver 메모리 회수·장시간 메모리 수용의 근거로 사용하지 않는다.

## 구현·환경 격리

[`tests/linux_vulkan_device`](../tests/linux_vulkan_device/CMakeLists.txt)는 설치된 `GraphicsSDK::Core`를 `find_package`로 사용하는 별도 project다. 독립 native identity 조회 때문에 probe 자체가 고정 Vulkan-Headers를 사용한다. SDK public header에 Vulkan 의존성을 추가하지 않았다. build library와 설치 library의 bytes 일치도 확인했다.

[`tools/run_linux_device.py`](../tools/run_linux_device.py)는 실제 Linux host, source/headers commit, clean checkout을 확인한다. 전용 runtime build는 preset을 `GFX_BUILD_TESTS=OFF`로 덮어쓰고 SDK와 이 device probe만 구성한다. 기존 full compile/CPU CI는 별도로 유지하며 [같은 commit의 compile CI](https://github.com/blackvrice/GraphicsSDK/actions/runs/40163549173)도 성공했다. 기존 전체 CI Python 문제 `P3-LNX-14`는 미완료다.

ICD는 child 환경의 `VK_DRIVER_FILES`로 선택하고 구형 loader용 `VK_ICD_FILENAMES`도 같은 경로로 맞췄다. 새 변수가 우선하는 계약은 [Khronos loader 문서](https://github.com/KhronosGroup/Vulkan-Loader/blob/main/docs/LoaderDriverInterface.md#overriding-the-default-driver-discovery)를 확인했다. 상속한 VK/VVL 입력을 제거하고 implicit layer를 비활성화했으며 명시적 layer 경로는 빈 제어용 directory로 지정했다. `LP_NUM_THREADS=2`와 XDG runtime 경로도 child에만 적용했다.

no-driver는 없는 manifest를 지정한다. no-loader는 system library를 바꾸지 않고 child의 `LD_LIBRARY_PATH`로 로드 불가능한 작은 자체 fixture를 선택한다. 시스템 설정 변경·패키지 제거로 만든 실패가 아니다. `vulkaninfo --summary`의 DISPLAY 부재·surface 정보 skip은 headless 관측으로 보존하고 WSI 성공으로 세지 않는다.

실제 Linux의 새 checkout과 고정 headers, g++-14/Ninja, 위 runtime package가 필요하다. runner는 기존 build/install/evidence root를 덮어쓰지 않고 거부한다.

```sh
python3 tools/run_linux_device.py
```

## 증거와 잔여 수용

[artifact ZIP](../benchmarks/reports/validation/2026-10-25-phase3-linux-device/artifact.zip)의 SHA256 `10313fe87df79146c6bf5fc3cf7301bfb87544dddc99ae2d21506bd88a183930`은 GitHub artifact digest와 일치했다. 내부 diagnostics, job에 출력한 receipt, 검증 commit의 probe/runner/preset/workflow bytes와 native SDK fingerprint도 대조했다. 보존된 Windows SDK fingerprint `2e511b8e53dbef4bbb2bf3d86ab4b19cf03e8765111de2b3dad4fe45fa551401`는 변경하지 않았다.

`P3-LNX-04`는 관측한 software device/API 범위에서 완료한다. `P3-LNX-15`는 Linux 물리 GPU의 ID/driver·선택 및 공통 GPU 참조/validation/수명 검사의 필수 수용으로 남기고 `P3-GATE-01`의 의존성에 추가한다. `P3-LNX-05` 이후 전송/그리기, `P3-LNX-11` 정상 layer 실행, `P3-LNX-12` 전체 소비자/입력 감사, Linux WSI는 완료로 바꾸지 않는다. 이번 설치 device probe는 그 전체 수용을 대신하지 않는다. 보류한 메모리/Intel AV 조사도 유지한다.
