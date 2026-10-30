# Phase 3 비교 결과 통합 — Windows 완료 범위

이 문서는 `tools/phase3_comparison_report.py`가 원본을 재검증한 뒤 생성한다. **P3-GATE-02는 미완료**다. 기존 측정의 통합이며 새 GPU 실행·새 표본 수집이 아니다.

## 장치와 해석 범위

장치: NVIDIA GeForce GTX 1660 SUPER. device UUID `be4ae3472ccd6deb68ccf2e33670e7bd`, driver UUID `61fefcd074265598aa87bdd69012bf6a`, native driver `2604679168`.

제품 SDK fingerprint `197bd38300429ca10e77214bae33a269ec938cf78f112a66fb3182a37298582b`, SDL pin `3b2bc951500ca6a1f428af77e036e732d9a8afe3`.

- LLGL은 사용자 승인 `llgl-fixed-pin-transfer-src-v1`이다. 고정 upstream 원본과 구분한다.
- CPU 캠페인의 SDK/SDL은 유일한 이름·native Vulkan/DXGI 대조다. LLGL과 별도 GPU 캠페인만 실제 renderer native handle을 관측했다.
- SDK/SDL application API 요청은 1.1, LLGL 별도 관측은 1.4.357이다. 같은 API 요청·내부 barrier·clear·staging 정책이라는 주장은 하지 않는다.
- 업로드/CPU draw는 fence 완료까지의 wall time이다. PSO/자원 생성은 공개 create/release 호출이며 물리 메모리 회수·native 할당 수·cache 초기화를 입증하지 않는다.
- GPU 구간은 render pass 직전 TOP_OF_PIPE부터 직후 BOTTOM_OF_PIPE까지다. query marker·의존성·스케줄링이 포함되며 비계측 GPU 시간으로 보정하지 않는다.
- 캠페인 간 실행 시점·계측 모드가 다르다. 서로 다른 캠페인의 CPU/GPU P50을 빼거나 나누어 overhead·병목을 추정하지 않는다.
- 각 행은 독립 프로세스 5개 × retained 30개다. column별 표본 수는 중복된 구간이므로 합산하지 않는다. P95/P99는 기술 통계이며 신뢰구간이 아니다.
- 온도·전력·driver cache·다른 프로세스 활동을 배타적으로 통제하지 않았다. SDK 전체 순위나 차이의 원인은 확정하지 않는다.

## 재검사와 라이선스 입력

```text
python tools/phase3_comparison_report.py --check
python tools/phase3_comparison_report.py --write
```

기본 동작도 check다. 각 캠페인의 독립 verifier로 raw 표본·참조·환경·source ZIP·라이선스 입력을 먼저 재검사한다. JSON에는 원본 receipt·요약·CSV·ZIP·manifest·verifier의 SHA-256을 기록한다. 각 ZIP의 고지·compiler 입력과 원본 보고서의 재실행 명령을 함께 보존한다. 외부 실행 파일을 재배포하지 않으며 hash는 외부 신뢰 서명이 아니다. 이 검사는 offline 재생이며 실제 GPU 재실행을 대신하지 않는다.

## P3-CMP-07 — three-sdk-upload

실행 기반 commit `a6b93b524d8db6e63e35ddc40ea060a62b9f13d9`. [원본 receipt](../benchmarks/reports/validation/2026-10-29-phase3-three-sdk-upload/native/receipt.json), [source·license ZIP](../benchmarks/reports/validation/2026-10-29-phase3-three-sdk-upload/source.zip), [manifest](../benchmarks/reports/validation/2026-10-29-phase3-three-sdk-upload/manifest.json), [독립 verifier](../benchmarks/reports/validation/2026-10-29-phase3-three-sdk-upload/verify.py).

전체 프로세스 87개. 계수: measurement_samples=1800.

전체 참조 검사: verified_images=2148, verified_bytes=57908372340.

