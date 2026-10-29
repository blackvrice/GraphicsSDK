# P3-CMP-08 세 SDK Vulkan draw·instancing 성능 비교

2026-10-29. **P3-CMP-08 완료**. GraphicsSDK·고정 SDL·사용자 승인 **LLGL 649c0cc + transfer-src patch v1**을 같은 Windows NVIDIA에서 새로 검증·측정했다. **75개 프로세스, 새 total 표본 1,350개 / CPU 구간 값 5,400개, 전체 readback 1,623회 / 15,613,260바이트**를 보존했다. 과거 시간 표본은 재사용하지 않았다.

## 소스·현재 장치

실행 기반 커밋은 `bc7e1c65ff6d3deb901be7444cb34f81cd213747`다. 새 소비자·runner의 실제 실행 바이트를 source ZIP과 fingerprint에 연결한다. 기반 커밋에 새 소비자가 이미 들어 있었다는 뜻은 아니다. 기존 SDK/SDL 빌드는 `646c2cfa5980b881b455970e0dd9990b95bfbc35`에서 만들어졌으며 실제 compiler/linker closure·설치 SDK·SDL static·소비자와 현재 SDK 소스가 바뀌지 않았는지 감사해 재사용했다. 이번에 SDK/SDL을 재빌드했다는 주장은 하지 않는다.

SDK 28파일 fingerprint는 `197bd38300429ca10e77214bae33a269ec938cf78f112a66fb3182a37298582b`, SDL pin은 `3b2bc951500ca6a1f428af77e036e732d9a8afe3`다. 새 LLGL 소비자는 Windows x64 Release·MSVC `/O2 /MD`·IPO OFF다. compiler/linker 입력 435개와 전체 동결 입력 1803개를 실행 전·각 프로세스 전·실행 후 확인했다. LLGL의 기존 승인 10줄 패치와 CMP-04 observer DLL은 그대로다. 외부 DLL/EXE/lib/driver는 재배포하지 않는다.

fresh probe와 모든 성공 프로세스에서 장치·driver UUID·LUID·native ID/driver/API·CPU/RAM/OS/DXGI 정보가 일치했다. **NVIDIA GeForce GTX 1660 SUPER**, device UUID `be4ae3472ccd6deb68ccf2e33670e7bd`, driver UUID `61fefcd074265598aa87bdd69012bf6a`, LUID `3a82010000000000`, driverVersion 2604679168다. 과거 준비 기록의 LUID를 현재 귀속으로 대신 사용하지 않았다.

SDK/SDL은 유일한 정확한 이름의 native Vulkan·DXGI 대조 방식이다. SDL native handle 직접 확인은 미완료다. LLGL은 실제 renderer native device/physical device/queue와 DXGI를 대조한다. SDK/SDL application API 요청은 소스상 1.1 (4198400), LLGL은 별도 관측 probe·correctness에서 1.4.357 (4211045)이다. API 요청까지 같다는 주장은 하지 않으며 성능 프로세스에서는 요청을 직접 관측하지 않았다.

## 고정 작업·CPU 시간 경계

기존 [draw 계약](../benchmarks/phase3_comparison/draw.json)과 shader 원본을 사용한다. 65×37 RGBA8, scissor `[5,3,55,31]`, fullscreen triangle 3 vertices, first vertex/instance 0이다. `draw1`은 1 call×1 instance, `draw1000`은 1,000 calls×1 instance, `instanced1000`은 1 call×1,000 instances다. 반복마다 red/green triangle과 blue/cyan 바탕을 교대하고 모든 warmup·표본의 전체 9,620바이트를 GPU readback과 CPU 참조로 비교한다.

`record_ms`는 command begin부터 render pass·pipeline·scissor·draw·command end까지, `submit_ms`는 queue 제출, `wait_ms`는 fence 완료 대기이며 `total_ms`가 primary end-to-end CPU wall time이다. 각 행의 첫 세 구간 합과 total의 일치를 검사한다. shader·pipeline·texture 준비, readback·전체 비교, 종료, warmup·validation·observer 실행은 통계 밖이다. GPU timestamp는 별도 미완료 `P3-BEN-09`다. 매 반복 readback을 거치므로 연속 streaming 최대 처리량도 주장하지 않는다.

LLGL은 CMP-04의 공개 Draw/DrawInstanced 매핑을 사용한다. 자동 offscreen render pass와 명시적 full attachment clear, viewport 설정을 포함한다. SDK/SDL은 기존 load-op clear 경로다. LLGL texture는 ColorAttachment|Sampled|CopySrc와 NoInitialData다. LLGL command submit과 별도 fence submit 비용은 submit 구간에 모두 포함한다. native usage/layout·driver 내부 명령까지 동일하게 만들지 않았으므로 이 차이는 결과 해석에 남긴다. workload를 native Vulkan 호출로 우회하거나 vendor patch를 늘리지 않았다.

