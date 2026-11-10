# P7-PKG-08 초기 배포 계약 초안

현재 CMake와 공개 헤더의 버전은 `0.0.1`이다. 이 문서는 그 개발
스냅샷에서 확인한 설치 경계와 초기 배포 후보의 수용 기준을 정의한다.
최종 지원표는 Shader/PSO 수용(`P5-SHD-08`) 뒤 `P7-PKG-02`에서 확정한다.
아래 도구 버전은 관측한 조합이며 최소 지원 버전은 해당 범위의 검증 후 결정한다.

## 패키지 후보와 근거

| 후보 | 관측한 빌드 | 확인한 실행 범위와 수용 조건 |
| --- | --- | --- |
| Windows x64 · D3D12 | Visual Studio 2022 17.14, MSVC 19.44, Windows SDK 10.0.26100.0, Release 정적 라이브러리 | 기존 설치 소비자와 NVIDIA 기본 표시의 사용자 확인. WARP는 software 확인으로 구분한다. 릴리스 대상 GPU·드라이버와 수용 범위는 최종 검증 기록에 명시한다. [빌드](BUILD.md) · [사용자 표시](PHASE2_MANUAL_PRESENTATION_REPORT.md) |
| Windows x64 · Vulkan | 같은 Windows 도구 구성, Vulkan opt-in, Release 정적 라이브러리 | 현재 정책의 GTX 1660 SUPER와 Intel UHD 770 offscreen, 해당 NVIDIA 조합의 Win32 출력 경로. 해당 Intel 드라이버의 Vulkan 출력은 미지원으로 분류한다. [GPU·드라이버·출력 정책](PHASE3_WINDOWS_SUPPORT.md) |
| Linux x86_64 · Vulkan headless | Ubuntu 24.04, GNU C++ 14.2/Ninja, Release, Vulkan ON·XCB OFF | 재배치 패키지, 외부 소비자와 lavapipe의 전체 참조. 물리 GPU와 ICD·드라이버·validation은 `P3-LNX-15` 실행 근거가 필요하다. [설치·입력 감사](PHASE3_LINUX_INPUT_AUDIT_REFRESH.md) |
| Linux x86_64 · Vulkan X11/XCB | 동일 계열의 GNU 빌드에서 XCB ON, 별도 설정 | Xvfb/lavapipe에서 표시 회귀와 ON/OFF 입력 감사. 실제 X11 표시와 사람의 창 조작은 `P3-WSI-08` 수용 조건이다. [XCB 범위](PHASE3_LINUX_WSI_SCOPE.md) · [검증](PHASE3_LINUX_XCB_MATRIX_REPORT.md) |
| macOS arm64 · Metal | Xcode 16.4, Apple clang 17, macOS SDK 15.5, Release, arm64 정적 라이브러리 | GitHub 호스팅 macOS에서 재배치한 ZIP으로 설치 소비자와 가상 Metal 장치의 전체 이미지 검증. 물리 Mac 표시·성능은 별도 수용이다. [툴체인](PHASE4_MACOS_CORE_REPORT.md) · [설치](PHASE4_METAL_PACKAGE_REPORT.md) |
| macOS x86_64 · Metal | 같은 Xcode 계열의 Intel 전용 Release 정적 라이브러리 | Intel 호스팅 환경의 재배치 소비자와 가상 Metal 실행. arm64와 각각의 단일 아키텍처 아카이브·실행 근거를 보존한다. [설치](PHASE4_METAL_PACKAGE_REPORT.md) |

표의 backend를 각 빌드에서 명시적으로 선택하고, Windows D3D12와 Vulkan 및
Linux headless와 XCB는 서로 다른 프로필로 기록한다. 통합 backend 패키지,
Windows ARM64·x86, Linux ARM64, macOS universal, Debug 배포, 다른 배포판이나
툴체인은 개별 후보 등록과 검증이 필요한 범위다. Native Wayland와 XWayland는
초기 XCB 지원 범위 밖이다. 모든 backend를 끈 Core는 CPU 검증용 구성이다.

