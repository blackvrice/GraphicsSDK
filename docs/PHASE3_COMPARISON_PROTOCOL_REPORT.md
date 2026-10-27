# P3-BEN-02 — 비교 정확성·분리 실행·통계

2026-10-27 **P3-BEN-02를 완료했다.** 현재 Windows NVIDIA Vulkan 조합에서 13개 작업의
GraphicsSDK/SDL 정확성을 필수 core/sync validation으로 확인하고, 대표 작업의 별도
반복 실행·원본 표본·통계 재계산을 연결했다. 실제 실행은 총 **66개 프로세스**다.
해상도·규모별 본 성능 수용 `P3-BEN-03`~`06`은 미완료로 유지한다.

## 입력과 환경

- 실행 코드 커밋: `18c316de929a4d83cf668cfa0552b67252c38901`.
- [BEN-01의 설치 SDK·SDL·네 소비자](PHASE3_COMPARISON_INPUTS_REPORT.md)를 재사용했다.
  모든 실제 파일 해시를 시작·종료에 확인하고, 실행 중에는 소비자·검증 런타임·소스·계약을
  각 프로세스 시작 전에 다시 확인했다. 과거 timing은 가져오지 않았다.
- SDK 28파일과 네 소비자의 C++ 소스는 변경하지 않았다. 제품 Windows fingerprint는
  `197bd38300429ca10e77214bae33a269ec938cf78f112a66fb3182a37298582b`다.
- NVIDIA GeForce GTX 1660 SUPER, driver `32.0.16.2101`. UUID
  `be4ae3472ccd6deb68ccf2e33670e7bd`, driver UUID `61fefcd074265598aa87bdd69012bf6a`,
  LUID `907c010000000000`가 모든 성공 실행에서 BEN-01과 일치했다.
- SDL pin `3b2bc951500ca6a1f428af77e036e732d9a8afe3` 및
  [검증 runtime lock](../tools/vulkan-validation.lock.json)을 유지했다. 실제 VVL DLL SHA256은
  `39fb7e8398408c9bdecb4400ea7217350610c48ebbbfca908922ea805654b0aa`다.
  DLL·manifest의 실제 바이트와 연결 경로를 확인했고 DLL은 배포하지 않았다.

## 실제 실행 순서와 정확성

[고정 프로토콜](../benchmarks/phase3_comparison/protocol.json)에 따라 모든 프로세스를 직렬 실행했다.

| 순서 | 실행 | 확인 결과 |
|---|---|---|
| 1 | upload 4종·draw 3종·PSO 2종·allocation 4종 × 두 SDK = **26개 validation 프로세스** | 각 warmup 1 + sample 3. 모든 changing payload/full-image/full-resource readback 통과. 총 **972,917,088 bytes** 검증 |
| 2 | 프로필×SDK별 필수 layer 부재 8개 + 없는 GPU 이름 8개 = **16개 실패 확인** | 모두 종료 코드 1, 결과 stdout 없음. 기대한 오류만 허용 |
| 3 | odd upload / draw1 / pso1 / buffer1 × 두 SDK × 반복 3 = **24개 측정 프로세스** | 각 warmup 3 + sample 12. **288개 표본**, **2,966,040 bytes** full changing readback 통과 |

검증 프로세스에는 실제 pinned layer 경로, 필수 layer 이름, sync/submit-time validation 설정과
loader trace를 지정했다. 반환된 loaded layer 경로와 실제 loader 기록도 검사했다.
필수 layer 부재 control은 두 소비자가 공통으로 먼저 수행하는 **SDK 필수 검증 선행 단계**의
거부를 확인한다. SDL 단독 layer fallback 실험으로 해석하지 않는다.

성능 프로세스에서는 상속된 Vulkan/VVL/SDL/GFX 설정을 제거하고 빈 layer/settings 경로를
사용했다. 검증 DLL이 활성화된 결과는 통계에 들어갈 수 없다. 원본 명령, 환경 override,
PID, 단조 시계의 시작·종료 구간, stdout/stderr를 모두 남겼다.

각 반복에서 프로필 블록 순서를 고정 seed로 섞고 SDK·SDL을 인접하게 실행했다.
같은 프로필의 다음 반복에서는 SDK 순서를 뒤집었다. 정확성·실패 확인을 전부 통과하기
전에는 성능 프로세스를 시작하지 않는다. readback과 GPU 사용의 포함/제외 범위는
[BEN-01 workload 계약](../benchmarks/phase3_comparison/contract.json)을 그대로 따른다.

## 통계 연결 결과

검증에서 출력한 78개 계측 표본과 성능 warmup 72회는 성능 통계에서 제외했다.
프로필·SDK별 3개 프로세스의 12개 표본, 즉 **36개 표본**으로 각 열의 통계를 계산한다.
P50/P95/P99는 정렬한 표본의 `(N-1)*q` 위치를 선형 보간한다. MAD는 P50으로부터의
절대 편차 중앙값이다. min/max와 각 프로세스 중앙값도 보존하며 표본 삭제는 0개다.

