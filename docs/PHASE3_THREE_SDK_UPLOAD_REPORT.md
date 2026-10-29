# P3-CMP-07 세 SDK Vulkan 업로드 성능 비교

2026-10-29. **P3-CMP-07 완료**. 현재 GraphicsSDK·고정 SDL·사용자 승인 `LLGL 649c0cc + transfer-src patch v1`을 동일 Windows NVIDIA에서 새로 검증·측정했다. 총 **87개 프로세스, 새 성능 표본 1,800개, 전체 readback 2,148회 / 57,908,372,340바이트**가 통과했다. 과거 시간 표본은 재사용하지 않았다.

## 입력·장치 귀속

실행 기반 커밋은 `a6b93b524d8db6e63e35ddc40ea060a62b9f13d9`다. 새 LLGL 측정 소비자와 runner의 실제 실행 소스 바이트를 source ZIP·fingerprint에 연결한다. 이 기반 커밋에 새 소비자가 이미 포함돼 있었다는 주장은 아니다. 기존 SDK/SDL 빌드는 `646c2cfa5980b881b455970e0dd9990b95bfbc35`에서 만들었으며, 실제 compiler/linker closure·설치 SDK·SDL static·소비자·현재 제품 소스가 그대로인지 실행 전후 감사해 재사용했다. SDK/SDL을 이번에 재빌드했다고 표시하지 않는다.

제품 SDK 28파일과 기존 SDK/SDL C++ 소비자는 변경하지 않았다. SDK fingerprint는 `197bd38300429ca10e77214bae33a269ec938cf78f112a66fb3182a37298582b`, SDL pin은 `3b2bc951500ca6a1f428af77e036e732d9a8afe3`다. 새 LLGL 소비자는 Windows x64 Release·MSVC·`/O2 /MD`·IPO OFF이며 두 translation unit의 실제 compiler/linker 입력을 감사했다. LLGL DLL은 승인된 10줄 patch 프로필 그대로다. 고정 원본을 수정 프로필로 바꾸어 표시하지 않는다.

세 SDK의 fresh probe와 모든 성공 실행에서 장치 UUID·driver UUID·LUID·native ID/driver/API·CPU/RAM/OS/DXGI 환경이 일치했다. GPU는 **NVIDIA GeForce GTX 1660 SUPER**, device UUID `be4ae3472ccd6deb68ccf2e33670e7bd`, driver UUID `61fefcd074265598aa87bdd69012bf6a`, LUID `3a82010000000000`, driverVersion 2604679168다. 옛 준비 기록의 LUID를 현재 값으로 재작성하거나 현재 귀속의 근거로 그대로 재사용하지 않았다.

SDK/SDL 장치 귀속은 유일한 정확한 이름의 native Vulkan·DXGI 일치 방식이며 **SDL native handle 직접 확인은 아니다**. LLGL은 실제 renderer의 native device/physical device/queue를 조회하고 DXGI와 대조했다. SDK/SDL은 명시적 application API 1.1을 사용한다. LLGL의 API 요청은 별도 관측 probe에서 **1.4.357 (4211045)**을 확인했다. 정상 측정에서는 관측 레이어를 제거했으며 당시 API 요청을 직접 관측했다고 표시하지 않는다. API 요청까지 동일하다는 주장은 하지 않는다.

## 동일 작업·시간 경계

입력은 65×37 RGBA8, 1920×1080 RGBA8, 3840×2160 RGBA8, 3840×2160 RGBA16F다. 기존 BEN-03 계약과 같은 payload 함수를 사용하며 반복마다 내용을 바꾼다. 모든 warmup·측정 이미지의 모든 바이트를 GPU readback과 비교한다. RGBA16F는 기존 finite half 값만 사용한다. LLGL required validation에서는 기존 독립 CPU reference SHA-256도 재확인했다. CPU 참조 상수의 재사용은 과거 timing의 재사용과 구분한다.

측정은 staging map·CPU payload 복사 → command record → submit → 업로드 fence 완료의 **end-to-end wall time**이다. payload 생성, 전체 readback·비교, 자원 준비·종료, validation과 warmup은 통계 밖이다. readback은 매 반복 사이에 수행하므로 검증이 포함된 실행 주기의 비교다. 연속 streaming 최대 처리량이나 GPU timestamp/PCIe bandwidth가 아니다.