## 검증·새 표본

성능 전에 fresh probe 3개, observer-only probe 1개, LLGL observer workload 3개, 필수 core+sync validation 9개, LLGL core/sync 진단 2개, 의도한 거부 12개가 통과했다. 거부는 세 SDK의 missing-layer/wrong-target 6개와 LLGL sync 비활성화·wrong call·wrong instance·wrong scissor·readback 변조·observer 비활성화 6개다. SDK/SDL missing-layer는 기존 소비자의 SDK required-validation 선행 단계 거부다. 잘못된 sync 진단 명령은 record만 하고 제출하지 않았다.

LLGL observer의 audit·validation 24프레임에서 native draw **8,016회**, instances **16,008개**, vertex arguments **48,024개**, bad arguments 0을 확인했다. 종료 후 native instance/device live count 0이다. 성능 실행은 observer를 제거하고 native count를 `null`로 기록한다. 측정 중 직접 관측한 수로 오인하지 않는다.

이후 3종×세 SDK×독립 5회, **45개 성능 프로세스**를 직렬 실행했다. 각 프로세스 warmup 5회·retained 30개, 입력/SDK당 150개다. seed 20261027로 시나리오 블록과 서로 다른 SDK 순열 5개를 배정한다. 6개 순열을 완전히 균형 배치한 설계는 아니다. validation 표본 27개·observer audit 표본 9개·측정 warmup 225회는 통계에서 제외한다. retained 표본·이상치 삭제는 0개다.

모든 실행에서 상속 VK/VVL/SDL/GFX 설정을 제거하고 implicit layers를 비활성화했다. 측정은 빈 layer/settings 경로를 사용하고 validation 미로드를 확인한다. LLGL은 실제 runtime modules에서 validation·observer 미로드를 확인한다. correctness에서는 고정 VVL DLL 경로·core/sync 실제 진단과 full changing reference SHA-256까지 검사한다.

P50/P95/P99는 `(N-1)×q` 선형 보간, MAD는 P50으로부터 절대 편차의 중앙값이다. 36개 입력/SDK/column 조합의 min/max·프로세스별 중앙값과 모든 원본을 [CSV](../benchmarks/reports/validation/2026-10-29-phase3-three-sdk-draw/metrics.csv)·JSON에 남겼다. 단위 ms다.

| 입력 | SDK | total P50 ms | P95 ms | P99 ms | MAD ms |
|---|---|---:|---:|---:|---:|
| draw1 | GraphicsSDK | 0.094650 | 0.127455 | 0.192893 | 0.010550 |
| draw1 | 고정 SDL | 0.071000 | 0.118550 | 0.166416 | 0.008900 |
| draw1 | LLGL 649c0cc + transfer-src patch v1 | 0.056650 | 0.071540 | 0.101596 | 0.007000 |
| draw1000 | GraphicsSDK | 0.185000 | 0.219305 | 0.238112 | 0.008700 |
| draw1000 | 고정 SDL | 0.128950 | 0.160670 | 0.306281 | 0.009300 |
| draw1000 | LLGL 649c0cc + transfer-src patch v1 | 0.121700 | 0.141315 | 0.154902 | 0.004100 |
| instanced1000 | GraphicsSDK | 0.137000 | 0.202920 | 0.219806 | 0.014000 |
| instanced1000 | 고정 SDL | 0.108350 | 0.131655 | 0.156234 | 0.007450 |
| instanced1000 | LLGL 649c0cc + transfer-src patch v1 | 0.113250 | 0.174575 | 0.182602 | 0.009750 |

- `draw1` total P50 관측 순서: LLGL 649c0cc + transfer-src patch v1 0.056650 ms → 고정 SDL 0.071000 ms → GraphicsSDK 0.094650 ms.
- `draw1000` total P50 관측 순서: LLGL 649c0cc + transfer-src patch v1 0.121700 ms → 고정 SDL 0.128950 ms → GraphicsSDK 0.185000 ms.
- `instanced1000` total P50 관측 순서: 고정 SDL 0.108350 ms → LLGL 649c0cc + transfer-src patch v1 0.113250 ms → GraphicsSDK 0.137000 ms.


