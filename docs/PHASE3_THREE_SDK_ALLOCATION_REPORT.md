# P3-CMP-10 세 SDK Vulkan 자원 생성·공개 해제 성능 비교

2026-10-29. **P3-CMP-10 완료**. GraphicsSDK·고정 SDL·사용자 승인 **LLGL 649c0cc + transfer-src patch v1**을 같은 Windows NVIDIA에서 새로 실행했다. **98개 프로세스, 새 생성/해제 표본 쌍 1,800개 / CPU 값 3,600개, 전체 자원 readback 1,083,082개 / 7,427,776,356바이트**를 검증했다. 이미지와 buffer 각각 541,541개이며 공개 자원 생성·사용·해제 각각 1,083,082개다. 과거 시간 표본은 재사용하지 않았다.

## 소스·현재 장치

실행 기반 커밋은 `4f37f2030a712d206718b18d568206c63b9c7063`다. 새 LLGL 소비자·runner의 실제 실행 바이트를 source ZIP·fingerprint에 연결한다. 기반 커밋에 이미 새 소비자가 있었다는 뜻은 아니다. SDK/SDL은 `646c2cfa5980b881b455970e0dd9990b95bfbc35`에서 만든 기존 빌드를 실제 compiler/linker closure·설치 SDK·SDL static·소비자·현재 제품 소스 일치 감사 후 재사용했다. 이번에 SDK/SDL을 재빌드했다는 주장은 하지 않는다.

제품 SDK 28파일은 변경하지 않았다. SDK fingerprint `197bd38300429ca10e77214bae33a269ec938cf78f112a66fb3182a37298582b`, SDL pin `3b2bc951500ca6a1f428af77e036e732d9a8afe3`다. 새 LLGL 소비자는 Windows x64 Release·MSVC `/O2 /MD`·IPO OFF이며 compiler/linker 입력 435개·전체 동결 입력 1803개를 실행 전·각 프로세스 전·실행 후 확인했다. LLGL 승인 10줄 patch와 CMP-04 observer DLL은 그대로다. 원본 upstream과 패치 프로필을 구분하고 외부 EXE/DLL/lib/driver는 재배포하지 않는다.

fresh probe와 모든 성공 실행의 장치 UUID·driver UUID·LUID·native ID/driver/API·CPU/RAM/OS/DXGI 환경이 일치했다. **NVIDIA GeForce GTX 1660 SUPER**, device UUID `be4ae3472ccd6deb68ccf2e33670e7bd`, driver UUID `61fefcd074265598aa87bdd69012bf6a`, LUID `3a82010000000000`, driverVersion 2604679168다. 과거 LUID를 현재 귀속으로 대신 사용하지 않았다.

SDK/SDL은 유일한 정확한 이름의 native Vulkan·DXGI 대조 방식이며 SDL native handle 직접 확인은 미완료다. LLGL은 실제 renderer native device/physical device/queue와 DXGI를 대조한다. SDK/SDL application API 요청은 소스상 1.1 (4198400), LLGL은 별도 관측에서 1.4.357 (4211045)이다. API 요청까지 동일하다고 주장하지 않으며 성능 실행 중 요청을 직접 관측하지 않았다.

## 고정 자원 작업·시간 경계

기존 [자원 계약](../benchmarks/phase3_comparison/allocation.json)의 `buffer1`, `buffer1000`, `texture1`, `texture1000`을 사용한다. buffer는 4,096바이트 device resource, texture는 65×37 RGBA8 sampled 2D·mip/layer/sample 각 1이다. 매 반복 모두 새 공개 자원을 생성하고 resource index·iteration·byte index에 따라 변하는 전체 payload를 업로드한 뒤 다운로드하여 모든 바이트를 CPU 참조와 비교한다. 전체 readback에는 모든 warmup과 retained 표본도 포함한다.

