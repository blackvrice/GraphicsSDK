# P4-MTL-07 Metal API Validation

macOS CI는 기본 설치 SDK 검사에 이어 새 프로세스에서 같은 소비자를 실행한다. 첫 Metal 장치를
만들기 전에 `MTL_DEBUG_LAYER=1`과 `MTL_DEBUG_LAYER_ERROR_MODE=assert`를 설정한다.
SDK는 앱의 프로세스 설정을 그대로 사용한다. `DeviceDesc.validation`과 장치별
`validationErrorCount()`는 Metal에서 `Unsupported`다. 검증 근거는 각 프로세스의 종료 상태,
Metal 진단 원문과 픽셀·버퍼 비교 결과로 남긴다. [Apple API Validation 문서](https://developer.apple.com/documentation/xcode/validating-your-apps-metal-api-usage)

검증기가 켜졌는지 확인하려고 SDK를 사용하지 않는 작은 blit 프로그램을 실행한다.

- 정상 대조는 16바이트를 복사해 전체 바이트를 비교한다. 검증 옵션을 비운 실행과 켠 실행에서
  각각 성공해야 한다.
- 오류 대조는 16바이트 source에서 16바이트 destination으로 20바이트를 복사하려고 한다.
  검증기가 blit 인코딩 시점에 범위를 진단하고 `SIGABRT`로 프로세스를 종료해야 한다. 테스트는 오류 복사를 GPU에
  제출하기 전에 끝난다.
- 설치 SDK 회귀 검사는 제출과 수명, 버퍼 전송, 텍스처와 render pass, draw, 레이어 수명,
  AppKit 창의 크기 변경과 BGRA readback을 검사한다. AppKit 화면 부재는 별도로 기록하며,
  창 테스트의 표시 콜백은 drawable 픽셀 검증과 구분한다.

API 오류는 assertion으로 검출한다. 경고는 집계하지 않으며 Shader Validation과 물리 GPU 검증은
별도 범위다. 검증 모드 실행은 성능 비교에 사용하지 않는다.

## 실행과 재검증

GitHub Actions의 `macos-core.yml`은 arm64와 x86_64에서 네이티브 대조 및 설치 SDK 테스트를
실행하고 `macos-metal-validation` 폴더를 아티팩트에 포함한다. macOS의 깨끗한 체크아웃에서
Xcode 도구, CMake, Ninja와 Python을 사용해 같은 검사를 실행할 수 있다.

```sh
python3 tools/macos_core.py run --architecture "$(uname -m)" --output out/macos-core-evidence
python3 tools/macos_metal_probe.py run --core out/macos-core-evidence --output out/macos-metal-evidence
python3 tools/macos_metal_backend.py run --availability out/macos-metal-evidence \
  --output out/macos-metal-backend-evidence
cmake -S tests/macos_metal_presentation -B out/macos-metal-presentation-build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$PWD/out/macos-metal-backend-evidence-install"
cmake --build out/macos-metal-presentation-build
python3 tools/macos_metal_validation.py run \
  --backend out/macos-metal-backend-evidence \
  --presentation out/macos-metal-presentation-build/metal_presentation \
  --output out/macos-metal-validation
```

기록을 받은 OS에서는 Metal 실행 파일 없이 종료 상태, 오류 진단 원문, 버퍼·이미지 readback과
해시를 다시 확인할 수 있다.

```sh
python3 tools/macos_metal_validation.py check --output out/macos-metal-validation
```

## 2026-11-04 실행 결과

macOS 실행 소스는 `bcc986d`이고 SDK 지문은
`7f89bd2d3460289aa5d65b1786cdccbd30585caf4e442b0c294ab24e7fae6a06`이다.
두 아키텍처의 검증 활성 프로세스에서 Apple의 `Metal API Validation Enabled` 진단을 확인했다.
오류 대조는 source와 destination의 16바이트 범위를 넘는 20바이트 복사를 진단한 뒤
Darwin `SIGABRT`로 종료했다. [macOS CI](https://github.com/blackvrice/GraphicsSDK/actions/runs/41781574931)

| 검사 | ARM64 | Intel x86_64 |
|---|---:|---:|
| 정상 네이티브 blit, 검증 off/on | 2/2 | 2/2 |
| 범위 초과 blit 검출 | 1/1 | 1/1 |
| 검증을 켠 설치 SDK 회귀 | 6/6 | 6/6 |
| 버퍼·텍스처·draw 전체 readback 바이트 | 2,723,089 | 2,723,089 |
| 창 크기 변경 전후 BGRA 프레임 비교 | 8/8 | 8/8 |
| 양수 표시 타임스탬프 관찰 | 미관찰 | 관찰 |
| 전체 Python 계약 | 1,504/1,504 | 1,504/1,504 |

검증 대상은 GitHub runner의 Apple 가상 Metal 장치다. AppKit 창의 두 픽셀 크기에서 readback을
비교했다. ARM은 직접 Metal 표시 대조에서도 양수 타임스탬프를 관찰하지 못했다. 실제 Mac GPU의
검증과 Shader Validation은 별도 범위다.

원본 진단, 프로세스별 환경 및 종료 상태, 전체 payload와 Python 검사 기록을 묶었다. 재생은
증거를 만든 Darwin 프로세스의 종료 코드 `-6`을 사용하며 Windows에서도 같은 결과를 검증한다.
Windows·Linux·macOS CI가 원본을 현재 검사기로 재생한다.
[증거 목록과 결과](../benchmarks/reports/validation/2026-11-04-metal-validation/summary.json)

```sh
python3 benchmarks/reports/validation/2026-11-04-metal-validation/verify.py
```
