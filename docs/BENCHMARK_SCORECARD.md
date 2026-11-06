# Graphics SDK 벤치마크 통합 점수표

> 관측 기준: 2026-10-20 ~ 2026-10-24, 저장소 main 브랜치의 고정 벤치마크 보고서.
> **이 점수는 특정 실행 환경과 workload에서 측정한 P50 상대점수이며 SDK의 보편적인 FPS, 전체 품질, 안정성 또는 통계적으로 입증된 우열이 아니다.**
> 원본·원자료를 교체하거나 수정하지 않는다.

## 1. 한눈에 보는 SDK별 점수

**점수 단위: 100점 만점. 높을수록 이 *실험군* 내 해당 작업의 P50 비용이 낮다.**
서로 다른 실험군의 점수는 서로 다른 비교집합·캐시·메모리 정책·측정일을 포함하므로 **열 사이에 직접 순위를 매기지 않는다.**

| SDK | D3D12: Diligent 실험 (8) | D3D12: Forge 실험 (8) | D3D12: bgfx 실험 (6) | Vulkan: NVIDIA (12) | Vulkan: Intel (12) | D3D12 E–H (14) |
|---|---:|---:|---:|---:|---:|---:|
| GraphicsSDK | **90.0** | **67.4** | **88.0** | **64.0** | **62.1** | — |
| LLGL | 89.3 | 66.2 | 78.6 | — | — | **90.2** |
| SDL3 GPU | **90.5** | 69.1 | 86.7 | **94.7** | **98.3** | **97.3** |
| Diligent Core | 73.8 | — | — | — | — | — |
| The Forge | — | **94.4** | — | — | — | — |
| bgfx | — | — | 79.3 | — | — | — |
| NVRHI / Dawn 등 | 미측정 | 미측정 | 미측정 | 미측정 | 미측정 | 미측정 |

- 숫자 뒤 괄호는 **그 실험의 측정 workload 수**다. 동일 작업의 최초·탐색 측정은 최종 표에 중복 반영하지 않았다.
- **—는 데이터 없음/비교 불가이지 0점이 아니다.** bgfx에는 기본 GPU buffer 생성과 명시적 PSO 생성이 없어 8종 공통 실험과 동일한 종합점수가 아니다.
- Vulkan NVIDIA/Intel에서는 GraphicsSDK와 SDL3 두 구현만 측정했다. 다른 SDK의 Vulkan 점수를 추정하지 않는다.
- 이 통합표는 의도적으로 **전체 SDK 통합 1위**를 선정하지 않는다.

## 2. 채점 원칙과 계산식

하나의 비교 캠페인에서 동일 workload를 실행한 구현들의 관측된 P50 가운데 최솟값을 기준으로 한다.

**workload 점수 = 100 × (그 캠페인 해당 workload의 최저 P50) / (평가할 SDK의 P50).**

- 값이 낮은 CPU wall time 또는 CPU API 생성 비용일수록 높은 점수다. 최단 P50 SDK만 해당 행에서 100점을 받는다.
- 카테고리 점수는 포함된 workload 점수의 **산술평균**이다.
- 캠페인 종합점수는 측정된 카테고리 점수의 **동일 가중 산술평균**이다.
- D3D12 카테고리: **Allocation** (buffer + texture; bgfx는 texture만), **Draw** (draw + instanced), **Upload** (1080p + 4K RGBA8 + 4K RGBA16F), **PSO** (pipeline; bgfx 제외).
- Vulkan 카테고리: **Draw** (draw1, draw1000, instanced1000), **Upload** (3종), **PSO 생성** (pso1, pso8), **리소스 생성** (buffer1, buffer1000, texture1, texture1000).
- E–H 카테고리: **Sprites** (2), **Mesh** (2), **Compute** (5), **Video** (5); 카테고리 평균의 동일 가중 평균.
- 점수는 표시할 때 소수 첫째 자리로 반올림한다. 최종 계산은 반올림 전 값으로 수행한다.

**중요:** 점수의 단위는 프로젝트별 상대점수다. 실험군별 최솟값이 다르므로 다른 실험군의 90점과 90점은 동일한 절대 성능을 의미하지 않는다. 세 캠페인 D3D12 결과를 하나의 절대 순위로 합치지 않는다.