`create_ms`는 공개 device resource 생성, `public_release_ms`는 완료된 GPU 사용과 readback 후 공개 핸들 해제 호출의 CPU 시간이다. descriptor·vector 준비, staging 생성/해제, payload 생성·쓰기, GPU copy·fence/idle, 전체 readback·비교, native handle 조회·고유성 검사·counter·SHA, 종료·warmup·validation은 두 구간 밖이다. 두 구간 사이에 작업이 있으므로 두 값은 연속 total의 분할이 아니다. 공개 생성·해제 호출 내부 비용은 포함한다.

buffer usage는 SDK general device buffer, SDL vertex buffer, LLGL VertexBuffer|CopySrc|CopyDst다. texture는 sampled RGBA8이며 LLGL의 실제 native image extent/format/mip/layer/sample/transfer usage와 buffer/image handle 형식을 확인했다. LLGL은 초기 데이터 없이 기본 생성 정책을 사용하며 backend 기본 초기화 비용도 공개 create 호출에 포함될 수 있다. native usage bit·heap·suballocation·내부 초기화·allocator cache까지 동일하게 만든 비교는 아니다.

LLGL은 CMP-06의 공개 copy·완료 대기·전체 payload 검증·정리 경로를 유지하고 create/release CPU 경계만 추가했다. 이 고정 LLGL의 공개 CopyBuffer에는 필요한 transfer barrier가 없어 **업로드와 다운로드를 별도 완료된 제출**로 나눈다. 각 fence 완료·idle은 timer 밖이며 vendor patch를 늘리거나 workload를 native Vulkan으로 우회하지 않았다. 공개 해제 후 idle도 timer 밖에서 확인한다. SDK/SDL은 기존 단일 copy pass/command 경로다. SDK texture staging은 footprint의 padded row pitch를 사용하고 SDL/LLGL은 tight layout을 사용한다. 이 제출·staging·동기화 주기 차이는 다음 반복의 상태에 영향을 줄 수 있어 결과 해석에 남긴다.

LLGL은 모든 성공 반복에서 live public resource와 native buffer/image handle을 timer 밖에서 **366,366회** 검사했다. 각 batch 안에서 동시에 살아 있는 자원들이 서로 다른지 확인한 누적 검사 횟수이며, 해제 후 다른 반복에서 같은 handle 값이 재사용될 수 있다. 전체 실행의 handle 값이 전부 다르다는 뜻은 아니다. 이는 native 자원 객체 고유성 검증이며 VkDeviceMemory 할당 횟수·물리 페이지 수·memory 회수 시점 검증은 아니다. SDK/SDL에 같은 직접 native resource 검사 결과가 있다고 표시하지 않는다. allocator/cache 상태는 모른다. 공개 해제 수치가 짧아도 즉시 물리 메모리를 반환한다는 뜻은 아니다.

고정 소스의 동작도 확인했다. LLGL `VKRenderSystem::CreateTexture`는 이 descriptor의 NoInitialData 미설정 경로에서 기본 image data를 만들고 업로드하며 `VKDevice::FlushCommandBuffer`에서 fence 완료를 기다린다. `Release(Buffer&)`/`Release(Texture&)`는 내부 `SyncGPU` → queue idle을 거친다. 이 내부 초기화·동기화는 **공개 호출 안에 있으므로 측정에 포함**된다. 외부 검증용 staging·copy·fence/idle을 timer 밖에 둔다는 계약과 구분한다. `initial_data_supplied=false`는 호출자가 초기 데이터를 주지 않았다는 뜻이며 backend의 기본 초기화 부재를 뜻하지 않는다.

고정 SDL의 공개 release 경로는 내부 texture/buffer를 pending-destroy 목록에 등록하고, 별도 정리 루프가 reference count를 확인하여 파괴한다. 따라서 짧은 release CPU 시간은 native 파괴 완료 시간과 다르다. 이 내용은 [실행 입력 hash와 연결한 소스 검토](../benchmarks/reports/validation/2026-10-29-phase3-three-sdk-allocation/source-semantics.json)의 정적 경로 확인이며 내부 호출 횟수·native allocation 수를 실측했다는 뜻은 아니다. 개별 timing 차이의 인과적 기여량은 분리하지 않았다.

