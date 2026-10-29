# P3-CMP-09 세 SDK Vulkan PSO 생성·공개 해제 성능 비교

2026-10-29. **P3-CMP-09 완료**. GraphicsSDK·고정 SDL·사용자 승인 **LLGL 649c0cc + transfer-src patch v1**을 같은 Windows NVIDIA에서 새로 실행했다. **58개 프로세스, 새 생성/해제 표본 쌍 900개 / CPU 값 1,800개, 전체 이미지 1,082회 / 10,408,840바이트**를 검증했다. public PSO 생성·사용·해제 각각 4,869개다. 과거 시간 표본은 재사용하지 않았다.

## 소스·현재 장치

실행 기반 커밋은 `ff49dcc66f37316a2bb596fb2d438b936b85e504`다. 새 LLGL 소비자·runner의 실제 실행 바이트를 source ZIP·fingerprint에 연결하며 기반 커밋에 이미 새 소비자가 있었다는 뜻은 아니다. SDK/SDL은 `646c2cfa5980b881b455970e0dd9990b95bfbc35`에서 만든 기존 빌드를 실제 compiler/linker closure·설치 SDK·SDL static·소비자·현재 제품 소스 일치 감사 후 재사용했다. 이번에 SDK/SDL을 재빌드했다는 주장은 하지 않는다.

제품 SDK 28파일은 변경하지 않았다. SDK fingerprint `197bd38300429ca10e77214bae33a269ec938cf78f112a66fb3182a37298582b`, SDL pin `3b2bc951500ca6a1f428af77e036e732d9a8afe3`다. 새 LLGL 소비자는 Windows x64 Release·MSVC `/O2 /MD`·IPO OFF이며 compiler/linker 입력 436개·전체 동결 입력 1803개를 실행 전·각 프로세스 전·실행 후 검사했다. LLGL 승인 10줄 patch와 CMP-04 observer DLL은 그대로다. 원본 upstream과 패치 프로필을 구분하고 외부 EXE/DLL/lib/driver는 재배포하지 않는다.

fresh probe와 모든 성공 실행의 장치 UUID·driver UUID·LUID·native ID/driver/API·CPU/RAM/OS/DXGI 환경이 일치했다. **NVIDIA GeForce GTX 1660 SUPER**, device UUID `be4ae3472ccd6deb68ccf2e33670e7bd`, driver UUID `61fefcd074265598aa87bdd69012bf6a`, LUID `3a82010000000000`, driverVersion 2604679168다. 과거 LUID를 현재 귀속으로 대신 사용하지 않았다.

SDK/SDL은 유일한 정확한 이름의 native Vulkan·DXGI 대조 방식이며 SDL native handle 직접 확인은 미완료다. LLGL은 실제 renderer native device/physical device/queue와 DXGI를 대조한다. SDK/SDL application API 요청은 소스상 1.1 (4198400), LLGL은 별도 관측에서 1.4.357 (4211045)이다. API 요청까지 동일하다고 주장하지 않으며 성능 실행 중 요청을 직접 관측하지 않았다.

## 고정 PSO 작업·시간 경계

기존 [PSO 계약](../benchmarks/phase3_comparison/pso.json)을 사용한다. `pso1`/`pso8`은 매 반복 각각 1개/8개 PSO를 새로 공개 생성한다. shader는 기존 fullscreen vertex·red/green fragment SPIR-V이며 반복과 stripe에 따라 색을 바꾼다. 65×37 RGBA8, scissor 영역 `[5,3,55,31]`을 정수 edge 함수로 1개/8개 stripe로 나눈다. 모든 PSO를 각 stripe에 바인딩해 3 vertices를 그린 뒤 GPU fence 완료·idle을 확인하고 공개 해제를 호출한다. 이후 전체 9,620바이트 readback을 CPU 참조와 비교한다.

`create_ms`에는 공개 pipeline 생성과 필요한 shader 객체 생성·해제가 포함된다. SDK는 createGraphicsPipeline 내부 shader 처리, SDL/LLGL은 공개 shader 생성·pipeline 생성·shader 해제를 사용한다. descriptor·SPIR-V payload 준비와 pipeline 보관 vector 할당은 timer 밖이다. LLGL의 live shader handle을 descriptor에 연결하는 작업과 반환 값·오류 report 확인은 공개 생성 경로에 포함한다. public handle 고유성 검사·lifecycle counter·SHA 계산은 timer 밖이다.

`public_release_ms`는 완료된 GPU 사용 후 공개 pipeline 핸들 해제 호출만 측정한다. GPU draw·fence/idle·readback·자원 준비·종료·warmup·validation·observer 실행은 두 구간 밖이다. 두 구간 사이에 GPU 작업이 있으므로 두 값은 연속 total의 분할이 아니다. native pipeline 생성/파괴 수, 물리 GPU 메모리 회수 시점, allocator/cache 상태와 GPU timestamp는 측정하지 않았다.

