# P3-BEN-09 SDK·SDL draw native GPU timestamp 비교

2026-10-29. **P3-BEN-09 완료**. 기존 CPU 비교와 분리한 전용 계측 프로세스에서 SDK·고정 SDL의 같은 native render-pass 구간을 실제 NVIDIA GPU counter로 검증·측정했다. **111개 프로세스, 새 GPU 표본 900개, 계측 영향용 CPU 표본 2,700개, 전체 이미지 3,206회 / 30,841,720바이트**를 보존했다. CPU wait/total을 GPU 시간으로 대체하지 않았다. Linux 물리 GPU와 Phase 3 전체 수용은 별도 미완료다.

## 범위·소스·실제 장치

[사용자 범위 결정](PHASE3_DRAW_SCOPE.md)에 따라 기존 BEN-04 CPU 계약을 보존하고 새 GPU 계측을 별도 수행했다. 고정 SDL 공개 API를 확장하거나 SDK·SDL·vendor 소스를 수정하지 않았다. 기존 draw 실행 파일을 사용하고 자체 pass-through Vulkan layer만 추가했다. 원래 draw·pipeline·scissor·render pass 호출은 다음 layer로 그대로 전달하며, 계측 모드에서 GPU query reset과 두 timestamp만 삽입한다.

실행 기반 커밋은 `a2f10ee090b6aad90d131a5013dda815039537e2`이며 새 레이어·runner의 실제 실행 바이트를 source ZIP·fingerprint로 연결한다. 그 기반 커밋에 이미 새 레이어가 있었다는 뜻은 아니다. SDK/SDL은 `646c2cfa5980b881b455970e0dd9990b95bfbc35`의 기존 빌드를 compiler/linker closure·설치 SDK·SDL static·현재 제품 소스 일치 감사 후 재사용했다. SDK/SDL을 이번에 재빌드했다는 주장은 하지 않는다. SDK 28파일 fingerprint는 `197bd38300429ca10e77214bae33a269ec938cf78f112a66fb3182a37298582b`, SDL pin은 `3b2bc951500ca6a1f428af77e036e732d9a8afe3`, 기존 draw 소비자 fingerprint는 `e11454fe0f0fd09fd796c66b4994c296a711411a9f5cc39f121d83998c342a27`다.

새 레이어는 Windows x64 Release·MSVC `/O2 /MD`·IPO OFF다. compiler/linker 입력 282개와 동결 입력 1335개를 실행 전·각 프로세스 전·실행 후 확인했다. [CPU 보존 기록](../benchmarks/reports/validation/2026-10-29-phase3-draw-gpu/cpu-campaign-preservation.json)은 기존 draw 계약·범위 결정·BEN-04/CMP-08 raw receipt·승인 프로필이 기반 Git blob과 같은 바이트임을 확인한다. 그 과거 시간 표본은 새 통계에 넣지 않았다. 새 원본에는 실제 PID·직렬 단조 시계 구간·argv·환경·stdout/stderr·native query JSON·소스/모듈 해시를 연결했다. 외부 EXE/DLL/lib/driver는 재배포하지 않는다.

GPU는 **NVIDIA GeForce GTX 1660 SUPER**, device UUID `be4ae3472ccd6deb68ccf2e33670e7bd`, driver UUID `61fefcd074265598aa87bdd69012bf6a`, LUID `3a82010000000000`, driverVersion 2604679168다. 모든 성공 caller의 native/DXGI·CPU/RAM/OS metadata를 fresh probe와 대조했다. 레이어에서는 실제 renderer `vkCreateDevice`의 physical device properties/UUID/LUID, command-pool family, 실제 `vkQueueSubmit` queue와 fence를 연결했다. GPU 계측의 이 native 귀속은 기존 CPU 보고서의 이름 기반 SDL 귀속을 소급해서 바꾸지 않는다.

두 SDK의 실제 application API 요청은 1.1 (4198400)이다. 실제 관측된 queue family는 [0], timestamp valid bits는 [64], period는 [1] ns다. period는 counter 단위이며 정확도·실효 해상도를 보장하는 값은 아니다. Native device/command/queue/fence handle과 uint64 counter는 정밀도 손실이 없도록 십진 문자열로 기록했다.