## 3. Windows D3D12: Diligent 캠페인 (8종)

[원본 Diligent 보고서](DILIGENT_BENCHMARK_REPORT.md) · i7-14700K / GTX 1660 SUPER / 2026-10-20

| SDK | Allocation (2) | Draw (2) | Upload (3) | PSO (1) | **종합** |
|---|---:|---:|---:|---:|---:|
| SDL3 GPU | 83.2 | 96.2 | 94.7 | 87.8 | **90.5** |
| GraphicsSDK | 89.0 | 99.1 | 97.0 | 75.0 | **90.0** |
| LLGL | 97.5 | 98.2 | 61.4 | 100.0 | **89.3** |
| Diligent Core | 64.9 | 98.9 | 95.3 | 36.0 | **73.8** |

- Draw P50: Diligent 0.513750 ms, GraphicsSDK 0.521150 ms, LLGL 0.519950 ms, SDL3 0.555800 ms — 이 제한된 10k workload의 차이는 작다.
- Pipeline P50: LLGL 0.003600 ms, GraphicsSDK 0.004800 ms, Diligent 0.010000 ms. 드라이버·캐시 cold/warm 상태는 통제하지 않았다.

## 4. Windows D3D12: The Forge 캠페인 (8종)

[원본 Forge 보고서](FORGE_BENCHMARK_REPORT.md) · i7-14700K / GTX 1660 SUPER / 2026-10-21

| SDK | Allocation (2) | Draw (2) | Upload (3) | PSO (1) | **종합** |
|---|---:|---:|---:|---:|---:|
| The Forge | **100.0** | **99.6** | 98.6 | 79.5 | **94.4** |
| SDL3 GPU | 3.1 | 95.3 | 96.5 | 81.6 | **69.1** |
| GraphicsSDK | 3.1 | 93.5 | **99.2** | 73.8 | **67.4** |
| LLGL | 3.1 | 95.7 | 65.9 | **100.0** | **66.2** |

**매우 중요한 해석:** The Forge는 D3D12MA와 미리 준비된 메모리 풀 등의 기본 정책을 사용한다. 배치 buffer 1,000개의 end-to-end P50은 Forge **8.077900 ms** 대 GraphicsSDK **234.160650 ms**, texture 1,000개는 Forge **6.193150 ms** 대 GraphicsSDK **219.601100 ms**다. 같은 논리 작업의 실제 측정이지만 native allocation 횟수·캐시·풀 정책이 서로 달라 **실제 GPU 연산 성능이 29~35배라는 뜻은 아니다**. 이 실험의 Allocation 카테고리가 종합점수를 크게 끌어올린다. Draw와 Upload 점수도 반드시 따로 확인한다.

또한 Forge의 실제 adapter 구성은 VS 2022 호환 설정, 구 NVAPI 배제, 일부 native readback 검증 사용이라는 조건을 가진다. 공식 전체 엔진 기능 검증으로 일반화하지 않는다.

## 5. Windows D3D12: bgfx 캠페인 (6종)

[원본 bgfx 보고서](BGFX_BENCHMARK_REPORT.md) · i7-14700K / GTX 1660 SUPER / 2026-10-21

| SDK | Texture 생성 (1) | Draw (2) | Upload (3) | PSO | **측정 범위 평균** |
|---|---:|---:|---:|---:|---:|
| GraphicsSDK | 66.1 | 97.8 | **100.0** | — | **88.0** |
| SDL3 GPU | 66.8 | 96.3 | 97.0 | — | **86.7** |
| bgfx | **100.0** | 66.2 | 71.7 | — | **79.3** |
| LLGL | 66.4 | **100.0** | 69.3 | — | **78.6** |

- bgfx는 texture 1,000개 P50 **147.540500 ms**로 해당 실험의 다른 라이브러리보다 낮았다.
- draw 10k는 bgfx **0.955000 ms**, GraphicsSDK **0.499200 ms**였다. 이것은 bgfx가 모든 렌더링에서 두 배 느리다는 뜻은 아니다. 명령 인코딩·정렬·backend 제출 구조가 다르다.
- bgfx dynamic vertex buffer 1,000개 풀링 결과 **5.329050 ms**는 별도 resource policy이므로 기본 buffer batch 점수에서 제외했다.
- bgfx 공개 createProgram은 native PSO 생성과 동일하지 않아 pipeline 점수는 **미측정**이다.

