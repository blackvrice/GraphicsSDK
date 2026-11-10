# P7-PKG-14 공통 배포 후보 무결성 검사

네 개의 후보 매니페스트와 그 SHA-256을 넘겨 Windows D3D12·Vulkan,
Linux Vulkan 헤드리스·X11/XCB, macOS Metal arm64·Intel의 설치 ZIP 여섯 개를
한 번에 검사한다. Linux는 각 프로필의 DWARF 분리 설치 ZIP과 디버그 ZIP을 쓴다.
[Windows 후보](PHASE7_WINDOWS_RELEASE_CANDIDATE.md) ·
[Linux DWARF 분리](PHASE7_LINUX_DEBUG_SYMBOLS.md) ·
[macOS 후보](PHASE7_MACOS_RELEASE_CANDIDATE.md)

- 전체 ZIP과 모든 멤버의 SHA-256을 대조한다. 공개 헤더, 설치 CMake export,
  정적 라이브러리, MIT LICENSE, Vulkan 후보의 제3자 고지까지 프로필별 허용 목록을 적용한다.
- CMake 패키지 버전과 공개 `gfx::version`이 매니페스트와 일치해야 한다.
  macOS는 각 Mach-O 객체의 아키텍처와 해시를 다시 확인한다.
  Linux 디버그 ZIP의 아카이브는 같은 프로필의 원본 아카이브 해시와 대조한다.
- 성공하면 JSON에 프로필별 소스 커밋과 SDK 지문, 매니페스트 해시, ZIP과 고지 해시,
  기존 실행 기록의 범위를 출력한다. 여러 소스 커밋을 담은 후보를 함께 검사할 수 있다.
  이번 입력은 모두 `0.0.1`이며 세 소스 커밋에서 생성됐다.

## 재현

저장소 루트에서 실행한다. 아래 SHA-256은 이번에 검증한 매니페스트의 값이다.
다른 후보를 검사할 때는 제작자가 별도로 전달한 매니페스트 해시를 사용한다.
ZIP을 푸는 과정이나 플랫폼별 컴파일러는 필요 없다.

```powershell
$candidateArgs = @(
  '--windows', 'out/phase7-windows-release',
  '0b5d1474c9cdeb2de6b94647f032db2999f0039c701b09316c2341a39d4856b1',
  '--linux-headless', 'out/phase7-linux-dwarf/headless/split',
  '964ccb14d6747ee99d713535b74253d6bd5c076e5121cf31fd5055f74c2c1747',
  '--linux-xcb', 'out/phase7-linux-dwarf/xcb/split',
  '056486d56c1e5666619f6e30f23d1ae43c30353791c7b6e4dc3cefb8932e8ce3',
  '--macos', 'out/phase7-macos-release-42750332817',
  '7ef9527ef1927ef6aaa54c7c37d79ee272bf4d4af72a0a87c7fced773b94e9f3'
)
python tools/release_candidate_audit.py @candidateArgs
python -m unittest discover -s tests -p 'test_release_candidate_audit.py'
```

Windows와 네트워크를 끈 Linux Docker에서 여섯 설치 ZIP과 두 디버그 ZIP을
검사해 같은 결과를 얻었다. Windows에서 독립 실행 두 번의 JSON 바이트도
일치했다. 원본 stdout의 SHA-256은
`f5001c69dc19b2563b044254455bf0801dd107e1887c1d211acba99255eeed9a`다.
잘못된 매니페스트 해시, XCB 프로필을 헤드리스로 지정한 입력, 디버그 ZIP
누락·교체, 추가 파일 및 변경한 설치 ZIP은 모두 거부됐다.

검사 범위는 이미 만든 후보의 바이트와 기록된 증거의 일관성이다. 원본
컴파일·링크 입력과 실행 재생은 각 플랫폼의 감사 절차를 사용한다.
최종 배포 프로필과 실제 장비에서 얻은 수용 근거를 연결하는 작업은
`P7-PKG-05`에 남아 있다.