## 검증·새 통계

성능 전에 fresh probe 3개, observer-only probe 1개, LLGL observer workload 4개, required core+sync validation 12개, LLGL core/sync 실제 진단 2개, 의도한 거부 16개가 통과했다. 거부는 세 SDK별 missing required layer/wrong target 6개와 LLGL sync 비활성화·missing resource·alias resource·skip release·wrong resource payload·stale iteration·skip copy·readback 변조·short readback·observer 비활성화 10개다. SDK/SDL missing-layer는 기존 소비자의 SDK validation 선행 단계 거부다. 잘못된 sync 진단 명령은 record만 하고 제출하지 않았다.

LLGL audit·validation의 32회 iteration에서 native draw 0과 종료 후 live instance/device 0을 관측했다. observer는 instance API 요청·수명·불필요 draw 부재 확인용이며 native copy/메모리 할당 수를 관측했다고 주장하지 않는다. 성능에서는 observer·VVL을 제거하고 native draw count는 `null`로 기록한다. 성능의 full payload 비교는 계속 수행하되 별도 SHA 계산만 생략한다.

그 뒤 네 입력×세 SDK×독립 5회, **60개 측정 프로세스**를 직렬 실행했다. 프로세스별 warmup 5회·retained 30쌍, 입력/SDK당 150쌍이다. seed 20261027로 시나리오 블록과 서로 다른 SDK 순열 5개를 배정한다. 6개 순열의 완전 균형 설계는 아니다. validation 36쌍·observer audit 12쌍·측정 warmup 300회는 통계에서 제외하고 retained 표본·이상치 삭제는 0개다.

상속 VK/VVL/SDL/GFX 설정을 제거하고 implicit layers를 비활성화했다. 성능은 빈 layer/settings 경로와 validation 미로드를 확인하며 LLGL은 runtime modules에서 observer·VVL 미로드도 확인한다. correctness는 고정 VVL DLL·core/sync 진단과 모든 changing resource payload SHA-256을 확인한다.

P50/P95/P99는 `(N-1)×q` 선형 보간, MAD는 P50으로부터 절대 편차의 중앙값이다. 24개 입력/SDK/column 조합의 min/max·프로세스별 중앙값과 원본 전체를 [CSV](../benchmarks/reports/validation/2026-10-29-phase3-three-sdk-allocation/metrics.csv)·JSON에 보존한다. 단위 ms다.

