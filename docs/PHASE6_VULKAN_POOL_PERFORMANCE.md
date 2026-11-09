# P6-POOL-04 Vulkan 풀 예약량과 생성·해제 지연

[P6-POOL-01 측정 계약](PHASE6_VULKAN_POOL_WORKLOAD.md)에 따라 동일한 SDK 기준
커밋에서 A 전용 할당, B 기존 적응형 풀, C 4슬롯 후보를 Release로 각각 빌드했다.
같은 두 Windows Vulkan 장치에서 혼합 churn, 고정 희소 점유, 기록·제출 보유를
실행한다. 각 버퍼의 논리 ID·크기·payload seed와 생성·해제·보유 순서를 비교하고,
매 회차의 전체 읽기 검증과 최종 소유량 0을 요구한다.

## 측정 구간

- **별도 native 관측.** 계측용 프로세스에서 기존 Vulkan 관측 계층을 사용한다.
  대상 버퍼와 Upload/Readback 보조 버퍼의 할당과 예약량을 분리하고, 구간별
  AllocateMemory/FreeMemory 호출 및 바이트 보존을 확인한다. 보유 시험의
  wrapper 해제와 fence 뒤의 native 회수도 별도 구간이다.
- **지연.** 프로세스마다 워밍업 1회 뒤 같은 프로필을 10회 실행했다. 생성 배치는
  `createBuffer`, 해제 배치는 wrapper 참조 반환, 보유의 wait 배치는
  `wait(fence)`의 CPU 경과 시간이다. payload 준비, readback 및 명령 기록·제출은
  생성·해제 타이머 밖이다. 지연 프로세스의 관측·오류 주입 및 validation layer는
  모두 비활성이다.
- **반복 편차.** 장치·프로필마다 먼저 A/A 8짝을 실행해 p50·p95 차이 절댓값의
  최댓값을 저장했다. 이후 A/B/C의 가능한 여섯 순서를 두 번씩 실행했다.
  프로세스마다 10개 회차의 배치 시간을 구간별로 합한 뒤 nearest-rank p50과
  p95를 계산한다. p95는 10회 중 최댓값이다. 비교는 같은 블록에서 A를 뺀 차이의
  중앙값과 먼저 저장한 A/A 편차를 사용한다.

기존 SDL 및 LLGL 결과는 더 좁은 이전 할당 프로필에 해당한다. 고정 SDL의 release는
안전한 시점까지 해제를 지연하고, 고정 LLGL Vulkan의 release는 `SyncGPU()`를
호출한다. 이번 계약은 기록이 원래 자원을 보유하는 동안 wrapper만 반환한 시점과,
이후 fence에서 native 자원이 회수되는 시점을 별도로 측정한다. 동일 구간을 비교할
수 있는 A/B/C를 이번 표에 포함했다. [이전 경쟁 측정](PHASE3_VULKAN_ADAPTIVE_BUFFER_POOL_REPORT.md)

## 예약량과 할당 호출

GTX 1660 SUPER와 Intel UHD Graphics 770의 native 관측 18개에서 아래 값이
장치별로 일치했다. 예약량은 대상 버퍼의 체크포인트 최댓값이며 호출 수는 프로필
한 회차에 성공한 대상 할당의 합계다. 보조 Upload/Readback의 호출과 바이트는
별도로 검증했다.

| 프로필 | A 최대 예약 bytes / 호출 | B 최대 예약 bytes / 호출 | C 최대 예약 bytes / 호출 |
|---|---:|---:|---:|
| 혼합 churn | 394,752 / 1,120 | 473,472 / 291 | 433,728 / 348 |
| 고정 희소 | 168,640 / 628 | 168,640 / 84 | 168,640 / 148 |
| 기록·제출 보유 | 394,752 / 768 | 433,728 / 320 | 433,728 / 376 |

희소 프로필에서 20개를 남겼을 때의 예약량은 A/B/C 순서로 **27,200 / 168,640 /
81,600바이트**였다. 버퍼 32개를 다시 채웠을 때는 **70,720 / 168,640 /
81,600바이트**였다. 첫 라운드 최초 채움과 재충전까지의 누적 호출 수는 각각
**124/20/36회**, **156/20/36회**였다. 세 정책 모두 최종 해제 시 native
버퍼와 예약량이 0이었다.

## 지연 결과

