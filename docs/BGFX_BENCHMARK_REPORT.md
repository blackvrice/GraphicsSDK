# bgfx D3D12 비교 추가

측정일: 2026-10-21. 고정 bgfx/bx/bimg를 별도 선택형 benchmark build로 추가했다.
7개 시나리오 × 4개 SDK × 3개 독립 프로세스 = 84개 프로세스, 표본 7,920개를 보존했다. 6개 시나리오는 같은 계약으로 비교하고 bgfx pooled buffer는 별도로 기록했다.

## 환경과 공통 조건

Intel(R) Core(TM) i7-14700K, RAM 68,473,667,584 bytes, NVIDIA GeForce GTX 1660 SUPER, LUID `0:95995`, driver `32.0.16.1088`.
Windows 10.0.26220, MSVC 194435228, x64 Release `/O2 /MD`, LTO 없음, validation OFF, headless. API와 render 처리는 같은 스레드이며 한 작업의 GPU 완료 후 다음 작업을 시작한다.
공식 amalgamated bgfx/bx와 bimg image core를 사용한다. bx의 MSVC 표준 전처리기·버전 옵션 및 SEH 요구에 맞춰 외부 라이브러리는 C++ unwinding을 끈다. ASTC decoding과 video는 이 빌드에서 끈다.
[고정 버전](../benchmarks/competitors/bgfx-build.lock.json)은 bgfx 연구 pin과 그 시각보다 늦지 않은 공식 bx/bimg 커밋이다. upstream에서 보증한 submodule 조합이라는 의미는 아니다.

## 비교 가능한 frame P50

단위 ms. P95/P99/MAD와 각 run P50은 [전체 CSV](../benchmarks/reports/2026-10-21-bgfx-final/frame-percentiles.csv)에 있다.

| 시나리오 | Count | LLGL | SDL3 | bgfx | GraphicsSDK |
|---|---:|---:|---:|---:|---:|
| textures | 1000 | 222.200450 | 220.707850 | 147.540500 | 223.155250 |
| draw | 10000 | 0.480600 | 0.518750 | 0.955000 | 0.499200 |
| instanced | 10000 | 0.395400 | 0.395650 | 0.481350 | 0.398400 |
| upload1080 | 1 | 1.821650 | 1.174950 | 1.657650 | 1.094000 |
| upload4k | 1 | 7.946700 | 5.787650 | 8.056300 | 5.762550 |
| upload4k16f | 1 | 15.498000 | 11.879650 | 15.056100 | 11.681650 |

Draw/instancing은 warmup 20 + 측정 120회/프로세스, 업로드는 warmup 10 + 120회, 할당은 warmup 5 + 30회다.
텍스처 할당은 uninitialized 64×64 RGBA8 1,000개 생성·release·idle이다. Draw는 동일 DXBC, vertex-ID triangle, 64×64 RGBA8, blend/depth/cull OFF로 10,000회 실행한다. Instancing은 3 vertices × 10,000 instances의 한 제출이다.
업로드는 persistent 1080p RGBA8 / 4K RGBA8 / 4K RGBA16F 전체 복사다. GPU timestamp는 모두 null이며 대기 시간을 GPU 실행 시간으로 해석하지 않는다.

## 별도로 기록한 buffer와 미지원 PSO

bgfx dynamic vertex buffer는 64 KiB 논리 영역 1,000개를 풀에서 할당한다. frame P50/P95/P99는 5.329050/6.448480/7.077114 ms다.
이 값은 기본 GPU resource 생성의 native allocation 비용과 직접 비교하지 않는다. `resource_policy`를 다르게 기록하여 자동 비교 도구도 비교를 거부한다. 원본에는 기존 SDK buffer 결과도 있으나 bgfx와 비용 비율을 계산하지 않았다.
`pipeline`은 명시적으로 unsupported다. `createProgram()`은 shader link이며 실제 D3D12 PSO는 첫 draw의 state 조합에서 지연 생성·캐싱된다. 이를 PSO 생성 시간으로 보고하거나 0으로 채우지 않는다.

## 제출 경계와 correctness