| 입력 | SDK | 모드 | clock / 구간 | P50 ms | P95 ms | P99 ms | MAD ms |
|---|---|---|---|---:|---:|---:|---:|
| odd | GraphicsSDK | uninstrumented | CPU / total_ms | 0.197950 | 0.831030 | 1.930802 | 0.051050 |
| odd | SDL pinned | uninstrumented | CPU / total_ms | 0.119250 | 0.705110 | 1.245397 | 0.025700 |
| odd | LLGL + transfer-src patch v1 | uninstrumented | CPU / total_ms | 0.260350 | 1.477445 | 4.110216 | 0.060600 |
| upload1080 | GraphicsSDK | uninstrumented | CPU / total_ms | 2.421350 | 10.462735 | 16.290186 | 0.550800 |
| upload1080 | SDL pinned | uninstrumented | CPU / total_ms | 2.218400 | 10.853200 | 21.051547 | 0.473200 |
| upload1080 | LLGL + transfer-src patch v1 | uninstrumented | CPU / total_ms | 2.767750 | 13.496815 | 18.199223 | 0.619900 |
| upload4k | GraphicsSDK | uninstrumented | CPU / total_ms | 19.483650 | 41.909100 | 63.587264 | 3.853400 |
| upload4k | SDL pinned | uninstrumented | CPU / total_ms | 9.977200 | 31.021185 | 39.852695 | 1.522750 |
| upload4k | LLGL + transfer-src patch v1 | uninstrumented | CPU / total_ms | 25.748800 | 41.113090 | 44.237599 | 1.931950 |
| upload4k16f | GraphicsSDK | uninstrumented | CPU / total_ms | 42.818200 | 68.522325 | 108.288916 | 5.464150 |
| upload4k16f | SDL pinned | uninstrumented | CPU / total_ms | 34.759750 | 91.863285 | 150.031128 | 12.751150 |
| upload4k16f | LLGL + transfer-src patch v1 | uninstrumented | CPU / total_ms | 58.666050 | 77.486940 | 92.123265 | 7.200650 |

## P3-CMP-08 — three-sdk-draw

실행 기반 commit `bc7e1c65ff6d3deb901be7444cb34f81cd213747`. [원본 receipt](../benchmarks/reports/validation/2026-10-29-phase3-three-sdk-draw/native/receipt.json), [source·license ZIP](../benchmarks/reports/validation/2026-10-29-phase3-three-sdk-draw/source.zip), [manifest](../benchmarks/reports/validation/2026-10-29-phase3-three-sdk-draw/manifest.json), [독립 verifier](../benchmarks/reports/validation/2026-10-29-phase3-three-sdk-draw/verify.py).

전체 프로세스 75개. 계수: measurement_samples=1350.

전체 참조 검사: verified_images=1623, verified_bytes=15613260.