## 6. Windows Vulkan: GraphicsSDK 대 SDL3

서로 다른 GPU 모델의 수치를 섞지 않고 NVIDIA와 Intel에서 각각 채점했다. 모든 값은 해당 GPU에서 두 SDK만의 상대점수다.

| GPU | SDK | Draw (3) | Upload (3) | PSO 생성 (2) | Resource 생성 (4) | **평균** |
|---|---|---:|---:|---:|---:|---:|
| NVIDIA GTX 1660 SUPER | SDL3 GPU | **100.0** | **100.0** | **100.0** | 78.6 | **94.7** |
| NVIDIA GTX 1660 SUPER | GraphicsSDK | 76.0 | 65.0 | 63.9 | 50.9 | **64.0** |
| Intel UHD 770 | SDL3 GPU | **100.0** | 98.3 | **100.0** | 94.9 | **98.3** |
| Intel UHD 770 | GraphicsSDK | 31.1 | 86.3 | 77.6 | 53.3 | **62.1** |

원본: [draw](PHASE3_VULKAN_DRAW_BENCHMARK_REPORT.md), [upload](PHASE3_VULKAN_UPLOAD_REPORT.md), [PSO](PHASE3_VULKAN_PSO_BENCHMARK_REPORT.md), [allocation](PHASE3_VULKAN_ALLOCATION_BENCHMARK_REPORT.md).

- NVIDIA draw1: GraphicsSDK **0.1224 ms** / SDL3 **0.0847 ms**.
- Intel draw1: GraphicsSDK **1.1378 ms** / SDL3 **0.2586 ms**. 특히 Intel의 GraphicsSDK CPU recording 구간이 길게 관측되어 추가 진단 우선순위가 높다.
- Vulkan PSO 점수는 **생성 CPU API 구간만** 사용했다. public handle release는 SDK별 지연 회수 정책이 달라 제외했다.
- Resource 생성 점수도 **생성 API P50만** 사용했다. 실제 GPU 메모리 회수 속도·물리 allocation 수는 아니다. buffer의 기본 정책 차이가 점수에 강하게 반영된다.
- macOS Metal, Linux Vulkan, 다른 GPU 모델로 결과를 일반화하지 않는다.

## 7. D3D12 Phase 1 E–H: LLGL 대 SDL3

CPU 완료 시간 기반, 14개 public semantic workload. 두 SDK의 네이티브 shader binding ABI는 완전히 같지 않다.

| SDK | Sprites (2) | Mesh (2) | Compute (5) | Video (5) | **평균** |
|---|---:|---:|---:|---:|---:|
| SDL3 GPU | **100.0** | 94.0 | **99.4** | **95.9** | **97.3** |
| LLGL | 97.1 | **99.2** | 92.3 | 72.3 | **90.2** |

원본: [Phase 1 SDL3 비교 보고서](PHASE1_SDL_COMPARISON_REPORT.md), [14개 workload P50/P95/P99](../benchmarks/reports/2026-10-21-phase1-sdl/TABLE.md).

- video_4k P50: SDL3 **2.890150 ms** / LLGL **5.861750 ms**.
- video_p010 P50: LLGL **8.362850 ms** / SDL3 **10.489500 ms**. 영상 작업 전반의 일방적 승리는 아니다.
- 이 E–H 벤치마크에는 GraphicsSDK, Forge, Diligent, bgfx가 참가하지 않았다.

## 8. 점수에 넣지 않은 측정과 미완료 범위

