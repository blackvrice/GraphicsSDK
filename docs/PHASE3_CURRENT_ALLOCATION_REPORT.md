# P3-BEN-06 — 현재 제품/SDL Vulkan 자원 생성 본 측정

2026-10-27, **P3-BEN-06 완료**. 고정 BEN-01 계약의 버퍼·텍스처 1개/1,000개 사례를
현재 제품과 SDL에서 새로 실행했다. **52개 프로세스·1,200쌍의 CPU 시간 표본**과
총 **716,716개 자원·4,915,238,328바이트**의 전체 readback 검증을 통과했다.
생성·공개 해제 통계 16열을 원본에서 재계산했다. 물리 메모리 회수는 측정하지 않았다.

## 실행 대상과 입력 경계

- 실행 소스: `d65efd4ebc33a0c3706dafb6700109719e007e7d`.
- NVIDIA GeForce GTX 1660 SUPER, 드라이버 `32.0.16.2101`,
  GPU UUID `be4ae3472ccd6deb68ccf2e33670e7bd`,
  driver UUID `61fefcd074265598aa87bdd69012bf6a`, LUID `287e010000000000`.
- SDL 고정 커밋 `3b2bc951500ca6a1f428af77e036e732d9a8afe3`, Vulkan-Headers
  `b379292b2ab6df5771ba9870d53cf8b2c9295daf`. VVL DLL SHA-256
  `39fb7e8398408c9bdecb4400ea7217350610c48ebbbfca908922ea805654b0aa`.
- Windows x64 Release, MSVC `194435228`, `/O2 /MD`, IPO OFF. SDK D3D12/Vulkan ON,
  SDL static GPU/Vulkan ON. SDK 제품 소스와 네 C++ 비교 소비자는 이번 작업에서 변경하지 않았다.
- 준비·BEN-02 qualification 소스는 `2323f3ab0e4c6de6ae5c75b17c2dfdc257ac88ac`이다.
  기존 소스·빌드·도구·의존성·런타임 입력 해시를 live 검사하고
  [새 탐색 실행](../benchmarks/reports/validation/2026-10-27-phase3-current-allocation/current-binding-probe/probe.stdout)에서
  같은 GPU·드라이버·LUID를 확인해 재사용했다. 이 단계에서 SDK/SDL을 새로 빌드하거나
  BEN-02를 다시 실행한 것은 아니다. 준비와 본 측정 revision을 각각 보존했으며
  이전 qualification 시간은 본 통계에 포함하지 않았다.

## 동등 결과·측정 구간·표본

[고정 계약](../benchmarks/phase3_comparison/allocation.json)과
[수용 범위](PHASE3_ALLOCATION_SCOPE.md)는 각 반복의 공개 자원 생성 구간 `create_ms`와
공개 handle 해제 구간 `public_release_ms`를 분리한다.

- 버퍼는 4,096바이트, 텍스처는 65×37 RGBA8이며 각각 1개/1,000개를 생성한다.
  모든 자원의 payload가 반복·자원 index·byte offset에 따라 바뀐다. GPU로 업로드하고
  다시 내려받은 전체 데이터가 reference와 일치해야 한다. 텍스처 검증 바이트에서 row padding은 제외한다.
- SDK는 general device buffer, SDL은 vertex-usage buffer를 생성한다. 공개 사용 용도의 차이를 유지하며,
  양쪽 모두 같은 전체 copy/readback 결과를 검증한다. 텍스처는 sampled RGBA8 2D 용도다.
  native usage flag·heap·할당 횟수가 같다고 주장하지 않는다.
- descriptor/vector 준비, staging 자원 생성·해제, payload 생성·쓰기, GPU copy,
  fence/idle 대기, readback·reference 비교, 최종 teardown은 두 타이머 밖이다.
  각 수치는 **1개 또는 1,000개 전체 구간**이다. staging buffer 생성/해제 시간은 포함하지 않는다.
- core/sync 필수 validation 8개와 missing-layer/wrong-target 거부 4개를 먼저 통과했다.
  이후 측정 40개 프로세스: 사례/SDK별 독립 5회, 각 5 warmup + 30 retained 표본.
  seed `20261027`의 사례 블록에서 인접 SDK 쌍 순서를 반복마다 교대했다.
- 그룹별 150쌍, 총 1,200쌍을 보존했다. warmup 200회와 validation 표본 24쌍은 통계에서 제외했다.
  검증 실행의 readback은 **109,837,728바이트**, 측정 실행은 **4,805,400,600바이트**다.
  warmup을 포함해 버퍼 **358,358개**, 텍스처 **358,358개**를 검증했다.
  공개 생성/해제/사용/검증 카운터가 각각 **716,716개**로 일치했다.

## 새 측정 결과

단위 ms. 원본 전체에서 `(N-1)×q` 선형 보간으로 P50/P95/P99를 계산한다.
MAD는 중앙 절대 편차다. 이상치를 제거하지 않았으며, 열별 min/max와 독립 프로세스 중앙값은
[전체 통계](../benchmarks/reports/2026-10-27-phase3-current-allocation/summary.json)에 있다.

