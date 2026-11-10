# P7-PKG-11 Linux SDK 디버그 심볼 분리

Ubuntu 24.04 x86_64 · GNU C++ 14의 Vulkan 헤드리스와 X11/XCB Release
후보에 같은 심볼 정책을 적용한다. `linux_package.py --debug-info`는
Release 최적화와 `NDEBUG`를 적용하면서 DWARF를 생성한다.
`-ffile-prefix-map`으로 SDK 소스 루트를 상대 경로로 바꾼다.
입력 감사에서 기록한 정적 라이브러리와 객체별 SHA-256을 분리의 출발점으로
사용한다.

## 배포 파일과 사용 방법

| 파일 | 역할 |
| --- | --- |
| `GraphicsSDK-<version>-<profile>-<commit>-split-<runtime-hash>-<debug-hash>.zip` | 일반 설치 ZIP. 감사된 공개 헤더·CMake export·고지를 포함하고 `lib/libGraphicsSDKCore.a`만 GNU `objcopy --strip-debug` 결과로 교체한다. |
| 같은 이름의 `.debug.zip` | 감사된 원본 `lib/libGraphicsSDKCore.a` 한 파일. DWARF와 링크에 필요한 객체를 함께 포함하는 정적 라이브러리다. |
| `manifest.json` | 감사 receipt, 원본과 배포 ZIP·객체의 해시, 전역 링크 심볼 집계 해시, 분리 및 디버그 재링크 검증 결과. |

정적 SDK의 소스 줄 디버깅은 일반 ZIP을 별도 경로에 푼 뒤 같은 이름의
`.debug.zip`으로 그 경로의 아카이브를 교체하고 소비자를 다시 링크하는
방식이다. 디버그 ZIP은 원본 객체 전체를 포함하므로 디버그 정보 외 코드와
데이터도 함께 보관한다.

분리 도구는 각 원본 객체의 `.debug_info`와 `.debug_line`, 배포 객체의
디버그 섹션 제거, 객체 순서와 전역 링크 심볼 일치를 확인한다.
일반 ZIP의 설치본과 두 ZIP을 조합한 설치본에서 각각 독립 소비자를
링크하고 실행한다. 디버그 재링크의 ELF에서 `Backend.cpp`와 `Vulkan.cpp`의
소스 줄 정보를 읽을 수 있는지 확인한다. 검증 대상 런타임은 lavapipe와
XCB 프로필의 Xvfb다.

## 결과

네트워크를 차단한 Ubuntu 24.04 Docker에서 소스 커밋 `cf52edc79ca0`을
기준으로 두 Release SDK를 감사했다. 프로필마다 원본 아카이브 객체 6개와
외부 설치 소비자 9개를 확인했다.

| 프로필 | DWARF 포함 아카이브 | 일반 설치 아카이브 | 일반 ZIP | 디버그 ZIP |
| --- | ---: | ---: | ---: | ---: |
| 헤드리스 | 15,359,200 B | 567,592 B | 194,538 B | 3,189,577 B |
| X11/XCB | 16,803,174 B | 626,518 B | 214,392 B | 3,486,561 B |

분리 전후 전역 링크 심볼은 동일했다. 각 프로필의 일반 설치와 디버그
재링크에서 전송 24회의 28,521,273바이트가 일치했다. XCB의 두 링크 방식
모두 present 3회, 리사이즈 1회와 전체 픽셀 3,836바이트를 확인했다.
일반 소비자 ELF에서는 DWARF 섹션이 제거됐고 디버그 재링크에서는 SDK
두 파일의 소스 줄을 읽었다.

프로필마다 동일한 감사 입력으로 두 번 만든 일반 ZIP, 디버그 ZIP 및
manifest가 바이트 단위로 일치했다. Windows에서도 두 후보의 파일과
해시를 검증했다.

## 재현

프로필별로 깨끗한 Linux 체크아웃을 사용한다. 고정 Vulkan-Headers, GNU
C++ 14, CMake, Ninja, GNU binutils와 lavapipe가 필요하다. XCB 환경은
[X11/XCB 후보 절차](PHASE7_LINUX_XCB_RELEASE_CANDIDATE.md)를 따른다.

```sh
# 헤드리스
python3 tools/linux_package.py --debug-info
profile=headless

# X11/XCB는 별도 체크아웃에서 위 두 줄 대신 실행
# python3 tools/linux_package.py --xcb --debug-info
# profile=xcb

pin=$(sha256sum out/linux-package-evidence/receipt.json | cut -d ' ' -f 1)
python3 tools/linux_debug_symbols.py pack --profile "$profile" \
  --audit out/linux-package-evidence --audit-sha256 "$pin" \
  --output out/linux-debug-split
python3 tools/linux_debug_symbols.py check --profile "$profile" \
  --audit out/linux-package-evidence --audit-sha256 "$pin" \
  --output out/linux-debug-split
```

다른 OS에서 `check`로 파일을 검사할 때는 독립적으로 확인한 감사 receipt
SHA-256을 사용한다. ELF 섹션 검사와 소비자 실행은 Linux의 `pack`에서 한다.
`P7-PKG-03`의 최종 Linux 수용은 배포 지원표와 실제 환경의 필수 근거를
연결해 별도로 판단한다.