| 입력 | SDK | record P50 ms | submit P50 ms | wait P50 ms |
|---|---|---:|---:|---:|
| draw1 | graphicssdk | 0.026250 | 0.021500 | 0.044750 |
| draw1 | sdl | 0.016200 | 0.022250 | 0.032500 |
| draw1 | llgl | 0.011500 | 0.014900 | 0.029050 |
| draw1000 | graphicssdk | 0.061550 | 0.025650 | 0.098250 |
| draw1000 | sdl | 0.036550 | 0.018950 | 0.072350 |
| draw1000 | llgl | 0.030650 | 0.012700 | 0.080050 |
| instanced1000 | graphicssdk | 0.025050 | 0.019850 | 0.093400 |
| instanced1000 | sdl | 0.016250 | 0.022450 | 0.069400 |
| instanced1000 | llgl | 0.015100 | 0.022150 | 0.077450 |

구간별 P50은 서로 다른 표본 위치의 중앙값이므로 세 P50의 합이 total P50과 같을 필요는 없다. GraphicsSDK의 record P50은 draw1 0.026250 → draw1000 0.061550 ms였고, SDL은 0.016200 → 0.036550 ms, LLGL 프로필은 0.011500 → 0.030650 ms였다. instanced1000의 record P50은 각각 0.025050/0.016250/0.015100 ms다. 1,000회 공개 호출과 단일 instanced 호출의 CPU 기록 비용 차이를 관측했으며, total에는 GPU 진행과 제출·fence 대기도 섞인다. 이 분해만으로 SDK 내부 검증·barrier·driver 중 어느 부분이 차이의 원인인지는 확정할 수 없다.

이는 이번 장치·driver·공개 API 계약의 관측이다. record/submit/wait 분해도 제공하지만 backend 내부 원인이나 SDK 전체 우열을 입증하지 않는다. full clear 경로·fence 제출 방식·API 요청이 다르며 CPU/driver cache·온도·전력·다른 프로세스 활동을 배타적으로 통제하지 않았다. P95/P99는 5개 독립 프로세스의 기술 통계이며 신뢰 구간이 아니다.

## 재검사·남은 범위

새 회귀 테스트 34개와 전체 Python 테스트 **1,109개, 실패·오류·건너뜀 0개**가 통과했다. CPU 구간·native 호출 수·표본·순서·환경 혼입·장치 귀속·원본 변조를 거부하며 실행 전후 입력도 일치했다. [전체 테스트 receipt](../benchmarks/reports/validation/2026-10-29-phase3-three-sdk-draw/python-receipt.json)는 기반 커밋과 staged 입력을 기록하며 최종 커밋 CI와 구분한다.

```text
python tools/compare_llgl_draw.py run --build-root out/NEW_LLGL_DRAW/build --output out/NEW_COMPARISON
python tools/compare_llgl_draw.py check --output out/NEW_COMPARISON --live
python tools/check_llgl_draw_comparison.py
python benchmarks/reports/validation/2026-10-29-phase3-three-sdk-draw/verify.py
python tools/roadmap.py --check
```

새 빌드는 `benchmarks/llgl_vulkan_draw_compare`를 사용하며 승인 LLGL DLL·CMP-04 observer·고정 VVL·SDK/SDL 준비 receipt가 필요하다. [raw receipt](../benchmarks/reports/validation/2026-10-29-phase3-three-sdk-draw/native/receipt.json), [요약](../benchmarks/reports/validation/2026-10-29-phase3-three-sdk-draw/summary.json), [source ZIP](../benchmarks/reports/validation/2026-10-29-phase3-three-sdk-draw/source.zip), [독립 verifier](../benchmarks/reports/validation/2026-10-29-phase3-three-sdk-draw/verify.py)를 보존했다.

독립 verifier는 표준 라이브러리만 사용하고 ZIP 코드를 실행하지 않는다. 원본 source bytes·세 fingerprint·raw 결과·정확한 순서/환경·직렬 PID/구간·현재 장치·전체 이미지 합계·CPU 구간/통계를 재계산한다. 현재 checkout은 Git CRLF/LF만 정규화해 원본과 비교하며 native fingerprint는 ZIP raw bytes를 임시 폴더에 복원해 검사한다. hash는 신뢰 서명이 아니며 offline 검사는 GPU 재실행을 대신하지 않는다. Windows/Linux CI에도 독립 replay를 추가했다.

기존 승인·correctness·업로드 계약을 보존하고 [draw 성능 확장](../benchmarks/phase3_comparison/competitors/llgl-transfer-src-draw-performance.json)만 연결한다. `P3-CMP-08`만 완료하며 다음은 **P3-CMP-09 PSO 성능 비교**다. CMP-10·BEN-09 GPU timestamp·Linux 물리 GPU/표시·최종 gate는 미완료다. 장시간 메모리·Intel 충돌 조사는 재개하지 않았다.
