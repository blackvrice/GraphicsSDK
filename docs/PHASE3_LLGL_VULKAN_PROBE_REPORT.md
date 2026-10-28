# P3-CMP-02 LLGL Vulkan 빌드·장치 확인

2026-10-28, **Windows x64 NVIDIA에서 완료**했다. 고정 LLGL을 Vulkan 전용 Release로 새로 빌드하고 실제 native 장치·드라이버·큐, 최소 buffer 복사·전체 읽기 참조, 검증 레이어 로드와 두 거부 검사를 확인했다. 이는 `P3-CMP-03`~`06`의 네 workload correctness나 `07`~`10`의 성능 비교 완료를 뜻하지 않는다. 성능 표본은 0개다.

## 실제 구성과 결과

| 항목 | 관측·고정 내용 |
|---|---|
| LLGL | `649c0cc8fa608312c5e4b94dae409a5859fc7553`, tracked 변경 없는 checkout. 기존 D3D12 DLL을 사용하지 않고 별도 새 출력 폴더에 빌드 |
| Vulkan headers | `b379292b2ab6df5771ba9870d53cf8b2c9295daf` / 1.3.290, tracked 변경 없음 |
| 빌드 | Visual Studio 17 2022 x64, MSVC 14.44.35207, Windows SDK 10.0.26100.0, CMake 4.3.1. 실제 compiler command 3개에서 `/O2`, `/MD`, no-LTO 확인 |
| 선택 target | `LLGL`, `LLGL_Vulkan`, `llgl_vulkan_probe`. 다른 renderer, XR, wrapper, tests/examples OFF. LLGL wrapper debug layer OFF, native DebugDevice는 검증 실행에만 요청 |
| validation 설정 | GPU-assisted/자동 SPIR-V reflection OFF. 기존 고정 VVL 1.3.290 DLL·manifest와 prerequisite receipt/lock 해시 대조. core+sync·submit-time 설정을 별도 파일과 child 환경으로 요청 |
| 실제 GPU | NVIDIA GeForce GTX 1660 SUPER, vendor 4318, device 8644, native driverVersion 2604679168, device/loader API version 4211045 (1.4.357) |
| native UUID | device `be4ae3472ccd6deb68ccf2e33670e7bd`, driver `61fefcd074265598aa87bdd69012bf6a`. LUID는 원본 receipt에 보존 |
| 실제 queue | graphics family 0 / index 0 / flags 15. LLGL이 돌려준 queue와 `vkGetDeviceQueue` 결과 일치 |
| 정상 실행 | 별도 프로세스 plain identity, plain minimal smoke, validation minimal smoke 3개. 실제 UUID/driver/queue가 모두 일치 |
| 최소 참조 | smoke마다 4096-byte buffer를 공개 WriteBuffer → CopyBuffer → Submit(command/fence) → 5초 제한 WaitFence → ReadBuffer로 3회 확인. pass·byte마다 변하는 전체 payload 비교. 두 smoke 합계 24,576바이트, delivered error 0 |
| 거부 검사 | 틀린 GPU 이름은 fallback 없이 실패. 필수 layer가 없는 별도 검색 경로에서는 LLGL의 silent layer fallback 전에 실패. 두 프로세스 모두 의도한 오류 문구·exit 1, 성공 JSON 없음 |

native identity는 별도 이름 검색으로 추정하지 않는다. `RenderSystem::GetNativeHandle`에서 얻은 실제 `VkPhysicalDevice`로 properties2/UUID/LUID를 조회하고 실제 logical device/queue도 확인한다. vendor preference는 장치를 확정하는 조건으로 사용하지 않는다. 정확한 GPU 이름·discrete NVIDIA·유효 LUID와 actual handle 검사를 함께 통과해야 한다.

LLGL 소스는 loader maximum을 instance API로 요청한다. 이번 probe는 **device 지원 API와 loader API를 실제 조회**했으며, instance 생성 인수를 가로채 관측하지는 않았다(`instance_API_request_observed=false`). 같은 request라고 가정하거나 이 값으로 SDK/SDL 요청의 동등성을 승인하지 않는다. 후속 workload에서 실제 요청·지원 차이를 기록해야 한다.

## 실제 빌드·실행 입력 감사

[입력 기록](../benchmarks/reports/validation/2026-10-28-phase3-llgl-vulkan/probe/inputs.json)은 compiler read 3개, linker read 3개와 실제 compiler command 3개를 근거로 한다. 읽은 파일 729개를 다음과 같이 분류했고, 새로 선택된 경로·라이선스 branch는 거부한다.