아래 값은 **실행·통계 프로토콜을 확인한 대표 작업의 기술 통계**다. P95/P99의 신뢰 구간이나
전체 workload 성능 수용을 뜻하지 않는다. 단위는 ms이며 표시만 반올림했다.

| 대표 작업 / 구간 | SDK | P50 | P95 | P99 | MAD |
|---|---|---:|---:|---:|---:|
| odd upload / total | GraphicsSDK | 0.067900 | 0.170025 | 0.180325 | 0.002200 |
| odd upload / total | SDL | 0.052600 | 0.065300 | 0.076575 | 0.001800 |
| draw1 / total | GraphicsSDK | 0.081150 | 0.165550 | 0.191330 | 0.009350 |
| draw1 / total | SDL | 0.055200 | 0.074750 | 0.109255 | 0.004800 |
| pso1 / create | GraphicsSDK | 0.083000 | 0.099750 | 0.102925 | 0.004300 |
| pso1 / create | SDL | 0.060650 | 0.078950 | 0.083000 | 0.004400 |
| buffer1 / create | GraphicsSDK | 0.054000 | 0.073050 | 0.085755 | 0.003900 |
| buffer1 / create | SDL | 0.001300 | 0.001900 | 0.001900 | 0.000300 |

draw의 record/submit/wait와 PSO·allocation의 public_release를 포함한 **18개 열의 전체
통계·프로세스 중앙값**은 [summary JSON](../benchmarks/reports/2026-10-27-phase3-comparison-protocol/summary.json)에 있다.
upload/draw는 완료 wall time, PSO/allocation은 CPU API 시간이다. 이들을 합친 순위는 없다.
driver cache는 통제하지 않았고, SDK general device buffer와 SDL vertex usage 차이도 유지한다.
native allocation 수·물리 메모리 회수·GPU timestamp나 SDL native handle 검증으로 확대하지 않는다.

## 회귀·원본 검사

- 새 Python 검사 **19개**, 전체 discovery **785개 통과**, skip 0.
  [실행 커밋의 전체 로그](../benchmarks/reports/validation/2026-10-27-phase3-comparison-protocol/python-local/unittest.log)를 보존했다.
- 첫 전체 검사에서 기존 과거 pool JSON을 읽던 중 `int('8192')` 관련 오류 1건이 발생했다.
  기록은 Git과 일치했고 소스·fixture를 수정하지 않았다. 관련 33개와 전체 785개 재실행,
  실행 커밋의 전체 785개 검사가 통과했다. 원인은 확정하지 않았다.
  [실패 원본](../benchmarks/reports/validation/2026-10-27-phase3-comparison-protocol/ben02-python-initial.log)과
  [재검사 경위](../benchmarks/reports/validation/2026-10-27-phase3-comparison-protocol/python-initial-context.json)를 보존했다.
- 같은 소스 커밋의 [Windows·portable core CI](https://github.com/blackvrice/GraphicsSDK/actions/runs/40440925078)와
  [Linux Vulkan compile CI](https://github.com/blackvrice/GraphicsSDK/actions/runs/40440925081)가 성공했다.
  [원본 CI 상태](../benchmarks/reports/validation/2026-10-27-phase3-comparison-protocol/ci-runs.json)를 보존했다.
- [raw receipt](../benchmarks/reports/validation/2026-10-27-phase3-comparison-protocol/accepted/receipt.json)에서
  원본 stdout/stderr, 순서, 환경 분리, work counters와 모든 통계를 다시 확인했다.
  통계 변경·프로세스 누락/추가/순서 변경·검증 혼입·readback/표본 변경·실행 중첩·실패 control
  성공 처리·검증 DLL 변경·과거 timing/순위 승격 등 **12개 변조를 모두 거부**했다.
- [Windows 실행 bytes manifest](../benchmarks/reports/validation/2026-10-27-phase3-comparison-protocol/source-manifest-windows.json)와
  [Linux Git bytes manifest](../benchmarks/reports/validation/2026-10-27-phase3-comparison-protocol/source-manifest-linux-git.json)는
  각 74파일 source ZIP에 연결한다. SDK 28파일은 BEN-01 ZIP과 byte 일치한다.
  Linux snapshot은 Linux benchmark 실행 증거가 아니다.

저장소 root에서 다음 명령으로 원본 source ZIP을 임시 폴더에 복원해 통계와 변조 거부를
재검사할 수 있다. GPU 실행은 하지 않는다.

```text
python benchmarks/reports/validation/2026-10-27-phase3-comparison-protocol/verify.py --json
```

장치 귀속은 앞 단계의 native Vulkan·DXGI 정확한 이름/ID/LUID 일치 방식이다. Intel 조사,
Linux 물리 GPU·사람의 표시 수용은 진행하지 않았다. 로드맵에서는 **P3-BEN-02만 완료**로
바꾸며, 다음 작업은 **P3-BEN-03 업로드 본 재측정**이다.