## GPU 구간·완료·참조

기존 65×37 RGBA8·scissor `[5,3,55,31]`·fullscreen triangle 3 vertices·first vertex/instance 0을 사용한다. `draw1`은 1 call×1 instance, `draw1000`은 1,000 calls×1 instance, `instanced1000`은 1 call×1,000 instances다. 반복마다 red/green triangle과 blue/cyan clear를 바꾸고 모든 warmup·표본의 전체 이미지 9,620바이트를 기존 CPU 참조와 비교한다. 레이어도 각 draw의 인자와 scissor, render area·pipeline bind·call/instance/vertex 합계를 검사한다.

GPU 시작 marker는 native `vkCmdBeginRenderPass` 직전 TOP_OF_PIPE, 끝 marker는 `vkCmdEndRenderPass` 직후 BOTTOM_OF_PIPE다. render-pass clear·draw·pass end를 포함한다. pass 이전 transition, 자원 준비, readback과 CPU 기록/제출/대기를 이 GPU 구간으로 합치지 않는다. pipeline 의존성·GPU scheduling·이전 작업의 진행과 겹침까지 배타적으로 분리한 순수 shader 실행 시간은 아니다. 같은 위치의 marker 사이 elapsed time 비교다.

각 pass에 독립 2-slot timestamp pool을 만들고 GPU command로 reset한다. 실제 제출 fence의 성공한 wait와 signaled 상태를 관측한 뒤 `vkGetQueryPoolResults`를 64_BIT|WITH_AVAILABILITY로 읽는다. **WAIT/PARTIAL은 사용하지 않는다.** 양쪽 availability·VK_SUCCESS를 요구하며, 폭 [64]에 맞춘 `(end-start)&((1<<valid_bits)-1)`에 실제 period를 곱해 ms로 변환한다. host submit→completed-fence 구간이 counter 전체 wrap 주기보다 짧아야 하며 GPU elapsed가 그 host 구간을 초과하지 않는지 0.001 ms 허용치로 확인한다. 이 host 시간은 sanity bound이며 GPU 값의 대체물이 아니다.

