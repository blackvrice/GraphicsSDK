# Phase 3 — 실제 Linux Vulkan 컴파일·CPU 계약 검사

2026-10-25 · `P3-LNX-03` / `P3-LNX-13` · 검증 커밋 `592a7b3`.

GitHub-hosted Ubuntu에서 **실제 Linux configure·GCC 컴파일·링크와 CPU 실행**을 완료했다. `linux-vulkan`과 `linux-vulkan-validation` 두 preset 모두 SDK static library 및 등록된 테스트 executable을 빌드했고, 각 11개 CPU 계약 검사를 skip 없이 통과했다. Windows의 `project(... NONE)` fixture와 구분되는 native Linux 결과다.

성공한 [전용 workflow 실행](https://github.com/blackvrice/GraphicsSDK/actions/runs/40161823073)과 [job](https://github.com/blackvrice/GraphicsSDK/actions/runs/40161823073/job/121074808016)을 근거로 삼는다. GPU·Khronos layer·WSI·설치 소비자는 실행하지 않았다. `validation` preset의 컴파일 성공을 validation layer 실행 성공으로 세지 않는다.

## 실제 환경과 결과

| 항목 | 관측 |
|---|---|
| 실행 환경 | GitHub-hosted Linux VM, Ubuntu 24.04.5 LTS, x86_64 |
| Kernel | `6.17.0-1022-azure` |
| Compiler | g++-14, Ubuntu `14.2.0-4ubuntu2~24.04.1` |
| CMake / Ninja | `3.31.6` / `1.13.2` — 실제 PATH에서 관측한 도구 |
| Vulkan-Headers | commit `b379292b2ab6df5771ba9870d53cf8b2c9295daf`, 변경 없는 checkout |
| Native source commit | `592a7b3cb6efed209da92865349aa26642c0be11` |

| Preset | Native configure/build | SDK compile units | Static library | CPU CTest | GPU/WSI/layer 실행 |
|---|---|---:|---:|---:|---|
| `linux-vulkan` | 성공 | 6 | 413,918 bytes | 11/11, skip 0 | 수행 안 함 |
| `linux-vulkan-validation` | 성공 | 6 | 413,918 bytes | 11/11, skip 0 | 수행 안 함 |

두 library의 SHA256은 `c6870603fd53a581c3ac6a4fcb70e6a58220d0ff0433f17f51f2e8a045e38b32`다. SDK 실행 배포물로 업로드한 것이 아니라 CI에서 실제 생성한 파일의 크기·해시를 receipt에 기록했다. 보존 artifact에는 configure/build/CPU 로그·cache·compile commands·JUnit·receipt가 들어 있다. [요약](../benchmarks/reports/2026-10-25-phase3-linux-compile/summary.json)과 [원래 native receipt](../benchmarks/reports/validation/2026-10-25-phase3-linux-compile/final-native/receipt.json)에 연결한다.

CPU 선택은 core/adapter/memory-budget/vertex-layout/binding-layout/render-pass 구조 계약, SPIR-V interface/vertex/binding, timestamp math, 생성 output ownership의 **11개 이름을 명시한 정규식**이다. 실제 Vulkan adapter/device를 생성하는 GPU suite는 선택하지 않았다. JUnit에서 테스트 이름·개수와 failure/error/skipped 부재를 대조했다.

## 구현과 재현

새 [.github/workflows/linux-vulkan-compile.yml](../.github/workflows/linux-vulkan-compile.yml)은 Ubuntu runner에서 source와 검토된 headers commit을 checkout하고 g++-14/Ninja를 준비한다. [tools/run_linux_compile.py](../tools/run_linux_compile.py)는 실제 host가 Linux인지 검사한 뒤 기존 두 preset을 그대로 configure/build하고 CPU만 실행한다. Windows 또는 가짜 `CMAKE_SYSTEM_NAME`으로 수용하지 않는다.

runner는 다음 항목을 확인한다.

- Git commit·CI SHA 일치, tracked source와 headers checkout의 무변경 상태.
- D3D12 OFF/Vulkan ON/Release/test ON 및 validation profile의 실제 CMake cache 값.
- 요청한 native compiler와 cache compiler 경로가 같은 실행 파일로 해석됨. cache 항목의 STRING/FILEPATH 타입은 고정하지 않음.
- 여섯 SDK compile command에 동일한 native source fingerprint가 들어 있음, 실제 static library 생성 및 CPU 11개 실행.
- 각 subprocess의 명령·exit code·원본 stdout/stderr와 진단 파일 해시. 실패한 실행도 로그를 보존.

실제 Linux의 새 Git checkout과 고정 headers, compiler/CMake/Ninja가 준비되면 다음을 사용한다. runner는 기존 build/evidence root를 덮어쓰지 않고 거부한다.

```sh
python3 tools/run_linux_compile.py --compiler g++-14
```

출력은 `out/linux-vulkan-compile-evidence`, 두 build root는 기존 preset의 격리 경로다. 시스템 compiler/package 준비는 workflow의 임시 Linux VM에서 수행했으며 local Windows의 WSL·드라이버·기능은 변경하지 않았다. GitHub runner 선택 방식은 [공식 문서](https://docs.github.com/en/actions/how-tos/write-workflows/choose-where-workflows-run/choose-the-runner-for-a-job)를 따른다.

## 플랫폼별 source identity

| 기준 | SDK fingerprint |
|---|---|
| 이번 Linux checkout의 실제 바이트 | `ca69fbe86eb5fb231503e070927322b95a80fbde10a1a951133f640b0dec2830` |
| 같은 커밋의 SDK 27개 Git blob 재계산 | 위 Linux 값과 일치 |
| 보존된 Windows 작업본/제품의 실제 바이트 | `2e511b8e53dbef4bbb2bf3d86ab4b19cf03e8765111de2b3dad4fe45fa551401` |

기존 fingerprint는 파일 원본 바이트를 hash하므로 LF/CRLF checkout 차이를 기록한다. 이번에 hash 알고리즘이나 SDK C++/`CMakeLists.txt`를 바꾸지 않았다. 실제 Linux compile commands·runner·CMake 입력을 검증 커밋의 Git blob과 대조했다. Windows의 과거 GPU 증거를 Linux 값에 재귀속하지 않는다. 후속 문서/근거 커밋에서도 이 SDK source set은 변경하지 않았다.

## 최초 실패와 보존

첫 [실행 40161563022](https://github.com/blackvrice/GraphicsSDK/actions/runs/40161563022)은 기본 preset의 native SDK build까지 성공한 뒤 runner가 `CMAKE_CXX_COMPILER:FILEPATH`만 기대해 중단됐다. compiler 경로를 CMake cache의 타입과 독립적으로 해석해 요청 compiler와 비교하도록 수정했다. SDK 이식성 오류를 수정한 것으로 주장하지 않는다. 두 preset 전체를 새 VM에서 다시 실행한 결과가 위 최종 성공이다.

첫 [artifact ZIP](../benchmarks/reports/validation/2026-10-25-phase3-linux-compile/first-attempt.zip)·[job log](../benchmarks/reports/validation/2026-10-25-phase3-linux-compile/first-job.log)와 최종 [artifact ZIP](../benchmarks/reports/validation/2026-10-25-phase3-linux-compile/final-artifact.zip)·[job log](../benchmarks/reports/validation/2026-10-25-phase3-linux-compile/final-job.log)을 별도 보존했다. 최종 ZIP의 SHA256 `41ed03f27d2e4314244da1bd09bf765d1c25de52374cbb9bd44838f9d72b9a69`는 GitHub artifact digest와 다운로드 바이트 양쪽에서 확인했다. 내부 진단 파일·receipt·Git 입력도 각각 검증했다.

## 남은 범위와 기존 CI 상태

`P3-LNX-03`의 native build 성공 기준과 `P3-LNX-13`의 Vulkan ON compile/CPU CI 기준을 충족했다. `P3-LNX-04`~`12`, Linux WSI, shader/toolchain 확대, 설치 입력·notice 감사·배포 수용은 미완료다. 이 CI VM에서 GPU/ICD/layer 가용성을 검사하지 않았고 hardware/software Vulkan 실행·물리 GPU 성능·장시간 메모리·실제 device loss를 수용하지 않는다.

기존 `Build and correctness` workflow 전체는 별도다. 변경 전 [실행 40161105666](https://github.com/blackvrice/GraphicsSDK/actions/runs/40161105666)은 portable native build·CTest 뒤 Python 614개 중 errors=92로 실패했다. [발췌](../benchmarks/reports/validation/2026-10-25-phase3-linux-compile/baseline-python-failures-excerpt.log)를 보존했다. 이번 native compile workflow의 성공이 기존 Python 증거 검사나 모든 CI의 정상화를 뜻하지 않는다. 전체 CI 증거 검사 정합성은 새 필수 잔여 `P3-LNX-14`로 기록하고 최종 회귀의 선행 조건에 연결한다. 보류된 메모리/Intel AV 조사는 변경하지 않았다.