## 설치와 소비자 호환

- 공개 진입점은 C++23의 `<graphics/Graphics.hpp>`, `gfx` namespace,
  CMake 3.25 이상의 `GraphicsSDK::Core`다. 외부 소비자는 설치 헤더와
  export가 참조하는 정적 아카이브를 같은 패키지에서 사용한다.
  공개 동작은 [API 문서](PUBLIC_API.md)와 backend별 지원 표를 따른다.
- 설치 CMake 버전 파일은 `ExactVersion`으로 생성한다. 재현 빌드는
  `find_package(GraphicsSDK 0.0.1 EXACT CONFIG REQUIRED)`를 사용한다.
  이 검사는 버전을 선택한다. 같은 버전이라도 OS·아키텍처·컴파일러/표준
  라이브러리·CRT·빌드 설정과 backend 옵션은 별도 패키지 식별 필드다.
  ABI 범위는 같은 승인 프로필의 헤더와 아카이브로 소비자를 재빌드하는
  경우에 한정한다. 버전 간 바이너리 교체와 툴체인 교차 링크는 별도 수용이다.
  공개 API·ABI 변경은 버전별 릴리스 노트에 기록하고 소비자 재빌드 요건을
  명시한다. 이미 배포한 프로필의 바이트를 변경할 때에는 새 배포 식별자를 사용한다.
- `gfx::version`은 공개 헤더의 버전이다. `gfx::buildFingerprint()`는
  현재 구현이 해시하는 SDK 소스 입력을 식별한다. 프로필과 빌드 산출물을
  구분하려면 패키지·아카이브 SHA-256, 소스 커밋과 빌드 옵션을 함께 기록한다.
  `gfx::backendCompiled()`와 실제 장치 선택·capability 결과는 구분한다.
  DXIL·SPIR-V·MSL 경로와 외부 도구 요건은
  [셰이더 계약](SHADER_PIPELINE.md)에 연결한다.
- 배포 소비자는 이전 설치 경로와 별도 디렉터리에서 빌드하고, CI는
  `NO_DEFAULT_PATH` 등으로 재배치한 패키지의 실제 export·아카이브 사용을
  확인한다. CMake 검색 성공, 빌드 성공과 런타임 성공은 각각 기록한다.

## 파일과 수용 기준

| 항목 | 확인 기준 |
| --- | --- |
| 설치 payload | 공개 `include/graphics/*.hpp`, `GraphicsSDKCore.lib` 또는 `libGraphicsSDKCore.a`, `GraphicsSDKConfig*.cmake`와 `GraphicsSDKTargets*.cmake`, MIT `LICENSE`를 기본 목록으로 삼는다. 프로필의 실제 입력에 필요한 제3자 고지를 패키징 단계에서 추가하고 정확한 멤버와 SHA-256을 검사한다. 별도 심벌 산출물은 플랫폼 payload 작업에서 확정한다. |
| 런타임 전제 | Windows 시스템 라이브러리, Vulkan loader·ICD와 선택 validation 계층, XCB 프로필의 X11 연결 및 libxcb, macOS 시스템 프레임워크를 프로필별로 기록한다. SDK ZIP의 허용 목록과 실제 컴파일·링크 의존성의 분류는 [배포 감사](DEPENDENCY_LICENSES.md)를 따른다. |
| 출처와 무결성 | 패키지마다 버전·OS·아키텍처·컴파일러/SDK·구성·backend·소스 커밋·SDK 지문·파일 해시를 연결한다. 컴파일·링크 입력과 고지 원문을 보존하고 누락·추가·변조 파일을 거부하는 대조군을 실행한다. |
| 설치 소비자 | 깨끗한 prefix로 풀어 정확한 버전의 패키지를 선택하고 설치 헤더와 아카이브만으로 컴파일·링크한다. 요청 버전 불일치와 지정한 설치본 누락의 거부를 별도로 검사한다. |
| 실행 근거 | CPU 계약, software/가상 장치, 물리 GPU·표시·수동 조작, 성능 결과를 분리한다. 성공/실패/미실행과 실제 장치·드라이버·validation을 기록한다. 필수 구성의 실제 실행을 릴리스 수용표에 연결한다. |
| 릴리스 | `P7-PKG-03`·`04`에서 OS별 payload, `05`에서 공통 감사, `06`에서 CI·smoke, `07`에서 버전별 아티팩트와 릴리스 문서를 수용한다. 예제·테스트·벤치마크 실행 입력과 API 문서를 버전이 일치하는 소스와 연결한다. |

