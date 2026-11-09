# P6-OPT-13 Vulkan Descriptor cache/pool 기준선

Windows x64 MSVC Release의 기존 Vulkan 설치 SDK에서 sampled image 하나와
정적 sampler를 사용하는 공개 `bindResources` 경로를 측정한다.
[기존 검증 셰이더](../tests/VulkanBindingSupport.hpp)를 사용하고 파이프라인,
두 1×1 텍스처, 256×7 렌더 타깃, readback은 미리 만든다.

프레임당 1·64·256회 바인딩과 같은 수의 draw를 기록한다. 각 draw는
서로 겹치지 않는 세로 띠를 그린다. 반복 패턴은 프레임 안에서 같은 텍스처를
사용하고, 교대 패턴은 두 텍스처를 번갈아 사용한다. 프레임마다 첫 색을
바꾸고 모든 RGBA 바이트를 독립 띠 참조와 비교한다. 1회 구성의 두 패턴은
같은 작업량과 이미지의 대조군이다.

## 실행 계약

- 관측과 validation은 두 장치에서 구성당 3프레임씩 별도 실행한다.
  256회 구성에서는 마지막 draw 전에 다른 텍스처로 257번째 바인딩을
  요청한다. 거부 뒤 마지막 띠의 픽셀로 직전 스냅샷 보존을 확인한다.
- 관측 계층은 생성·set 할당·갱신·bind·풀 파괴를 센다. 제출 후 공개 명령
  목록을 놓고 fence 완료를 확인하기 전까지 풀의 보유를 확인한다.
  완료 관측 뒤 반환과 미제출 기록 폐기의 반환을 각각 확인한다.
- 타이밍은 두 계층을 끄고 새 프로세스 4개에서 실행한다. 반복·교대 순서를
  블록마다 바꾸며 프로세스당 워밍업 4회 뒤 16개 표본을 보존한다.
  `bindResources` 호출들의 CPU 시간 합, 그 합을 포함한 전체 명령 기록,
  제출 직후 명령 핸들 해제, fence 대기·회수를 구분한다.
  readback 비교와 초기 준비는 측정 구간 밖이다.

## 결과

GTX 1660 SUPER와 Intel UHD 770에서 72개 프로세스를 실행했다.
1,032개 프레임의 7,397,376바이트를 비교했고, validation 12건에서
오류는 0이었다. 한도를 넘긴 24개 요청은 거부됐고 마지막 띠의 참조는
모두 일치했다.

관측 12건에서 스냅샷 3,864개를 확인했다. 미제출 대조군 12개가 이 합계에
포함된다. 반복·교대 모두 성공한 바인딩마다 Vulkan 풀 생성·set 할당·갱신·bind
호출이 한 번씩 일어났으며 각 풀이 반환됐다. 제출 프레임의 최대 보유 풀 수는
바인딩 수와 같았다. 이 값은 완료를 확인하기 전 SDK 소유량이며 실제 GPU 신호
발생 시각과 드라이버의 디스크립터 메모리 바이트는 별도 측정 범위다.

CPU 시간(µs)은 프로세스별 중앙값을 먼저 구한 뒤 네 프로세스의 중앙값으로
요약했다. 전체 기록에는 바인딩 시간이 포함된다.

| GPU | 바인딩 수 | 패턴 | 바인딩 합 | 전체 기록 | fence 대기·회수 |
| --- | ---: | --- | ---: | ---: | ---: |
| GTX 1660 SUPER | 1 | 반복 | 3.35 | 25.02 | 62.00 |
| GTX 1660 SUPER | 1 | 교대 | 3.40 | 25.62 | 67.35 |
| GTX 1660 SUPER | 64 | 반복 | 118.72 | 161.60 | 94.68 |
| GTX 1660 SUPER | 64 | 교대 | 120.82 | 168.05 | 98.15 |
| GTX 1660 SUPER | 256 | 반복 | 547.40 | 653.10 | 167.55 |
| GTX 1660 SUPER | 256 | 교대 | 544.58 | 646.45 | 192.78 |
| Intel UHD 770 | 1 | 반복 | 5.18 | 347.12 | 433.02 |
| Intel UHD 770 | 1 | 교대 | 6.00 | 376.12 | 432.40 |
| Intel UHD 770 | 64 | 반복 | 106.28 | 565.42 | 539.40 |
| Intel UHD 770 | 64 | 교대 | 110.98 | 658.65 | 622.12 |
| Intel UHD 770 | 256 | 반복 | 608.30 | 1180.90 | 716.02 |
| Intel UHD 770 | 256 | 교대 | 609.55 | 1229.88 | 767.50 |

256회 구성에서 두 패턴의 바인딩 합은 GTX 약 0.55ms, Intel 약 0.61ms였다.
같은 텍스처를 사용해도 관측된 풀 생성·반환 수는 교대 패턴과 같았다.
바인딩 합은 공개 호출 안의 검증·SDK 기록·native 호출을 포함한다.
[측정 요약](../benchmarks/reports/validation/2026-11-09-vulkan-descriptor-baseline/summary.json) ·
[프로세스별 원자료](../benchmarks/reports/validation/2026-11-09-vulkan-descriptor-baseline/campaign.zip)

## 후속 후보 계약

`P6-OPT-04`에서 이 반복·교대 작업을 그대로 사용해 후보의 native 호출 수,
최대 보유량, 전체 이미지, 회수 구간과 CPU 시간을 비교한다. 후보를 적용할
때는 리소스·view·layout 변경 시 분리, 스냅샷 불변성, 미제출·미완료
set의 보존, 한도에서의 거부 원자성과 장치 퇴역 시 반환을 확인한다.
상수 버퍼·배열·다중 스레드에 대한 비용은 별도 범위다.
후보 구현에는 Shader/PSO 수용(`P5-SHD-08`)이 선행한다.

## 재현

저장소 루트에서 Vulkan 헤더와
[고정 validation 런타임](../tools/prepare_vulkan_validation.py)을 준비한다.
관측 DLL과 manifest는 벤치마크와 함께 빌드한다. `<install>`, fingerprint와
source commit은 비교 대상 Release 설치 SDK를 가리킨다.

```powershell
cmake -S benchmarks/vulkan_descriptor_baseline -B out/descriptor-build -G "Visual Studio 17 2022" -A x64 "-DGFX_INSTALLED_SDK=<install>"
cmake --build out/descriptor-build --config Release --parallel 1
python tools/run_vulkan_descriptor_baseline.py run --binary out/descriptor-build/Release/vulkan_descriptor_baseline.exe --sdk "<fingerprint>" --sdk-source-commit "<commit>" --output out/descriptor-results
python tools/run_vulkan_descriptor_baseline.py check --input out/descriptor-results
```
