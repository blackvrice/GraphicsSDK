# Phase 3 — Linux Vulkan 전용 build preset

2026-10-25 · 작업 `P3-LNX-02` · 기준 커밋 `8172b4f`.

`CMakePresets.json`에 Linux Vulkan configure/build/test preset 두 조합을 추가했다. 기존 Windows·portable-core preset은 그대로 유지했다. **preset 정의와 Windows에서 수행한 CMake 설정 검사를 완료했으며, 실제 Linux configure·SDK compile·GPU 실행은 아직 수행하지 않았다.** [환경 조사](PHASE3_LINUX_ENVIRONMENT.md)의 WSL 미설치 관측은 그대로 유효하다. Linux native configure/compile 성공은 `P3-LNX-03`의 별도 완료 조건이다.

SDK `include/`, `src/`, `CMakeLists.txt`는 바꾸지 않았고 제품 fingerprint는 `2e511b8e53dbef4bbb2bf3d86ab4b19cf03e8765111de2b3dad4fe45fa551401`로 유지된다. 이번 preset 파일 변경은 그 SDK fingerprint의 입력 범위 밖이므로 [preset 자체의 별도 해시](../benchmarks/reports/2026-10-25-phase3-linux-presets/summary.json)를 기록했다.

## 구성과 격리 경로

| 항목 | `linux-vulkan` | `linux-vulkan-validation` |
|---|---|---|
| Host / generator | Linux / Ninja | 동일, 기본 preset 상속 |
| Backend | D3D12 OFF / Vulkan ON | 동일 |
| Build type | Release, build jobs 1 | 동일 |
| 테스트 / 예제 | 테스트 ON / 예제 OFF | 동일 |
| Required validation tests | OFF | ON |
| Build root | `out/linux-vulkan` | `out/linux-vulkan-validation` |
| Install root | `out/linux-vulkan-install` | `out/linux-vulkan-validation-install` |
| Headers 기본 경로 | `${sourceDir}/.deps/Vulkan-Headers` | 동일 |
| CTest | jobs 1, 실패 출력, 테스트 0개면 error | 동일 |