| 사례 | SDK | 구간 | P50 | P95 | P99 | MAD |
|---|---|---|---:|---:|---:|---:|
| buffer1 | GraphicsSDK | 생성 | 0.045700 | 0.071955 | 0.095528 | 0.005900 |
| buffer1 | SDL | 생성 | 0.001000 | 0.002100 | 0.002100 | 0.000200 |
| buffer1 | GraphicsSDK | 공개 해제 | 0.106150 | 0.138845 | 0.150853 | 0.006900 |
| buffer1 | SDL | 공개 해제 | 0.000100 | 0.000200 | 0.000200 | 0.000000 |
| buffer1000 | GraphicsSDK | 생성 | 35.356900 | 50.313905 | 62.542423 | 3.549050 |
| buffer1000 | SDL | 생성 | 0.274000 | 0.321570 | 0.355522 | 0.010200 |
| buffer1000 | GraphicsSDK | 공개 해제 | 119.253700 | 147.637525 | 171.381062 | 2.421450 |
| buffer1000 | SDL | 공개 해제 | 0.048900 | 0.056410 | 0.072022 | 0.001900 |
| texture1 | GraphicsSDK | 생성 | 0.048300 | 0.066875 | 0.072559 | 0.005000 |
| texture1 | SDL | 생성 | 0.161300 | 0.197705 | 0.288489 | 0.009650 |
| texture1 | GraphicsSDK | 공개 해제 | 0.103750 | 0.125275 | 0.137551 | 0.003900 |
| texture1 | SDL | 공개 해제 | 0.000700 | 0.000955 | 0.001251 | 0.000100 |
| texture1000 | GraphicsSDK | 생성 | 44.346350 | 62.399465 | 65.441651 | 5.435700 |
| texture1000 | SDL | 생성 | 161.859350 | 181.587490 | 184.210239 | 5.014300 |
| texture1000 | GraphicsSDK | 공개 해제 | 119.673000 | 137.954715 | 166.988265 | 2.559350 |
| texture1000 | SDL | 공개 해제 | 0.149750 | 0.180330 | 0.208027 | 0.006400 |

이번 조건에서는 SDK의 텍스처 생성 P50이 짧았고, 버퍼 생성과 모든 공개 해제 P50은 길었다.
제품 최적화를 수행한 결과는 아니다. 양쪽 default allocation 정책과 내부 pool/cache 상태를
동등하게 통제하거나 조회하지 않았으며, 전원 상태와 다른 프로세스 활동도 독점 통제하지 않았다.
공개 해제는 native destruction을 지연할 수 있고 작은 값은 타이머 해상도의 영향을 받는다.
차이의 원인, native 할당 수, 물리 회수, GPU 성능 또는 SDK 전체 순위를 단정하지 않는다.
이 작업은 고정된 반복 생성·사용·해제 계약의 비교다. 장시간 메모리/Intel 충돌 조사는 재개하지 않았다.

## 재현·검증 근거

- 새 자원 비교 검사 18개, [전체 Python 회귀 857개](../benchmarks/reports/validation/2026-10-27-phase3-current-allocation/python-local/receipt.json)가
  실패·오류·건너뜀 0으로 통과했다. 실행 전후 입력 해시가 같았다.
- 실행 커밋의 [Windows·portable core CI](https://github.com/blackvrice/GraphicsSDK/actions/runs/40470511662)와
  [Linux Vulkan compile CI](https://github.com/blackvrice/GraphicsSDK/actions/runs/40470511659)가 성공했다.
  보관 근거가 추가된 커밋은 Windows/Linux CI에서 아래 verifier를 별도로 실행한다.
- [원본 receipt](../benchmarks/reports/validation/2026-10-27-phase3-current-allocation/accepted/receipt.json)의
  명령·환경·PID·직렬 구간·stdout/stderr와 통계를 다시 검사했다.
  통계·누락/추가·순서·검증 혼입·구간·자원 사용·readback 집계·중첩·control·LUID·과거 표본·
  캐시/할당 수/물리 회수 주장 등 **19개 변조를 모두 거부**했다.
- [Windows bytes manifest](../benchmarks/reports/validation/2026-10-27-phase3-current-allocation/source-manifest-windows.json)와
  [Linux Git bytes manifest](../benchmarks/reports/validation/2026-10-27-phase3-current-allocation/source-manifest-linux-git.json)는
  각각 **91파일** source ZIP에 연결한다. SDK 28파일은 직전 PSO ZIP과 byte 일치한다.
  Linux snapshot은 Linux 벤치마크 실행 증거가 아니다. 외부 DLL·실행 파일·라이브러리는 배포하지 않았다.

저장소 root에서 원본 Windows 소스 bytes를 임시 복원해 재검사할 수 있다.
아래 명령은 GPU나 native executable을 실행하지 않는다.

```text
python benchmarks/reports/validation/2026-10-27-phase3-current-allocation/verify.py --json
```

로드맵의 **P3-BEN-06만 완료**로 바꾼다. **P3-BEN-09 GPU timestamp 비교는 필수 미완료**다.
다음 비교 개발 항목은 **P3-BEN-07 Linux 비교 환경 probe 이식**이며 실제 Linux GPU/driver
환경 검증이 필요하다.