SDK는 padded staging, SDL/LLGL은 tight staging이다. 65×37은 정렬 경계의 고정 overhead까지 포함한다. 기존 LLGL correctness의 공개 `Upload` 매핑을 그대로 사용했다. LLGL map/unmap의 내부 staging copy·동기화 등 공개 API 내부 비용도 구간에 포함한다. backend 내부 barrier·allocation·cache·native usage/layout까지 같게 만들거나 native Vulkan으로 workload를 대신하지 않았다. source·측정 경계를 맞춘 공개 API 결과의 비교다.

모든 측정 프로세스에서 상속 VK/VVL/SDL/GFX 설정을 제거하고 implicit layer를 비활성화했다. 빈 layer/settings 경로를 사용하고 validation DLL 미로드를 확인한다. LLGL은 실제 runtime modules와 observer DLL 미로드도 확인한다. 별도 관측 probe의 비용과 validation 실행 시간은 성능 표본에 넣지 않는다.

## 실행 순서·새 통계

성능 전에 fresh probe 3개, observer-only probe 1개, 필수 core+sync validation 12개, LLGL core/sync 진단 2개, 거부 9개를 모두 통과했다. 거부는 세 SDK별 missing required layer·wrong target 6개와 LLGL sync 비활성화·readback 변조·observer 비활성화 3개다. SDK/SDL missing-layer는 기존 소비자의 SDK required-validation 선행 단계 거부를 의미한다. 잘못된 native sync 진단 명령은 record만 하고 제출하지 않았다.

그 뒤 4종×세 SDK×독립 반복 5회, 총 **60개 측정 프로세스**를 직렬 실행했다. 각 프로세스는 warmup 5회와 retained 표본 30개다. seed 20261027로 시나리오 블록을 섞고 시나리오별 세 SDK 순서의 6개 순열 중 서로 다른 5개를 회차에 배정했다. 5회이므로 모든 순서를 완전히 균형 배치했다고 주장하지 않는다.

각 입력/SDK는 5개 프로세스·150개 표본이다. validation 출력 표본 36개와 측정 warmup 300회는 통계에서 제외했다. 표본·이상치 삭제는 0개다. P50/P95/P99는 `(N-1)×q` 선형 보간, MAD는 P50으로부터 절대 편차의 중앙값이다. min/max·프로세스별 중앙값도 JSON/CSV에 보존한다. 단위 ms, 표시는 소수점 6자리다.

| 입력 | SDK | P50 ms | P95 ms | P99 ms | MAD ms |
|---|---|---:|---:|---:|---:|
| odd | GraphicsSDK | 0.197950 | 0.831030 | 1.930802 | 0.051050 |
| odd | 고정 SDL | 0.119250 | 0.705110 | 1.245397 | 0.025700 |
| odd | LLGL + transfer-src patch v1 | 0.260350 | 1.477445 | 4.110216 | 0.060600 |
| upload1080 | GraphicsSDK | 2.421350 | 10.462735 | 16.290186 | 0.550800 |
| upload1080 | 고정 SDL | 2.218400 | 10.853200 | 21.051547 | 0.473200 |
| upload1080 | LLGL + transfer-src patch v1 | 2.767750 | 13.496815 | 18.199223 | 0.619900 |
| upload4k | GraphicsSDK | 19.483650 | 41.909100 | 63.587264 | 3.853400 |
| upload4k | 고정 SDL | 9.977200 | 31.021185 | 39.852695 | 1.522750 |
| upload4k | LLGL + transfer-src patch v1 | 25.748800 | 41.113090 | 44.237599 | 1.931950 |
| upload4k16f | GraphicsSDK | 42.818200 | 68.522325 | 108.288916 | 5.464150 |
| upload4k16f | 고정 SDL | 34.759750 | 91.863285 | 150.031128 | 12.751150 |
| upload4k16f | LLGL + transfer-src patch v1 | 58.666050 | 77.486940 | 92.123265 | 7.200650 |

- `odd` P50 관측 순서: 고정 SDL 0.119250 ms → GraphicsSDK 0.197950 ms → LLGL + transfer-src patch v1 0.260350 ms.
- `upload1080` P50 관측 순서: 고정 SDL 2.218400 ms → GraphicsSDK 2.421350 ms → LLGL + transfer-src patch v1 2.767750 ms.
- `upload4k` P50 관측 순서: 고정 SDL 9.977200 ms → GraphicsSDK 19.483650 ms → LLGL + transfer-src patch v1 25.748800 ms.
- `upload4k16f` P50 관측 순서: 고정 SDL 34.759750 ms → GraphicsSDK 42.818200 ms → LLGL + transfer-src patch v1 58.666050 ms.