| 입력 | SDK | CPU 구간 | P50 ms | P95 ms | P99 ms | MAD ms |
|---|---|---|---:|---:|---:|---:|
| buffer1 | GraphicsSDK | create_ms | 0.094550 | 0.209175 | 0.362280 | 0.019550 |
| buffer1 | GraphicsSDK | public_release_ms | 0.183550 | 0.391000 | 1.300593 | 0.021700 |
| buffer1 | 고정 SDL | create_ms | 0.002300 | 0.004200 | 0.006161 | 0.000600 |
| buffer1 | 고정 SDL | public_release_ms | 0.000200 | 0.000300 | 0.000351 | 0.000000 |
| buffer1 | LLGL 649c0cc + transfer-src patch v1 | create_ms | 0.662550 | 4.245470 | 6.479150 | 0.138850 |
| buffer1 | LLGL 649c0cc + transfer-src patch v1 | public_release_ms | 0.202750 | 3.105735 | 4.400529 | 0.026850 |
| buffer1000 | GraphicsSDK | create_ms | 101.882800 | 145.069970 | 156.289668 | 24.598700 |
| buffer1000 | GraphicsSDK | public_release_ms | 266.123150 | 424.362950 | 486.790541 | 59.608850 |
| buffer1000 | 고정 SDL | create_ms | 0.566750 | 1.021425 | 1.756499 | 0.205000 |
| buffer1000 | 고정 SDL | public_release_ms | 0.124750 | 0.451015 | 0.608718 | 0.062950 |
| buffer1000 | LLGL 649c0cc + transfer-src patch v1 | create_ms | 759.834150 | 948.058560 | 1128.688641 | 90.418000 |
| buffer1000 | LLGL 649c0cc + transfer-src patch v1 | public_release_ms | 2.399500 | 4.611625 | 6.318042 | 0.716600 |
| texture1 | GraphicsSDK | create_ms | 0.144500 | 0.222955 | 0.274567 | 0.032250 |
| texture1 | GraphicsSDK | public_release_ms | 0.190250 | 0.299180 | 0.385702 | 0.022400 |
| texture1 | 고정 SDL | create_ms | 0.286550 | 0.438585 | 0.622787 | 0.036700 |
| texture1 | 고정 SDL | public_release_ms | 0.001200 | 0.002155 | 0.002806 | 0.000200 |
| texture1 | LLGL 649c0cc + transfer-src patch v1 | create_ms | 2.272300 | 3.391980 | 4.040808 | 0.383150 |
| texture1 | LLGL 649c0cc + transfer-src patch v1 | public_release_ms | 0.230700 | 0.477845 | 1.167355 | 0.032450 |
| texture1000 | GraphicsSDK | create_ms | 80.555900 | 148.376465 | 164.725825 | 26.525600 |
| texture1000 | GraphicsSDK | public_release_ms | 201.384850 | 377.498620 | 467.242513 | 40.338550 |
| texture1000 | 고정 SDL | create_ms | 308.246400 | 446.042435 | 532.749974 | 44.027600 |
| texture1000 | 고정 SDL | public_release_ms | 0.596500 | 1.166295 | 2.674393 | 0.289400 |
| texture1000 | LLGL 649c0cc + transfer-src patch v1 | create_ms | 1856.040600 | 2336.241625 | 2415.821229 | 358.675850 |
| texture1000 | LLGL 649c0cc + transfer-src patch v1 | public_release_ms | 6.284850 | 11.508140 | 17.219949 | 1.285350 |

- `buffer1 / create_ms` P50 관측: 고정 SDL 0.002300 ms → GraphicsSDK 0.094550 ms → LLGL 649c0cc + transfer-src patch v1 0.662550 ms.
- `buffer1000 / create_ms` P50 관측: 고정 SDL 0.566750 ms → GraphicsSDK 101.882800 ms → LLGL 649c0cc + transfer-src patch v1 759.834150 ms.
- `texture1 / create_ms` P50 관측: GraphicsSDK 0.144500 ms → 고정 SDL 0.286550 ms → LLGL 649c0cc + transfer-src patch v1 2.272300 ms.
- `texture1000 / create_ms` P50 관측: GraphicsSDK 80.555900 ms → 고정 SDL 308.246400 ms → LLGL 649c0cc + transfer-src patch v1 1856.040600 ms.
- `buffer1 / public_release_ms` P50 관측: 고정 SDL 0.000200 ms → GraphicsSDK 0.183550 ms → LLGL 649c0cc + transfer-src patch v1 0.202750 ms.
- `buffer1000 / public_release_ms` P50 관측: 고정 SDL 0.124750 ms → LLGL 649c0cc + transfer-src patch v1 2.399500 ms → GraphicsSDK 266.123150 ms.
- `texture1 / public_release_ms` P50 관측: 고정 SDL 0.001200 ms → GraphicsSDK 0.190250 ms → LLGL 649c0cc + transfer-src patch v1 0.230700 ms.
- `texture1000 / public_release_ms` P50 관측: 고정 SDL 0.596500 ms → LLGL 649c0cc + transfer-src patch v1 6.284850 ms → GraphicsSDK 201.384850 ms.