| 입력 | SDK | 모드 | clock / 구간 | P50 ms | P95 ms | P99 ms | MAD ms |
|---|---|---|---|---:|---:|---:|---:|
| draw1 | GraphicsSDK | uninstrumented | CPU / record_ms | 0.026250 | 0.039475 | 0.045328 | 0.005250 |
| draw1 | GraphicsSDK | uninstrumented | CPU / submit_ms | 0.021500 | 0.031705 | 0.037022 | 0.004050 |
| draw1 | GraphicsSDK | uninstrumented | CPU / wait_ms | 0.044750 | 0.059140 | 0.135531 | 0.002200 |
| draw1 | GraphicsSDK | uninstrumented | CPU / total_ms | 0.094650 | 0.127455 | 0.192893 | 0.010550 |
| draw1 | SDL pinned | uninstrumented | CPU / record_ms | 0.016200 | 0.027660 | 0.065216 | 0.002900 |
| draw1 | SDL pinned | uninstrumented | CPU / submit_ms | 0.022250 | 0.041580 | 0.057155 | 0.004650 |
| draw1 | SDL pinned | uninstrumented | CPU / wait_ms | 0.032500 | 0.050585 | 0.090385 | 0.002400 |
| draw1 | SDL pinned | uninstrumented | CPU / total_ms | 0.071000 | 0.118550 | 0.166416 | 0.008900 |
| draw1 | LLGL + transfer-src patch v1 | uninstrumented | CPU / record_ms | 0.011500 | 0.016700 | 0.018700 | 0.003750 |
| draw1 | LLGL + transfer-src patch v1 | uninstrumented | CPU / submit_ms | 0.014900 | 0.024520 | 0.033551 | 0.002850 |
| draw1 | LLGL + transfer-src patch v1 | uninstrumented | CPU / wait_ms | 0.029050 | 0.034285 | 0.058977 | 0.001400 |
| draw1 | LLGL + transfer-src patch v1 | uninstrumented | CPU / total_ms | 0.056650 | 0.071540 | 0.101596 | 0.007000 |
| draw1000 | GraphicsSDK | uninstrumented | CPU / record_ms | 0.061550 | 0.074245 | 0.079169 | 0.003850 |
| draw1000 | GraphicsSDK | uninstrumented | CPU / submit_ms | 0.025650 | 0.032595 | 0.042261 | 0.002950 |
| draw1000 | GraphicsSDK | uninstrumented | CPU / wait_ms | 0.098250 | 0.114855 | 0.119908 | 0.004600 |
| draw1000 | GraphicsSDK | uninstrumented | CPU / total_ms | 0.185000 | 0.219305 | 0.238112 | 0.008700 |
| draw1000 | SDL pinned | uninstrumented | CPU / record_ms | 0.036550 | 0.046120 | 0.058364 | 0.003950 |
| draw1000 | SDL pinned | uninstrumented | CPU / submit_ms | 0.018950 | 0.030750 | 0.044681 | 0.004550 |
| draw1000 | SDL pinned | uninstrumented | CPU / wait_ms | 0.072350 | 0.082565 | 0.224784 | 0.002100 |
| draw1000 | SDL pinned | uninstrumented | CPU / total_ms | 0.128950 | 0.160670 | 0.306281 | 0.009300 |
| draw1000 | LLGL + transfer-src patch v1 | uninstrumented | CPU / record_ms | 0.030650 | 0.039130 | 0.043418 | 0.002850 |
| draw1000 | LLGL + transfer-src patch v1 | uninstrumented | CPU / submit_ms | 0.012700 | 0.024720 | 0.027573 | 0.001300 |
| draw1000 | LLGL + transfer-src patch v1 | uninstrumented | CPU / wait_ms | 0.080050 | 0.086775 | 0.102203 | 0.004100 |
| draw1000 | LLGL + transfer-src patch v1 | uninstrumented | CPU / total_ms | 0.121700 | 0.141315 | 0.154902 | 0.004100 |
| instanced1000 | GraphicsSDK | uninstrumented | CPU / record_ms | 0.025050 | 0.047785 | 0.056004 | 0.005150 |
| instanced1000 | GraphicsSDK | uninstrumented | CPU / submit_ms | 0.019850 | 0.034120 | 0.049210 | 0.003350 |
| instanced1000 | GraphicsSDK | uninstrumented | CPU / wait_ms | 0.093400 | 0.107690 | 0.147805 | 0.008300 |
| instanced1000 | GraphicsSDK | uninstrumented | CPU / total_ms | 0.137000 | 0.202920 | 0.219806 | 0.014000 |
| instanced1000 | SDL pinned | uninstrumented | CPU / record_ms | 0.016250 | 0.021065 | 0.031669 | 0.002050 |
| instanced1000 | SDL pinned | uninstrumented | CPU / submit_ms | 0.022450 | 0.038525 | 0.053030 | 0.003950 |
| instanced1000 | SDL pinned | uninstrumented | CPU / wait_ms | 0.069400 | 0.080790 | 0.114295 | 0.002300 |
| instanced1000 | SDL pinned | uninstrumented | CPU / total_ms | 0.108350 | 0.131655 | 0.156234 | 0.007450 |
| instanced1000 | LLGL + transfer-src patch v1 | uninstrumented | CPU / record_ms | 0.015100 | 0.029665 | 0.033744 | 0.003700 |
| instanced1000 | LLGL + transfer-src patch v1 | uninstrumented | CPU / submit_ms | 0.022150 | 0.044805 | 0.051861 | 0.006450 |
| instanced1000 | LLGL + transfer-src patch v1 | uninstrumented | CPU / wait_ms | 0.077450 | 0.100010 | 0.106571 | 0.007700 |
| instanced1000 | LLGL + transfer-src patch v1 | uninstrumented | CPU / total_ms | 0.113250 | 0.174575 | 0.182602 | 0.009750 |

## P3-CMP-09 — three-sdk-pso

실행 기반 commit `ff49dcc66f37316a2bb596fb2d438b936b85e504`. [원본 receipt](../benchmarks/reports/validation/2026-10-29-phase3-three-sdk-pso/native/receipt.json), [source·license ZIP](../benchmarks/reports/validation/2026-10-29-phase3-three-sdk-pso/source.zip), [manifest](../benchmarks/reports/validation/2026-10-29-phase3-three-sdk-pso/manifest.json), [독립 verifier](../benchmarks/reports/validation/2026-10-29-phase3-three-sdk-pso/verify.py).

전체 프로세스 58개. 계수: measurement_samples=900.

전체 참조 검사: verified_images=1082, verified_bytes=10408840.

