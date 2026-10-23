# Phase 3 — Vulkan 업로드 CPU 구간 진단과 두 최적화 후보의 기각

2026-10-23 Windows x64 / MSVC 19.44. **제품 SDK 소스·공개 API·ABI는 변경하지 않았다.**
새 개발 결과는 CPU API 구간 profiler, 동일 실행 구간의 baseline/candidate/SDL 비교 runner,
원본·명령·통계를 재검사하는 report 도구와 회귀/라이선스 증거다. 두 구현 후보는 측정 후 제외했다.
Phase 3 전체 완료나 SDK 성능 우위를 주장하지 않는다.

## 실행 계약과 귀속

[이전 B-upload 계약](PHASE3_VULKAN_UPLOAD_REPORT.md)을 그대로 사용했다. 동일 Vulkan/Release,
CPU thread 1개, headless, frame-in-flight 1개, 재사용 upload buffer와 texture에 바뀌는 tight
payload를 쓰고 submit fence 완료까지 wall time을 잰다. 매 반복 전체 이미지 readback/비교는
측정 밖이다. RGBA8 1080p/4K와 RGBA16F 4K의 layout, payload, 동기화 경계를 유지했다.
Readback이 반복 사이 cache·온도·메모리 상태에 영향을 주므로 연속 streaming 최대 처리량은 아니다.
Shader compile, 초기 device/resource 생성, GPU timestamp, 표시 및 readback 비용은 primary 구간 밖이다.

기존 실행 파일과 신규 후보 실행 파일은 **같은 benchmark source fingerprint**
`6b3f3835af7d87340ad777f29a4f32bf2ca05c27d08315f057afdf38f370f200`를 가진다. 이전 보고서의 시간 값을 재사용하지 않고
이번 실행에서 기준 SDK와 후보, 고정 SDL을 seed 20261024로 섞어 측정했다. 각 cell은
독립 프로세스 3개, warmup 5회 / 측정 30개이며 총 **1,620개 primary 표본**이다.
16개 required-layer correctness 프로세스(odd-pitch 포함)를 먼저 끝내고 validation 없는
54개 성능 프로세스를 serial 실행했다. 다른 GPU 회귀 검사와 성능 실행을 겹치지 않았다.
Outlier 삭제 없이 P50/P95/P99, MAD, min/max 및 프로세스별 median을 JSON/CSV에 보존한다.
짧은 bounded 실험이며 통계적 유의성, tail 안정성 또는 다른 장치로의 일반화를 보장하지 않는다.

Native UUID/driver UUID/LUID, DXGI, CPU/OS/RAM/compiler와 실제 loaded validation DLL SHA를
고정/검사했다. SDL 장치 귀속은 고유한 정확 이름과 native Vulkan/DXGI 대응이며 SDL native
handle 직접 증명이 아니다. 시스템 설정은 바꾸지 않았다. SDL pin은 `3b2bc951500ca6a1f428af77e036e732d9a8afe3`이다.

## 매핑 유지 실험 — 미채택

`Buffer::write`/begin/record/submit/wait의 인접 steady-clock 경계를 나눠 보니 write와 wait가
큰 비중이었다. 최초의 비어 있지 않은 write에서 upload allocation을 한 번 map하고 allocation
해제 시 unmap하는 작은 실험을 했다. 동일 profile 소스, 장치당 3개 작업, warmup 3 / 측정 15다.

| GPU | 작업 | 기존 write ms | 매핑 유지 write ms |
|---|---|---:|---:|
| NVIDIA GeForce GTX 1660 SUPER | upload1080 | 1.1632 | 1.1312 |
| NVIDIA GeForce GTX 1660 SUPER | upload4k | 7.3712 | 8.0126 |
| NVIDIA GeForce GTX 1660 SUPER | upload4k16f | 19.0257 | 19.5740 |
| Intel(R) UHD Graphics 770 | upload1080 | 0.9963 | 1.2848 |
| Intel(R) UHD Graphics 770 | upload4k | 7.8569 | 7.9946 |
| Intel(R) UHD Graphics 770 | upload4k16f | 20.4328 | 20.1303 |