- `buffer1000/buffer1` create P50 비율: GraphicsSDK 1077.555배, 고정 SDL 246.413배, LLGL 649c0cc + transfer-src patch v1 1146.833배.
- `texture1000/texture1` create P50 비율: GraphicsSDK 557.480배, 고정 SDL 1075.716배, LLGL 649c0cc + transfer-src patch v1 816.811배.

이는 입력당 1개와 1,000개 공개 생성 batch의 관측 차이이며 자원당 비용을 직접 분리하거나 선형 확장을 보장한 값은 아니다. create/public release의 관측 순서를 나누어 제시했으며 SDK 전체 순위로 합치지 않는다. 내부 allocator·cache·기본 초기화·해제 지연 정책 중 무엇이 차이의 원인인지는 이 CPU 값만으로 확정할 수 없다. 짧은 공개 해제 구간은 timer 해상도·overhead의 상대적 영향도 크다. CPU/driver cache·온도·전력·다른 프로세스 활동을 배타적으로 통제하지 않았다. P95/P99는 5개 독립 프로세스의 기술 통계이며 신뢰 구간이 아니다.

## 재검사·남은 범위

새 회귀 테스트 40개와 전체 Python 테스트 **1,187개, 실패·오류·건너뜀 0개**가 통과했다. CPU 구간·native 호출 수·표본·순서·환경 혼입·장치 귀속·원본 변조를 거부하며 실행 전후 입력도 일치했다. [전체 테스트 receipt](../benchmarks/reports/validation/2026-10-29-phase3-three-sdk-allocation/python-receipt.json)는 기반 커밋과 staged 입력을 기록하며 최종 커밋 CI와 구분한다.

```text
python tools/compare_llgl_allocation.py run --build-root out/NEW_LLGL_ALLOCATION/build --output out/NEW_COMPARISON
python tools/compare_llgl_allocation.py check --output out/NEW_COMPARISON --live
python tools/check_llgl_allocation_comparison.py
python benchmarks/reports/validation/2026-10-29-phase3-three-sdk-allocation/verify.py
python tools/roadmap.py --check
```

새 빌드는 `benchmarks/llgl_vulkan_allocation_compare`이며 승인 LLGL DLL·CMP-04 observer·고정 VVL·SDK/SDL 준비 receipt가 필요하다. [raw receipt](../benchmarks/reports/validation/2026-10-29-phase3-three-sdk-allocation/native/receipt.json), [요약](../benchmarks/reports/validation/2026-10-29-phase3-three-sdk-allocation/summary.json), [source ZIP](../benchmarks/reports/validation/2026-10-29-phase3-three-sdk-allocation/source.zip), [독립 verifier](../benchmarks/reports/validation/2026-10-29-phase3-three-sdk-allocation/verify.py)를 보존했다.

독립 verifier는 표준 라이브러리만 사용하며 ZIP 코드를 실행하지 않는다. ZIP raw source·세 fingerprint·실행 순서/환경·직렬 PID/구간·장치·public resource lifecycle·모든 원본 표본/통계를 재계산한다. CPU 참조는 uint32 wrap을 적용해 전체 payload를 별도로 재생성한다. 현재 checkout은 Git CRLF/LF만 정규화해 비교하며 native fingerprint는 ZIP raw bytes를 임시 폴더에 복원해 검사한다. hash는 신뢰 서명이 아니며 offline 검사는 GPU 재실행을 대신하지 않는다. Windows/Linux CI에도 새 독립 replay를 추가했다.

기존 승인·correctness·업로드·draw·PSO 계약을 보존하고 [자원 성능 확장](../benchmarks/phase3_comparison/competitors/llgl-transfer-src-allocation-performance.json)만 연결한다. `P3-CMP-10`만 완료한다. 이후 Linux 비교는 실제 Linux GPU 환경이 필요하며 `P3-BEN-07`~`09`, `P3-LBEN-01`~`04`, 최종 gate는 미완료다. 이번 Windows 값이나 Docker software 검증으로 Linux hardware 완료를 대신하지 않는다. 장시간 메모리·Intel 충돌 조사는 재개하지 않았다.