| 입력 | SDK | 모드 | clock / 구간 | P50 ms | P95 ms | P99 ms | MAD ms |
|---|---|---|---|---:|---:|---:|---:|
| pso1 | GraphicsSDK | uninstrumented | CPU / create_ms | 0.108650 | 0.118810 | 0.143448 | 0.004850 |
| pso1 | GraphicsSDK | uninstrumented | CPU / public_release_ms | 0.003900 | 0.004755 | 0.004900 | 0.000500 |
| pso1 | SDL pinned | uninstrumented | CPU / create_ms | 0.079950 | 0.094615 | 0.119726 | 0.005550 |
| pso1 | SDL pinned | uninstrumented | CPU / public_release_ms | 0.000100 | 0.000200 | 0.000351 | 0.000000 |
| pso1 | LLGL + transfer-src patch v1 | uninstrumented | CPU / create_ms | 0.085800 | 0.100955 | 0.104616 | 0.012150 |
| pso1 | LLGL + transfer-src patch v1 | uninstrumented | CPU / public_release_ms | 0.002850 | 0.003700 | 0.003851 | 0.000650 |
| pso8 | GraphicsSDK | uninstrumented | CPU / create_ms | 0.232000 | 0.255855 | 0.394612 | 0.014100 |
| pso8 | GraphicsSDK | uninstrumented | CPU / public_release_ms | 0.007900 | 0.009100 | 0.009551 | 0.000900 |
| pso8 | SDL pinned | uninstrumented | CPU / create_ms | 0.128200 | 0.159510 | 0.239339 | 0.004150 |
| pso8 | SDL pinned | uninstrumented | CPU / public_release_ms | 0.000200 | 0.000300 | 0.000400 | 0.000000 |
| pso8 | LLGL + transfer-src patch v1 | uninstrumented | CPU / create_ms | 0.127400 | 0.150355 | 0.154173 | 0.010450 |
| pso8 | LLGL + transfer-src patch v1 | uninstrumented | CPU / public_release_ms | 0.004500 | 0.005800 | 0.006402 | 0.000600 |

## P3-CMP-10 — three-sdk-allocation

실행 기반 commit `4f37f2030a712d206718b18d568206c63b9c7063`. [원본 receipt](../benchmarks/reports/validation/2026-10-29-phase3-three-sdk-allocation/native/receipt.json), [source·license ZIP](../benchmarks/reports/validation/2026-10-29-phase3-three-sdk-allocation/source.zip), [manifest](../benchmarks/reports/validation/2026-10-29-phase3-three-sdk-allocation/manifest.json), [독립 verifier](../benchmarks/reports/validation/2026-10-29-phase3-three-sdk-allocation/verify.py).

전체 프로세스 98개. 계수: measurement_samples=1800.

전체 참조 검사: verified_images=541541, verified_buffers=541541, verified_bytes=7427776356.