이 순서는 이번 GPU·driver·공개 업로드 계약의 P50 관측이다. SDK 전체 순위나 차이 원인·최적화 완료로 확대하지 않는다. CPU/driver cache·온도·전력·다른 프로세스 활동을 배타적으로 통제하지 않았으며, 실행 순서를 바꿔도 그런 변동을 제거했다고 할 수 없다. P95/P99는 적은 독립 프로세스의 기술 통계이며 신뢰 구간이 아니다. draw·PSO·자원 생성 성능은 별도 gate다.

## 감사·재검사

새 회귀 테스트 30개와 전체 Python 테스트 **1,075개, 실패·오류·건너뜀 0개**가 통과했다. 통계·순서·프로세스 누락/추가·검증/observer 혼입·LUID·readback·표본·오류 통제·원본 변조를 거부한다. 실행 전후 입력도 일치했다. [전체 테스트 receipt](../benchmarks/reports/validation/2026-10-29-phase3-three-sdk-upload/python-receipt.json)는 기반 커밋과 staged 입력을 기록하며 최종 커밋의 CI 결과와 구분한다.

새 소비자의 compiler/linker 입력은 433개, 전체 동결 입력은 1805개다. 실제 명령·PID·단조 시계 구간·stdout/stderr·환경 override·원본 표본·runtime hash를 보존한다. 이전 SDK/SDL compiler closure는 보수적인 입력 목록으로, 이번 실행에서 새로 컴파일하거나 실제 symbol 도달성을 분석했다는 뜻은 아니다. source·라이선스·메타데이터만 보존하며 외부 DLL/EXE/lib/driver는 재배포하지 않는다.

독립 verifier는 ZIP 코드를 실행하지 않고 source ZIP 원본 바이트, 세 소스 fingerprint, 모든 raw 결과·순서·프로세스 직렬 구간·환경 분리·현재 장치 귀속·readback 합계와 통계를 재계산한다. native fingerprint는 실제 Windows 바이트로 검사한다. 현재 checkout 소스는 Git의 줄바꿈 차이만 정규화해 비교하고 ZIP 원본은 별도 임시 폴더에 복원한다. checkout과 과거 증거는 재작성하지 않는다. metadata hash는 외부 신뢰 서명이 아니며 offline 재검사는 GPU 재실행을 대신하지 않는다.

```text
python tools/compare_llgl_upload.py run --build-root out/NEW_LLGL_UPLOAD/build --output out/NEW_COMPARISON
python tools/compare_llgl_upload.py check --output out/NEW_COMPARISON --live
python tools/check_llgl_upload_comparison.py
python benchmarks/reports/validation/2026-10-29-phase3-three-sdk-upload/verify.py
python tools/roadmap.py --check
```

새 LLGL 빌드는 `benchmarks/llgl_vulkan_upload_compare`를 사용하며 승인 DLL·CMP-04 observer build·고정 VVL과 감사된 기존 SDK/SDL 준비 receipt가 필요하다. [raw receipt](../benchmarks/reports/validation/2026-10-29-phase3-three-sdk-upload/native/receipt.json), [요약](../benchmarks/reports/validation/2026-10-29-phase3-three-sdk-upload/summary.json), [CSV](../benchmarks/reports/validation/2026-10-29-phase3-three-sdk-upload/metrics.csv), [source ZIP](../benchmarks/reports/validation/2026-10-29-phase3-three-sdk-upload/source.zip), [독립 verifier](../benchmarks/reports/validation/2026-10-29-phase3-three-sdk-upload/verify.py)를 참조한다.

기존 승인·correctness 계약은 그대로 보존하고 [업로드 성능 확장](../benchmarks/phase3_comparison/competitors/llgl-transfer-src-upload-performance.json)만 새로 연결한다. `P3-CMP-07`만 완료한다. 다음은 `P3-CMP-08` draw/instancing 비교이며 `P3-CMP-09`~`10`, `P3-BEN-09` GPU timestamp, Linux 물리 GPU와 최종 gate는 미완료다. 장시간 메모리/Intel 충돌 조사는 재개하지 않았다.
