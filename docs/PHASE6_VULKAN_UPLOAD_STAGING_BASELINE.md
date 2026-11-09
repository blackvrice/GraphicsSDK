# P6-OPT-07 Vulkan Upload ring 기준선

이 단계는 Windows Vulkan 설치 SDK의 작은 버퍼 업로드에 범위를 고정한다.
버퍼당 256, 1,024, 4,096바이트를 순환하며 배치당 1·16·64개를 업로드한다.
한 장치의 생성 스레드와 한 큐에서 배치를 제출하고 fence까지 기다린다.
같은 Device 버퍼와 Readback 버퍼를 양쪽에서 미리 준비한다. 별도 명령 목록의
readback으로 모든 바이트를 비교하며, payload는 배치와 슬롯마다 바뀐다.

| 기준 경로 | Upload 버퍼 소유와 할당 |
|---|---|
| Transient | 배치마다 복사 요청 수만큼 전용 Upload 버퍼를 생성한다. 업로드 fence 완료 뒤 명령 목록을 해제하고 버퍼를 반환한다. |
| Reuse | 같은 크기의 전용 Upload 버퍼를 슬롯별로 미리 생성한다. 다음 배치에서는 이전 배치 fence를 기다린 슬롯에 다시 쓴다. |

공개 `copyBuffer`는 offset 0을 사용한다. 현재 Vulkan `Buffer::write`는 매핑,
복사, 필요 시 flush, unmap을 수행하며 쓰기 버전과 in-flight 사용을 검사한다.
따라서 Reuse는 애플리케이션이 현재 API로 사용할 수 있는 실질 기준선이다.
이전 텍스처 업로드 실험은 큰 단일 이미지와 별도의 측정 범위를 사용한다.
[기존 텍스처 업로드 진단](PHASE3_VULKAN_UPLOAD_MEMORY_REPORT.md)

## 측정 계약

- **정확성.** 64개 배치를 두 정책과 두 GPU에서 고정 Vulkan validation layer로
  각각 5회 실행한다. 별도 native 관측에서는 세 배치 크기별로 5회 실행해
  Upload 할당과 Device/Readback 보조 할당을 분리한다. 실제 Vulkan 호출,
  요청·반환 바이트, 종료 시 SDK 소유량 0을 확인한다.
- **시간.** 장치·배치마다 Transient/Reuse 쌍을 4개 독립 프로세스 블록으로
  실행하고 순서를 번갈아 바꾼다. 각 프로세스에서 5회 워밍업 뒤 30개 표본을
  보관한다. validation과 관측 계층을 끈다. CPU 경과 시간을 Upload 생성,
  쓰기, 명령 기록, 제출, fence 대기, Upload 해제로 나눠 기록한다.
  payload 생성, command wrapper 생성·정리, 초기 preallocation, 별도
  readback은 시간 구간 밖이다. 구간 합계와 프로세스 중앙값을 사용한다.
  Reuse의 배치당 생성·해제 열은 0이며, 초기·종료 할당은 native 관측에 포함한다.

## 기준선 결과

Windows x64의 GTX 1660 SUPER와 Intel UHD Graphics 770에서 설치 SDK
`8d6659e4464b47b810ca89a0b492946421c3b19d748962a20f4733cd4dad8c16`를
사용했다. 해당 SDK 소스 커밋은 `69030be228d7ea641a3ddae88d0772733623f756`다.
고정 validation 4개, 별도 native 관측 12개, 시간 측정 48개로 **64개 프로세스**를
실행했다. 전체 readback **83,778,560바이트**와 시간 표본 **1,440개**를 검증했다.

| 배치 크기 | Upload 할당 호출, Transient / Reuse | 최대 Upload 예약 bytes, 두 정책 공통 |
|---|---:|---:|
| 1 | 5 / 1 | 256 |
| 16 | 80 / 16 | 27,136 |
| 64 | 320 / 64 | 113,152 |