LLGL은 CMP-05의 GPU 사용·readback·종료 매핑을 그대로 사용하고 create/release에만 CPU 경계를 추가했다. 자동 offscreen pass+full attachment clear와 ColorAttachment|Sampled|CopySrc를 사용한다. SDK/SDL은 기존 load-op clear 경로다. GPU use는 timer 밖이지만 backend의 render-pass 호환성 처리·내부 cache·shader 처리·해제 지연 정책까지 동일하게 만든 비교는 아니다. workload를 native Vulkan으로 우회하거나 vendor patch를 늘리지 않았다.

모든 SDK가 같은 descriptor/shader를 반복 생성하는 **uncontrolled driver cache repeated public creation** 계약이다. 5회 warmup 후 retained 표본을 수집하며 cold-cache·cache miss 비용 또는 driver cache 상태를 안다고 표시하지 않는다. 한 프로세스 안의 반복 표본을 서로 독립된 장치 초기화로 취급하지 않는다.

## 검증·새 통계

성능 전에 fresh probe 3개, observer-only probe 1개, LLGL observer workload 2개, required core+sync validation 6개, LLGL core/sync 실제 진단 2개, 의도한 거부 14개가 통과했다. 거부는 세 SDK별 missing required layer/wrong target 6개와 LLGL sync 비활성화·missing pipeline·skip release·wrong pipeline·skip region·wrong scissor·readback 변조·observer 비활성화 8개다. SDK/SDL missing-layer는 기존 소비자의 SDK validation 선행 단계 거부다. 잘못된 sync 진단 명령은 record만 하고 제출하지 않았다.

LLGL audit·validation 16프레임의 native draw 72회·instances 72개·vertex arguments 216개·bad arguments 0, 종료 후 live instance/device 0을 관측했다. 이는 **PSO 사용의 draw 관측**이며 native pipeline 생성 개수 검증은 아니다. 성능 프로세스의 native draw counts는 `null`이고 직접 관측했다고 표시하지 않는다. 전체 이미지 검증은 모든 warmup과 retained 표본에도 적용했다.

그 뒤 두 입력×세 SDK×독립 5회, **30개 측정 프로세스**를 직렬 실행했다. 프로세스별 warmup 5회·retained 30쌍, 입력/SDK당 150쌍이다. seed 20261027로 시나리오 블록과 서로 다른 SDK 순열 5개를 배정한다. 6개 순열의 완전 균형 설계는 아니다. validation 18쌍·observer audit 6쌍·측정 warmup 150회는 통계에서 제외하고 retained 표본·이상치 삭제는 0개다.

상속 VK/VVL/SDL/GFX 설정을 제거하고 implicit layers를 비활성화했다. 성능에는 빈 layer/settings 경로와 validation 미로드를 확인하며 LLGL은 runtime modules에서 observer·VVL 미로드도 확인한다. correctness는 고정 VVL DLL·core/sync 진단과 전체 changing stripe SHA-256을 확인한다.

P50/P95/P99는 `(N-1)×q` 선형 보간, MAD는 P50으로부터 절대 편차의 중앙값이다. 12개 입력/SDK/column 조합의 min/max·프로세스별 중앙값과 모든 원본을 [CSV](../benchmarks/reports/validation/2026-10-29-phase3-three-sdk-pso/metrics.csv)·JSON에 보존한다. 단위 ms다.

| 입력 | SDK | CPU 구간 | P50 ms | P95 ms | P99 ms | MAD ms |
|---|---|---|---:|---:|---:|---:|
| pso1 | GraphicsSDK | create_ms | 0.108650 | 0.118810 | 0.143448 | 0.004850 |
| pso1 | GraphicsSDK | public_release_ms | 0.003900 | 0.004755 | 0.004900 | 0.000500 |
| pso1 | 고정 SDL | create_ms | 0.079950 | 0.094615 | 0.119726 | 0.005550 |
| pso1 | 고정 SDL | public_release_ms | 0.000100 | 0.000200 | 0.000351 | 0.000000 |
| pso1 | LLGL 649c0cc + transfer-src patch v1 | create_ms | 0.085800 | 0.100955 | 0.104616 | 0.012150 |
| pso1 | LLGL 649c0cc + transfer-src patch v1 | public_release_ms | 0.002850 | 0.003700 | 0.003851 | 0.000650 |
| pso8 | GraphicsSDK | create_ms | 0.232000 | 0.255855 | 0.394612 | 0.014100 |
| pso8 | GraphicsSDK | public_release_ms | 0.007900 | 0.009100 | 0.009551 | 0.000900 |
| pso8 | 고정 SDL | create_ms | 0.128200 | 0.159510 | 0.239339 | 0.004150 |
| pso8 | 고정 SDL | public_release_ms | 0.000200 | 0.000300 | 0.000400 | 0.000000 |
| pso8 | LLGL 649c0cc + transfer-src patch v1 | create_ms | 0.127400 | 0.150355 | 0.154173 | 0.010450 |
| pso8 | LLGL 649c0cc + transfer-src patch v1 | public_release_ms | 0.004500 | 0.005800 | 0.006402 | 0.000600 |