초기 진단은 이 매핑 변경만으로 뚜렷한 쓰기 비용 감소를 보여주지 않아 제외했다.
이 초기 before/trial 12개 프로세스는 primary 비교와 분리해 보존한다. GPU hardware counter나
driver 내부 원인을 측정한 자료가 아니다. Vulkan의 [map 규약](https://docs.vulkan.org/refpages/latest/refpages/source/vkMapMemory.html)과
[unmap 규약](https://docs.vulkan.org/refpages/latest/refpages/source/vkUnmapMemory.html)은
유지 매핑 자체의 합법성과 별개로 애플리케이션 동기화 책임을 요구한다. 성능 이득을 보장하지 않는다.

## 캐시 가능 Upload 메모리 후보 — 미채택

Native property probe에서 NVIDIA의 coherent/visible type 3은 noncached, type 4는 cached이며,
Intel의 type 1은 noncached, type 2는 cached였다. 기존 코드는 compatible한 첫 coherent type을
선택한다. 고정 SDL Vulkan 소스도 transfer용 cached memory를 우선하므로, Upload에 한해
coherency tier 안에서 cached type을 선호하는 후보를 시험했다. 실제 `memoryTypeBits` 호환성,
coherent 우선, noncoherent fallback, 첫 index tie를 유지하고 Device/Readback 선택과 map/unmap은
그대로 뒀다. 별도 CPU 정책 검사 57 assertions가 경계·mask·fallback을 통과했다.

이는 probe의 native property 가용성, 소스 정책과 관측된 API 시간에 기반한 실험이다.
각 SDK allocation의 실제 native memory type handle을 별도 profiler로 캡처한 증거는 아니다.
Vulkan [메모리 속성 규약](https://docs.vulkan.org/spec/latest/chapters/memory.html)은 cached 여부와
coherence/flush 책임을 구분하며, cached 속성이 이 작업에서 더 빠르다는 보장은 하지 않는다.

| GPU | 작업 | 기존 SDK P50 ms | 캐시 후보 P50 ms | 변화 | SDL P50 ms |
|---|---|---:|---:|---:|---:|
| NVIDIA GeForce GTX 1660 SUPER | upload1080 | 2.8593 | 2.9242 | +2.27% | 2.4002 |
| NVIDIA GeForce GTX 1660 SUPER | upload4k | 14.7117 | 15.4783 | +5.21% | 10.3392 |
| NVIDIA GeForce GTX 1660 SUPER | upload4k16f | 30.8612 | 31.8375 | +3.16% | 25.0291 |
| Intel(R) UHD Graphics 770 | upload1080 | 4.1852 | 3.8133 | -8.89% | 3.2828 |
| Intel(R) UHD Graphics 770 | upload4k | 17.1555 | 17.9873 | +4.85% | 12.5143 |
| Intel(R) UHD Graphics 770 | upload4k16f | 32.9073 | 34.3477 | +4.38% | 29.6388 |

양수는 후보 비용 증가다. 6개 cell 중 5개가 약 2~5% 느려 일관된 개선 근거가 없으므로
후보를 제외했다. Intel 1080p의 감소만으로 전체 정책을 채택하지 않았다. 위 SDL 수치는
동일 실행 구간의 참고 비교이며 전체 SDK 순위나 CPU/GPU/driver 원인 증명이 아니다.

별도 CPU API 진단 12개 프로세스는 warmup 3 / 측정 15다. 아래 값은 각 열의 median으로,
열 median의 합이 total median과 같을 필요는 없다. 개별 표본은 다섯 구간 합이 total과
일치하는지 검사한다. 이 진단 시간은 primary 1,620개 표본에 섞지 않는다.

| GPU | 작업 | 기존 write / wait / total ms | 후보 write / wait / total ms |
|---|---|---:|---:|
| NVIDIA GeForce GTX 1660 SUPER | upload1080 | 1.1237 / 0.8950 / 2.4744 | 1.3270 / 0.9302 / 2.6207 |
| NVIDIA GeForce GTX 1660 SUPER | upload4k | 8.0202 / 4.2899 / 12.6973 | 10.0901 / 3.5078 / 13.8464 |
| NVIDIA GeForce GTX 1660 SUPER | upload4k16f | 19.1152 / 7.8824 / 27.5625 | 17.8468 / 8.5030 / 25.7731 |
| Intel(R) UHD Graphics 770 | upload1080 | 1.2903 / 1.5088 / 3.4540 | 1.0895 / 1.8438 / 3.8364 |
| Intel(R) UHD Graphics 770 | upload4k | 7.6875 / 7.5120 / 16.7713 | 9.2119 / 6.4314 / 16.4911 |
| Intel(R) UHD Graphics 770 | upload4k16f | 18.8010 / 12.8097 / 32.1006 | 19.9841 / 12.8794 / 33.3214 |

begin/record/submit도 원본에 모두 남긴다. write에는 SDK 확인·map·memcpy·필요 시 flush·unmap이,
wait에는 host scheduling과 fence 대기가 포함된다. 순수 memcpy 대역폭이나 GPU 실행 시간으로
해석할 수 없다. Vulkan driver 내부 병목이나 하드웨어 결함은 규명하지 않았다.

## 검증·보존·재현

Primary correctness+measurement에서 **1,954개 전체 이미지 / 69,656,525,120 bytes**,
별도 최신 profile에서 **216개 이미지 / 7,763,558,400 bytes**가 일치했다.
후보 SDK는 통합 55 + Vulkan-only 30 + backend OFF 11 = **96개 선택 CTest**와 설치 소비자
9종을 통과했다. 기존 GPU suite의 이미지·draw·depth·timestamp·presentation counters 및
의도적 validation controls도 검사한다. 이는 후보의 정확성 증거이며 새 제품 릴리스 수용이 아니다.
후보 통합 등록 58개 중 보류 memory 3개는 처음부터 제외했다. Python 252개와 report 변조
controls 6개도 통과했다. Missing notices/변경·추가 library/변경 inventory를 거부한다.

최종 제품 SDK fingerprint는 기준과 같은 `44b92cdf3a4ae01c97af10fe25d57b6dea487bf6ed7826a7f322e338a1d39e25`다.
기준 SDK의 이전 93개 선택 CTest는 과거 증거로 보존하며 이번에 재실행했다고 주장하지 않는다.
캐시 후보 fingerprint는 `f0cc198c5c4aca3130c3705f6b1e6d1857652c27fd64aa48a9d872d5fd7767af`이고, 매핑 후보는
`7f3845c751ae9ac808b9a5089329a8781d53e971afa6da883a88b07a9c97f35f`다.
원래 SDK ZIP 22개는 변경하지 않았다. 새 `vulkan-upload-memory.zip`은 **기각된 실험용 SDK**이며
제품 교체/배포용으로 권장하지 않는다. 후보 소스·CMake·정책 테스트의 원본과 source-only ZIP은
[candidate receipt](../benchmarks/reports/validation/2026-10-23-phase3-vulkan-upload-memory/cached-candidate/receipt.json)에 보존한다.
매핑 후보는 이전 source ZIP과 보존한 Vulkan.cpp 대체본으로 fingerprint를 재구성한다.

후보 [SDK inventory](../licenses/generated/windows-sdk-vulkan-upload-memory-release/inventory.json)는
85 inputs / 2 components / compiler 37·linker 36 logs,
[비교 inventory](../licenses/generated/windows-benchmarks-vulkan-upload-memory-release/inventory.json)는
676 inputs / 21 components / compiler·linker 각 5 logs다. 고정 SDL static closure와 이전 SDK
library를 SHA로 재사용한 보수적 입력 목록이며 symbol reachability나 vendor 재빌드 주장은 아니다.
공개 소스 ZIP에 vendor tree, DLL/EXE/LIB/PDB·VVL runtime을 넣지 않는다.

[원본 결과](../benchmarks/results/2026-10-23-vulkan-upload-memory/results.json),
[요약](../benchmarks/reports/2026-10-23-phase3-vulkan-upload-memory/summary.json),
[CSV](../benchmarks/reports/2026-10-23-phase3-vulkan-upload-memory/metrics.csv),
[증거](../benchmarks/reports/2026-10-23-phase3-vulkan-upload-memory/evidence.json)와
[검증 scripts](../benchmarks/reports/validation/2026-10-23-phase3-vulkan-upload-memory/verification-scripts/verify.py)를 따른다.
Linux runtime/WSI와 interactive 출력은 미검증이다. 장시간 메모리 기준·과거 Intel AV 보류는
변경하지 않았고, 이 실험으로 메모리 수용이나 하드웨어 원인을 확정하지 않았다.
다음 개발 범위는 다른 Vulkan 작업(draw/allocation/PSO) 비교와 Linux native 검증이다.
