# P3-BEN-04 — 현재 제품/SDL draw CPU 재측정

2026-10-27 **사용자가 선택한 고정 CPU 비교 계약으로 P3-BEN-04를 완료했다.** draw1,
draw1000, instanced1000의 **40개 프로세스 / 새 표본 900개 / 전체 이미지 readback 1,074회**를
검증했다. GPU timestamp 비교는 **필수 미완료 P3-BEN-09**이며 최종 비교 gate의 선행 조건에 남아 있다.

## 사용자 범위 결정과 현재 입력

기존 완료 문구의 CPU/GPU 구간과 고정 BEN-01 draw 계약의 CPU-only 범위 차이를 확인했다.
사용자는 “고정 CPU 비교 계약으로 완료하고 GPU timestamp 비교는 별도 미완료 항목으로 분리”를
선택했다. [범위 결정](PHASE3_DRAW_SCOPE.md)에 따라 CPU wait/total을 GPU 시간으로 바꾸지 않고
새 작업 ID를 추가했다. 기존 142개 ID는 유지해 현재 **143개 작업 ID**다.

실행 코드 커밋은 `2323f3ab0e4c6de6ae5c75b17c2dfdc257ac88ac`다. SDK 28파일과 네 비교
소비자의 C++·shader fixture는 변경하지 않았다. 제품 Windows fingerprint는
`197bd38300429ca10e77214bae33a269ec938cf78f112a66fb3182a37298582b`다.
SDL pin `3b2bc951500ca6a1f428af77e036e732d9a8afe3`와 기존 VVL lock을 유지했다.

첫 metadata-only probe에서 이전 qualification과 native/DXGI LUID가 달랐다.
GPU UUID·driver UUID는 같았다. [원본 probe·경위](../benchmarks/reports/validation/2026-10-27-phase3-current-draw/initial-binding-probe/context.json)를
보존하고 현재 식별자로 SDK·SDL·네 소비자를 새로 빌드했다. BEN-01 장치 확인 8개·잘못된
대상 거부 8개와 BEN-02 프로토콜 66개 실행을 새로 통과한 뒤 draw 본 측정을 시작했다.
이전 표본과 새 qualification의 대표 표본은 본 측정 통계에 합치지 않았다.

| 입력 | 실제 확인 범위 |
|---|---|
| GPU / driver | NVIDIA GeForce GTX 1660 SUPER / `32.0.16.2101` |
| UUID / driver UUID | `be4ae3472ccd6deb68ccf2e33670e7bd` / `61fefcd074265598aa87bdd69012bf6a` |
| 현재 LUID | `287e010000000000` / DXGI `0:97832`. 이전 qualification은 `517c010000000000` |
| 빌드 | Visual Studio 17 2022 / x64 / Release / `/O2` / `/MD` / IPO OFF, 실제 compiler `MSVC 194435228` |
| shader header SHA256 | `5da606968101ff8eb75b967f5eb215640664d679c9462a0c291406d23ac266b7` |
| 새 준비 / qualification | `out/phase3-current-draw-inputs-20261027/`, `out/phase3-current-draw-qualification-20261027/` |
| 본 측정 | `out/phase3-current-draw-20261027/` |

[현재 준비 receipt](../benchmarks/reports/validation/2026-10-27-phase3-current-draw/accepted/qualification/prepared/receipt.json)는
실제 compiler/linker 입력·설치 SDK·SDL static·소비자 hash와 장치 probe를 연결한다.
고정 VVL DLL/manifest를 실행 전·중·후 확인했으며 DLL은 배포하지 않았다.
장치 귀속은 native Vulkan·DXGI의 유일한 정확한 이름/ID/LUID 일치이고 SDL native handle 직접 검증은 아니다.

## workload와 구간

[draw 계획](../benchmarks/phase3_comparison/draw.json)을 고정했다. 대상은 65×37 RGBA8,
scissor [5,3,55,31], vertex 3개 fullscreen triangle, first vertex/instance 0이다.

| scenario | 프레임당 public draw 호출 | 호출당 instance |
|---|---:|---:|
| draw1 | 1 | 1 |
| draw1000 | 1,000 | 1 |
| instanced1000 | 1 | 1,000 |

두 SDK는 같은 SPIR-V fixture를 사용하고 매 iteration에서 빨강/초록 triangle과 파랑/cyan
clear를 바꾼 전체 이미지 참조를 비교했다. 반복 draw는 같은 영역을 덮어쓰는 기존 workload다.
source-declared submitted draw 호출 합계는 **358,716**이며 native driver의 draw 수를 관측한 것은 아니다.

| 단계 | 실행과 확인 |
|---|---|
| 필수 core/sync validation | 3종 × 두 SDK = **6개**, 각 warmup 1 + sample 3. 24개 full images / **230,880 bytes** |
| 실패 확인 | SDK별 필수 layer 부재·없는 GPU 이름 = **4개**, 기대한 오류·종료 1·결과 stdout 없음 |
| 본 측정 | 3종 × 두 SDK × 반복 5 = **30개**, 각 warmup 5 + sample 30. 1,050개 full images / **10,101,000 bytes** |