Linux host 조건은 **configure·build·test 각각**에 지정했고 validation preset이 상속한다. configure 조건만 지정하면 다른 종류의 preset 호출이 차단되지 않으므로 별도로 보호한다. `${presetName}`은 각 configure preset의 build/install 경로를 분리한다. 사용한 host 조건·상속·macro·noTestsAction 형식은 [CMake 3.25 preset 문서](https://cmake.org/cmake/help/v3.25/manual/cmake-presets.7.html)를 따른다.

`CMAKE_SYSTEM_NAME=Linux`로 Windows를 Linux처럼 표시하지 않고, compiler·ICD·validation runtime을 자동 설치/다운로드하지 않는다. `portable-core`는 기존 Vulkan OFF 기본 구성으로 별개다. 새 preset이 기존 Windows/portable 빌드나 보존된 SDK 설치 폴더를 재사용하지 않는다.

## 실제 Linux에서 필요한 전제

- CMake **3.25 이상**, Ninja와 C++23 `std::expected`를 지원하는 실제 compiler/표준 라이브러리. 기존 CI의 g++-14는 compiler 선택 예시이며 이번 Linux 실행 증거가 아니다.
- 검토된 Vulkan-Headers `v1.3.290`, commit `b379292b2ab6df5771ba9870d53cf8b2c9295daf`의 변경 없는 checkout. 경로는 `include/vulkan/vulkan_core.h`가 들어 있는 **checkout root**다. 다른 헤더/변경 사항은 새 compiler-input·license 감사가 필요하다.
- SDK configure/compile과 실제 실행을 구분한다. 코드가 native Linux에서 링크할 `${CMAKE_DL_LIBS}`와 `libvulkan.so.1` loader 경로는 현재 구현에 존재하지만 실제 build 결과는 미검증이다.
- 실제 GPU 테스트에는 Vulkan 1.1 loader/ICD·graphics queue·해당 기능을 제공하는 GPU가 필요하다. 설치된 manifest 파일만으로 런타임 성공 처리하지 않는다.
- validation 구성 실행에는 Linux용 Khronos validation layer와 debug-utils/validation-features/layer-settings 지원이 필요하다. 해당 기능이 없을 때 테스트를 skip하거나 성공으로 바꾸지 않는다. Linux의 실제 layer 로딩·오류 controls는 `P3-LNX-11`에서 별도로 수용한다.

기본 헤더 경로와 고정 commit 확인 예시다. 아래 Linux 명령들은 이번에 실행한 것이 아니다.

```sh
git -C .deps/Vulkan-Headers rev-parse HEAD
git -C .deps/Vulkan-Headers status --porcelain
```

## 재현 명령

다음 명령은 실제 Linux shell에서 저장소 root를 작업 디렉터리로 사용한다. compiler 선택은 첫 configure 때 고정하고 다른 compiler의 기존 cache를 혼용하지 않는다. 기본 경로에 위 headers checkout이 준비됐다는 전제다.

```sh
CXX=g++-14 cmake --preset linux-vulkan
cmake --build --preset linux-vulkan
ctest --preset linux-vulkan
cmake --install out/linux-vulkan

CXX=g++-14 cmake --preset linux-vulkan-validation
cmake --build --preset linux-vulkan-validation
ctest --preset linux-vulkan-validation
cmake --install out/linux-vulkan-validation
```

외부 headers checkout을 사용할 때는 **각 configure에** 같은 root를 명시한다. 첫 build root의 CLI override가 다른 root로 자동 전파되지는 않는다.

```sh
CXX=g++-14 cmake --preset linux-vulkan -DGFX_VULKAN_HEADERS=/path/to/Vulkan-Headers
CXX=g++-14 cmake --preset linux-vulkan-validation -DGFX_VULKAN_HEADERS=/path/to/Vulkan-Headers
```

필요한 layer가 표준 검색 경로 밖에 있다면 실제 Linux library/manifest 경로와 loader 환경을 해당 실행 receipt에 기록한다. Windows validation DLL 경로를 사용하지 않는다. preset 자체는 사용자 loader 설정을 지우거나 validation ERROR 0을 수용하지 않는다. noTestsAction은 **0개 등록 테스트의 실패**이며 모든 GPU 테스트가 실행됐다는 증거는 아니다.

설치 경로 격리는 package consumer 성공을 뜻하지 않는다. 설치된 패키지만 사용하는 Linux consumer·입력 감사는 `P3-LNX-12`, Vulkan ON CI는 `P3-LNX-13`, Linux WSI는 별도 `P3-WSI-*` 작업에 남아 있다.

## 이번 검증과 한계

Windows의 **CMake 4.3.1**과 별도 Windows Ninja로 다음 14개 명령 검사를 수행했다. 최소 버전 3.25에서 직접 실행한 것은 아니다.

| 검사 | 결과·의미 |
|---|---|
| 실제 저장소 `cmake --list-presets=all` | JSON/schema 파싱 성공. Windows에서는 Linux configure/build/test preset 모두 비활성화 |
| 두 preset × configure/build/test 직접 호출 | 6회 모두 disabled 진단으로 nonzero. 실제 `out/linux-vulkan*` SDK build/install root를 만들지 않음 |
| 격리 fixture의 두 configure | `project(PresetContractFixture NONE)`에서 정상 구성. backend/Release/test/validation flag·headers 기본 경로와 분리된 build/install macro 확인 |
| 격리 fixture의 외부 headers override | CLI root가 cache에 반영됨. 실제 SDK 헤더 검사·native compiler 실행을 뜻하지 않음 |
| 격리 fixture의 두 CTest 호출 | 실제 테스트를 등록하지 않아 2회 모두 `No tests were found`로 nonzero. 빈 테스트 성공을 막는 설정 확인 |

fixture는 `out/linux-vulkan-preset-contracts/fixture`에 두었고 **복사본의 세 host 조건만 true로 변경**했다. 실제 저장소의 조건은 Linux 그대로이며, fixture에는 SDK 소스·C++ language·GPU 테스트를 넣지 않았다. fake Linux target이나 실제 Linux build 결과로 취급하지 않는다.

첫 조사에서는 configure 조건만 있으면 build/test 목록·호출까지 차단된다고 잘못 가정해 실패했다. 각 조건을 추가했다. 중간 시도에서는 첫 CTest 확인이 만든 작은 진단 폴더와 CMake의 configure/build별 오류 문구 차이를 검증 도구에서 처리했다. [pilot 목록](../benchmarks/reports/validation/2026-10-25-phase3-linux-presets/pilot-v1/repository-list.stdout)과 관련 원본은 별도 보존하고, 최종 성공 결과와 구분했다.

[최종 명령·exit code](../benchmarks/reports/validation/2026-10-25-phase3-linux-presets/run/commands.json), [검증 runner](../benchmarks/reports/validation/2026-10-25-phase3-linux-presets/run.py), [요약·해시](../benchmarks/reports/2026-10-25-phase3-linux-presets/summary.json)에 근거를 연결한다. native Linux configure·컴파일·GPU·설치 소비자·실제 validation·Linux CI는 미검증으로 유지한다. 보류된 장시간 메모리·Intel AV 조사는 변경하지 않았다.
