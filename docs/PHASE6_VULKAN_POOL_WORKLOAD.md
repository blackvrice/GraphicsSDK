# P6-POOL-01 Vulkan 소형 버퍼 풀 측정 계약

기존 적응형 풀은 64·256·1024·4096바이트 클래스에 31개씩 할당한 뒤, native block마다
하나만 남겼을 때 27,200바이트의 live 요구량에 168,640바이트를 예약했다. 예약/요구량은
6.2배였다. 같은 시험의 dedicated 경로는 block마다 버퍼가 하나씩 있어 124개가 모두
남았고, 풀은 20개만 남았다. 두 경로를 비교하는 새 시험은 **동일한 논리 버퍼를 생존**시킨다.
[기존 churn 결과](VULKAN_POOL_CHURN_REPORT.md)

## 고정 워크로드

세 비교군은 동일한 SDK 기준 코드에서 빌드한다. A는 dedicated, B는 기존 적응형 정책,
C는 새로 격리할 **block당 최대 4 slot** 정책이다. B와 C는 native 요구 크기·정렬,
memory type, dedicated 요구·선호의 기존 적격성 규칙을 공유한다. 첫 block 1 slot,
빈 slot 우선 사용, 살아 있는 최대 block 기준 두 배 성장, 빈 block 즉시 반환을
유지한다. C의 block 상한은 `min(64 KiB, 4 × stride)`다.

| 프로필 | 비교군 모두에 동일한 입력 |
|---|---|
| 희소 점유 | 크기별 31개 생성 후 0부터 세는 생성 순번 0·1·3·7·15만 유지. 네 크기를 순환하며 각 8개를 추가. 전체 해제 후 4096바이트 하나를 생성·해제. 총 4라운드. |
| 혼합 churn | 기존 `mixed`의 크기 순서와 논리 인덱스 생존 규칙으로 96개까지 채우고 1/2·1/3·1/4 주기로 유지. 4라운드마다 전체 해제. 총 16라운드. |
| 기록·제출 보유 | 기존 `held`의 24개를 종료된 recording에 보유시킨 뒤 wrapper를 해제. 고정 순서로 72개를 추가하고 이전 기록을 제출한 뒤 fence 완료 후 원본 payload 확인. 총 8라운드. |

생존자 선택에는 요청 크기와 논리 생성 순번만 사용한다. 실제 block·offset·handle을
보고 대상을 고르면 그 실행은 성능 비교에서 제외한다. 프로필·라운드별로 요청 크기,
ID, payload seed, 생성·해제 순서와 체크포인트의 live ID 목록을 기록하고 비교군 간
일치시킨다. 혼합과 보유 프로필의 입력 순서와 payload 생성은 [기존 시험](../experiments/vulkan_pool_churn/main.cpp)을
재사용한다. 희소 점유는 기존 크기 순서와 payload seed를 유지하고 생존 규칙만 고정한다.

## 예약량과 호출 수 기준

`reserved`는 추적 대상 버퍼를 지원하는 고유 native 메모리 할당의 크기 합계다.
`required`는 해당 시점에 살아 있는 버퍼의 native 요구 크기 합계이며, 논리 요청량
`payload`와 따로 기록한다. 추적 대상에는 wrapper가 해제돼도 recording이나 제출이
보유하는 버퍼를 포함한다. Upload/Readback 보조 버퍼는 별도 열에 집계한다. 예약량은
bytes 단위의 할당 요청 관측이며 OS residency나 물리 VRAM 회수는 별도 검증 대상이다.
요구량이 0이면 배율을 비워 두고 live 할당 수·bytes가 0인지 확인한다.

C는 각 pooled class에서 block이 최대 4 slot이고 빈 block을 즉시 해제하므로
pooled 예약량은 live slot bytes의 4배 이하다. slot 정렬 패딩은 live slot bytes와
native 요구량의 차이로 따로 보며 dedicated bytes는 따로 더한다. 실제 요구 크기가
아래 입력과 같은 환경에서는 고정 희소 프로필의 모델을 직접 대조할 수 있다.