호출 수는 native 관측 프로세스의 5배치와 초기·종료 구간을 합한 값이다.
두 GPU에서 수치가 일치했고 모든 할당이 반환됐다. 최대 예약량에는 해당 프로세스의
Upload 버퍼만 포함한다.

아래는 각 프로세스에서 여섯 CPU 구간의 합계를 먼저 중앙값으로 집계한 뒤, 네 독립
프로세스의 중앙값을 취한 값이다. 열의 단위는 ms다.

| GPU · 배치 | Transient | Reuse |
|---|---:|---:|
| GTX 1660 SUPER · 1 | 1.368 | 0.150 |
| GTX 1660 SUPER · 16 | 4.146 | 1.476 |
| GTX 1660 SUPER · 64 | 12.580 | 2.072 |
| Intel UHD 770 · 1 | 0.652 | 0.546 |
| Intel UHD 770 · 16 | 1.592 | 0.810 |
| Intel UHD 770 · 64 | 4.257 | 1.367 |

64개 Transient 배치에서 생성·해제 관측 중앙값은 GTX에서 각각 4.206ms·6.729ms,
Intel에서 2.518ms·0.055ms였다. Reuse의 쓰기는 각각 0.023ms·0.021ms였으며,
GTX에서는 fence 대기 1.842ms, Intel에서는 기록 0.770ms와 fence 대기 0.514ms가
더 큰 구간이었다. 이 수치는 프로세스별 짧은 실행에서 관측한 값이다. SDK 호출
시간을 기록했고, 드라이버 내부 비용과 GPU 실행 시간은 별도 계측이 필요하다.
후보에 대한 개선 판정은 같은 조건의 별도 실행에서 다룬다.
[측정 요약](../benchmarks/reports/validation/2026-11-09-vulkan-upload-staging/summary.json) ·
[배치·native 원자료](../benchmarks/reports/validation/2026-11-09-vulkan-upload-staging/campaign.zip)

## 후보 구현과 수용 기준

`P6-OPT-08`은 이 버퍼 업로드 프로필에 적용할 격리 후보를 구현한다. 사용 중
범위의 바이트를 완료 전 덮어쓰지 않는 규칙을 정하고, 기록 중·종료된 미제출
명령·제출 후 fence의 소유권을 각 범위와 연결한다. 작은 용량에서 wrap과
포화를 유도한다. 잘못된 범위·정렬·장치, 제출 거부·재시도, 종료/장치 손실,
용량 초과의 명시적 동작과 최종 반환을 시험한다. 여러 배치가 겹치는 정확성
검증은 단일 배치 순차 시간 측정과 구분한다.

`P6-OPT-09`는 같은 payload, 정렬, 작업 순서, 큐와 완료 경계에서
후보와 Reuse를 우선 비교한다. Transient는 할당 비용의 비교 자료로 사용한다.
채택 판단에 전체 readback·수명·오류 회귀, fresh 반복의 생성·쓰기·기록·
제출·대기·해제, native 할당 호출·최대 예약량, wrap·포화 처리 비용을 포함한다.
선행 Shader/PSO 수용은 후보 구현의 의존성이다.

## 재현

Windows MSVC x64 Release 설치 SDK와 [기존 관측 계층](PHASE6_VULKAN_POOL_REGRESSION.md),
고정 [Vulkan validation 도구](../tools/prepare_vulkan_validation.py)를 사용한다.
`<install>`은 설치 SDK 절대 경로다. `--sdk`와 `--source-commit`은
사용하는 SDK의 fingerprint와 원본 커밋을 지정한다.

```powershell
cmake -S benchmarks/vulkan_upload_staging -B out/upload-staging-build -G "Visual Studio 17 2022" -A x64 "-DGFX_INSTALLED_SDK=<install>"
cmake --build out/upload-staging-build --config Release --parallel 1
python tools/run_vulkan_upload_staging.py run --binary out/upload-staging-build/Release/vulkan_upload_staging.exe --layer out/pool03/churn --sdk "<fingerprint>" --source-commit "<commit>" --output out/upload-staging-results
python tools/run_vulkan_upload_staging.py check --input out/upload-staging-results
```
