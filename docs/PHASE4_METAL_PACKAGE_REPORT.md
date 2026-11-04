# P4-MTL-09 macOS Metal 설치 패키지

macOS ARM64와 Intel 빌드는 각각 Metal을 켠 정적 SDK를 설치하고 ZIP으로 묶는다. 패키지에는
공개 C++ 헤더 13개, `libGraphicsSDKCore.a`, CMake 패키지 파일 4개와 MIT 라이선스를 담는다.
Metal, Foundation, QuartzCore는 CMake 타깃에 프레임워크 이름으로 기록하고 소비자가 선택한
macOS SDK에서 링크한다.

검증기는 ZIP을 체크아웃 밖의 공백이 있는 경로에 풀고, 별도 디렉터리에 복사한 순수 C++ 소비자를
`find_package(GraphicsSDK CONFIG REQUIRED)`와 `GraphicsSDK::Core`로 빌드한다. 기존 설치 경로가
검색 경로에 있어도 지정된 패키지가 없으면 설정에 실패해야 한다. 정상 실행은 설치 SDK의 소스
지문과 offscreen Metal draw의 전체 이미지 18개를 확인한다.

Ninja가 기록한 실제 include 입력에서 SDK 자체 소스와 소비자 소스·재배치한 공개 헤더를 구분한다.
Apple SDK와 clang·libc++ 헤더를 명시한 경로에서 확인한다. Apple 링크 맵으로 소비자가 재배치된
정적 아카이브의 오브젝트를 사용했는지 검사하고, `otool -L`로 직접 링크된 시스템 라이브러리와
프레임워크를 확인한다. ZIP 멤버, Mach-O 아키텍처, 설치 라이선스와 파일별 SHA-256을 기록한다.
[Apple 링크 맵 문서](https://developer.apple.com/documentation/xcode/build-settings-reference#Write-Link-Map-File)

## 검증 결과

소스 커밋 `ddcb43882c11cabcb562de2e7f29521af4eb2358`의 GitHub 호스팅 macOS ARM64와
Intel 러너에서 검증했다. 두 소비자 모두 SDK 지문
`d5b99eb929a64b8877d63a53ca665ce2a1540af4a5d2b5e5bf0afcc8d88767fb`를 확인했다.
[macOS CI 실행](https://github.com/blackvrice/GraphicsSDK/actions/runs/41800419439)

| 러너 | ZIP 파일 수 | Metal 참조 이미지 | 비교한 바이트 | Python 계약 |
| --- | ---: | ---: | ---: | ---: |
| [ARM64](https://github.com/blackvrice/GraphicsSDK/actions/runs/41800419439/artifacts/13308055369) | 19 | 18 | 161,024 | 1,512 |
| [Intel x86_64](https://github.com/blackvrice/GraphicsSDK/actions/runs/41800419439/artifacts/13308275149) | 19 | 18 | 161,024 | 1,512 |

두 ZIP을 내려받아 내부 파일의 경로, 크기, 권한, SHA-256을 CI 기록과 대조했다. 양쪽 링크 맵에서
재배치된 아카이브의 `Backend`, `Bindings`, `Metal`, `RenderPass`, `VertexInput` 오브젝트를
확인했다. Metal 실행 범위는 GitHub 가상 장치의 오프스크린 draw이며 실제 Mac GPU 수용과 성능 측정은 남아 있다.

오프라인 재검증은 아래 명령으로 수행한다. 아카이브에는 컴파일 입력과 링크 기록, 전체 이미지,
파일 목록 및 테스트 로그를 담는다.
[보관 목록](../benchmarks/reports/validation/2026-11-04-metal-package/summary.json)

```sh
python3 benchmarks/reports/validation/2026-11-04-metal-package/verify.py
```

## 실행

`macos-core.yml`은 `macos-metal-sdk-<architecture>-<commit>`에 아키텍처별 ZIP을 보관한다.
같은 실행의 `macos-core-<architecture>-<commit>`에 검증 로그와 파일 목록을 보관한다.
다운로드한 ZIP은 다른 OS에서도 해당 기록과 대조할 수 있다.

```sh
python3 tools/macos_metal_package.py check \
  --output out/macos-metal-package \
  --package out/GraphicsSDK-macos-metal-arm64.zip
```

`--output`에는 CI 아티팩트의 `macos-metal-package` 폴더를 지정한다. macOS에서 원본 설치와
재배치 검사를 다시 실행하려면 먼저 같은 체크아웃에서 `macos_core.py`, `macos_metal_probe.py`,
`macos_metal_backend.py`를 `macos-core.yml`과 같은 순서로 실행한다.

```sh
python3 tools/macos_metal_package.py run \
  --backend out/macos-metal-backend-evidence \
  --output out/macos-metal-package
```