| 체크포인트 | live 버퍼 / 요구 bytes | A 예약 bytes | B 예약 bytes | C 예약 bytes |
|---|---:|---:|---:|---:|
| 최초 생성 완료 | 124 / 168,640 | 168,640 | 168,640 | 168,640 |
| 고정 생존자만 유지 | 20 / 27,200 | 27,200 | 168,640 | 81,600 |
| 빈 slot 재충전 | 52 / 70,720 | 70,720 | 168,640 | 81,600 |
| 전체 해제 | 0 / 0 | 0 | 0 | 0 |

위 수치는 CPU 모델이다. C의 희소 지점 목표는 예약/요구량 3.0배와 refill 중
추가 할당 0회다. 재충전까지 누적 대상 할당 성공 수는 A/B/C 순서로 156/20/36회다.
실제 native 요구 크기나 적격성이 다르면 해당 환경의 모델을 별도로 계산하고 원인을
기록한다. 전체 해제 직후 4096바이트 단일 요청은 세 비교군 모두 요구량만 예약하고,
다시 해제하면 0이어야 한다.

## 실행과 채택 기준

1. **정확성 먼저.** 설치 SDK로 동일 payload 전체를 readback하고 각 체크포인트에서
   적격성·정렬·비중첩·native 및 public 통계를 대조한다. recording 보유와 fence 완료
   뒤의 반환, host/큰 버퍼의 dedicated 분기, 합성 할당·bind OOM의 rollback,
   exact fallback·재시도까지 새 fingerprint에서 검증한다. 오류 주입은 정확성 실행에만
   적용한다. Native observer 및 고정 Vulkan validation layer의 활성 여부를 기록한다.
2. **별도 성능 실행.** 같은 Release 도구·장치·드라이버에서 validation, fault layer,
   observer를 끄고 fresh process로 실행한다. 워밍업 1회 뒤 프로필 전체를 10회
   반복한다. Dedicated 반복 편차를 먼저 수집한 뒤 A/B/C를 한 번씩 실행하는 비교
   블록을 구성한다. 가능한 여섯 순서를 두 번씩 사용한다. 장치 초기화, payload 준비,
   readback 및 fence 대기는 create/release 타이머 밖이다. 생성과 wrapper 해제는
   배치별로 측정한다. 제출 보유의 `wait(fence)`는 대기와 SDK 회수를 포함한 별도
   구간으로 기록한다. 구간별 raw sample, 프로세스별 nearest-rank p50·p95와 블록별
   paired 차이를 보관한다. Native 할당·반환 호출과 예약량은 별도 관측 실행에서 대조한다.
3. **판정.** 새 후보는 희소와 재충전 지점에서 B보다 예약량이 낮고, 혼합 및 보유
   지점의 peak 예약량은 B 이하이며, 전체 해제에서 0을 충족해야 한다.
   최초 생성 완료와 재충전 완료에서 전용 할당보다 누적 성공 할당 수가 적어야 한다. 지연
   판정은 비교군의 결과를 보기 전에 장치·프로필·구간별 dedicated 반복 간
   p50·p95 절대 편차를 고정한다. 각 지표의 편차는 별도 dedicated 대 dedicated
   짝을 8개 수집해 차이 절댓값의 최댓값으로 정한다. 후보의 중앙 paired 차이가
   이 범위를 초과해 느려지면 기본 채택 조건을 충족하지 못한다. 타이머 해상도나
   실행 변동으로 개선을 구분할 수 없으면 성능 우위를 기록하지 않는다. 실제 결과를
   근거로 기본값·opt-in·기각을 별도 결정한다.

## 계약 재현

```text
python tools/check_vulkan_pool_workload.py --output out/phase6-pool-workload/contract.json
```

검사기는 두 GPU와 validation ON/OFF에서 기록된 희소 프로필 8개를 읽는다. 기존
적응형 4개 실행의 라운드별 예약량을 CPU 모델과 대조하고, 기존 dedicated 4개
실행이 당시의 native block 규칙 아래서 124개를 유지했음을 확인한다. 출력 JSON에는
새 비교의 고정 생존 순번과 세 정책의 모델 체크포인트가 들어간다. 이번 단계의
결과는 계약과 모델 검산이며, C의 native 구현과 실측은 `P6-POOL-02` 이후의 증거다.
[검사기](../tools/check_vulkan_pool_workload.py) · [기존 native 원자료](../benchmarks/results/2026-10-24-vulkan-pool-churn/results.json)
