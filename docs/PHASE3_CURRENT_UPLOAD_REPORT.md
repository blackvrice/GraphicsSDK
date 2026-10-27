# P3-BEN-03 — 현재 제품/SDL Vulkan 업로드 재측정

2026-10-27 **P3-BEN-03을 완료했다.** 현재 Windows NVIDIA 환경에서 업로드 4종을 두 SDK로
다시 검증·측정했다. 업로드 실행 **52개 프로세스**, 새 원본 표본 **1,200개**, 전체 이미지
readback **1,432회 / 38,605,581,560 bytes**가 통과했다. 다른 프로필의 본 측정은 포함하지 않는다.

## 현재 입력과 장치 귀속

실행 코드 커밋은 `646c2cfa5980b881b455970e0dd9990b95bfbc35`다. 제품 SDK 28파일과 비교
소비자의 C++ 코드는 바꾸지 않았다. Windows SDK fingerprint는
`197bd38300429ca10e77214bae33a269ec938cf78f112a66fb3182a37298582b`이며,
SDL pin은 `3b2bc951500ca6a1f428af77e036e732d9a8afe3`다.

처음에는 이전 BEN-02 기록으로 시작했으나 native Vulkan·DXGI의 LUID가 달라 첫 validation
결과를 거부했다. GPU UUID와 driver UUID·driver 버전은 같았다. 이 시도에서 성능 표본은
생성하지 않았고, [거부 원본과 경위](../benchmarks/reports/validation/2026-10-27-phase3-current-upload/initial-binding-rejection/context.json)를 보존했다.
LUID 변경 원인을 추정하거나 기존 장치 일치 조건을 완화하지 않았다.

현재 LUID로 SDK·SDL·네 소비자를 새로 빌드하고 BEN-01 장치 확인 8개와 잘못된 대상 거부
8개를 통과했다. 이어 BEN-02 프로토콜 66개 실행을 새로 통과한 뒤 업로드를 시작했다.
그 프로토콜의 대표 측정 288개나 과거 보고서 표본은 이번 업로드 통계에 합치지 않았다.

| 환경 | 실제 확인 값 |
|---|---|
| GPU / driver | NVIDIA GeForce GTX 1660 SUPER / `32.0.16.2101` |
| UUID / driver UUID | `be4ae3472ccd6deb68ccf2e33670e7bd` / `61fefcd074265598aa87bdd69012bf6a` |
| 현재 LUID | `517c010000000000` (native·DXGI 일치). 이전 기록은 `907c010000000000` |
| 빌드 | Visual Studio 17 2022 / x64 / Release / `/O2` / `/MD` / IPO OFF, 실제 compiler `MSVC 194435228` |
| 새 build roots | `out/phase3-current-upload-inputs-20261027/` SDK build/install, SDL static, 네 소비자 |
| 새 프로토콜 검증 | `out/phase3-current-upload-qualification-20261027/` |
| 본 측정 | `out/phase3-current-upload-20261027-accepted/` |

실제 compiler/linker 입력, 설치 Core·SDL·소비자 해시, 장치/환경과 고정 VVL DLL 해시는
[현재 준비 receipt](../benchmarks/reports/validation/2026-10-27-phase3-current-upload/accepted/qualification/prepared/receipt.json)와
[본 측정 receipt](../benchmarks/reports/validation/2026-10-27-phase3-current-upload/accepted/receipt.json)에 연결했다.
runtime DLL은 로컬에만 두었고 배포하지 않았다. 장치 귀속은 native Vulkan·DXGI의 유일한
정확한 이름·ID·LUID 일치 방식이며 SDL native handle을 직접 확인한 것은 아니다.

## workload와 실행 계약

[고정 업로드 계획](../benchmarks/phase3_comparison/upload.json)에 따라 다음을 직렬 실행했다.

| 단계 | 실행 / 결과 |
|---|---|
| 필수 core/sync validation | 4종 × 두 SDK = **8개** 프로세스, 각 warmup 1 + sample 3. **862,694,560 bytes** full changing readback 통과 |
| 실패 확인 | SDK별 필수 layer 부재·없는 GPU 이름 = **4개**. 기대한 오류와 종료 1, 결과 stdout 없음 |
| 본 측정 | 4종 × 두 SDK × 반복 5 = **40개** 프로세스, 각 warmup 5 + sample 30. **37,742,887,000 bytes** full changing readback 통과 |

각 반복의 scenario 순서는 고정 seed로 섞고 같은 scenario의 SDK·SDL을 인접 실행했다.
다음 반복에서는 SDK 순서를 뒤집었다. 모든 validation·실패 확인이 끝나기 전에는 성능
실행을 시작하지 않았다. 필수 layer 부재 control은 두 소비자의 공통 SDK 검증 선행 단계에서 거부한다.