- [D3D12 GPU timestamp](GPU_TIMESTAMP_BENCHMARK_REPORT.md): GraphicsSDK·LLGL·Forge는 동일 draw interval을 비교할 수 있지만 Diligent는 시작 query resolve가 측정 구간에 포함된다. SDL3와 bgfx에는 동일 공개 측정 API가 없다. **CPU wall 시간 점수에 GPU timestamp를 합산하지 않았다.**
- [CPU/메모리](CPU_MEMORY_BENCHMARK_REPORT.md): 프로세스 사용률은 회차 기반이고 GPU 체크포인트는 transient peak의 하한이다. 동일한 안정성·메모리 효율 종합점수로 바꾸지 않았다.
- [LLGL 복구된 public 업로드](LLGL_UPLOAD_BENCHMARK_REPORT.md): 기존 WriteTexture 검증 오류와 달리 공개 buffer-to-texture 경로로 재검증했다. 다른 revision/캠페인 결과를 섞지 않았다.
- Phase 1/2/3의 검증·리소스 수명·device loss·GBV 결과는 별도의 [ROADMAP](ROADMAP.md)과 보고서에서 관리한다. 장기 GBV 메모리 및 과거 Intel AV 문제의 완료 여부를 이 점수로 인증하지 않는다.
- NVRHI, Dawn, Metal, Linux runtime과 실사용 게임·UI·영상 편집 워크로드는 공통 실측값이 없으므로 **점수를 만들지 않았다.**

## 9. SDK별 읽기 쉬운 결론과 개선 방향

| SDK | 실제 관측된 장점 | 확인된 제약 / 다음 질문 |
|---|---|---|
| **GraphicsSDK** | 일부 D3D12 draw/upload가 경쟁군과 근접하거나 최단 P50 | Vulkan draw/PSO/리소스 생성 특히 Intel recording 비용 분석; memory allocator 정책 비교 |
| **LLGL** | 일부 D3D12 pipeline·draw 비용 경쟁력, Phase 1 mesh 결과 양호 | 복구된 upload 경로의 staging/대기 비용, Vulkan과 Metal은 이번 동일 실측 미포함 |
| **SDL3 GPU** | D3D12/Vulkan 여러 영역에서 일관된 낮은 CPU 완료 시간, E–H compute/video 강점 | 모든 저수준 기능·GPU timestamp 직접 API를 제공하는 SDK는 아님 |
| **The Forge** | 현재 Forge 캠페인의 1,000개 리소스 배치 생성 P50 매우 낮음 | 풀/캐시 정책을 분리한 반복 검증 필요; 빌드·설정 및 배포 라이선스 주의 |
| **bgfx** | 텍스처 생성 비용 낮음 | 같은 조건의 10k draw 비용 높음; 명시적 PSO/버퍼 생성은 비교 불가 |
| **Diligent Core** | 같은 D3D12 draw/업로드 작업에서 근접한 결과 | 해당 실험의 반복 pipeline·allocation 비용; Metal 별도 상용 정책 |
| **NVRHI / Dawn** | 아키텍처 조사 자료는 있음 | 동일 측정 계약의 실측 벤치마크 부재 |

**GraphicsSDK 우선 과제:** (1) Intel Vulkan CPU recording 병목 분리 측정, (2) Vulkan PSO create 경로의 캐시/핸들·root-signature 대응 비용 확인, (3) allocation policy·native allocation trace와 공개 API 시간을 함께 제시, (4) D3D12 draw/upload 품질 유지, (5) Linux Vulkan·macOS Metal과 실제 UI/게임 workload 확보.

## 10. 측정 환경과 출처

- CPU: Intel Core i7-14700K; GPU: NVIDIA GeForce GTX 1660 SUPER, Vulkan 별도 Intel UHD 770.
- OS: Windows 10.0.26220; toolchain: MSVC 19.44, x64 Release /O2, /MD, LTO 없음.
- 기본 실험: headless, VSync 해당 없음, validation OFF, 단일 caller, 한 frame-in-flight.
- 독립 프로세스 반복과 이미지/버퍼 readback 검증을 사용했지만 OS background load, GPU clock/power, driver cache cold/warm은 충분히 통제하지 않았다.
- 각 행의 P50과 원자료 JSON/CSV는 연결된 개별 보고서에 보존되어 있다.
- 원본 비교 문서: [BENCHMARK_REPORT](BENCHMARK_REPORT.md), [COMPETITOR_ANALYSIS](COMPETITOR_ANALYSIS.md), [FEATURE_MATRIX](FEATURE_MATRIX.md).

> **재현성 원칙:** 새 측정 시 기존 score를 무조건 갱신하지 말고 campaign ID, GPU, workload contract revision, 결과 경계, SDK pin, 표본 수와 제외 사유를 먼저 맞춘다. 비교 조건이 다르면 같은 파일에서도 별도 열로 기록한다.
