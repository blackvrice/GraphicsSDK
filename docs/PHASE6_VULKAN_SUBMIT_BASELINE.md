# P6-OPT-15 Vulkan 동기화·명령 기록 기준선

Windows x64 MSVC Release의 기존 Vulkan 설치 SDK에서 4KiB 복사 16개를
한 배치로 실행한다. 배치당 데이터와 복사 순서는 동일하며 명령 목록당 복사 수만
1·4·16개로 바꾼다. 생성 스레드의 단일 큐에서 각각 16·4·1개의 명령 목록을
차례로 제출하고 마지막 fence를 한 번 기다린다.

슬롯마다 독립 Upload·Readback 버퍼를 미리 만들고 배치 번호와 슬롯에 따라
데이터 패턴을 바꾼다. 각 복사에는 공개 상태 전이를 기록한다. 모든 명령 목록 핸들은 마지막
fence 이후까지 보유한다. 완료 값과 각 Readback의 전체 바이트를 확인한다.

## 실행 계약

- 각 구성은 고정 Vulkan core·sync validation과 별도의 native 관측에서
  세 배치를 실행한다. 관측 계층은 명령 풀 생성·파괴, 명령 버퍼 할당,
  queue submit, fence 생성·조회·대기·파괴, 복사 수를 센다.
  제출마다 fence 하나를 생성하고, 최종 wait가 반환하면 모든 제출의 fence가
  회수된 상태인지 확인한다. 공개 명령 핸들을 놓는 구간에서 명령 풀을 반환한다.
- Native `vkGetFenceStatus`의 완료와 미완료를 따로 집계한다. SDK wait가
  조회만으로 끝나면 native `vkWaitForFences` 호출은 0회다.
  native wait가 필요하면 마지막 제출의 fence 하나를 유한 시간으로 기다린다.
  이 배치 구간의 queue/device idle 호출은 0이어야 한다.
- 시간 측정은 두 계층을 끄고 구성당 새 프로세스 네 번으로 수행한다.
  블록마다 제출 단위 순서를 바꾸고 워밍업 4회 후 20개 표본을 보관한다.
  명령 `begin` 합, 전체 기록, 제출, fence 대기, 공개 핸들 해제와 전체 시간을
  나눠 기록한다. `begin` 합은 전체 기록의 일부다. 전체 시간은 첫 기록에서
  마지막 명령 핸들 해제까지다. 버퍼 생성·쓰기, 공개 명령 객체 생성,
  readback 검증은 시간 구간 밖이다.

## 결과

GTX 1660 SUPER와 Intel UHD 770의 총 36개 프로세스에서 9,792개
복사의 전체 40,108,032바이트를 비교했다. 고정 validation 6건에서 오류는 0이었다.
별도 관측 6건에서 복사 288개와 제출 126개를 확인했다. 명령 풀, 명령 버퍼
할당과 fence는 각각 제출 수와 일치했다. fence 조회는 완료 126회,
미완료 126회였고 native wait는 18배치에서 배치마다 1회였다.
측정한 배치 안의 queue/device idle과 종료 시 미회수 자원은 0이었다.

표는 배치당 CPU 경과 시간(µs)이다. 프로세스별 중앙값을 구한 뒤
네 프로세스의 중앙값을 사용했다.

| GPU | 명령당 복사 | 배치당 제출 | 명령 begin 합 | 전체 기록 | 제출 | fence 대기 | 해제 | 전체 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| GTX 1660 SUPER | 1 | 16 | 75.88 | 119.38 | 327.15 | 60.18 | 49.10 | 592.17 |
| GTX 1660 SUPER | 4 | 4 | 30.08 | 66.10 | 89.98 | 131.15 | 17.90 | 319.00 |
| GTX 1660 SUPER | 16 | 1 | 14.80 | 48.58 | 43.65 | 160.32 | 10.00 | 285.90 |
| Intel UHD 770 | 1 | 16 | 4464.60 | 5623.15 | 304.85 | 968.50 | 3920.72 | 11400.42 |
| Intel UHD 770 | 4 | 4 | 1007.58 | 1327.30 | 80.90 | 373.75 | 884.88 | 2666.50 |
| Intel UHD 770 | 16 | 1 | 303.65 | 547.83 | 35.95 | 288.30 | 339.42 | 1295.00 |

두 GPU에서 4·1회 제출의 전체 시간이 같은 블록의 16회 제출보다 네 번 모두
짧았다. GTX의 fence 대기 구간은 묶어서 제출할 때 길어졌고, 기록·제출·해제가
줄어 배치 전체 시간은 짧아졌다. 이 결과의 적용 범위는 독립 버퍼를 사용하는
16개 복사와 마지막 fence 대기다.
[측정 요약](../benchmarks/reports/validation/2026-11-09-vulkan-submit/summary.json) ·
[프로세스별 원자료](../benchmarks/reports/validation/2026-11-09-vulkan-submit/campaign.zip)

## 후속 후보 계약

`P6-OPT-06`의 후보는 같은 제출 단위별로 기준선과 비교한다.
명령 풀·버퍼 재사용 또는 fence 회수 정책을 평가할 때 복사·상태 전이·순서,
마지막 fence 완료와 전체 readback을 유지한다. 부분 완료, 여러 in-flight
배치, 실패 후 재시도와 장치 퇴역은 후보에서 별도로 검증한다.
이 기준선은 CPU 경과 시간과 native 호출 횟수를 다루며,
후보 구현에는 Shader/PSO 수용(`P5-SHD-08`)이 선행한다.

## 재현

저장소 루트에서 Vulkan 헤더와
[고정 validation 런타임](../tools/prepare_vulkan_validation.py)을 준비한다.
관측 DLL과 manifest는 벤치마크와 함께 빌드한다.
`<install>`, fingerprint와 source commit은 비교할 Release 설치 SDK의 값이다.

```powershell
cmake -S benchmarks/vulkan_submit_baseline -B out/submit-baseline-build -G "Visual Studio 17 2022" -A x64 "-DGFX_INSTALLED_SDK=<install>"
cmake --build out/submit-baseline-build --config Release --parallel 1
python tools/run_vulkan_submit_baseline.py run --binary out/submit-baseline-build/Release/vulkan_submit_baseline.exe --sdk "<fingerprint>" --sdk-source-commit "<commit>" --output out/submit-baseline-results
python tools/run_vulkan_submit_baseline.py check --input out/submit-baseline-results
```