| 분류 | 파일 수 | 이번 선택 |
|---|---:|---|
| LLGL source/header | 373 | 각 실제 입력의 BSD 고지 확인, BSD-3-Clause. examples/다른 renderer/외부 SDK를 root license만으로 승인하지 않음 |
| Vulkan header | 13 | 각 입력의 SPDX 확인, 이번 실제 13개는 Apache-2.0 |
| 자체 probe | 1 | MIT |
| compiler/Windows SDK 시스템 입력 | 340 | 별도 시스템 전제조건, 재배포하지 않음 |
| 생성 링크 입력 | 2 | 이번 LLGL import library와 시스템 Vulkan loader용 새 import library. 정해진 두 경로만 허용 |

시스템 `vulkan-1.dll`의 실제 export 목록에서 `.def`를 생성하고 Microsoft `lib.exe`로 import-only library를 만들었다. 이는 loader 구현을 SDK에 포함한 것이 아니다. 원본 export 로그·`.def`·생성 명령·도구/loader/import library 해시를 보존하며 실제 로드 경로도 Windows System32와 대조한다. 외부 Vulkan SDK 번들 바이너리를 빌려 링크하지 않았다.

빌드 DLL과 probe의 실제 로드 경로는 이번 출력 폴더와 일치한다. compiler/linker 입력, 도구·설정·원본·DLL 등 **754개** 기록을 probe 전후 대조했다. runtime 모듈도 별도 경로·해시로 보존한다. 시스템/driver와 명시한 자체 산출물·고정 layer 이외의 모듈은 거부한다. 원본 시스템/driver/validation/LLGL 바이너리는 저장소에 재배포하지 않는다. 적용 루트 라이선스와 입력별 고지는 [감사 기록](../benchmarks/reports/validation/2026-10-28-phase3-llgl-vulkan/probe/inputs.json), [라이선스 파일](../benchmarks/reports/validation/2026-10-28-phase3-llgl-vulkan/licenses/LLGL-BSD-3-Clause.txt)에 보존했다.

직접 실행한 초기 장치 조회에서는 PC의 OBS implicit capture hook이 보였다. **최종 통제 실행은 child 환경에서 implicit Vulkan layer를 비활성화했고 세 runtime snapshot에 외부 capture hook이 없다.** 시스템 설정이나 OBS 상태를 변경하지 않았다. 이 차이와 관계없이 본 작업의 시간은 성능 표본으로 쓰지 않는다.

검증 실행은 실제 고정 layer DLL의 로드 경로와 delivered error 0을 확인했다. core+sync 설정 요청은 고정했지만 이 최소 smoke만으로 모든 validation feature/error 통제가 동작한다고 주장하지 않는다. 네 workload의 전체 readback·필수 validation·feature 통제는 `P3-CMP-03`~`06`에서 별도로 수용한다.

## 재현과 검증

```text
python tools/prepare_llgl_vulkan.py --output out/NEW_BUILD --cmake ABSOLUTE_CMAKE_PATH
python tools/llgl_vulkan_probe.py --build-root out/NEW_BUILD --output out/NEW_PROBE --gpu-name "NVIDIA GeForce GTX 1660 SUPER"
python benchmarks/reports/validation/2026-10-28-phase3-llgl-vulkan/verify.py
python -m unittest discover -s tests -p test_llgl_vulkan_probe.py -v
python tools/roadmap.py --check
```

빌드 도구는 새 출력 경로만 허용한다. 검증 실행에는 기존 고정 Windows VVL prerequisite가 필요하다. 독립 [verify.py](../benchmarks/reports/validation/2026-10-28-phase3-llgl-vulkan/verify.py)는 원본 로그/metadata 해시·[자체 소스 ZIP](../benchmarks/reports/validation/2026-10-28-phase3-llgl-vulkan/source.zip)의 fingerprint·실제 3개 출력·2개 거부·입력 분류·수용 경계를 확인한다. 자체 소스 ZIP은 검토용 snapshot이며 외부 dependency checkout과 Windows 실행 환경을 대신하지 않는다. offline 재검사는 GPU를 실행하거나 외부 소스를 다시 획득하지 않으며, metadata 해시는 신뢰 서명이 아니다.

새 회귀 테스트 18개와 전체 Python 테스트 926개가 통과했다. 고정된 저장 결과 재검사와 변조 거부도 포함하며 Windows/Linux CI에서 다시 확인한다. 결과 원본은 [summary.json](../benchmarks/reports/validation/2026-10-28-phase3-llgl-vulkan/summary.json)에 연결한다.

로드맵에서는 `P3-CMP-02`만 완료한다. 다음은 **`P3-CMP-03` LLGL 업로드 correctness adapter**다. Linux 물리 GPU·수동 화면 수용, SDL draw GPU timestamp, 나머지 경쟁 SDK·최종 gate의 상태와 요구는 유지한다. 메모리 증가/Intel 충돌 조사는 재개하지 않았다.