## 독립 소비자 검사

저장소 밖의 프로젝트로 기존 [D3D12 소비자](../tests/consumer/main.cpp) 또는
[Vulkan 전송 소비자](../tests/VulkanTests.cpp)를 `main.cpp`로 복사해
사용한다. SDK 설치본을 새 위치에 풀고 다음 프로젝트를 구성한다.

```cmake
cmake_minimum_required(VERSION 3.25)
project(ReleaseContractConsumer LANGUAGES CXX)
find_package(GraphicsSDK ${REQUESTED_VERSION} EXACT CONFIG REQUIRED
  PATHS "${PACKAGE_PREFIX}" NO_DEFAULT_PATH)
get_target_property(archive GraphicsSDK::Core IMPORTED_LOCATION_RELEASE)
cmake_path(IS_PREFIX PACKAGE_PREFIX "${archive}" NORMALIZE relocated)
if(NOT relocated OR NOT EXISTS "${archive}")
  message(FATAL_ERROR "Imported archive must be inside the relocated package")
endif()
add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE GraphicsSDK::Core)
```

Release에서 `REQUESTED_VERSION=0.0.1`과 실제 재배치 `PACKAGE_PREFIX`를
지정해 빌드·실행한다. 거부 검사는 각각 새 빌드 경로에서 `0.0.0`, `0.0.2`,
존재하지 않는 prefix를 요청한다. 마지막 검사는 기존 설치본을
`CMAKE_PREFIX_PATH`에 둔 상태에서도 지정된 패키지 누락이 구성 실패로
보고되는지 확인한다.

### 검증 결과

2026-11-10 · SDK 소스 기준 `f7a34eb`.

| 범위 | 결과 |
| --- | --- |
| Windows D3D12 · 새 Release 설치 | CMake 기본 설치 파일 19개를 공백이 있는 외부 경로에 재배치했다. `0.0.1 EXACT` 소비자가 GTX 1660 SUPER에서 실행됐으며 64MiB 자원 보유·반환과 Depth/MRT 1,024픽셀을 확인했다. |
| Windows Vulkan · 새 Release 설치 | 같은 방식으로 기본 설치 파일 19개를 재배치했다. `0.0.1 EXACT` 소비자가 GTX 1660 SUPER와 Intel UHD 770에서 717개 검사, 48개 전송과 전체 57,042,546바이트 비교를 통과했다. |
| Windows 거부 대조군 | 두 설치본 모두 `0.0.0`, `0.0.2`와 존재하지 않는 지정 경로를 거부했다. 지정 경로 누락 시 이전 prefix로 검색을 이어가지 않았다. |
| Linux · 보관 증거 | 복원된 설치 receipt와 검증용 ZIP의 SHA-256을 대조했다. ZIP 20개 멤버의 경로·크기·해시와 lavapipe 검사 2,670개의 보관 기록이 일치했다. [Linux 입력 감사](PHASE3_LINUX_INPUT_AUDIT_REFRESH.md) |
| macOS · 보관 증거 | arm64와 x86_64의 패키지 재배치·설치 헤더와 링크 입력·Metal 전체 이미지 증거를 재생했다. 패키지 감사 Python 테스트 17개도 통과했다. [Metal 패키지](PHASE4_METAL_PACKAGE_REPORT.md) |

Windows 실행 범위는 설치 소비자 smoke다. Linux와 macOS는 기존 실행의
패키지 증거를 재생했다. 새 릴리스 아티팩트의 공통 감사와 실물 플랫폼 수용은
각 후속 작업에서 기록한다.
