# P3-BEN-05 — 현재 제품/SDL Vulkan PSO 본 측정

2026-10-27, **P3-BEN-05 완료**. 고정 BEN-01 PSO 계약의 파이프라인 1개·8개 사례를
현재 제품과 SDL에서 새로 실행했다. **28개 프로세스·600쌍의 CPU 시간 표본**과 전체
이미지 **716회·6,887,920바이트** 검증을 통과했다. 생성과 공개 해제 통계 8열을
원본에서 재계산했다. 드라이버 캐시는 통제하지 않았다.

## 실행 대상과 재사용 경계

- 실행 소스: `37a8dda009fdbce81bb64aa25036a658c7f40fc0`.
- NVIDIA GeForce GTX 1660 SUPER, 드라이버 `32.0.16.2101`,
  GPU UUID `be4ae3472ccd6deb68ccf2e33670e7bd`,
  driver UUID `61fefcd074265598aa87bdd69012bf6a`, LUID `287e010000000000`.
- SDL 고정 커밋 `3b2bc951500ca6a1f428af77e036e732d9a8afe3`, Vulkan-Headers
  `b379292b2ab6df5771ba9870d53cf8b2c9295daf`. VVL DLL SHA-256
  `39fb7e8398408c9bdecb4400ea7217350610c48ebbbfca908922ea805654b0aa`.
- Windows x64 Release, MSVC `194435228`, `/O2 /MD`, IPO OFF. SDK D3D12/Vulkan ON,
  SDL static GPU/Vulkan ON. 이 작업에서 SDK 제품 소스와 네 C++ 비교 소비자는 변경하지 않았다.
- 준비·BEN-02 qualification의 소스는 `2323f3ab0e4c6de6ae5c75b17c2dfdc257ac88ac`이다.
  기존 빌드·도구·의존성·소스 입력 해시를 live 검사하고
  [새 탐색 실행](../benchmarks/reports/validation/2026-10-27-phase3-current-pso/current-binding-probe/probe.stdout)에서
  GPU·드라이버·LUID가 같음을 확인해 재사용했다. SDK/SDL을 이 단계에서 다시 빌드하거나
  BEN-02를 다시 실행했다고 주장하지 않는다. 준비와 본 측정의 revision은 각각 보존했다.
  이전 qualification의 시간 표본은 본 측정 통계에 포함하지 않았다.

## 구간·동등 작업·표본

[고정 계약](../benchmarks/phase3_comparison/pso.json)과
[수용 범위](PHASE3_PSO_SCOPE.md)는 CPU API 구간을 두 열로 분리한다.

- `create_ms`: 공개 파이프라인 생성 호출. 각 구현 안의 셰이더 객체 생성·해제를 포함한다.
  SDK의 셰이더 바이너리/description 복사와 SDL의 shader descriptor 준비는 타이머 밖이다.
- `public_release_ms`: 공개 pipeline handle 해제 호출. GPU 렌더링, fence/idle 대기,
  readback, 최종 teardown은 두 타이머 밖이다. 생성/해제의 합을 end-to-end 또는 GPU 시간으로
  해석하지 않는다. 각 수치는 해당 반복의 **파이프라인 1개 또는 8개 전체 구간**이다.
- 같은 SPIR-V fullscreen/red/green fixture, 65×37 RGBA8, 영역 `[5,3,55,31]`을 사용한다.
  생성한 모든 파이프라인이 개별 scissor 영역을 그린 뒤 전체 이미지의 red/green stripe와
  blue/cyan clear가 반복마다 바뀌는 reference와 일치해야 한다.
- core/sync 필수 validation 4개 프로세스와 missing-layer/wrong-target 거부 4개를 먼저 통과했다.
  이후 20개 측정 프로세스: 사례/SDK별 독립 5회, 각 5 warmup + 30 표본.
  인접 SDK 쌍의 순서를 반복마다 바꾸고 seed `20261027`로 사례 블록을 배치했다.
- 사례/SDK별 150쌍, 총 600쌍을 보존했다. warmup 100회와 validation 표본 12쌍은 통계에서 제외했다.
  검증 실행의 readback은 153,920바이트, 측정 실행은 6,734,000바이트다.
  공개 카운터의 생성/해제/사용 영역은 각각 **3,222개**로 일치했다.
  이는 native pipeline 생성 횟수 또는 물리 GPU 메모리 반환의 증명이 아니다.

## 새 측정 결과