pool은 제출 완료 후 읽고 파괴하며 생성/파괴 수와 instance/device retirement를 확인한다. 관측만 모드는 native 작업·fence 귀속을 기록하지만 query pool·counter·GPU ms는 `null`이다. 비계측 모드는 자체 레이어와 native report 자체가 없다. 고정 SDL/SDK 공개 API나 자체 기존 timestamp 기능만으로 상대 SDK의 계측을 대신하지 않았다. [Vulkan timestamp 설명](https://docs.vulkan.org/samples/latest/samples/api/timestamp_queries/README.html), [query 결과·availability 규칙](https://docs.vulkan.org/refpages/latest/refpages/source/vkGetQueryPoolResults.html)을 기준으로 구현했다.

## 실제 validation·거부 대조

core+sync required validation에서 두 SDK×세 draw 시나리오 6개와 관측만 2개가 통과했다. 레이어 순서는 자체 계측 → 고정 VVL → driver이며 실제 loader callstack도 저장·검사했다. 추가 query 명령도 VVL을 거치도록 했다. [공식 loader layer ordering](https://github.com/KhronosGroup/Vulkan-Loader/blob/main/docs/LoaderApplicationInterface.md)의 배치를 따른다. VVL DLL·settings·sync enable과 caller 오류 0·정상 full reference를 함께 검사했다.

- 끝 marker 누락 2개: 원래 draw/전체 이미지와 fence 완료는 정상이나 실제 query 결과가 VK_NOT_READY, availability `[1,0]`으로 남아 GPU 측정에서 거부된다.
- 잘못된 구간 2개: 두 marker를 pass 뒤에 기록하면 이미지가 정상이어도 `start_before_render_pass=false`와 boundary fault로 거부한다. 이 짧은 값도 정상 workload GPU 표본으로 채택하지 않는다.
- timestamp layer 누락 2개: caller의 이미지 검증은 성공하지만 native counter report가 없어 GPU 측정으로 채택하지 않는다.
- required VVL 누락 2개·wrong target 2개: 정확한 실패 원인과 nonzero exit·표본 부재를 검사한다. SDL required-layer preflight도 기존 SDK 선행 경로를 유지한다.
- core/callback 양성 대조 1개: host-only 잘못된 sType 질의를 하고 실제 `VUID-VkPhysicalDeviceProperties2-sType-sType` callback ID를 자체 messenger와 stderr에 기록했다. 정상 GPU workload를 기록·제출하지 않고 device를 정리해 실패 반환한다. 단순한 실패 코드만으로 callback 성공을 판정하지 않는다.

이 21개 준비/통제 프로세스 뒤에 성능 90개를 실행했다. 잘못된 GPU command를 제출하는 negative control은 사용하지 않았다.

## 새 GPU 통계·계측 영향

세 입력×두 SDK×세 모드×독립 반복 5회, 총 90개 성능 프로세스다. 각 프로세스 warmup 5회·retained 30개이며 각 입력/SDK/모드당 150개다. seed 20261029로 시나리오 블록과 SDK/모드 6개 조합의 순서를 섞고 다섯 회차에 서로 다른 순서를 배정했다. 완전히 균형 잡힌 모든 순열 설계는 아니다. validation/control과 warmup 450 이미지는 통계 밖이며 retained 표본·이상치 삭제는 0개다.

GPU 통계는 timestamp 모드 30개 프로세스의 실제 counter 표본 **900개**다. CPU 영향 통계는 세 모드 90개 프로세스의 새 total 표본 **2,700개**다. 기존 CPU 캠페인의 시간을 섞거나 이 계측 CPU 값을 기존 baseline으로 바꾸지 않는다. P50/P95/P99는 `(N-1)×q` 선형 보간, MAD는 P50으로부터 절대 편차의 중앙값이다. min/max·프로세스별 중앙값과 모든 원본은 [CSV](../benchmarks/reports/validation/2026-10-29-phase3-draw-gpu/metrics.csv)·JSON에 보존한다. 단위 ms다.

| 입력 | SDK | GPU P50 ms | P95 ms | P99 ms | MAD ms |
|---|---|---:|---:|---:|---:|
| draw1 | graphicssdk | 0.007920 | 0.009312 | 0.009954 | 0.000368 |
| draw1 | sdl | 0.005120 | 0.006482 | 0.007060 | 0.000448 |
| draw1000 | graphicssdk | 0.040880 | 0.054090 | 0.055944 | 0.001264 |
| draw1000 | sdl | 0.036976 | 0.039968 | 0.040850 | 0.000816 |
| instanced1000 | graphicssdk | 0.037760 | 0.058086 | 0.059233 | 0.000912 |
| instanced1000 | sdl | 0.034304 | 0.054755 | 0.056001 | 0.000800 |

| 입력 | SDK | 비계측 CPU P50 ms | 관측만 CPU P50 ms | timestamp CPU P50 ms | 관측/비계측 | timestamp/비계측 |
|---|---|---:|---:|---:|---:|---:|
| draw1 | graphicssdk | 1.969800 | 1.316850 | 1.099400 | 0.669 | 0.558 |
| draw1 | sdl | 0.273200 | 0.436450 | 0.613050 | 1.598 | 2.244 |
| draw1000 | graphicssdk | 1.308000 | 2.021100 | 1.892450 | 1.545 | 1.447 |
| draw1000 | sdl | 0.526050 | 1.261650 | 1.247650 | 2.398 | 2.372 |
| instanced1000 | graphicssdk | 0.538400 | 0.566600 | 0.971550 | 1.052 | 1.805 |
| instanced1000 | sdl | 0.336550 | 0.621750 | 0.304250 | 1.847 | 0.904 |

위 CPU 비율은 각각의 독립 process 표본 중앙값 비율이며 같은 frame의 paired speedup이나 GPU 보정 계수가 아니다. 관측 비용, per-pass pool 생성/결과 조회/파괴와 두 marker/reset의 영향이 존재한다. 특히 1,000 calls에는 native draw 관측 비용도 반복된다. **GPU marker 자체의 비계측 GPU 시간과 차이를 직접 분리해 빼지 않았고, 그 효과가 0이라고 주장하지 않는다.** 작은 GPU/CPU 구간은 timer/counter 해상도와 driver scheduling의 상대적 영향도 크다.

CPU 중앙값 비율이 1보다 작은 조합도 관측됐다. 이를 음의 계측 overhead나 성능 향상으로 해석하지 않는다. process별 분포·실행 순서·기존 GPU/driver 상태의 변동을 포함한 모드별 기술 통계이며, 계측 자체의 인과적 비용을 정확히 분리한 추정치는 아니다.

이 값은 이번 NVIDIA/driver/고정 draw 구간에서의 계측된 결과다. SDK 전체 우열·다른 hardware 성능·순수 shader 비용 또는 원인 규명을 뜻하지 않는다. CPU/driver cache·온도·전력·다른 process의 GPU 활동을 배타적으로 통제하지 않았다. 다섯 독립 process의 P95/P99는 기술 통계이며 신뢰 구간이 아니다.

## 재검사·남은 범위

새 회귀 테스트 38개와 전체 Python 테스트 **1,225개, 실패·오류·건너뜀 0개**가 통과했다. CPU 구간·native 호출 수·표본·순서·환경 혼입·장치 귀속·원본 변조를 거부하며 실행 전후 입력도 일치했다. [전체 테스트 receipt](../benchmarks/reports/validation/2026-10-29-phase3-draw-gpu/python-receipt.json)는 기반 커밋과 staged 입력을 기록하며 최종 커밋 CI와 구분한다.

```text
cmake -S benchmarks/vulkan_draw_timestamp -B out/NEW_TIMESTAMP/build -A x64
cmake --build out/NEW_TIMESTAMP/build --config Release
python tools/run_draw_gpu_timestamps.py run --build-root out/NEW_TIMESTAMP/build --output out/NEW_GPU_COMPARISON
python tools/run_draw_gpu_timestamps.py check --output out/NEW_GPU_COMPARISON --live
python tools/check_draw_gpu_timestamps.py
python benchmarks/reports/validation/2026-10-29-phase3-draw-gpu/verify.py
python tools/roadmap.py --check
```

승인된 기존 SDK/SDL 준비 receipt·Vulkan-Headers·고정 VVL runtime이 필요하다. [raw receipt](../benchmarks/reports/validation/2026-10-29-phase3-draw-gpu/native/receipt.json), [요약](../benchmarks/reports/validation/2026-10-29-phase3-draw-gpu/summary.json), [source ZIP](../benchmarks/reports/validation/2026-10-29-phase3-draw-gpu/source.zip), [독립 verifier](../benchmarks/reports/validation/2026-10-29-phase3-draw-gpu/verify.py)를 보존했다.

독립 verifier는 표준 라이브러리만 사용하고 ZIP 코드를 실행하지 않는다. ZIP raw source·layer/SDK/caller fingerprint·실제 native identity·queue·fence·availability·uint64 counter 변환·서로 분리한 GPU/CPU 통계·제어 실패를 재검사한다. 현재 checkout은 CRLF/LF만 정규화해 ZIP과 비교하며 native fingerprint는 ZIP raw bytes를 임시 폴더에 복원한다. hash는 신뢰 서명이 아니며 offline replay는 GPU 재실행이 아니다. Windows/Linux CI에도 새 재검사를 추가했다.

`P3-BEN-09`만 완료한다. 이전 범위 결정과 CPU 캠페인은 당시 기록으로 보존한다. Linux 물리 GPU·실제 X11 표시·Linux benchmark/최종 gate는 미완료이며, Docker/software 결과나 이번 Windows timestamp로 대신하지 않는다. 다음 선행 ID 기준 필수 후보는 실제 Linux GPU가 필요한 `P3-LNX-15`다. 장시간 메모리·Intel 충돌 조사는 재개하지 않았다.