Native 관측 18개, A/A 편차 수집 96개, 순서를 맞춘 A/B/C 비교 216개를 합쳐
총 **330개 새 프로세스**를 실행했다. 아래는 12개 비교 블록의 프로세스별 p50을
다시 중앙값으로 집계한 CPU 경과 시간이다. 한 회차의 배치 합계이며 단위는 µs다.

| GPU · 프로필 | 생성 A / B / C | wrapper 해제 A / B / C |
|---|---:|---:|
| GTX 1660 SUPER · 혼합 | 100.72 / 29.91 / 34.00 | 219.46 / 58.83 / 67.53 |
| GTX 1660 SUPER · 희소 | 57.02 / 9.13 / 14.31 | 119.02 / 16.79 / 29.39 |
| GTX 1660 SUPER · 보유 | 68.01 / 31.81 / 36.33 | 111.35 / 35.56 / 45.23 |
| Intel UHD 770 · 혼합 | 43.71 / 15.24 / 18.64 | 57.78 / 17.37 / 19.99 |
| Intel UHD 770 · 희소 | 26.93 / 5.74 / 8.25 | 33.11 / 5.67 / 9.40 |
| Intel UHD 770 · 보유 | 33.42 / 16.19 / 17.85 | 29.82 / 11.34 / 13.72 |

4슬롯 후보의 생성·해제 p50은 두 GPU의 세 프로필 모두에서 전용 할당보다 빨랐고
paired 차이가 A/A 편차를 넘었다. 기존 적응형 풀의 생성·해제 p50 중앙값은 여섯
행 모두에서 더 낮았다. B와 C의 수치는 별도 오차 기준 없이 관측 중앙값으로만
비교한다.

보유 시험의 fence wait p50은 GTX에서 A/B/C 순서로 41.08/29.92/29.38 µs,
Intel에서 17.17/15.70/15.00 µs였다. GTX의 C는 wait p50·p95 모두 A/A 편차를
넘어 빨랐다. Intel의 wait와 혼합·보유 생성 및 해제 p95는 편차 범위 안이었다.
Intel A/A의 일부 p95 편차는 1.25~3.62초까지 커져 해당 구간의 개선 판정은 유보한다.
측정한 28개 지표 중 C가 느린 방향으로 편차를 초과한 항목은 0개였다.

이 조건에서 C는 희소·재충전 예약량 감소, 혼합·보유 peak 제한, 전용 할당 대비
최초 채움·재충전 호출 감소를 충족했다. 기본 정책 또는 opt-in 채택 여부는
`P6-POOL-05`에서 판단한다. [실행별 요약](../benchmarks/reports/validation/2026-11-09-vulkan-pool-performance/summary.json) ·
[배치 원자료](../benchmarks/reports/validation/2026-11-09-vulkan-pool-performance/campaign.zip)

## 재현

[격리 후보 생성 절차](PHASE6_VULKAN_POOL_FOUR_SLOT.md)대로 세 source를 생성하고
같은 Vulkan-Headers, MSVC x64 Release 옵션으로 각각 설치한다.
[회귀 검증 절차](PHASE6_VULKAN_POOL_REGRESSION.md)의 `churn` 빌드에서 관측 DLL을
준비한다. 아래 예에서 `$base/<정책>/install`은 세 SDK 설치 경로다.

```powershell
$base = "$( (Resolve-Path .).Path )/out/pool-performance"
foreach ($policy in 'dedicated', 'adaptive', 'four-slot') {
    cmake -S experiments/vulkan_pool_four_slot/performance -B "$base/$policy/bench" -G "Visual Studio 17 2022" -A x64 "-DGFX_INSTALLED_SDK=$base/$policy/install"
    if ($LASTEXITCODE) { throw "Configure failed for $policy" }
    cmake --build "$base/$policy/bench" --config Release --parallel 1 -- /nr:false
    if ($LASTEXITCODE) { throw "Build failed for $policy" }
}
python tools/run_vulkan_pool_performance.py run --build "$base" --layer out/pool03/churn --output "$base/matched"
python tools/run_vulkan_pool_performance.py check --input "$base/matched"
```

재검사 도구는 실행 순서, 논리 작업량, native 체크포인트와 할당 보존, 배치 원자료의
프로세스별 백분위수와 paired 통계를 다시 계산한다.
