# P7-PKG-12 macOS Metal 배포 후보

| 프로필 | 검증한 호스팅 환경 | ZIP 크기 | ZIP SHA-256 |
| --- | --- | ---: | --- |
| arm64 | macOS 15.8.2 (24H232), Apple Silicon | 119,550 B | `3b53914e04f24de2629244e9d50c8713eb00d04dd29a889d858631e4307d5796` |
| x86_64 | macOS 15.7.9 (24G830), Intel | 129,107 B | `80adf3c227ef7a8b4d0129143a4aa002e4015a070ff9d963b1469e26084e1c27` |

두 후보는 버전 `0.0.1`, 소스 `f71c783ecdc273f49076950ef1a8aef5e7dbb43f`,
SDK 지문 `8d6659e4464b47b810ca89a0b492946421c3b19d748962a20f4733cd4dad8c16`에
고정했다. 이 소스와 작업 시작 시점의 `8380c72` 사이에는 Linux 패키징·문서·테스트
변경이 있었다. 두 ZIP은 해당 커밋의 성공한 GitHub Actions 실행에서 받았다.
[macOS 검증 실행 42750332817](https://github.com/blackvrice/GraphicsSDK/actions/runs/42750332817)

## 패키지와 검증 범위

- 각 후보는 Release 정적 아카이브, 공개 헤더 13개, CMake export 4개, LICENSE로
  구성된다. ZIP 바이트를 보존하며, 이름에 버전·아키텍처·backend·빌드 설정과
  커밋 및 ZIP 해시의 앞 12자를 넣는다. 전체 값과 19개 파일의 크기·해시는
  `manifest.json`에 기록한다.
- 원본 감사 receipt의 SHA-256을 확인한 뒤 ZIP의 전체 해시와 파일별 해시를
  검증한다. 공개 `gfx::version`과 설치 CMake 패키지 버전이 일치해야 한다.
  두 후보는 버전, 소스 커밋, SDK 지문, GitHub 실행 ID를 공유해야 한다.
- ZIP 안 BSD 정적 아카이브의 객체 6개를 열어 각 객체가 대상 CPU의 64비트
  Mach-O 객체인지 확인한다. 객체별 크기와 SHA-256도 기록한다.
  [Apple Mach-O 헤더](https://raw.githubusercontent.com/apple-oss-distributions/xnu/main/EXTERNAL_HEADERS/mach-o/loader.h) · [Apple CPU 식별자](https://raw.githubusercontent.com/apple-oss-distributions/xnu/main/osfmk/mach/machine.h)
- 호스팅 macOS에서 공백이 있는 독립 설치 경로로 옮긴 뒤 `find_package`,
  `GraphicsSDK::Core` 링크, Metal·Foundation·QuartzCore 프레임워크와
  SDK 입력 경계를 검사했다. 가상 Metal 장치에서 아키텍처별 이미지 18개
  (161,024바이트)의 전체 픽셀을 검증했고 후보 도구가 그 기록을 재검사한다.
  [설치 소비자와 기존 보관 증거](PHASE4_METAL_PACKAGE_REPORT.md)

물리 Mac GPU·실제 표시·성능, 최소 macOS 버전 및 최종 지원 구성은 수용 대상에
남아 있다. `P7-PKG-04`에서 승인된 구성에 후보와 실행 근거를 연결한다.

## 재현

저장소 루트에서 실행한다. GitHub Actions 원본 아티팩트의 보관 기간은 14일이다.
같은 바이트를 다시 확인하려면 만료 전에 아래 네 아티팩트를 받아 보관한다.
버전과 소스가 같아도 새 CI 실행의 ZIP과 receipt 해시는 해당 실행에서 확인한다.

```powershell
$run = '42750332817'
$source = 'f71c783ecdc273f49076950ef1a8aef5e7dbb43f'
$inputRoot = "out/phase7-macos-input-$run"
$outputRoot = "out/phase7-macos-release-$run"
gh run download $run --repo blackvrice/GraphicsSDK `
  -p 'macos-core-*' -p 'macos-metal-sdk-*' --dir $inputRoot

$auditArgs = @(
  '--arm64-audit', "$inputRoot/macos-core-arm64-$source/macos-metal-package",
  '2a088b05a5e518c9929692c3ee9e29d416187bb9f806ab46644328c4c13f1f6b',
  '--x86-64-audit', "$inputRoot/macos-core-x86_64-$source/macos-metal-package",
  '3764dea8abbdbf4ee99829b1d18a9007229ab855389912f040884b39b7f16d1a'
)
python tools/macos_release_candidate.py pack @auditArgs --output $outputRoot `
  --arm64-zip "$inputRoot/macos-metal-sdk-arm64-$source/GraphicsSDK-macos-metal-arm64.zip" `
  --x86-64-zip "$inputRoot/macos-metal-sdk-x86_64-$source/GraphicsSDK-macos-metal-x86_64.zip"
python tools/macos_release_candidate.py check @auditArgs --output $outputRoot
python -m unittest discover -s tests -p 'test_macos*.py'
```

Windows에서 생성·검증을 두 번 실행해 두 ZIP과 매니페스트가 각각 바이트 단위로
일치함을 확인했다. 매니페스트 SHA-256은
`7ef9527ef1927ef6aaa54c7c37d79ee272bf4d4af72a0a87c7fced773b94e9f3`다.
서로 바꾼 ZIP, 변경한 매니페스트, 변경한 ZIP은 거부됐다.