단위 ms. P50/P95/P99는 원본 전체의 `(N-1)×q` 선형 보간이고 MAD는 중앙 절대 편차다.
이상치를 제거하지 않았다. 열별 min/max와 독립 프로세스 중앙값은
[전체 통계](../benchmarks/reports/2026-10-27-phase3-current-pso/summary.json)에 있다.

| 사례 | SDK | 구간 | P50 | P95 | P99 | MAD |
|---|---|---|---:|---:|---:|---:|
| pso1 | GraphicsSDK | 생성 | 0.109300 | 0.167415 | 0.190651 | 0.006000 |
| pso1 | SDL | 생성 | 0.081350 | 0.110600 | 0.130696 | 0.005000 |
| pso1 | GraphicsSDK | 공개 해제 | 0.004300 | 0.006385 | 0.023330 | 0.000500 |
| pso1 | SDL | 공개 해제 | 0.000100 | 0.000200 | 0.000351 | 0.000000 |
| pso8 | GraphicsSDK | 생성 | 0.240150 | 0.375765 | 0.422518 | 0.008950 |
| pso8 | SDL | 생성 | 0.130050 | 0.204990 | 0.275111 | 0.007500 |
| pso8 | GraphicsSDK | 공개 해제 | 0.008300 | 0.011665 | 0.017935 | 0.000800 |
| pso8 | SDL | 공개 해제 | 0.000300 | 0.000400 | 0.000400 | 0.000000 |

이번 조건에서는 두 사례 모두 SDK의 생성·공개 해제 P50이 SDL보다 길었다.
제품 최적화를 수행한 결과는 아니다. 드라이버의 메모리/디스크 캐시를 초기화하거나
조회하지 않았고, 반복되는 red/green 셰이더와 같은 description을 사용한다.
따라서 cold-cache 컴파일 성능이나 cache hit/miss, native 생성 횟수로 분류할 수 없다.
전원 상태·다른 프로세스 활동도 독점 통제하지 않았다. 공개 해제의 내부 지연 처리와
실제 native destruction은 다를 수 있고 작은 값은 타이머 해상도의 영향도 받는다.
이 차이의 원인, 실제 메모리 회수, GPU 성능 또는 SDK 전체 순위를 단정하지 않는다.

## 재현·검증 근거

- 새 PSO 비교 검사 17개, [전체 Python 회귀 839개](../benchmarks/reports/validation/2026-10-27-phase3-current-pso/python-local/receipt.json)가
  실패·오류·건너뜀 0으로 통과했다. 실행 전후 입력 해시가 같았다.
- 실행 커밋의 [Windows·portable core CI](https://github.com/blackvrice/GraphicsSDK/actions/runs/40469176794)와
  [Linux Vulkan compile CI](https://github.com/blackvrice/GraphicsSDK/actions/runs/40469176758)가 성공했다.
  보관 근거가 추가된 커밋은 Windows/Linux CI에서 아래 PSO offline verifier를 별도로 실행한다.
- [원본 receipt](../benchmarks/reports/validation/2026-10-27-phase3-current-pso/accepted/receipt.json)의
  명령·환경·PID·직렬 구간·stdout/stderr와 8열 통계를 다시 검사했다.
  누락/추가·순서·타이밍 혼입·readback·구간·pipeline 사용·중첩·control·LUID·과거 표본·
  캐시/생성 횟수/메모리 반환 주장 등 **17개 변조를 모두 거부**했다.
- [Windows bytes manifest](../benchmarks/reports/validation/2026-10-27-phase3-current-pso/source-manifest-windows.json)와
  [Linux Git bytes manifest](../benchmarks/reports/validation/2026-10-27-phase3-current-pso/source-manifest-linux-git.json)는
  각각 **87파일** source ZIP에 연결한다. SDK 28파일은 직전 draw ZIP과 byte 일치한다.
  Linux snapshot은 Linux 벤치마크 실행 근거가 아니다. 외부 DLL·실행 파일·라이브러리는 배포하지 않았다.

저장소 root에서 아래 명령으로 원본 Windows 소스 bytes를 임시 복원해 재검사한다.
GPU나 native executable을 실행하지 않는다.

```text
python benchmarks/reports/validation/2026-10-27-phase3-current-pso/verify.py --json
```

로드맵의 **P3-BEN-05만 완료**로 바꾼다. GPU timestamp 비교 **P3-BEN-09는 필수 미완료**다.
다음 항목은 **P3-BEN-06 자원 생성·공개 해제 재측정**이다.
