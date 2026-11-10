# P7-PKG-13 Windows x64 배포 후보

D3D12와 Vulkan을 각각 한 개만 켠 Release SDK를 만든다. 두 프로필은
Visual Studio 2022 x64의 MSVC와 동적 Release CRT(`/MD`)를 사용한다.
D3D12 후보는 기본 DXBC 구성으로 빌드한다. Vulkan 후보는 고정된
Vulkan-Headers의 실제 Win32 컴파일 입력 12개를 기존 감사 정책과 대조하고
Apache-2.0 원문과 저작권 고지를 ZIP에 넣는다.

| 후보 | ZIP 내용 | 외부 실행 전제 |
| --- | --- | --- |
| Windows x64 · D3D12 | 정적 라이브러리, 공개 헤더 13개, CMake export 4개, MIT LICENSE | Windows D3D12·DXGI와 MSVC 런타임 |
| Windows x64 · Vulkan | 같은 구성과 Vulkan-Headers 고지 | Windows Vulkan loader·ICD와 MSVC 런타임 |

ZIP 이름은 버전, 플랫폼, backend, 빌드 설정, SDK 소스 커밋과 ZIP SHA-256의
앞 12자를 포함한다. `manifest.json`에 전체 해시, SDK 소스 지문, 도구 버전,
파일별 크기와 해시, 소비자 검사 결과를 기록한다. 빌드 작업과 MSBuild 추적은
인접한 `-work` 폴더에 저장한다.

각 ZIP을 공백이 포함된 별도 경로에 풀고, `find_package(GraphicsSDK … EXACT)`와
`GraphicsSDK::Core`로 독립 소비자를 빌드한다. MSBuild 기록에서 재배치한 헤더와
아카이브 사용을 확인하고, 소비자가 보고하는 버전, 소스 지문, 활성 backend가
프로필과 일치해야 통과한다. 존재하지 않는 버전과 지정 설치 경로 누락도 검사한다.
후자의 경우 기존 설치본을 `CMAKE_PREFIX_PATH`에 넣어도 설정이 실패해야 한다.

## 재현

Windows에서 Python 3.12 이상, CMake, Visual Studio 2022의 C++ 데스크톱
워크로드 및 Windows SDK를 사용한다. [Vulkan 빌드 안내](VULKAN.md)에 따라
`.deps/Vulkan-Headers`에 고정 커밋 `b379292b2ab6df5771ba9870d53cf8b2c9295daf`을
준비한다. SDK 소스와 설치 설정이 HEAD에 일치하는 상태에서 저장소 루트에서 실행한다.

```powershell
python tools/windows_release_candidate.py build --output out/windows-release
# 게시한 manifest_sha256 값을 별도로 보관하고 수신 측 검사에 사용한다.
$manifestSha256 = '빌드가 출력한 manifest_sha256 값'
python tools/windows_release_candidate.py check --output out/windows-release --manifest-sha256 $manifestSha256
python -m unittest discover -s tests -p 'test_*release_candidate.py'
```

두 후보 ZIP과 매니페스트를 함께 전달한다. `check`는 별도 경로에서
고정한 매니페스트 해시와 ZIP·멤버의 바이트를 확인한다. Windows SDK나
Vulkan 런타임 설치 없이도 이 무결성 검사를 실행할 수 있다.

## 검증 결과

SDK 버전 `0.0.1`, 소스 `7c5bf927ff5902e3ae975dd78c70882f2838e367`에서
CMake 4.3.1, MSVC 19.44.35228.0, Windows SDK 10.0.26100.0으로 검증했다.

| 후보 | 파일 수 | ZIP 크기 | ZIP SHA-256 |
| --- | ---: | ---: | --- |
| D3D12 | 19 | 675,633 B | `a750fc8e55e7ebfb06a453791acdab1be81c333aa711b52ed8ace1c609c1299b` |
| Vulkan | 20 | 864,273 B | `4ba116a87e0cae81025dea0db0b71472ddb8ad175066ab142b48caa62659cc13` |

두 소비자에서 재배치한 공개 헤더 13개와 정적 아카이브의 사용, 정확한 버전·백엔드,
버전 불일치 및 설치 경로 누락의 거부를 확인했다. 매니페스트 SHA-256은
`0b5d1474c9cdeb2de6b94647f032db2999f0039c701b09316c2341a39d4856b1`이다.

위 소비자는 CPU에서 버전·백엔드 계약을 검증한다. 같은 ZIP에서 실행한
오프스크린 렌더링·전송은 [Windows 후보 GPU 검증](PHASE7_WINDOWS_CANDIDATE_GPU.md)에
기록했다. 플랫폼별 최종 지원 구성은 `P7-PKG-02`, 공통 배포 감사 수용은
`P7-PKG-05`에 남아 있다.