- `pso1 / create_ms` P50 관측: 고정 SDL 0.079950 ms → LLGL 649c0cc + transfer-src patch v1 0.085800 ms → GraphicsSDK 0.108650 ms.
- `pso8 / create_ms` P50 관측: LLGL 649c0cc + transfer-src patch v1 0.127400 ms → 고정 SDL 0.128200 ms → GraphicsSDK 0.232000 ms.
- `pso1 / public_release_ms` P50 관측: 고정 SDL 0.000100 ms → LLGL 649c0cc + transfer-src patch v1 0.002850 ms → GraphicsSDK 0.003900 ms.
- `pso8 / public_release_ms` P50 관측: 고정 SDL 0.000200 ms → LLGL 649c0cc + transfer-src patch v1 0.004500 ms → GraphicsSDK 0.007900 ms.

`pso8/pso1` create P50 비율은 GraphicsSDK 2.135배, 고정 SDL 1.604배, LLGL 649c0cc + transfer-src patch v1 1.485배다. 이는 입력당 1개와 8개 공개 생성 batch의 차이이며 pipeline당 비용을 직접 분리한 값이나 선형 확장 보장이 아니다. create와 public release의 관측 순서를 각각 제시했으며 합쳐서 SDK 전체 순위를 매기지 않는다. shader 처리·내부 cache·release 지연 정책 중 무엇이 차이의 원인인지 이 CPU 값만으로 확정할 수 없다. CPU/driver cache·온도·전력·다른 프로세스 활동을 배타적으로 통제하지 않았다. P95/P99는 5개 독립 프로세스의 기술 통계이며 신뢰 구간이 아니다.

`pso8` create에서 SDL과 LLGL 프로필의 P50 차이는 0.000800 ms로 각 MAD(0.004150/0.010450 ms)보다 작다. 이 근소한 중앙값 순서로 지속적인 우위를 판정할 수 없다. SDL 공개 해제는 매우 짧은 호출 구간이라 timer 해상도·측정 overhead의 상대적 영향도 크다. 실제 native 파괴·물리 회수 시점은 별도 증거가 필요하다.

## 재검사·남은 범위

새 회귀 테스트 38개와 전체 Python 테스트 **1,147개, 실패·오류·건너뜀 0개**가 통과했다. CPU 구간·native 호출 수·표본·순서·환경 혼입·장치 귀속·원본 변조를 거부하며 실행 전후 입력도 일치했다. [전체 테스트 receipt](../benchmarks/reports/validation/2026-10-29-phase3-three-sdk-pso/python-receipt.json)는 기반 커밋과 staged 입력을 기록하며 최종 커밋 CI와 구분한다.

```text
python tools/compare_llgl_pso.py run --build-root out/NEW_LLGL_PSO/build --output out/NEW_COMPARISON
python tools/compare_llgl_pso.py check --output out/NEW_COMPARISON --live
python tools/check_llgl_pso_comparison.py
python benchmarks/reports/validation/2026-10-29-phase3-three-sdk-pso/verify.py
python tools/roadmap.py --check
```

새 빌드는 `benchmarks/llgl_vulkan_pso_compare`이며 승인 LLGL DLL·CMP-04 observer·고정 VVL·SDK/SDL 준비 receipt가 필요하다. [raw receipt](../benchmarks/reports/validation/2026-10-29-phase3-three-sdk-pso/native/receipt.json), [요약](../benchmarks/reports/validation/2026-10-29-phase3-three-sdk-pso/summary.json), [source ZIP](../benchmarks/reports/validation/2026-10-29-phase3-three-sdk-pso/source.zip), [독립 verifier](../benchmarks/reports/validation/2026-10-29-phase3-three-sdk-pso/verify.py)를 보존했다.

독립 verifier는 표준 라이브러리만 사용하며 ZIP 코드를 실행하지 않는다. ZIP raw source·세 fingerprint·실행 순서/환경·직렬 PID/구간·장치·pipeline public lifecycle·stripe CPU 참조·원본 표본/통계를 재계산한다. 현재 checkout은 Git CRLF/LF만 정규화해 비교하며 native fingerprint는 ZIP raw bytes를 임시 폴더에 복원해 검사한다. hash는 신뢰 서명이 아니며 offline 검사는 GPU 재실행을 대신하지 않는다. Windows/Linux CI에도 새 독립 replay를 추가했다.

기존 승인·correctness·업로드·draw 계약은 그대로 보존하고 [PSO 성능 확장](../benchmarks/phase3_comparison/competitors/llgl-transfer-src-pso-performance.json)만 연결한다. `P3-CMP-09`만 완료하며 다음은 **P3-CMP-10 자원 생성 성능 비교**다. BEN-09 GPU timestamp·Linux 물리 GPU/표시·최종 gate는 미완료다. 장시간 메모리·Intel 충돌 조사는 재개하지 않았다.
