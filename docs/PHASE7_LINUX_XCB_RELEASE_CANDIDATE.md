# P7-PKG-10 Linux Vulkan X11/XCB 배포 후보

Ubuntu 24.04 x86_64 · GNU C++ 14 · Release에 별도의 Vulkan X11/XCB
프로필을 추가했다. `linux_package.py --xcb`는 `GFX_VULKAN_XCB=ON`으로
SDK를 새로 빌드하고, 실제 GNU 컴파일 입력과 정적 아카이브 멤버를 확인한다.
Vulkan-Headers의 XCB 헤더는 검토한 외부 입력과 고지에, `xcb/xcb.h`는
배포판 소유자가 확인된 시스템 입력에 들어간다.
[기존 XCB 감사 근거](PHASE3_LINUX_INPUT_AUDIT_REFRESH.md)

`linux_release_candidate.py --profile xcb`는 지정한 receipt의 해시와
XCB ON 설정, 두 XCB 입력, ZIP 바이트를 확인한 뒤 별도의 버전 이름을
만든다. ZIP, 전역 링크 심볼 목록, manifest의 형식은
[헤드리스 후보](PHASE7_LINUX_RELEASE_CANDIDATE.md)와 같다.

## 재배치 설치 검증

- ZIP을 푼 임시 경로에서 `find_package(... EXACT ... NO_DEFAULT_PATH)`로
  공개 헤더와 Core 아카이브를 선택한다. CPU 소비자와 Vulkan 전송
  소비자를 새로 빌드하고 lavapipe에서 전체 버퍼를 비교한다.
- 별도의 XCB 소비자는 Xvfb에서 창을 열고 17×9와 31×13 크기의
  스왑체인을 검증한다. 서로 다른 세 프레임의 모든 유효 픽셀을 비교하고
  present 3회, 리사이즈 1회와 최종 자원 회수를 확인한다.
- Core의 CMake export와 전송 소비자의 직접 링크 의존성을 확인한다.
  창을 만든 XCB 소비자가 앱에서 `libxcb.so.1`을 연결한다.
  Vulkan loader, ICD, libxcb와 X11 서버는 시스템 런타임 전제다.

## 결과

네트워크를 차단한 Ubuntu 24.04 Docker에서 소스 커밋 `a060d4a2aeff`와
고정 Vulkan-Headers로 감사했다. 기본 감사의 독립 설치 소비자 9개와
Vulkan 검사 2,670개를 통과했다. ZIP의 일반 파일 20개와 ELF64 x86_64
객체 6개의 해시를 검증했다.

후보 ZIP으로 새로 빌드한 전송 소비자는 24회 전송의 28,521,273바이트를
비교했다. XCB 소비자는 3회 present, 1회 리사이즈와 전체 3,836바이트
참조를 통과했다. 전송 소비자의 직접 링크 목록에는 시스템 C/C++ 런타임이
있었고, 표시 소비자의 직접 링크 목록에는 `libxcb.so.1`이 추가됐다.
두 결과는 validation OFF의 software 검사다.

같은 감사 입력으로 다시 만든 ZIP, 심볼 목록과 manifest가 각각 바이트
단위로 일치했다. Windows에서 무결성 검사와 기존 헤드리스 후보 재검사를
통과했고, 프로필·아카이브·표시 결과 거부를 포함한 후보 테스트 8개가
Windows와 Linux에서 통과했다.

```text
Audit receipt SHA-256  b562eeb6ad78cc3d97fd7a7fc097b040d0aeb56dac75462e09846a34eecdc9d8
Candidate ZIP SHA-256  bc674eeef3a459771d191cd357f06c71c4b16c58898bc57704ec701d41c46928
```

## 재현

고정 Vulkan-Headers를 준비한 깨끗한 Ubuntu 체크아웃에서 실행한다.
GNU C++ 14, CMake, Ninja, GNU binutils, pkg-config, libxcb1-dev,
Vulkan loader와 lavapipe, Xvfb와 xauth가 필요하다.

```sh
python3 tools/linux_package.py --xcb
pin=$(sha256sum out/linux-package-evidence/receipt.json | cut -d ' ' -f 1)
python3 tools/linux_release_candidate.py pack --profile xcb \
  --audit out/linux-package-evidence --audit-sha256 "$pin" \
  --output out/linux-xcb-release-candidate
python3 tools/linux_release_candidate.py check --profile xcb \
  --audit out/linux-package-evidence --audit-sha256 "$pin" \
  --output out/linux-xcb-release-candidate
```

보관 자료를 다른 환경에서 검사할 때는 독립적으로 확인한 receipt SHA-256을
사용한다. 후보는 Xvfb/lavapipe에서 검증한다. 실제 X11 표시와 사용자 조작은
`P3-WSI-08`, 디버그 심볼 배포 정책과 최종 Linux 수용은
`P7-PKG-03`의 후속 범위다.