| 입력 | SDK | 모드 | clock / 구간 | P50 ms | P95 ms | P99 ms | MAD ms |
|---|---|---|---|---:|---:|---:|---:|
| buffer1 | GraphicsSDK | uninstrumented | CPU / create_ms | 0.094550 | 0.209175 | 0.362280 | 0.019550 |
| buffer1 | GraphicsSDK | uninstrumented | CPU / public_release_ms | 0.183550 | 0.391000 | 1.300593 | 0.021700 |
| buffer1 | SDL pinned | uninstrumented | CPU / create_ms | 0.002300 | 0.004200 | 0.006161 | 0.000600 |
| buffer1 | SDL pinned | uninstrumented | CPU / public_release_ms | 0.000200 | 0.000300 | 0.000351 | 0.000000 |
| buffer1 | LLGL + transfer-src patch v1 | uninstrumented | CPU / create_ms | 0.662550 | 4.245470 | 6.479150 | 0.138850 |
| buffer1 | LLGL + transfer-src patch v1 | uninstrumented | CPU / public_release_ms | 0.202750 | 3.105735 | 4.400529 | 0.026850 |
| buffer1000 | GraphicsSDK | uninstrumented | CPU / create_ms | 101.882800 | 145.069970 | 156.289668 | 24.598700 |
| buffer1000 | GraphicsSDK | uninstrumented | CPU / public_release_ms | 266.123150 | 424.362950 | 486.790541 | 59.608850 |
| buffer1000 | SDL pinned | uninstrumented | CPU / create_ms | 0.566750 | 1.021425 | 1.756499 | 0.205000 |
| buffer1000 | SDL pinned | uninstrumented | CPU / public_release_ms | 0.124750 | 0.451015 | 0.608718 | 0.062950 |
| buffer1000 | LLGL + transfer-src patch v1 | uninstrumented | CPU / create_ms | 759.834150 | 948.058560 | 1128.688641 | 90.418000 |
| buffer1000 | LLGL + transfer-src patch v1 | uninstrumented | CPU / public_release_ms | 2.399500 | 4.611625 | 6.318042 | 0.716600 |
| texture1 | GraphicsSDK | uninstrumented | CPU / create_ms | 0.144500 | 0.222955 | 0.274567 | 0.032250 |
| texture1 | GraphicsSDK | uninstrumented | CPU / public_release_ms | 0.190250 | 0.299180 | 0.385702 | 0.022400 |
| texture1 | SDL pinned | uninstrumented | CPU / create_ms | 0.286550 | 0.438585 | 0.622787 | 0.036700 |
| texture1 | SDL pinned | uninstrumented | CPU / public_release_ms | 0.001200 | 0.002155 | 0.002806 | 0.000200 |
| texture1 | LLGL + transfer-src patch v1 | uninstrumented | CPU / create_ms | 2.272300 | 3.391980 | 4.040808 | 0.383150 |
| texture1 | LLGL + transfer-src patch v1 | uninstrumented | CPU / public_release_ms | 0.230700 | 0.477845 | 1.167355 | 0.032450 |
| texture1000 | GraphicsSDK | uninstrumented | CPU / create_ms | 80.555900 | 148.376465 | 164.725825 | 26.525600 |
| texture1000 | GraphicsSDK | uninstrumented | CPU / public_release_ms | 201.384850 | 377.498620 | 467.242513 | 40.338550 |
| texture1000 | SDL pinned | uninstrumented | CPU / create_ms | 308.246400 | 446.042435 | 532.749974 | 44.027600 |
| texture1000 | SDL pinned | uninstrumented | CPU / public_release_ms | 0.596500 | 1.166295 | 2.674393 | 0.289400 |
| texture1000 | LLGL + transfer-src patch v1 | uninstrumented | CPU / create_ms | 1856.040600 | 2336.241625 | 2415.821229 | 358.675850 |
| texture1000 | LLGL + transfer-src patch v1 | uninstrumented | CPU / public_release_ms | 6.284850 | 11.508140 | 17.219949 | 1.285350 |

## P3-BEN-09 — draw-gpu

실행 기반 commit `a2f10ee090b6aad90d131a5013dda815039537e2`. [원본 receipt](../benchmarks/reports/validation/2026-10-29-phase3-draw-gpu/native/receipt.json), [source·license ZIP](../benchmarks/reports/validation/2026-10-29-phase3-draw-gpu/source.zip), [manifest](../benchmarks/reports/validation/2026-10-29-phase3-draw-gpu/manifest.json), [독립 verifier](../benchmarks/reports/validation/2026-10-29-phase3-draw-gpu/verify.py).

전체 프로세스 111개. 계수: measurement_GPU_samples=900, measurement_CPU_samples=2700.

전체 참조 검사: verified_images=3206, verified_bytes=30841720.

