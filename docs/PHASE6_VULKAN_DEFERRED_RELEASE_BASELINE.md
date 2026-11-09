# P6-OPT-12 Vulkan Deferred destruction 기준선

Windows x64 MSVC 19.44 Release에서 기존 Vulkan 설치 SDK의 버퍼 수명을
측정했다. 배치마다 4KiB Upload와 Device 버퍼를 1쌍 또는 32쌍 생성한다.
같은 명령에서 Upload → Device → 미리 만든 Readback으로 복사한 뒤 fence를
기다린다. 배치와 슬롯에 따라 데이터를 바꾸고 Readback의 모든 바이트를 비교한다.

| 정책 | 공개 핸들의 마지막 참조를 내려놓는 시점 |
| --- | --- |
| Early | 제출 직후, 다음 SDK fence 조회나 대기 전에 임시 버퍼와 명령 목록을 해제한다. |
| Late | fence 대기 성공 뒤 임시 버퍼와 명령 목록을 해제한다. |

관측·validation 실행에서는 별도 미제출 명령도 확인한다. 버퍼 핸들을
내려놓은 상태에서 기록이 자원을 보유하며, 기록을 폐기하면 GPU 제출 없이
반환한다. 제출 경로에서는 Early의 native 반환을 fence 대기 호출 안에서,
Late의 native 반환을 대기 뒤 공개 해제에서 추적한다. 비교 기준은 **SDK가
완료를 확인한 시점**이다. 실제 fence 신호가 발생한 시각은 별도 계측이 필요하다.

## 실행 계약

- 두 장치, 두 배치 크기와 두 정책으로 고정 core·sync validation을 실행한다.
  같은 조합을 기존 Vulkan 계층으로 별도 관측해 `vkDestroyBuffer`와
  `vkFreeMemory` 호출, 임시·지속 버퍼 할당, 최종 반환을 구간별로 확인한다.
- 타이밍은 관측·validation 없이 4개 독립 프로세스 쌍에서 정책 순서를 번갈아
  바꾼다. 프로세스마다 워밍업 5회와 측정 24회를 실행한다. 대기 전 공개 해제,
  fence 대기, 대기 후 공개 해제, 세 구간을 포함한 전체 CPU 경과 시간을 기록한다.
  버퍼 생성·쓰기·기록·제출과 readback 비교는 측정 구간 밖이다.
- 자원량은 계층이 추적한 Vulkan 메모리 할당 바이트다. 드라이버 캐시와
  실제 residency는 별도 계측 범위다.

## 결과

GTX 1660 SUPER와 Intel UHD 770에서 총 48개 프로세스를 실행했다.
전체 버퍼 16,632개의 68,124,672바이트를 비교했고, validation 8건의 오류는 0이었다.
native 관측 8건에서 Early의 임시 버퍼 660개는 fence 대기 호출 안에서,
Late의 660개는 대기 뒤 공개 해제에서 파괴했다. 미제출 기록의 임시 버퍼
264개는 기록 폐기에서 파괴했다. 모든 버퍼의 메모리가 함께 반환됐고,
장치 파괴에 맡긴 잔여 할당은 없었다. 두 장치와 정책에서 관측된 최대
할당량은 각각 1쌍 12KiB, 32쌍 384KiB였다. 이 값은 지속 Readback을 포함한다.

표는 프로세스별 24회 중앙값을 구한 뒤 네 프로세스의 중앙값을 구한
CPU 경과 시간(µs)이다. 대기와 해제 구간을 각각 집계했다.

| GPU | 쌍 | 정책 | 대기 전 해제 | fence 대기 | 대기 후 해제 | 전체 |
| --- | ---: | --- | ---: | ---: | ---: | ---: |
| GTX 1660 SUPER | 1 | Early | 0.57 | 405.38 | — | 406.10 |
| GTX 1660 SUPER | 1 | Late | — | 85.90 | 281.05 | 378.25 |
| GTX 1660 SUPER | 32 | Early | 4.32 | 9468.20 | — | 9473.38 |
| GTX 1660 SUPER | 32 | Late | — | 391.85 | 8805.02 | 9417.85 |
| Intel UHD 770 | 1 | Early | 0.60 | 499.40 | — | 500.15 |
| Intel UHD 770 | 1 | Late | — | 204.92 | 326.77 | 540.10 |
| Intel UHD 770 | 32 | Early | 4.42 | 3823.98 | — | 3828.42 |
| Intel UHD 770 | 32 | Late | — | 650.40 | 3067.95 | 3739.12 |

모든 장치·배치 조합에서 블록별 전체 시간 차이의 부호가 바뀌었다.
32쌍에서는 공개 핸들을 언제 해제하는지에 따라 회수 작업이 포함되는 CPU 구간이
달라졌다. 같은 구간 정의와 native 호출 집계를 후속 비교에 사용한다.
[측정 요약](../benchmarks/reports/validation/2026-11-09-vulkan-deferred-release/summary.json) ·
[프로세스별 원자료](../benchmarks/reports/validation/2026-11-09-vulkan-deferred-release/campaign.zip)

## 후속 후보 계약

`P6-OPT-03`에서 후속 회수 정책을 비교할 때 동일 복사와 대기 경계를 사용한다.
제출 전에 폐기한 기록과 제출 뒤 마지막 공개 핸들이 사라진 경우를 분리해 검증한다.
실제 완료 전 재사용 방지, 여러 제출의 부분 완료, 오류와 장치 세대 퇴역 시 반환을
후보 수용에 포함한다. 최대 보유 바이트와 최종 반환을 함께 비교하며,
후보 구현에는 Shader/PSO 수용(`P5-SHD-08`)이 선행한다.

## 재현

저장소 루트에서 Vulkan 헤더, [고정 validation 런타임](../tools/prepare_vulkan_validation.py)과
[기존 관측 계층](PHASE6_VULKAN_POOL_REGRESSION.md)을 준비한다.
`<layer-build>`는 관측 DLL과 manifest, `harness-fingerprint.txt`를 생성한 빌드 폴더다.
fingerprint와 source commit은 Release 설치 SDK의 값을 사용한다.

```powershell
cmake -S benchmarks/vulkan_deferred_release -B out/deferred-release-build -G "Visual Studio 17 2022" -A x64 "-DGFX_INSTALLED_SDK=<install>"
cmake --build out/deferred-release-build --config Release --parallel 1
python tools/run_vulkan_deferred_release.py run --binary out/deferred-release-build/Release/vulkan_deferred_release.exe --layer "<layer-build>" --sdk "<fingerprint>" --sdk-source-commit "<commit>" --output out/deferred-release-results
python tools/run_vulkan_deferred_release.py check --input out/deferred-release-results
```