모든 정확성·실패 확인이 끝난 다음 별도 성능 프로세스를 시작했다. 고정 seed의 scenario
블록 안에서 SDK·SDL을 인접 실행하고 다음 반복에는 SDK 순서를 뒤집었다. 성능 프로세스는
상속된 Vulkan/VVL/SDL/GFX 설정을 제거하고 빈 layer/settings 경로를 사용했다. 필수 layer
부재 control은 두 소비자가 먼저 수행하는 공통 SDK 검증 선행 단계의 거부를 확인한다.

CPU 구간은 **record / submit / wait / total**이다. record는 command/render pass 시작·pipeline·
scissor 설정·draw·pass 종료·command 종료, submit은 제출 API, wait는 fence 완료 대기를 포함한다.
각 iteration의 앞 세 구간 합이 total과 일치하는지 확인했다. 자원·shader·pipeline 준비,
전체 readback·참조 비교와 teardown은 타이머 밖이다. **GPU timestamp는 전혀 측정하지 않았다.**

## 현재 CPU total 결과

각 scenario·SDK의 **5개 프로세스 / 150개 표본**을 사용했다. validation 출력 18개와 성능
warmup 150회는 통계에서 제외했다. P50/P95/P99는 `(N-1)*q` 선형 보간, MAD는 P50에서의
절대 편차 중앙값이다. 원본 표본 삭제는 0개다. 단위 ms, 표시는 반올림했다.

| scenario | SDK | P50 | P95 | P99 | MAD |
|---|---|---:|---:|---:|---:|
| draw1 | GraphicsSDK | 0.070800 | 0.087805 | 0.095259 | 0.005000 |
| draw1 | SDL | 0.046250 | 0.056410 | 0.060828 | 0.002200 |
| draw1000 | GraphicsSDK | 0.135600 | 0.155060 | 0.189987 | 0.003450 |
| draw1000 | SDL | 0.102350 | 0.114630 | 0.136831 | 0.001750 |
| instanced1000 | GraphicsSDK | 0.106550 | 0.119630 | 0.128322 | 0.002850 |
| instanced1000 | SDL | 0.085100 | 0.104615 | 0.145148 | 0.003150 |

**이번 조건에서는 SDK의 CPU total P50이 세 사례 모두 SDL보다 길었다.** 원인을 확정하거나
최적화 완료로 처리하지 않았다. 이 값은 작은 render target·현재 GPU/driver의 관측이며
전체 SDK 순위나 GPU 실행 시간의 비교가 아니다. driver cache·전력·다른 프로세스 활동은
배타적으로 통제하지 않았다. 4구간 × 6그룹의 **24개 통계 열**, min/max와 각 프로세스 중앙값은
[전체 통계 JSON](../benchmarks/reports/2026-10-27-phase3-current-draw/summary.json)에 보존했다.

## 회귀와 원본 재검사

- 새 Python 검사 **19개**, 전체 discovery **822개 통과**, skip 0.
  [전체 로그](../benchmarks/reports/validation/2026-10-27-phase3-current-draw/python-local/unittest.log)를 보존했다.
- 첫 전체 검사에서 기존 과거 JSON을 읽는 `test_recorded_sources`에 `int('4096')` 관련 오류가
  한 번 발생했다. 데이터는 Git과 일치했고 소스·fixture는 수정하지 않았다. 관련 10개와
  전체 822개 재실행, 실행 커밋의 전체 822개 검사가 통과했다. 원인은 확정하지 않았다.
  [실패 원본](../benchmarks/reports/validation/2026-10-27-phase3-current-draw/ben04-python-initial.log)과
  [재검사 경위](../benchmarks/reports/validation/2026-10-27-phase3-current-draw/python-initial-context.json)를 보존했다.
- 실행 커밋의 [Windows·portable core CI](https://github.com/blackvrice/GraphicsSDK/actions/runs/40460689310)와
  [Linux Vulkan compile CI](https://github.com/blackvrice/GraphicsSDK/actions/runs/40460689301)가 성공했다.
  보관한 draw 근거가 있는 커밋에서는 Windows·Linux build CI가 아래 offline verifier도 별도로 실행한다.
- [raw receipt](../benchmarks/reports/validation/2026-10-27-phase3-current-draw/accepted/receipt.json)의
  명령·환경·PID·직렬 실행 구간·stdout/stderr를 다시 확인하고 4구간 통계를 재계산했다.
  통계·순서·누락/추가·검증 혼입·readback·CPU 구간·instance 수·중첩 실행·control·LUID·
  과거 표본·GPU 완료 승격의 **14개 변조를 모두 거부**했다.
- [Windows 실행 bytes manifest](../benchmarks/reports/validation/2026-10-27-phase3-current-draw/source-manifest-windows.json)와
  [Linux Git bytes manifest](../benchmarks/reports/validation/2026-10-27-phase3-current-draw/source-manifest-linux-git.json)는
  각 **83파일** source ZIP에 연결한다. SDK 28파일은 직전 ZIP과 byte 일치한다. Linux snapshot은
  Linux benchmark 실행 증거가 아니다.

저장소 root에서 다음 명령은 원본 Windows 소스 bytes를 임시 복원해 근거를 재검사한다.
GPU 실행은 하지 않는다.

```text
python benchmarks/reports/validation/2026-10-27-phase3-current-draw/verify.py --json
```

로드맵에서 **P3-BEN-04만 CPU 완료**로 바꾸고 **P3-BEN-09는 필수 미완료**를 유지한다.
다음 본 측정은 **P3-BEN-05 PSO 재측정**이다.
