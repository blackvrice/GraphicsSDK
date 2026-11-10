# P7-PKG-09 Linux Vulkan 헤드리스 배포 후보

Ubuntu 24.04 x86_64 · GNU C++ 14 · Release의 Vulkan 헤드리스 설치 감사
결과를 배포 후보로 구성한다. `linux_release_candidate.py`는 지정한 audit
receipt의 SHA-256을 확인하고 컴파일러 기록, CMake 옵션, 설치 ZIP,
정적 라이브러리, 고지와 파일별 해시를 대조한다. 기존
[Linux 설치·입력 감사](PHASE3_LINUX_INPUT_AUDIT_REFRESH.md)를 입력으로 사용한다.

## 산출물

후보 이름에는 SDK 버전, 플랫폼·아키텍처·backend·빌드 설정, 소스 커밋
12자리, ZIP SHA-256 12자리를 넣는다. `manifest.json`에는 전체 식별자를
기록한다.

| 파일 | 내용 |
| --- | --- |
| `GraphicsSDK-<version>-linux-x86_64-vulkan-headless-release-<commit>-<zip-hash>.zip` | 감사된 설치 ZIP과 동일한 바이트. 공개 헤더, Core 정적 라이브러리, CMake export, MIT 라이선스와 Vulkan-Headers 고지를 포함한다. |
| 같은 이름의 `.symbols.txt` | GNU `nm`으로 읽은 전역 링크 심볼과 아카이브 멤버 목록. 소스 수준 디버그 심볼 패키지는 별도 범위다. |
| `manifest.json` | 전체 소스·감사·패키지 해시, 컴파일러·설정, ZIP 파일과 아카이브 객체별 해시, 링크 심볼 파일 해시, 재배치 소비자 결과. |

ZIP은 정확한 일반 파일 목록으로 검사한다. 경로 이탈, 중복·추가 파일,
symlink, 변경된 바이트는 거부한다. 소비자는 공백이 있는 임시 경로에 푼
ZIP에서 `find_package(... EXACT CONFIG ... NO_DEFAULT_PATH)`로 SDK를
선택한다. CMake export가 그 위치의 Release 아카이브를 가리키는지 확인하고
별도 프로젝트에서 CPU와 Vulkan 전송 소비자를 빌드한다.

## 검증 결과

감사 소스 `d9aeb4c32285`의 ZIP으로 네트워크를 차단한 로컬 Ubuntu 24.04
Docker에서 실행했다. 헤드리스 ZIP 20개 파일과 ELF64 x86_64 객체 6개의
해시를 대조했고, 전역 링크 심볼 목록 274줄을 만들었다. 외부 CPU 소비자와
lavapipe 전송 소비자는 통과했다. Vulkan 소비자는 **361개 검사, 24개 전송,
전체 28,521,273바이트**를 확인했다.

소비자 ELF의 직접 의존성은 `libc.so.6`, `libgcc_s.so.1`, `libm.so.6`,
`libstdc++.so.6`였다. Vulkan loader와 ICD는 별도 런타임 전제다.
동일 감사 입력으로 다시 만든 ZIP·심볼 목록·manifest는 각각 바이트가 일치했다.
Windows에서 무결성 검사를 실행했고 양 OS에서 거부 테스트 6개가 통과했다.

## 재현

보존한 Linux 감사 폴더 또는 깨끗한 Linux 체크아웃에서
`python3 tools/linux_package.py`로 만든 감사 폴더를 지정한다.
`<receipt-sha256>`은 독립적으로 확인한 원본 감사 receipt의 SHA-256이다.
패키지 작성에는 Ubuntu 계열의 GNU C++ 14, CMake, Ninja, GNU binutils와
lavapipe가 필요하다. `check`는 감사 입력과 출력 파일의 무결성 및
소비자 결과 형식을 검사한다. 실제 Linux 소비자 재실행은 `pack`으로 한다.

```sh
python3 tools/linux_release_candidate.py pack \
  --audit out/linux-package-evidence \
  --audit-sha256 "<receipt-sha256>" \
  --output out/linux-release-candidate

python3 tools/linux_release_candidate.py check \
  --audit out/linux-package-evidence \
  --audit-sha256 "<receipt-sha256>" \
  --output out/linux-release-candidate
```

X11/XCB 프로필은 [별도 배포 후보](PHASE7_LINUX_XCB_RELEASE_CANDIDATE.md)로
검증한다. `P7-PKG-03`은 디버그 심볼 배포 정책과 지원표 확정 후의 최종 Linux
payload 수용을 다룬다.