| 입력 | SDK | 모드 | clock / 구간 | P50 ms | P95 ms | P99 ms | MAD ms |
|---|---|---|---|---:|---:|---:|---:|
| draw1 | GraphicsSDK | baseline | CPU / cpu_total_ms | 1.969800 | 5.896985 | 8.398068 | 1.193800 |
| draw1 | GraphicsSDK | audit | CPU / cpu_total_ms | 1.316850 | 5.421995 | 6.966875 | 1.004850 |
| draw1 | GraphicsSDK | timestamp | CPU / cpu_total_ms | 1.099400 | 5.483780 | 7.765055 | 0.836150 |
| draw1 | GraphicsSDK | timestamp | GPU / gpu_ms | 0.007920 | 0.009312 | 0.009954 | 0.000368 |
| draw1 | SDL pinned | baseline | CPU / cpu_total_ms | 0.273200 | 3.161020 | 5.196036 | 0.170350 |
| draw1 | SDL pinned | audit | CPU / cpu_total_ms | 0.436450 | 3.704755 | 6.929945 | 0.306000 |
| draw1 | SDL pinned | timestamp | CPU / cpu_total_ms | 0.613050 | 4.176490 | 6.146563 | 0.467950 |
| draw1 | SDL pinned | timestamp | GPU / gpu_ms | 0.005120 | 0.006482 | 0.007060 | 0.000448 |
| draw1000 | GraphicsSDK | baseline | CPU / cpu_total_ms | 1.308000 | 5.659395 | 7.668554 | 0.996250 |
| draw1000 | GraphicsSDK | audit | CPU / cpu_total_ms | 2.021100 | 6.213025 | 9.080193 | 1.074750 |
| draw1000 | GraphicsSDK | timestamp | CPU / cpu_total_ms | 1.892450 | 5.557215 | 8.173853 | 0.899550 |
| draw1000 | GraphicsSDK | timestamp | GPU / gpu_ms | 0.040880 | 0.054090 | 0.055944 | 0.001264 |
| draw1000 | SDL pinned | baseline | CPU / cpu_total_ms | 0.526050 | 3.818880 | 5.829891 | 0.339450 |
| draw1000 | SDL pinned | audit | CPU / cpu_total_ms | 1.261650 | 5.961820 | 8.833575 | 0.764300 |
| draw1000 | SDL pinned | timestamp | CPU / cpu_total_ms | 1.247650 | 5.991315 | 9.210496 | 0.764750 |
| draw1000 | SDL pinned | timestamp | GPU / gpu_ms | 0.036976 | 0.039968 | 0.040850 | 0.000816 |
| instanced1000 | GraphicsSDK | baseline | CPU / cpu_total_ms | 0.538400 | 3.994465 | 6.642218 | 0.347400 |
| instanced1000 | GraphicsSDK | audit | CPU / cpu_total_ms | 0.566600 | 3.546995 | 6.565009 | 0.362150 |
| instanced1000 | GraphicsSDK | timestamp | CPU / cpu_total_ms | 0.971550 | 3.536590 | 6.474805 | 0.690850 |
| instanced1000 | GraphicsSDK | timestamp | GPU / gpu_ms | 0.037760 | 0.058086 | 0.059233 | 0.000912 |
| instanced1000 | SDL pinned | baseline | CPU / cpu_total_ms | 0.336550 | 5.004445 | 6.452923 | 0.204100 |
| instanced1000 | SDL pinned | audit | CPU / cpu_total_ms | 0.621750 | 4.586210 | 5.824245 | 0.474700 |
| instanced1000 | SDL pinned | timestamp | CPU / cpu_total_ms | 0.304250 | 5.666615 | 6.805813 | 0.155400 |
| instanced1000 | SDL pinned | timestamp | GPU / gpu_ms | 0.034304 | 0.054755 | 0.056001 | 0.000800 |

## 최종 완료에 필요한 잔여 항목

아래 상태는 현재 로드맵에서 가져온다. 과거 보고서의 당시 `next_task`·pending 목록은 수정하지 않는다. Linux software 준비 성공은 물리 GPU 성능·표시 수용을 대신하지 않는다.

| ID | 작업 | 상태 | 필요한 환경 |
|---|---|---|---|
| P3-BEN-07 | Linux 비교 환경 probe 이식 | todo | Linux native probe 이식·실제 software/VVL·거부 대조 준비 검증 완료. 실제 Linux 물리 GPU/ICD·정확한 UUID와 SDK/SDL 동일 장치·driver/compiler·필수 validation 원본이 필요. software 성공은 최종 완료로 합산하지 않음. |
| P3-BEN-08 | Linux benchmark build 이식 | todo | 4종 소비자 native Linux build·software probe/대표 validation smoke·거부 대조 준비 검증 완료. 선행 P3-BEN-07의 실제 물리 GPU qualification과 같은 환경의 build/probe가 있어야 최종 완료. 전체 Linux 사례·성능 비교는 별도 미완료. |
| P3-LBEN-01 | Linux 업로드 비교 | todo | 동일 Linux GPU/driver의 지원 SDK 조합 |
| P3-LBEN-02 | Linux draw/instancing 비교 | todo | 동일 Linux GPU/driver의 지원 SDK 조합 |
| P3-LBEN-03 | Linux PSO 비교 | todo | 동일 Linux GPU/driver의 지원 SDK 조합 |
| P3-LBEN-04 | Linux 자원 생성 비교 | todo | 동일 Linux GPU/driver의 지원 SDK 조합 |

P3-GATE-01 통합 회귀·실제 Linux 표시 수용·P3-GATE-03 최종 판정도 별도 미완료다. 이 통합 도구는 작업 상태를 완료로 변경하지 않는다.