타이머에는 staging map·CPU payload 복사·record·submit·upload fence 완료 대기가 들어간다.
payload 생성, 전체 이미지 readback·비교, 자원 준비·종료는 밖이다. SDK padded staging과
SDL tight layout 차이는 유지했다. 65×37 RGBA8은 정렬 경계 사례로 측정하며 작은 전송의
고정 overhead도 결과에 포함된다. GPU timestamp나 PCIe bandwidth 측정으로 해석하지 않는다.

## 현재 측정 결과

각 scenario·SDK의 **5개 프로세스 / 표본 150개**를 사용했다. validation의 출력 표본 24개와
성능 warmup 200회는 통계에서 제외했다. P50/P95/P99는 `(N-1)*q` 위치 선형 보간,
MAD는 P50으로부터 절대 편차의 중앙값이다. 원본 표본은 삭제하지 않았다. 단위 ms, 표시만 반올림했다.

| 크기·포맷 | SDK | P50 | P95 | P99 | MAD |
|---|---|---:|---:|---:|---:|
| 65×37 RGBA8 | GraphicsSDK | 0.073100 | 0.104675 | 0.602281 | 0.008250 |
| 65×37 RGBA8 | SDL | 0.049050 | 0.080580 | 0.371860 | 0.005600 |
| 1920×1080 RGBA8 | GraphicsSDK | 1.725900 | 3.250195 | 3.928445 | 0.234800 |
| 1920×1080 RGBA8 | SDL | 1.462400 | 2.597780 | 3.196119 | 0.234100 |
| 3840×2160 RGBA8 | GraphicsSDK | 9.995800 | 20.959730 | 24.901387 | 3.356150 |
| 3840×2160 RGBA8 | SDL | 6.122750 | 9.091485 | 15.284915 | 0.498350 |
| 3840×2160 RGBA16F | GraphicsSDK | 24.777150 | 41.559655 | 49.898607 | 6.255650 |
| 3840×2160 RGBA16F | SDL | 13.551650 | 17.880040 | 27.491044 | 1.194800 |

**이번 조건에서는 GraphicsSDK의 P50이 네 사례 모두 SDL보다 길었다.** 성능 차이를 숨기거나
최적화 완료로 처리하지 않는다. 이 작업은 현재 제품의 비교를 완료한 것이며 최적화 구현은 하지 않았다.
driver cache·전력 상태·다른 프로세스 활동을 배타적으로 통제하지 않았고 차이의 원인을 확정하지 않았다.
단일 GPU·driver·현재 workload의 관측이며 전체 SDK 순위나 tail latency의 신뢰 구간이 아니다.
min/max와 다섯 프로세스의 중앙값까지 [전체 통계 JSON](../benchmarks/reports/2026-10-27-phase3-current-upload/summary.json)에 보존했다.

## 검증과 재현

- 새 Python 검사 **18개**, 전체 discovery **803개 통과**, skip 0.
  [전체 로그](../benchmarks/reports/validation/2026-10-27-phase3-current-upload/python-local/unittest.log)를 보존했다.
- 같은 실행 커밋의 [Windows·portable core CI](https://github.com/blackvrice/GraphicsSDK/actions/runs/40443162188)와
  [Linux Vulkan compile CI](https://github.com/blackvrice/GraphicsSDK/actions/runs/40443162336)가 성공했다.
- 원본 stdout/stderr와 명령·환경 override·PID·직렬 실행 구간을 확인하고 모든 통계를 재계산했다.
  통계·순서·프로세스 누락/추가·검증 혼입·readback·표본·실행 중첩·실패 control·LUID·
  과거 표본/다른 workload 승격의 **12개 변조를 모두 거부**했다.
- [Windows 실행 bytes manifest](../benchmarks/reports/validation/2026-10-27-phase3-current-upload/source-manifest-windows.json)와
  [Linux Git bytes manifest](../benchmarks/reports/validation/2026-10-27-phase3-current-upload/source-manifest-linux-git.json)는
  각 **77파일** source ZIP에 연결한다. SDK 28파일은 직전 ZIP과 byte 일치한다.
  Linux snapshot은 Linux benchmark 실행 증거가 아니다.

저장소 root에서 다음 명령은 원본 Windows 소스 bytes를 임시 복원해 통계·근거를 재검사한다.
GPU 실행은 하지 않는다.

```text
python benchmarks/reports/validation/2026-10-27-phase3-current-upload/verify.py --json
```

실제 새 측정은 [실행 안내](../benchmarks/phase3_comparison/README.md)의 도구에 현재 장치의
완료된 qualification 경로를 지정한다. LUID가 바뀌면 새 준비·qualification을 먼저 수행한다.
로드맵에서는 **P3-BEN-03만 완료**로 바꾸며, 다음 작업은 **P3-BEN-04 draw 본 재측정**이다.