bgfx는 인코딩 후 정렬·batching한다. Sequential view를 사용하고 `frame()`이 실행한 뒤 공개 `PlatformData.context/queue`로 전달한 동일 D3D12 direct queue에 fence를 signal/wait한다. 실제 device LUID와 backend에 등록된 queue identity도 검사한다.
record_ms는 bgfx API encoding, submit_ms는 frame의 정렬·backend 기록·제출을 포함한다. 기존 SDK의 부분 타이머와 native API overhead를 직접 동일시하지 않는다. 표는 GPU 완료까지의 end_to_end frame 비용이다.
할당 뒤 idle은 두 개의 빈 bgfx frame으로 resource command와 reclamation을 진행한 후 같은 queue를 기다린다. 내부 pool/cache/지연 회수 비용을 포함한 SDK API 관측값이며 native heap 개수의 비교가 아니다.
업로드는 campaign 전체에서 불변으로 유지하는 source vector를 `makeRef()`로 참조하고 SDK staging을 거친다. source는 완료 대기와 최종 readback까지 살아 있다. 공개 ownership-copy API의 추가 복사를 요구하지 않는다.
공통 DXBC payload를 변경하지 않고 고정 bgfx container v13만 덧붙인다. wrapper는 reflection으로 resource, user attributes/varyings, 잘못된 stage를 거부한다. CPU 계약 테스트 8개를 통과했다.
Draw는 실제 제출 수를 검사하고 측정 전후 빨간 중심·파란 clear 모서리를 readback 확인한다. upload는 측정 전후 모든 byte를 비교한다. primitive 통계를 instance-count 정답으로 사용하지 않는다.
별도 native debug-layer 실행은 7개 시나리오 및 10,000 draw/instance를 통과했다. ERROR/CORRUPTION은 GPU 완료와 종료 단계에서 실패 처리한다. GPU-based validation 실행을 의미하지 않는다.
[검증 원본](../benchmarks/reports/validation/2026-10-21-bgfx-final/summary.json)과 CPU 테스트는 성능 관측에서 분리했다. baseline과 bgfx가 같은 contract·GPU·driver·compiler인지 자동으로 검사했다.

같은 데스크톱에서 고정 seed로 실행 순서를 섞었다. background activity, power/clock, shader/PSO cache cold/warm 상태는 통제하지 않았다. 이 장비와 작업량의 관측값이며 SDK 전체 성능 우위나 통계적 유의성을 주장하지 않는다.

## 산출물

- [2026-10-21-bgfx-final-draw 원본 JSON/CSV](../benchmarks/results/2026-10-21-bgfx-final-draw/summary.json)
- [2026-10-21-bgfx-final-upload 원본 JSON/CSV](../benchmarks/results/2026-10-21-bgfx-final-upload/summary.json)
- [2026-10-21-bgfx-final-allocation 원본 JSON/CSV](../benchmarks/results/2026-10-21-bgfx-final-allocation/summary.json)
- [계약이 일치하는 bgfx ↔ GraphicsSDK 세부 비교](../benchmarks/reports/2026-10-21-bgfx-final/comparison.csv)
- [실행 파일·소스 snapshot·결과 hash](../benchmarks/reports/2026-10-21-bgfx-final/evidence.json)
- [측정한 자체 SDK·하네스 소스](../benchmarks/reports/sources/bgfx-1186aa133764d35e20c08cc934e115e0518ef1646c96f95f4ab30d5e4e6162a1.zip)
- [bgfx/도구 전이 고지와 실제 compiler/linker inventory](../licenses/generated/windows-benchmarks-bgfx-final-release/inventory.json)

초기 `copy()` 기반 2026-10-21-bgfx-draw/upload/allocation campaign은 탐색 자료로 보존하되 최종 표에서 제외했다. 최종 campaign은 makeRef 경로가 포함된 동일 실행 파일로 전 시나리오를 재측정한 결과다.
기존 Diligent 비교·원본 측정·SDK ZIP은 보존했다. The Forge 어댑터, low-level GPU timestamp 공통 비교, cache·CPU/GPU memory 통제 및 전체 A–H는 남아 있다.
