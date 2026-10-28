# GraphicsSDK 세부 개발 로드맵

기준일: 2026-10-28 · 기준 코드: `6556b81` · 현재 단계: **Phase 3**

이 문서는 현재 작업 상태표다. 원본 요구사항의 Phase 순서와 기존 검증 범위를 유지하면서 큰 묶음을 독립 작업으로 분할했다. 초기 개발 snapshot 0.0.1이며, Phase 3 또는 v0.1 전체 완료를 의미하지 않는다.

상태 원본은 [ROADMAP_TASKS.json](ROADMAP_TASKS.json), 이전 누적 기록은 [ROADMAP_HISTORY_2026-10-25.md](ROADMAP_HISTORY_2026-10-25.md)이다. 과거 보고서의 당시 미완료 문구는 현재 잔여 목록에 중복 합산하지 않는다.

## 지시·체크 방법

- **ID로 지시:** `P3-LNX-01 진행해줘`, `P3-WSI-03부터 진행해줘`처럼 한 작업을 지정한다.
- **다음 개발:** 현재 Phase의 미완료 필수 작업(Phase 6에서는 후속 최적화) 중 선행 조건을 충족한 작업을 `next_order` 순서로 선택한다. 조건부 작업·보류 조사는 자동 선택하지 않는다.
- **체크 시점:** 각 ID의 완료 기준과 필요한 실행 증거를 충족하면 그 ID만 완료로 바꾼다. 큰 그룹을 끝낼 때까지 체크를 미루지 않는다.
- **부분 완료:** 구현만 끝나고 GPU 검증이 남았다면 검증 ID는 미완료다. 한 ID도 너무 크면 새 하위 작업 ID로 나누고 기존 ID를 재사용하지 않는다.
- **근거:** 완료 항목에는 해당 플랫폼·fingerprint의 보고서/실행 증거를 연결한다. 코드가 있다는 이유, 다른 플랫폼의 성공, 기대한 미지원 응답만으로 실제 기능 지원을 완료 처리하지 않는다.
- **코드 변경 후:** 관련 최종 회귀·현재 제품 측정·단계 수용 ID를 다시 검토한다. 과거 완료 증거는 보존하고 새 fingerprint 검증으로 재귀속하지 않는다.
- **환경:** 선행 ID가 끝나도 Linux/Mac/GPU/사람의 출력 확인 환경이 필요한 작업은 해당 환경이 관측돼야 완료할 수 있다.
- **범위 결정:** 조건부 작업을 제외할 때는 지정된 범위 결정 ID에 근거를 남긴다. 제외 작업 자체를 완료로 체크하지 않으며, 원래 필수 요구를 자동 축소하지 않는다.
- **단계 종료:** 잎 작업의 개수로 전체 완료율을 추정하지 않는다. Phase 3 최종 수용은 `P3-GATE-03`에서 별도로 판단한다.

```powershell
python tools/roadmap.py --next
python tools/roadmap.py --task P3-LNX-01
python tools/roadmap.py --summary --phase 3
python tools/roadmap.py --check
# JSON의 상태/근거를 갱신한 뒤 문서 재생성
python tools/roadmap.py --write
```

**표기:** `[x] 완료`는 연결된 근거의 좁은 범위에 한정한다. `[ ] 미완료`와 `[ ] 보류`는 구분한다. 필수/범위 결정 후/후속 최적화/보류 유지도 별도 분류한다.

## 단계별 상태

| Phase | 완료 | 필수 잔여 | 조건부 잔여 | 후속 최적화 잔여 | 보류 | 상태 해석 |
|---|---:|---:|---:|---:|---:|---|
| 0 — 조사 | 3 | 0 | 0 | 0 | 0 | 기존 고정 snapshot과 검토된 플랫폼·입력 범위에서 완료. |
| 1 — 공통 벤치마크 기반 | 6 | 0 | 0 | 0 | 0 | 문서화된 Windows D3D12 subset 수용. 모든 플랫폼/SDK 조합 완료가 아님. |
| 2 — D3D12 core·잔여 수용 | 10 | 4 | 0 | 0 | 0 | 구현·자동 회귀와 NVIDIA 기본 표시의 사용자 창 조작 수용 완료. 장시간 메모리·Intel AV 원인, 실제 DPI·다중 모니터와 물리 손실은 필수 미완료다. |
| 3 — Vulkan | 53 | 18 | 8 | 0 | 0 | 현재 개발 단계. Windows 완료 항목, Linux native/WSI, 현재 제품 비교, 조건부 기능, 최종 수용을 독립 체크한다. |
| 4 — Metal | 0 | 9 | 0 | 0 | 0 | 후속 단계. 실제 Mac compile/runtime/performance를 구분하며 환경 부재를 완료로 표시하지 않는다. |
| 5 — Shader pipeline | 0 | 8 | 0 | 0 | 0 | DXC DXIL/SPIR-V·MSL·일반 reflection·cache. 현재 제한된 SPIR-V drawing과 구분. |
| 6 — GPU 자원 최적화 | 6 | 0 | 0 | 11 | 0 | 완료된 후보 관측과 남은 정책/성능/채택을 구분한다. 이 목록 전체를 Phase 3 선행 gate로 만들지 않는다. |
| 7 — 독립 SDK 배포 | 1 | 6 | 0 | 0 | 0 | Windows 설치 기반은 있음. 전체 지원 플랫폼의 배포·감사·CI·릴리스 수용은 미완료. |

## 다음 권장 작업

**`P3-LNX-15` — Linux 물리 GPU·공통 suite 수용**

P3-LNX-04의 software 경로와 분리해 실제 Linux 물리 GPU에서 ID/driver·exact 선택 및 공통 GPU suite를 검증한다. software/CPU 종류의 성공을 hardware 수용으로 합산하지 않는다.

**완료 기준:** 관측된 물리 GPU/driver의 native 식별·선택과 버퍼/텍스처/draw/binding/depth/timestamp·정상 validation/수명 참조의 실제 실행 근거.

**환경 조건:** 실제 Linux 물리 GPU/ICD와 VVL 환경. 하드웨어 전용 도구·수동 CI 준비 및 software 거부 검증 완료; SSH 대상 또는 GPU runner·ICD 경로·정확한 UUID 집합 필요. 물리 정상 suite/validation 실행은 미완료.

## 변경하지 않는 경계

- 보류 수용 항목: 없음. 각 작업의 재개·수용 상태를 따르며 하드웨어 원인 확정으로 표시하지 않는다.
- Windows Vulkan의 기본 그래픽스 기능과 최근 생성/로더/할당 장애 검증은 완료 근거를 보존한다. 미완료 Linux 검증과 혼합하지 않는다.
- 버퍼 풀은 실험 후보다. sparse 예약 6.2배 관측은 유지하며 제품 dedicated 기본 정책은 그대로다. 후보 채택은 Phase 6의 별도 결정이다.
- DXC/일반 reflection/MSL 변환은 Phase 5, allocator/upload ring/cache 최적화는 Phase 6, 전체 배포는 Phase 7이다. Linux native 설치 smoke 등 Phase 3 수용에 필요한 좁은 작업은 해당 ID에서 검사한다.
- Compute/render graph, stencil/MSAA/mip/array 등 초기 공통 RHI 밖 확장을 Phase 3 필수로 임의 추가하지 않는다. 후속 릴리스 범위는 기존 역사 문서에 남긴다.

## Phase 0 — 조사

기존 고정 snapshot과 검토된 플랫폼·입력 범위에서 완료.

### P0-RES — 조사 완료 범위

| 체크 · ID · 구분 | 작업 / 완료 기준 | 선행 · 환경 · 근거 |
|---|---|---|
| [x] `P0-RES-01`<br>완료 · 필수 | **읽기 전용 Sandbox 조사**<br>고정 SHA의 설계·인터페이스를 참고했고 수정·통합하지 않음.<br>완료: 연결된 보고서의 당시 플랫폼·입력·실행 범위에서 확인됨. | 선행: 없음<br>근거: [COMPETITOR_ANALYSIS](COMPETITOR_ANALYSIS.md) |
| [x] `P0-RES-02`<br>완료 · 필수 | **경쟁 SDK 아키텍처·지원·라이선스 조사**<br>일곱 SDK snapshot과 명시된 backend/상용 조건 구분.<br>완료: 연결된 보고서의 당시 플랫폼·입력·실행 범위에서 확인됨. | 선행: 없음<br>근거: [COMPETITOR_ANALYSIS](COMPETITOR_ANALYSIS.md) |
| [x] `P0-RES-03`<br>완료 · 필수 | **현재 Windows 입력·출처·notice 감사**<br>현재 검토된 구성의 compiler reads·라이선스·배포 검사. 미래 구성은 별도 감사.<br>완료: 연결된 보고서의 당시 플랫폼·입력·실행 범위에서 확인됨. | 선행: 없음<br>근거: [DEPENDENCY_LICENSES](DEPENDENCY_LICENSES.md) |

## Phase 1 — 공통 벤치마크 기반

문서화된 Windows D3D12 subset 수용. 모든 플랫폼/SDK 조합 완료가 아님.

### P1-BASE — 수용된 Windows 벤치마크 기반

| 체크 · ID · 구분 | 작업 / 완료 기준 | 선행 · 환경 · 근거 |
|---|---|---|
| [x] `P1-BASE-01`<br>완료 · 필수 | **공통 작업·정확성·통계 계약**<br>같은 GPU/driver/workload, readback, warmup·반복·원시 JSON/CSV.<br>완료: 연결된 보고서의 당시 플랫폼·입력·실행 범위에서 확인됨. | 선행: 없음<br>근거: [BENCHMARK_DESIGN](BENCHMARK_DESIGN.md) |
| [x] `P1-BASE-02`<br>완료 · 필수 | **D3D12 기본 경쟁 비교**<br>LLGL/SDL/Diligent/bgfx/Forge의 명시된 A–D 지원 범위.<br>완료: 연결된 보고서의 당시 플랫폼·입력·실행 범위에서 확인됨. | 선행: 없음<br>근거: [BENCHMARK_REPORT](BENCHMARK_REPORT.md) |
| [x] `P1-BASE-03`<br>완료 · 필수 | **고급 E/F/G/H 독립 기준선**<br>LLGL과 SDL의 문서화된 참조 작업. GraphicsSDK compute 완료 의미는 아님.<br>완료: 연결된 보고서의 당시 플랫폼·입력·실행 범위에서 확인됨. | 선행: 없음<br>근거: [PHASE1_COMPLETION_REPORT](PHASE1_COMPLETION_REPORT.md) |
| [x] `P1-BASE-04`<br>완료 · 필수 | **고급 공개 작업 CPU 비교**<br>LLGL/SDL의 14개 작업과 binding ABI 차이 구분.<br>완료: 연결된 보고서의 당시 플랫폼·입력·실행 범위에서 확인됨. | 선행: 없음<br>근거: [PHASE1_SDL_COMPARISON_REPORT](PHASE1_SDL_COMPARISON_REPORT.md) |
| [x] `P1-BASE-05`<br>완료 · 필수 | **타임스탬프·CPU/메모리 관측**<br>비교 가능한 timestamp 구간과 제한된 프로세스 지표.<br>완료: 연결된 보고서의 당시 플랫폼·입력·실행 범위에서 확인됨. | 선행: 없음<br>근거: [CPU_MEMORY_BENCHMARK_REPORT](CPU_MEMORY_BENCHMARK_REPORT.md), [GPU_TIMESTAMP_BENCHMARK_REPORT](GPU_TIMESTAMP_BENCHMARK_REPORT.md) |
| [x] `P1-BASE-06`<br>완료 · 필수 | **문서화된 GPU 메모리 이벤트 기반 수용**<br>Windows D3D12/LLGL process-lifetime 범위의 손실 없는 이벤트 검증. Phase 2 장시간 gate와 구분.<br>완료: 연결된 보고서의 당시 플랫폼·입력·실행 범위에서 확인됨. | 선행: 없음<br>근거: [PHASE1_GPU_MEMORY_REPORT](PHASE1_GPU_MEMORY_REPORT.md) |

## Phase 2 — D3D12 core·잔여 수용

구현·자동 회귀와 NVIDIA 기본 표시의 사용자 창 조작 수용 완료. 장시간 메모리·Intel AV 원인, 실제 DPI·다중 모니터와 물리 손실은 필수 미완료다.

### P2-CORE — D3D12 구현·자동 검증 완료

| 체크 · ID · 구분 | 작업 / 완료 기준 | 선행 · 환경 · 근거 |
|---|---|---|
| [x] `P2-CORE-01`<br>완료 · 필수 | **공통 RHI·자원·draw·queue·기본 swapchain**<br>기존 Windows hardware/validation/WARP 또는 보고서에 명시된 좁은 계약 유지.<br>완료: 연결된 보고서의 당시 플랫폼·입력·실행 범위에서 확인됨. | 선행: 없음<br>근거: [PUBLIC_API](PUBLIC_API.md) |
| [x] `P2-CORE-02`<br>완료 · 필수 | **Vertex/index/instance 입력**<br>기존 Windows hardware/validation/WARP 또는 보고서에 명시된 좁은 계약 유지.<br>완료: 연결된 보고서의 당시 플랫폼·입력·실행 범위에서 확인됨. | 선행: 없음<br>근거: [PHASE2_VERTEX_INPUT_REPORT](PHASE2_VERTEX_INPUT_REPORT.md) |
| [x] `P2-CORE-03`<br>완료 · 필수 | **명시적 binding layout·immutable sampler**<br>기존 Windows hardware/validation/WARP 또는 보고서에 명시된 좁은 계약 유지.<br>완료: 연결된 보고서의 당시 플랫폼·입력·실행 범위에서 확인됨. | 선행: 없음<br>근거: [PHASE2_BINDINGS_REPORT](PHASE2_BINDINGS_REPORT.md) |
| [x] `P2-CORE-04`<br>완료 · 필수 | **D32 depth·MRT·pass 계약**<br>기존 Windows hardware/validation/WARP 또는 보고서에 명시된 좁은 계약 유지.<br>완료: 연결된 보고서의 당시 플랫폼·입력·실행 범위에서 확인됨. | 선행: 없음<br>근거: [PHASE2_DEPTH_MRT_REPORT](PHASE2_DEPTH_MRT_REPORT.md) |
| [x] `P2-CORE-05`<br>완료 · 필수 | **통제된 장치 제거·generation 복구**<br>기존 Windows hardware/validation/WARP 또는 보고서에 명시된 좁은 계약 유지.<br>완료: 연결된 보고서의 당시 플랫폼·입력·실행 범위에서 확인됨. | 선행: 없음<br>근거: [PHASE2_RECOVERY_REPORT](PHASE2_RECOVERY_REPORT.md) |
| [x] `P2-CORE-06`<br>완료 · 필수 | **정확한 adapter 선택·OS budget snapshot**<br>기존 Windows hardware/validation/WARP 또는 보고서에 명시된 좁은 계약 유지.<br>완료: 연결된 보고서의 당시 플랫폼·입력·실행 범위에서 확인됨. | 선행: 없음<br>근거: [PHASE2_MEMORY_BUDGET_REPORT](PHASE2_MEMORY_BUDGET_REPORT.md), [PHASE2_ADAPTER_REPORT](PHASE2_ADAPTER_REPORT.md) |
| [x] `P2-CORE-07`<br>완료 · 필수 | **유한 fence wait·재시도 등록**<br>기존 Windows hardware/validation/WARP 또는 보고서에 명시된 좁은 계약 유지.<br>완료: 연결된 보고서의 당시 플랫폼·입력·실행 범위에서 확인됨. | 선행: 없음<br>근거: [PHASE2_FENCE_WAIT_REPORT](PHASE2_FENCE_WAIT_REPORT.md) |
| [x] `P2-CORE-08`<br>완료 · 필수 | **복사/전이 거부의 원자성**<br>기존 Windows hardware/validation/WARP 또는 보고서에 명시된 좁은 계약 유지.<br>완료: 연결된 보고서의 당시 플랫폼·입력·실행 범위에서 확인됨. | 선행: 없음<br>근거: [PHASE2_COPY_ATOMICITY_REPORT](PHASE2_COPY_ATOMICITY_REPORT.md) |
| [x] `P2-CORE-09`<br>완료 · 필수 | **자동 창 수명·작은 창·pacing**<br>기존 Windows hardware/validation/WARP 또는 보고서에 명시된 좁은 계약 유지.<br>완료: 연결된 보고서의 당시 플랫폼·입력·실행 범위에서 확인됨. | 선행: 없음<br>근거: [PHASE2_PRESENTATION_PACING_REPORT](PHASE2_PRESENTATION_PACING_REPORT.md) |

### P2-ACC — 보류·실제 환경 수용 잔여

| 체크 · ID · 구분 | 작업 / 완료 기준 | 선행 · 환경 · 근거 |
|---|---|---|
| [ ] `P2-ACC-01`<br>미완료 · 필수 | **장시간 메모리 수용**<br>2026-10-25 사용자 명시 재개. 현재 SDK의 NVIDIA GPU 900초와 WARP 300초는 원래 기준 통과. Full GBV는 warmup 9.398초/608 frames에서 baseline 대비 516.719MiB 증가로 512MiB guard 중단. 정책·입력·관측 환경을 보존했고 전체 세 모드 수용은 실패해 필수 미완료다. 별도 재개한 Intel AV 조사는 P2-ACC-02를 따른다.<br>완료: 원래 정책의 GPU 900초·full GBV 300초·WARP 300초, 각 30초 warmup·참조·정상 오류 0·메모리 plateau·동일 입력과 장치·환경의 실제 수용 증거. 기준 완화나 짧은 진단으로 대체하지 않음. | 선행: 없음<br>근거: [PHASE2_MEMORY_CAMPAIGN_REPORT](PHASE2_MEMORY_CAMPAIGN_REPORT.md), [PHASE2_MEMORY_RESUMPTION_REPORT](PHASE2_MEMORY_RESUMPTION_REPORT.md), [summary.json](../benchmarks/reports/2026-10-25-phase2-memory-resumed/summary.json) |
| [ ] `P2-ACC-02`<br>미완료 · 필수 | **과거 Intel AV 원인 조사**<br>2026-10-25 사용자 지시로 재개. 별도 최적화 SDK 심볼 2,429개 해석과 수집기 9개 제어 검증 완료. 현재 Intel 일반/계측 실행 각각 300초+30초 정상 종료했으나 미처리 AV는 비재현, private 메모리 기준은 별도 실패다. 실제 원인·수정은 미확정으로 필수 미완료이며 과거 자료를 보존한다.<br>완료: 실제 AV 재현 조건과 예외·스택·자원 수명 등 원인 근거, 수정 또는 통제된 환경 비교, 수정 전후 회귀 검증. 비재현만으로 완료하지 않는다. | 선행: 없음<br>근거: [PHASE2_FAULT_INVESTIGATION_REPORT](PHASE2_FAULT_INVESTIGATION_REPORT.md), [PHASE2_INTEL_CRASH_RESUMPTION_REPORT](PHASE2_INTEL_CRASH_RESUMPTION_REPORT.md) |
| [x] `P2-ACC-03`<br>완료 · 필수 | **실제 사용자 창 조작·표시 확인**<br>2026-10-26 사용자의 실제 monitor·입력 확인과 native 기록을 결합했다. 첫 최소화 누락을 보존하고 두 번째 1,302-frame 실행에서 실제 최소화/복원·resize·최대화·X 닫기와 자원 해제를 대조했다. NVIDIA 기본 표시(VSync on, 60fps cap, GBV off) 범위 수용이며 DPI·다른 adapter·물리 손실은 별도다.<br>완료: 사람이 확인한 표시 및 창 동작 기록. 자동 readback만으로 체크하지 않음. | 선행: `P2-CORE-09`<br>환경: Windows 실제 표시·사용자 조작 환경<br>근거: [PHASE2_MANUAL_PRESENTATION_REPORT](PHASE2_MANUAL_PRESENTATION_REPORT.md), [summary.json](../benchmarks/reports/2026-10-26-phase2-manual-presentation/summary.json) |
| [ ] `P2-ACC-04`<br>미완료 · 필수 | **실제 DPI·다중 모니터 출력 수용**<br>물리 모니터 이동과 DPI 변화의 지원 범위 검사.<br>완료: 지원 조합별 관측과 재현 가능한 수용 기록. | 선행: `P2-ACC-03`<br>환경: DPI가 다른 실제 다중 모니터 |
| [ ] `P2-ACC-05`<br>미완료 · 필수 | **예상하지 않은 TDR/hot-unplug 복구 수용**<br>통제된 RemoveDevice와 별도로 실제 손실 수용 범위 확인.<br>완료: 지원 환경에서 실제 사건·옛 generation 거부·복구 출력 확인. | 선행: `P2-CORE-05`<br>환경: 해당 사건을 안전하게 관측할 수 있는 실제 환경 |

## Phase 3 — Vulkan

현재 개발 단계. Windows 완료 항목, Linux native/WSI, 현재 제품 비교, 조건부 기능, 최종 수용을 독립 체크한다.

### P3-WIN — Windows에서 완료된 기능·장애 검증

| 체크 · ID · 구분 | 작업 / 완료 기준 | 선행 · 환경 · 근거 |
|---|---|---|
| [x] `P3-WIN-01`<br>완료 · 필수 | **Vulkan 1.1 장치·정확한 adapter 선택**<br>Windows NVIDIA/Intel 장치·ID·미지원 계약.<br>완료: 연결된 보고서의 당시 플랫폼·입력·실행 범위에서 확인됨. | 선행: 없음<br>근거: [PHASE3_VULKAN_TRANSFER_REPORT](PHASE3_VULKAN_TRANSFER_REPORT.md) |
| [x] `P3-WIN-02`<br>완료 · 필수 | **버퍼 전송·queue/fence·수명**<br>Upload/Device/Readback, 전이, 유한 대기, 기록/제출 보유.<br>완료: 연결된 보고서의 당시 플랫폼·입력·실행 범위에서 확인됨. | 선행: 없음<br>근거: [PHASE3_VULKAN_TRANSFER_REPORT](PHASE3_VULKAN_TRANSFER_REPORT.md) |
| [x] `P3-WIN-03`<br>완료 · 필수 | **Core/synchronization validation**<br>고정 process-local VVL, 오류 counter 및 negative controls. GPU-assisted는 별도.<br>완료: 연결된 보고서의 당시 플랫폼·입력·실행 범위에서 확인됨. | 선행: 없음<br>근거: [PHASE3_VULKAN_VALIDATION_REPORT](PHASE3_VULKAN_VALIDATION_REPORT.md) |
| [x] `P3-WIN-04`<br>완료 · 필수 | **컬러 텍스처 전송**<br>RGBA8/RGBA16F, padding/layout/version/readback.<br>완료: 연결된 보고서의 당시 플랫폼·입력·실행 범위에서 확인됨. | 선행: 없음<br>근거: [PHASE3_VULKAN_TEXTURE_REPORT](PHASE3_VULKAN_TEXTURE_REPORT.md) |
| [x] `P3-WIN-05`<br>완료 · 필수 | **컬러 Clear/Load/Store pass**<br>혼합 포맷·최대 8 attachments와 거부 원자성.<br>완료: 연결된 보고서의 당시 플랫폼·입력·실행 범위에서 확인됨. | 선행: 없음<br>근거: [PHASE3_VULKAN_COLOR_PASS_REPORT](PHASE3_VULKAN_COLOR_PASS_REPORT.md) |
| [x] `P3-WIN-06`<br>완료 · 필수 | **SPIR-V graphics pipeline·기본 draw**<br>외부 검증 SPIR-V 1.3의 제한된 인터페이스, scissor/blend/instancing. 일반 shader compiler 아님.<br>완료: 연결된 보고서의 당시 플랫폼·입력·실행 범위에서 확인됨. | 선행: 없음<br>근거: [PHASE3_VULKAN_DRAW_REPORT](PHASE3_VULKAN_DRAW_REPORT.md) |
| [x] `P3-WIN-07`<br>완료 · 필수 | **Vertex/index/instance 입력**<br>Float/UNorm layout, UInt16/32 index, bounded buffer view.<br>완료: 연결된 보고서의 당시 플랫폼·입력·실행 범위에서 확인됨. | 선행: 없음<br>근거: [PHASE3_VULKAN_VERTEX_REPORT](PHASE3_VULKAN_VERTEX_REPORT.md) |
| [x] `P3-WIN-08`<br>완료 · 필수 | **Texture/constant 배열·sampler 바인딩**<br>Immutable layout/snapshot와 lifetime·state 검증.<br>완료: 연결된 보고서의 당시 플랫폼·입력·실행 범위에서 확인됨. | 선행: 없음<br>근거: [PHASE3_VULKAN_BINDINGS_REPORT](PHASE3_VULKAN_BINDINGS_REPORT.md) |
| [x] `P3-WIN-09`<br>완료 · 필수 | **D32 depth·MRT**<br>읽기 전용/depth-only·비교/쓰기 mask·mixed MRT.<br>완료: 연결된 보고서의 당시 플랫폼·입력·실행 범위에서 확인됨. | 선행: 없음<br>근거: [PHASE3_VULKAN_DEPTH_REPORT](PHASE3_VULKAN_DEPTH_REPORT.md) |
| [x] `P3-WIN-10`<br>완료 · 필수 | **GPU timestamps**<br>2-slot query, availability/fence/wrap·query lifetime.<br>완료: 연결된 보고서의 당시 플랫폼·입력·실행 범위에서 확인됨. | 선행: 없음<br>근거: [PHASE3_VULKAN_TIMESTAMP_REPORT](PHASE3_VULKAN_TIMESTAMP_REPORT.md) |
| [x] `P3-WIN-11`<br>완료 · 필수 | **지원 장치의 Win32 presentation**<br>NVIDIA 실행, 현재 Intel caps=false 계약. resize/수명 검증이며 전체 GPU·사람의 표시 수용 아님.<br>완료: 연결된 보고서의 당시 플랫폼·입력·실행 범위에서 확인됨. | 선행: 없음<br>근거: [PHASE3_VULKAN_PRESENTATION_REPORT](PHASE3_VULKAN_PRESENTATION_REPORT.md) |
| [x] `P3-WIN-12`<br>완료 · 필수 | **생성 출력 소유권 제품 수정**<br>29개 single-output guard, 정의된 pipeline 부분 성공 정리 유지.<br>완료: 연결된 보고서의 당시 플랫폼·입력·실행 범위에서 확인됨. | 선행: 없음<br>근거: [VULKAN_CREATION_OWNERSHIP_REPORT](VULKAN_CREATION_OWNERSHIP_REPORT.md) |
| [x] `P3-WIN-13`<br>완료 · 필수 | **로더 초기화 실패·재시도**<br>Instance/device/messenger 및 지원 NVIDIA surface의 실제 loader 주입.<br>완료: 연결된 보고서의 당시 플랫폼·입력·실행 범위에서 확인됨. | 선행: 없음<br>근거: [VULKAN_LOADER_FAULT_REPORT](VULKAN_LOADER_FAULT_REPORT.md) |

### P3-BASE — 완료된 과거 Vulkan 기준선

| 체크 · ID · 구분 | 작업 / 완료 기준 | 선행 · 환경 · 근거 |
|---|---|---|
| [x] `P3-BASE-01`<br>완료 · 필수 | **SDL과 업로드 기준선**<br>보고서의 당시 fingerprint/Windows GPU 범위에서 완료. 현재 최종 제품의 새 측정으로 재귀속하지 않음.<br>완료: 연결된 보고서의 당시 플랫폼·입력·실행 범위에서 확인됨. | 선행: 없음<br>근거: [PHASE3_VULKAN_UPLOAD_REPORT](PHASE3_VULKAN_UPLOAD_REPORT.md) |
| [x] `P3-BASE-02`<br>완료 · 필수 | **SDL과 draw/instancing 기준선**<br>보고서의 당시 fingerprint/Windows GPU 범위에서 완료. 현재 최종 제품의 새 측정으로 재귀속하지 않음.<br>완료: 연결된 보고서의 당시 플랫폼·입력·실행 범위에서 확인됨. | 선행: 없음<br>근거: [PHASE3_VULKAN_DRAW_BENCHMARK_REPORT](PHASE3_VULKAN_DRAW_BENCHMARK_REPORT.md) |
| [x] `P3-BASE-03`<br>완료 · 필수 | **SDL과 반복 PSO 생성·공개 해제 기준선**<br>보고서의 당시 fingerprint/Windows GPU 범위에서 완료. 현재 최종 제품의 새 측정으로 재귀속하지 않음.<br>완료: 연결된 보고서의 당시 플랫폼·입력·실행 범위에서 확인됨. | 선행: 없음<br>근거: [PHASE3_VULKAN_PSO_BENCHMARK_REPORT](PHASE3_VULKAN_PSO_BENCHMARK_REPORT.md) |
| [x] `P3-BASE-04`<br>완료 · 필수 | **SDL과 buffer/texture 1·1000개 할당 기준선**<br>보고서의 당시 fingerprint/Windows GPU 범위에서 완료. 현재 최종 제품의 새 측정으로 재귀속하지 않음.<br>완료: 연결된 보고서의 당시 플랫폼·입력·실행 범위에서 확인됨. | 선행: 없음<br>근거: [PHASE3_VULKAN_ALLOCATION_BENCHMARK_REPORT](PHASE3_VULKAN_ALLOCATION_BENCHMARK_REPORT.md) |
| [x] `P3-BASE-05`<br>완료 · 필수 | **Native allocation 구간별 관측**<br>보고서의 당시 fingerprint/Windows GPU 범위에서 완료. 현재 최종 제품의 새 측정으로 재귀속하지 않음.<br>완료: 연결된 보고서의 당시 플랫폼·입력·실행 범위에서 확인됨. | 선행: 없음<br>근거: [PHASE3_VULKAN_ALLOCATION_TRACE_REPORT](PHASE3_VULKAN_ALLOCATION_TRACE_REPORT.md) |

### P3-SCP — 초기 지원·비교 범위 확정

| 체크 · ID · 구분 | 작업 / 완료 기준 | 선행 · 환경 · 근거 |
|---|---|---|
| [x] `P3-SCP-01`<br>완료 · 필수 | **D3D12/Vulkan 기능·미지원 표**<br>현재 caps/API/포맷/동기화 계약을 대조하고 D3D12/Vulkan 지원·미지원·조건부 이유와 검사 ID를 연결했다.<br>완료: 기능별 지원/미지원·필수/조건부 이유와 검사 ID가 연결된 표. 원래 필수 요구를 임의 제거하지 않음. | 선행: `P3-WIN-11`<br>근거: [PHASE3_SCOPE](PHASE3_SCOPE.md), [summary.json](../benchmarks/reports/2026-10-25-phase3-scope/summary.json) |
| [x] `P3-SCP-02`<br>완료 · 필수 | **Windows GPU·드라이버·출력 범위**<br>Windows GPU/driver의 정확한 출력 지원 조합을 현재 제품으로 재확인하고, 초기 지원 후보·추가 검증 대상·legacy/adapter fallback 미채택 근거를 기록했다.<br>완료: 출시 지원 조합, 추가 검증 대상 및 fallback 채택/미채택 이유 명시. | 선행: `P3-SCP-01`<br>근거: [PHASE3_WINDOWS_SUPPORT](PHASE3_WINDOWS_SUPPORT.md), [summary.json](../benchmarks/reports/2026-10-25-phase3-windows-support/summary.json) |

### P3-LNX — Linux 네이티브 구현·검증

| 체크 · ID · 구분 | 작업 / 완료 기준 | 선행 · 환경 · 근거 |
|---|---|---|
| [x] `P3-LNX-01`<br>완료 · 필수 | **실행 환경 확인**<br>Windows host의 WSL/컨테이너/VM CLI·선택 서비스와 원격 대상 가용성을 읽기 전용으로 조사했다. WSL 미설치로 Linux OS/compiler/loader/ICD/GPU/VVL은 미관측이며 runtime 작업은 미완료 유지한다.<br>완료: 실제 관측한 환경 receipt와 가능한 build/runtime 범위. 환경 부재는 명시하고 런타임 작업을 완료로 만들지 않음. | 선행: `P3-WIN-01`<br>근거: [PHASE3_LINUX_ENVIRONMENT](PHASE3_LINUX_ENVIRONMENT.md), [summary.json](../benchmarks/reports/2026-10-25-phase3-linux-environment/summary.json) |
| [x] `P3-LNX-02`<br>완료 · 필수 | **Vulkan 활성 Linux build preset**<br>Linux-only D3D12 OFF/Vulkan ON preset 두 조합과 외부 헤더·격리 build/install 경로를 구성했다. Windows CMake 정의/fixture 검사는 완료했으며 실제 Linux configure/compile은 P3-LNX-03에서 검사한다.<br>완료: portable-core와 구분된 재현 configure 명령·플랫폼 의존성 명시. | 선행: `P3-LNX-01`<br>환경: preset 정의 검사는 CMake 3.25+; 실제 configure에는 Linux C++23 compiler/CMake/Ninja 필요<br>근거: [CMakePresets.json](../CMakePresets.json), [PHASE3_LINUX_PRESETS](PHASE3_LINUX_PRESETS.md), [summary.json](../benchmarks/reports/2026-10-25-phase3-linux-presets/summary.json) |
| [x] `P3-LNX-03`<br>완료 · 필수 | **Linux SDK 실제 컴파일**<br>GitHub-hosted Ubuntu x86_64에서 두 Vulkan preset의 실제 GCC configure/compile/link와 CPU 11개씩을 완료했다. Linux native fingerprint와 Git commit·compile commands를 대조했으며 GPU runtime은 미검증이다.<br>완료: 현재 source fingerprint의 native build 성공 로그. Windows 빌드로 대체하지 않음. | 선행: `P3-LNX-02`<br>환경: Linux native build 환경<br>근거: [PHASE3_LINUX_COMPILE_REPORT](PHASE3_LINUX_COMPILE_REPORT.md), [summary.json](../benchmarks/reports/2026-10-25-phase3-linux-compile/summary.json), [linux-vulkan-compile.yml](../.github/workflows/linux-vulkan-compile.yml) |
| [x] `P3-LNX-04`<br>완료 · 필수 | **로더·adapter·장치 실행**<br>실제 Linux software ICD에서 loader·native UUID/driver·정확한 device 선택과 부정 대조를 검증했다. hardware 0개이며 물리 GPU·공통 GPU suite 수용은 P3-LNX-15 필수 gate로 분리해 유지한다.<br>완료: 실제 Linux Vulkan device 생성·정확한 선택·negative controls와 software/hardware 종류 명시. 물리 GPU 수용은 P3-LNX-15에서 별도 필수 검증. | 선행: `P3-LNX-03`<br>환경: Linux Vulkan runtime; 이번 수용은 software ICD, 물리 GPU는 P3-LNX-15 필수<br>근거: [PHASE3_LINUX_DEVICE_REPORT](PHASE3_LINUX_DEVICE_REPORT.md), [summary.json](../benchmarks/reports/2026-10-25-phase3-linux-device/summary.json) |
| [x] `P3-LNX-05`<br>완료 · 필수 | **버퍼·queue/fence·수명 실행**<br>기존 공통 suite를 실제 Linux software ICD의 in-tree CTest와 설치 SDK 소비자에서 실행했다. 전체 버퍼 참조·유한 wait·stale recording·retained allocation 계약 통과. 물리 GPU 수용은 P3-LNX-15 필수 gate에 유지한다.<br>완료: 전체 buffer 참조 일치와 수명/대기/거부 계약 통과. 이번 근거는 software ICD이며 물리 GPU 수용은 P3-LNX-15에서 별도 필수 검증. | 선행: `P3-LNX-04`<br>환경: Linux Vulkan runtime; 이번 수용은 software ICD, 물리 GPU는 P3-LNX-15 필수<br>근거: [PHASE3_LINUX_TRANSFER_REPORT](PHASE3_LINUX_TRANSFER_REPORT.md), [summary.json](../benchmarks/reports/2026-10-25-phase3-linux-transfer/summary.json) |
| [x] `P3-LNX-06`<br>완료 · 필수 | **텍스처 전송 실행**<br>기존 공통 suite를 실제 Linux software ICD의 in-tree CTest와 설치 SDK 소비자에서 실행했다. RGBA8/RGBA16F·홀수 폭·전체 texel/padding·전이·수명 계약 통과. 물리 GPU 수용은 P3-LNX-15 필수 gate에 유지한다.<br>완료: 전체 texel 및 padding 참조 일치, 지원 포맷 명시. 이번 근거는 software ICD이며 물리 GPU 수용은 P3-LNX-15에서 별도 필수 검증. | 선행: `P3-LNX-05`<br>환경: Linux Vulkan runtime; 이번 수용은 software ICD, 물리 GPU는 P3-LNX-15 필수<br>근거: [PHASE3_LINUX_TEXTURE_REPORT](PHASE3_LINUX_TEXTURE_REPORT.md), [summary.json](../benchmarks/reports/2026-10-25-phase3-linux-texture/summary.json) |
| [x] `P3-LNX-07`<br>완료 · 필수 | **컬러 pass·기본 draw 실행**<br>기존 컬러 pass·SPIR-V draw suite를 실제 Linux software ICD의 in-tree CTest와 설치 SDK 소비자에서 실행했다. Clear/Load/Store·삼각형·blend/scissor·instancing·MRT 전체 참조와 상태/수명 계약 통과. 물리 GPU 수용은 P3-LNX-15 필수 gate에 유지한다.<br>완료: 삼각형·blend/scissor/instancing와 MRT 컬러 참조 일치. 이번 근거는 software ICD이며 물리 GPU 수용은 P3-LNX-15에서 별도 필수 검증. | 선행: `P3-LNX-06`<br>환경: Linux Vulkan runtime; 이번 수용은 software ICD, 물리 GPU는 P3-LNX-15 필수<br>근거: [PHASE3_LINUX_DRAW_REPORT](PHASE3_LINUX_DRAW_REPORT.md), [summary.json](../benchmarks/reports/2026-10-25-phase3-linux-draw/summary.json) |
| [x] `P3-LNX-08`<br>완료 · 필수 | **입력·바인딩 실행**<br>기존 입력·바인딩 suite를 실제 Linux software ICD의 in-tree CTest와 설치 SDK 소비자에서 실행했다. Vertex/index/instance·texture/constant 배열·sampler·snapshot 전체 이미지와 경계/foreign/thread/version/lifetime 계약 통과. 물리 GPU 수용은 P3-LNX-15 필수 gate에 유지한다.<br>완료: 전체 이미지와 경계/foreign/thread/version/lifetime 거부 검사 통과. 이번 근거는 software ICD이며 물리 GPU 수용은 P3-LNX-15에서 별도 필수 검증. | 선행: `P3-LNX-07`<br>환경: Linux Vulkan runtime; 이번 수용은 software ICD, 물리 GPU는 P3-LNX-15 필수<br>근거: [PHASE3_LINUX_INPUT_REPORT](PHASE3_LINUX_INPUT_REPORT.md), [summary.json](../benchmarks/reports/2026-10-25-phase3-linux-input/summary.json) |
| [x] `P3-LNX-09`<br>완료 · 필수 | **D32·depth/MRT 실행**<br>기존 D32/depth/MRT suite를 실제 Linux software ICD의 in-tree CTest와 설치 SDK 소비자에서 실행했다. Depth-only/read-only·비교 8종·쓰기 mask·혼합 MRT의 색/깊이 전체 참조와 pass/pipeline 호환성·수명 계약 통과. 물리 GPU 수용은 P3-LNX-15 필수 gate에 유지한다.<br>완료: 색·깊이 전체 참조 및 pass/pipeline 호환성 검사 통과. 이번 근거는 software ICD이며 물리 GPU 수용은 P3-LNX-15에서 별도 필수 검증. | 선행: `P3-LNX-08`<br>환경: Linux Vulkan runtime; 이번 수용은 software ICD, 물리 GPU는 P3-LNX-15 필수<br>근거: [PHASE3_LINUX_DEPTH_REPORT](PHASE3_LINUX_DEPTH_REPORT.md), [summary.json](../benchmarks/reports/2026-10-25-phase3-linux-depth/summary.json) |
| [x] `P3-LNX-10`<br>완료 · 필수 | **Timestamp 실행**<br>실제 Linux software ICD의 in-tree/설치 SDK에서 Vulkan query 기반 elapsed·availability·수명을 실행하고 native queue valid bits/period를 대조했다. Wrap/변환은 CPU math로 구분해 통과했으며 실제 counter wrap은 미재현이다. 물리 GPU 수용은 P3-LNX-15 필수 gate에 유지한다.<br>완료: CPU elapsed로 대체하지 않은 Vulkan query 기반 결과와 오류 계약 통과. 이번 근거는 software ICD이며 물리 GPU 수용은 P3-LNX-15에서 별도 필수 검증. | 선행: `P3-LNX-05`<br>환경: Linux timestamp 지원 Vulkan runtime; 이번 수용은 software ICD, 물리 GPU는 P3-LNX-15 필수<br>근거: [PHASE3_LINUX_TIMESTAMP_REPORT](PHASE3_LINUX_TIMESTAMP_REPORT.md), [summary.json](../benchmarks/reports/2026-10-25-phase3-linux-timestamp/summary.json) |
| [x] `P3-LNX-11`<br>완료 · 필수 | **Linux core/sync validation 수용**<br>고정 VVL 1.3.290을 별도 Linux profile로 빌드하고 software ICD의 공통 suite 8개를 in-tree/설치 SDK에서 실행했다. 정상 오류 계수 0·실제 shared-library 경로/해시·core/sync/callback controls·missing-layer 실패 통과. 물리 GPU 수용은 P3-LNX-15 필수 gate에 유지한다.<br>완료: Linux 공통 suite의 정상 오류 0과 의도한 native 오류 검출. 누락 layer 명시적 실패. 이번 근거는 software ICD이며 물리 GPU 수용은 P3-LNX-15에서 별도 필수 검증. | 선행: `P3-LNX-09`, `P3-LNX-10`<br>환경: Linux 고정 VVL/Vulkan runtime; 이번 수용은 software ICD, 물리 GPU는 P3-LNX-15 필수<br>근거: [PHASE3_LINUX_VALIDATION_REPORT](PHASE3_LINUX_VALIDATION_REPORT.md), [summary.json](../benchmarks/reports/2026-10-25-phase3-linux-validation/summary.json) |
| [x] `P3-LNX-12`<br>완료 · 필수 | **Linux 설치 소비자·입력 감사**<br>실제 Linux에서 설치본 경로를 옮기고 저장소 밖 find_package 소비자 9개를 실행했다. GNU compiler/linker 실제 입력, object/archive, 설치 후 고지와 20개 파일의 검증용 SDK ZIP을 감사했다. software Vulkan 8개 suite/2666 assertions 및 감사 대조 13개 통과.<br>완료: 설치 소비자 실행과 실제 compiler/linker 입력·notice·payload 검사 통과. 고지는 전용 도구의 설치 후 패키징 단계에서 추가. 물리 GPU는 P3-LNX-15, 전체 CI는 P3-LNX-14, 정식 다중 플랫폼 배포는 Phase 7 별도. | 선행: `P3-LNX-11`<br>환경: Linux GNU/CMake/Ninja 및 Vulkan runtime; 이번 실행은 software ICD<br>근거: [PHASE3_LINUX_PACKAGE_REPORT](PHASE3_LINUX_PACKAGE_REPORT.md), [summary.json](../benchmarks/reports/2026-10-25-phase3-linux-package/summary.json) |
| [x] `P3-LNX-13`<br>완료 · 필수 | **Linux Vulkan compile CI**<br>portable-core와 분리된 Vulkan ON native Linux CI를 추가하고 두 preset의 compile/CPU 22개를 수용했다. GPU/layer/WSI 실행 및 기존 전체 CI Python 오류는 별도 잔여다.<br>완료: 실제 CI compile/CPU smoke 결과와 GPU 미실행 경계 기록. Software ICD 성공을 hardware 수용으로 표기하지 않음. | 선행: `P3-LNX-03`<br>환경: Linux CI runner<br>근거: [PHASE3_LINUX_COMPILE_REPORT](PHASE3_LINUX_COMPILE_REPORT.md), [summary.json](../benchmarks/reports/2026-10-25-phase3-linux-compile/summary.json), [linux-vulkan-compile.yml](../.github/workflows/linux-vulkan-compile.yml) |
| [x] `P3-LNX-14`<br>완료 · 필수 | **전체 CI Python 증거 검사 정합성**<br>고정 source ZIP으로 과거 Windows 실행 자료를 검증하고 현재 실행 source identity는 엄격히 유지했다. Windows 경로 별칭/고정 명령 비교를 수정했다. 실제 새 Windows/Linux 전체 CI에서 각 640 tests·540 subtests, 오류/실패/건너뜀 0. 기존 627개 ID와 과거 614개 baseline 로그를 보존했다.<br>완료: Windows/Linux fresh CI 전체 Python 통과 및 기존 자료·검증 계약 보존. 실제 SDK 파일 27개와 검사 입력 174개의 플랫폼별 바이트를 대조하고 과거 identity를 재할당하지 않음. 기존 614/627개 실패 원본 및 수정 후 640개 결과 보존. | 선행: `P3-LNX-03`<br>환경: Windows/Linux fresh CI; 고정 과거 source ZIP과 benchmark evidence. 물리 GPU 수용은 P3-LNX-15 별도<br>근거: [PHASE3_LINUX_COMPILE_REPORT](PHASE3_LINUX_COMPILE_REPORT.md), [PHASE3_LINUX_VALIDATION_REPORT](PHASE3_LINUX_VALIDATION_REPORT.md), [PHASE3_LINUX_PACKAGE_REPORT](PHASE3_LINUX_PACKAGE_REPORT.md), [PHASE3_PYTHON_CI_REPORT](PHASE3_PYTHON_CI_REPORT.md), [summary.json](../benchmarks/reports/2026-10-25-phase3-python-ci/summary.json), [build.yml](../.github/workflows/build.yml) |
| [ ] `P3-LNX-15`<br>미완료 · 필수 | **Linux 물리 GPU·공통 suite 수용**<br>P3-LNX-04의 software 경로와 분리해 실제 Linux 물리 GPU에서 ID/driver·exact 선택 및 공통 GPU suite를 검증한다. software/CPU 종류의 성공을 hardware 수용으로 합산하지 않는다.<br>완료: 관측된 물리 GPU/driver의 native 식별·선택과 버퍼/텍스처/draw/binding/depth/timestamp·정상 validation/수명 참조의 실제 실행 근거. | 선행: `P3-LNX-11`<br>환경: 실제 Linux 물리 GPU/ICD와 VVL 환경. 하드웨어 전용 도구·수동 CI 준비 및 software 거부 검증 완료; SSH 대상 또는 GPU runner·ICD 경로·정확한 UUID 집합 필요. 물리 정상 suite/validation 실행은 미완료.<br>근거: [PHASE3_LINUX_DEVICE_REPORT](PHASE3_LINUX_DEVICE_REPORT.md), [PHASE3_LINUX_HARDWARE_READINESS](PHASE3_LINUX_HARDWARE_READINESS.md), [summary.json](../benchmarks/reports/2026-10-25-phase3-linux-hardware-readiness/summary.json), [linux-hardware-acceptance.yml](../.github/workflows/linux-hardware-acceptance.yml) |

### P3-WSI — Linux 화면 출력

| 체크 · ID · 구분 | 작업 / 완료 기준 | 선행 · 환경 · 근거 |
|---|---|---|
| [x] `P3-WSI-01`<br>완료 · 필수 | **Linux window-system 지원 계약**<br>X11/XCB 우선·native Wayland/XWayland 미수용 조합과 단일 graphics/present queue·maintenance1 완료 fence·명시 BGRA8 형식 확장 계획을 결정했다.<br>완료: 실제 Linux XCB 창/surface·queue/format/완료 feature 및 native device 생성·DISPLAY 부재 대조 기록. 필수 후속 구현·GPU/validation·사람의 표시 검증 계획 연결. SDK Linux present 구현 완료가 아님. | 선행: `P3-SCP-01`, `P3-LNX-03`<br>환경: 선택한 Linux window system·GPU·표시 환경<br>근거: [PHASE3_LINUX_WSI_SCOPE](PHASE3_LINUX_WSI_SCOPE.md), [summary.json](../benchmarks/reports/2026-10-26-phase3-linux-wsi-scope/summary.json) |
| [x] `P3-WSI-02`<br>완료 · 필수 | **표면·instance extension 연결**<br>공통 Xcb connection/32-bit XID와 checkSurfaceSupport를 추가하고 선택 XCB 빌드의 실제 native surface 생성·해제·선택 queue/visual 확인을 연결했다.<br>완료: 설치 ON/OFF 소비자의 실제 Linux XCB surface·queue·누락 extension/procedure·잘못된 창·thread·surface 소실·headless 대조와 입력/고지 감사 통과. Linux swapchain/present는 미구현 유지. 로컬 기존 fence 예외는 별도 보존. | 선행: `P3-WSI-01`, `P3-LNX-04`<br>환경: 선택한 Linux window system·GPU·표시 환경<br>근거: [PHASE3_LINUX_XCB_SURFACE_REPORT](PHASE3_LINUX_XCB_SURFACE_REPORT.md), [summary.json](../benchmarks/reports/2026-10-26-phase3-linux-xcb-surface/summary.json) |
| [x] `P3-WSI-03`<br>완료 · 필수 | **Swapchain acquire/present**<br>선택 XCB swapchain의 native format/extent/image count/present mode와 acquire→submit→present를 연결하고 BGRA8 형식 및 모드별 최소/최대 이미지 한도를 구현했다.<br>완료: 실제 Xvfb/XCB·software ICD의 일반/고정 core+sync validation에서 각 24회 present·48개 전체 프레임 참조와 형식/모드 거부 controls 통과. Windows BGRA 5/5, Python 697 통과. 기존 Windows fence 예외 2개와 물리 GPU·사람의 표시·WSI-04~08은 별도 유지. | 선행: `P3-WSI-02`<br>환경: 선택한 Linux window system·GPU·표시 환경<br>근거: [PHASE3_LINUX_XCB_PRESENT_REPORT](PHASE3_LINUX_XCB_PRESENT_REPORT.md), [summary.json](../benchmarks/reports/2026-10-26-phase3-linux-xcb-present/summary.json) |
| [x] `P3-WSI-04`<br>완료 · 필수 | **출력 자원 완료·보유 수명**<br>기존 XCB current backbuffer/version·recording/submission 보유·present 완료·파괴/퇴역 계약을 설치 소비자와 실제 GPU event 대기로 검증하고 부분 생성·present 전 실패의 자원 회수 controls를 추가했다. 제품 SDK 소스는 변경하지 않았다.<br>완료: 실제 Linux XCB/software ICD 일반·고정 core+sync validation에서 각 6개 수명 경우·16개 거부·실제 미완료 fence 2개·present 11회와 전체 readback 3개 통과. native 객체 생성/해제 및 파괴 순서 오류 0 확인. Windows 표시 2/2와 Python 703 통과. resize/loss·전체 표시 matrix·물리 GPU·육안은 별도 유지. | 선행: `P3-WSI-03`<br>환경: 선택한 Linux window system·GPU·표시 환경<br>근거: [PHASE3_LINUX_XCB_LIFETIME_REPORT](PHASE3_LINUX_XCB_LIFETIME_REPORT.md), [summary.json](../benchmarks/reports/2026-10-26-phase3-linux-xcb-lifetime/summary.json) |
| [x] `P3-WSI-05`<br>완료 · 필수 | **Resize/out-of-date/suboptimal**<br>Vulkan resize의 공개 texture 참조 확인과 GPU drain 뒤 recording 참조 확인 순서를 수정했다. 실제 XCB geometry 변경·재생성, acquire/present out-of-date/suboptimal 제어, native 할당·열거·view 생성 실패 뒤 oldSwapchain 퇴역과 재시도를 검증했다.<br>완료: 실제 XCB/software ICD 일반·고정 core+sync validation에서 각 7개 경우·성공 resize 13회·거부 24개·전체 프레임 참조 46개·실제 present 45회 통과. 미완료 제출 resize 대기·모든 native 자원 회수와 재생성 실패 후 NULL oldSwapchain 재시도 확인. Python 711 통과. Windows 63/65의 기존 fence 예외 2개 유지. 상태 제어를 실제 OS 이벤트·물리 GPU·육안 수용으로 세지 않음. | 선행: `P3-WSI-04`<br>환경: 선택한 Linux window system·GPU·표시 환경<br>근거: [PHASE3_LINUX_XCB_RESIZE_REPORT](PHASE3_LINUX_XCB_RESIZE_REPORT.md), [summary.json](../benchmarks/reports/2026-10-26-phase3-linux-xcb-resize/summary.json) |
| [x] `P3-WSI-06`<br>완료 · 필수 | **창 숨김·최소화·닫기·surface 소실**<br>XCB backbuffer 입력·제출에 창/연결 검사와 sticky expiry를 적용하고 SURFACE_LOST를 보존해 새 surface로만 복구한다. 설치 SDK 예제에서 실제 WM/XCB 이벤트로 일시 정지·복원·닫기와 GPU drain을 연결했다.<br>완료: 실제 Xvfb/Openbox/software ICD 일반·고정 core/sync validation에서 각 11개 경우·거부 17개·전체 이미지 참조 19개·native present 20회 통과. 실제 숨김·WM 최소화/복원·닫기 protocol·창 파괴/XID 재사용·연결 단절과 별도 Vulkan 소실 status 제어 확인. 닫기 후 작업 전달·파괴 순서·validation 오류 0. Python 719 통과, Windows 63/65의 기존 fence 예외 2개 유지. 물리 GPU·육안·자연 driver 소실 수용과 구분. | 선행: `P3-WSI-05`<br>환경: 선택한 Linux window system·GPU·표시 환경<br>근거: [PHASE3_LINUX_XCB_EVENTS_REPORT](PHASE3_LINUX_XCB_EVENTS_REPORT.md), [summary.json](../benchmarks/reports/2026-10-27-phase3-linux-xcb-events/summary.json) |
| [x] `P3-WSI-07`<br>완료 · 필수 | **Linux 출력 자동 참조·validation**<br>같은 설치 Core에서 present·lifetime·resize·events·새 다중 창 소비자를 통합했다. 한 device에서 두 창을 동시 acquire하고 독립 resize/퇴역·서로 다른 전체 이미지 참조를 검증한다. private Openbox 시작 완료·생존 확인과 정규화한 실제 계층 로드 경로 판정을 추가했다.<br>완료: 실제 XCB/software ICD 일반·고정 core/sync validation의 다섯 suite 모두 성공. 구성마다 전체 참조 218개·native present 202회와 완료 wait, 새 두 창 matrix의 102개 이미지·24개 거부·모든 native 자원 회수 확인. 별도 core VUID 32개·sync hazard 1개·sync 비활성 비교·counter callback 1025개 검출, 잘못된 GPU 제출 0. Python 736 통과. 초기 WM 대기·계층 경로 판정 실패 원본 보존. 물리 GPU·사람의 Linux 표시·자연 driver 소실과 구분. | 선행: `P3-WSI-06`, `P3-LNX-11`<br>환경: 선택한 Linux window system·GPU·표시 환경<br>근거: [PHASE3_LINUX_XCB_MATRIX_REPORT](PHASE3_LINUX_XCB_MATRIX_REPORT.md), [summary.json](../benchmarks/reports/2026-10-27-phase3-linux-xcb-matrix/summary.json) |
| [ ] `P3-WSI-08`<br>미완료 · 필수 | **실제 Linux 표시 수용**<br>세션 바인딩 수동 표시 소비자·native GPU identity/driver 기록·빈 사용자 관측 양식을 준비하고 일반/고정 validation software smoke를 검증했다. 실제 사람이 보는 Linux 창의 출력·resize/닫기와 표시 경로 수용은 미완료다.<br>완료: 사람/환경/동작 관측 기록. 자동 readback과 물리 출력 수용을 구분. | 선행: `P3-WSI-07`<br>환경: 실제 Linux x86_64 X11 desktop·화면을 확인할 사람/표시 경로·명시 ICD와 정확한 adapter ID 필요. 사용자가 해당 환경 없음을 확인. 준비 도구 검증 완료; native run/사람 관측 0회. P3-LNX-15 물리 GPU 수용과 별도.<br>근거: [PHASE3_LINUX_XCB_MANUAL_READINESS](PHASE3_LINUX_XCB_MANUAL_READINESS.md), [README.md](../tests/linux_xcb_manual/README.md), [linux_xcb_manual.py](../tools/linux_xcb_manual.py), [summary.json](../benchmarks/reports/2026-10-27-phase3-linux-xcb-manual-readiness/summary.json), [linux-xcb-manual.yml](../.github/workflows/linux-xcb-manual.yml) |

### P3-CAP — 범위 결정이 필요한 기능·장치 확장

| 체크 · ID · 구분 | 작업 / 완료 기준 | 선행 · 환경 · 근거 |
|---|---|---|
| [x] `P3-CAP-01`<br>완료 · 필수 | **Vulkan debug marker**<br>공통 debugMarker를 optional debug-utils label에 연결했다. Windows NVIDIA/Intel 일반·required validation에서 실제 label 24개, Linux software에서 6개를 관측·전달했다. 함수 부재·UTF-8·thread/recording·퇴역을 검증했고 native 58개 및 로컬/Windows/Linux Python 667개가 통과했다. 초기 관측·CI 검증기·로컬 예외 실패를 보존했으며 Linux 물리 GPU 수용은 별도다.<br>완료: 실제 label 관측과 미지원/수명·thread 계약 검사. | 선행: `P3-SCP-01`<br>환경: debug-utils label 관측이 가능한 native GPU/driver/runtime 환경<br>근거: [PHASE3_SCOPE](PHASE3_SCOPE.md), [PHASE3_VULKAN_MARKER_REPORT](PHASE3_VULKAN_MARKER_REPORT.md), [summary.json](../benchmarks/reports/2026-10-26-phase3-vulkan-markers/summary.json), [vulkan-markers.yml](../.github/workflows/vulkan-markers.yml) |
| [x] `P3-CAP-02`<br>완료 · 범위 결정 후 | **Vulkan generation recovery**<br>사용자 지정으로 조건부 기능 선택. Vulkan 손실 generation 퇴역 후 최초 UUID adapter의 새 generation·실패 재시도·기존 handle/fence 거부·CPU 자원 재구축 구현.<br>완료: Windows NVIDIA/Intel 일반·필수 validation 및 Linux software ICD에서 submit/poll/fence 합성 손실·OOM/손실 재생성 실패·재시도·GPU 전체 참조 통과. 실제 TDR·물리 Linux GPU 수용과 구분. | 선행: `P3-SCP-01`<br>환경: 선택한 기능의 native GPU/driver/runtime 환경<br>범위 결정: `P3-SCP-01`<br>근거: [PHASE3_SCOPE](PHASE3_SCOPE.md), [PHASE3_VULKAN_RECOVERY_REPORT](PHASE3_VULKAN_RECOVERY_REPORT.md) |
| [ ] `P3-CAP-03`<br>미완료 · 범위 결정 후 | **Vulkan 손실 시험 경로**<br>초기 subset의 Vulkan loss injection은 미지원 유지. 선택 시 재현 가능한 시험 계약과 실제 native/합성 경계를 설계하며 예상 밖 TDR 수용과 구분한다.<br>완료: 선택 방식·실제 native/합성 경계와 error coverage 확인. | 선행: `P3-CAP-02`<br>환경: 선택한 기능의 native GPU/driver/runtime 환경<br>범위 결정: `P3-SCP-01`<br>근거: [PHASE3_SCOPE](PHASE3_SCOPE.md) |
| [ ] `P3-CAP-04`<br>미완료 · 범위 결정 후 | **Vulkan memory budget 계약**<br>공개 OS process budget과 같은 의미의 값을 제공할 근거가 없어 Unsupported를 유지한다. 별도 검토·구현 작업은 미완료이며 heap capacity로 대체하지 않는다.<br>완료: 정확한 의미·field mapping 또는 명시적 Unsupported 유지 근거. heap capacity로 대체 금지. | 선행: `P3-SCP-01`<br>환경: 선택한 기능의 native GPU/driver/runtime 환경<br>범위 결정: `P3-SCP-01`<br>근거: [PHASE3_SCOPE](PHASE3_SCOPE.md) |
| [ ] `P3-CAP-05`<br>미완료 · 범위 결정 후 | **현재 미지원 Intel 출력 확대**<br>현재 Intel UHD 770/32.0.101.6129의 출력은 미지원 유지하며 초기 legacy fallback은 미채택. 확대 선택 시 완료 보장을 유지하는 별도 경로와 해당 driver의 실제 검증을 구현한다.<br>완료: 해당 Intel GPU/driver에서 실제 present·수명·validation·참조 검사. | 선행: `P3-SCP-02`<br>환경: 선택한 기능의 native GPU/driver/runtime 환경<br>범위 결정: `P3-SCP-02`<br>근거: [PHASE3_WINDOWS_SUPPORT](PHASE3_WINDOWS_SUPPORT.md) |
| [ ] `P3-CAP-06`<br>미완료 · 범위 결정 후 | **추가 GPU/driver 실행**<br>초기 Windows 지원 후보는 P3-SCP-02 표의 정확한 조합으로 제한한다. 다른 GPU/driver/OS·software ICD로 범위를 넓히기 전 실제 장치별 지원·출력·수명·validation·설치 소비자 근거를 추가한다.<br>완료: 실제 식별된 장치별 수용 증거, 미보유/미지원은 통과로 세지 않음. | 선행: `P3-SCP-02`<br>환경: 선택한 기능의 native GPU/driver/runtime 환경<br>범위 결정: `P3-SCP-02`<br>근거: [PHASE3_WINDOWS_SUPPORT](PHASE3_WINDOWS_SUPPORT.md) |
| [ ] `P3-CAP-07`<br>미완료 · 범위 결정 후 | **GPU-assisted validation**<br>초기 subset은 core/sync validation을 필수로 유지한다. GPU-assisted는 별도 선택 기능이며 선택 시 설정·capability·오류 검출 controls와 실제 실행을 추가한다.<br>완료: 실제 GPU-assisted 실행 증거. 기존 sync validation과 구분. | 선행: `P3-SCP-01`<br>환경: 선택한 기능의 native GPU/driver/runtime 환경<br>범위 결정: `P3-SCP-01`<br>근거: [PHASE3_SCOPE](PHASE3_SCOPE.md) |

### P3-BEN — 현재 제품·동일 Vulkan 비교 기반

| 체크 · ID · 구분 | 작업 / 완료 기준 | 선행 · 환경 · 근거 |
|---|---|---|
| [x] `P3-BEN-01`<br>완료 · 필수 | **현재 제품용 비교 계약·build roots**<br>최종 비교할 source/SDK/driver/workload를 고정하고 과거 측정과 분리한다.<br>완료: 현재 fingerprint의 설치 SDK와 비교 소비자·raw receipt. 과거 timing 재귀속 금지. | 선행: `P3-SCP-01`<br>환경: 해당 플랫폼의 실제 비교 GPU·고정 의존성<br>근거: [PHASE3_COMPARISON_INPUTS_REPORT](PHASE3_COMPARISON_INPUTS_REPORT.md), [contract.json](../benchmarks/phase3_comparison/contract.json), [prepare_vulkan_comparison.py](../tools/prepare_vulkan_comparison.py), [summary.json](../benchmarks/reports/2026-10-27-phase3-comparison-inputs/summary.json), [receipt.json](../benchmarks/reports/validation/2026-10-27-phase3-comparison-inputs/accepted/receipt.json) |
| [x] `P3-BEN-02`<br>완료 · 필수 | **비교 정확성·분리 실행·통계**<br>같은 payload/참조/완료 범위와 validation/performance 별도 process를 연결한다.<br>완료: 동일 참조 통과 후 반복·warmup·P50/P95/P99/MAD·원본 출력 검증. | 선행: `P3-BEN-01`<br>환경: 해당 플랫폼의 실제 비교 GPU·고정 의존성<br>근거: [PHASE3_COMPARISON_PROTOCOL_REPORT](PHASE3_COMPARISON_PROTOCOL_REPORT.md), [protocol.json](../benchmarks/phase3_comparison/protocol.json), [run_vulkan_comparison.py](../tools/run_vulkan_comparison.py), [summary.json](../benchmarks/reports/2026-10-27-phase3-comparison-protocol/summary.json), [receipt.json](../benchmarks/reports/validation/2026-10-27-phase3-comparison-protocol/accepted/receipt.json) |
| [x] `P3-BEN-03`<br>완료 · 필수 | **현재 제품/SDL 업로드 재측정**<br>기존 upload 계약을 현재 제품으로 다시 실행한다.<br>완료: 지원 해상도/포맷별 참조·새 반복 측정·환경 일치. | 선행: `P3-BEN-02`<br>환경: 해당 플랫폼의 실제 비교 GPU·고정 의존성<br>근거: [PHASE3_CURRENT_UPLOAD_REPORT](PHASE3_CURRENT_UPLOAD_REPORT.md), [upload.json](../benchmarks/phase3_comparison/upload.json), [run_current_vulkan_upload.py](../tools/run_current_vulkan_upload.py), [summary.json](../benchmarks/reports/2026-10-27-phase3-current-upload/summary.json), [receipt.json](../benchmarks/reports/validation/2026-10-27-phase3-current-upload/accepted/receipt.json) |
| [x] `P3-BEN-04`<br>완료 · 필수 | **현재 제품/SDL draw 재측정**<br>사용자 선택한 고정 CPU 비교 계약으로 draw/instancing 규모와 shader/geometry·full readback을 맞춘다.<br>완료: draw 3종의 이미지 참조와 새 record/submit/wait/total CPU 구간·반복 통계. GPU timestamp 비교는 P3-BEN-09에서 별도 미완료. | 선행: `P3-BEN-02`<br>환경: 해당 플랫폼의 실제 비교 GPU·고정 의존성<br>근거: [PHASE3_DRAW_SCOPE](PHASE3_DRAW_SCOPE.md), [PHASE3_CURRENT_DRAW_REPORT](PHASE3_CURRENT_DRAW_REPORT.md), [draw.json](../benchmarks/phase3_comparison/draw.json), [run_current_vulkan_draw.py](../tools/run_current_vulkan_draw.py), [summary.json](../benchmarks/reports/2026-10-27-phase3-current-draw/summary.json), [receipt.json](../benchmarks/reports/validation/2026-10-27-phase3-current-draw/accepted/receipt.json) |
| [x] `P3-BEN-05`<br>완료 · 필수 | **현재 제품/SDL PSO 재측정**<br>동일 shader·생성/공개 해제 구간과 cache 조건 경계를 명시한다.<br>완료: 실제 pipeline GPU 사용·새 반복 수치. driver cache 통제 여부 구분. | 선행: `P3-BEN-02`<br>환경: 해당 플랫폼의 실제 비교 GPU·고정 의존성<br>근거: [PHASE3_CURRENT_PSO_REPORT](PHASE3_CURRENT_PSO_REPORT.md), [PHASE3_PSO_SCOPE](PHASE3_PSO_SCOPE.md), [pso.json](../benchmarks/phase3_comparison/pso.json), [run_current_vulkan_pso.py](../tools/run_current_vulkan_pso.py), [summary.json](../benchmarks/reports/2026-10-27-phase3-current-pso/summary.json), [receipt.json](../benchmarks/reports/validation/2026-10-27-phase3-current-pso/accepted/receipt.json) |
| [x] `P3-BEN-06`<br>완료 · 필수 | **현재 제품/SDL 자원 생성 재측정**<br>동일 buffer/texture 크기·개수와 공개 해제 범위를 맞춘다.<br>완료: 전체 자원 readback·현재 제품의 반복 생성/해제 측정. 물리 회수 주장 제외. | 선행: `P3-BEN-02`<br>환경: 해당 플랫폼의 실제 비교 GPU·고정 의존성<br>근거: [PHASE3_CURRENT_ALLOCATION_REPORT](PHASE3_CURRENT_ALLOCATION_REPORT.md), [PHASE3_ALLOCATION_SCOPE](PHASE3_ALLOCATION_SCOPE.md), [allocation.json](../benchmarks/phase3_comparison/allocation.json), [run_current_vulkan_allocation.py](../tools/run_current_vulkan_allocation.py), [summary.json](../benchmarks/reports/2026-10-27-phase3-current-allocation/summary.json), [receipt.json](../benchmarks/reports/validation/2026-10-27-phase3-current-allocation/accepted/receipt.json) |
| [ ] `P3-BEN-07`<br>미완료 · 필수 | **Linux 비교 환경 probe 이식**<br>Windows DXGI 중심 helper와 환경 식별을 Linux native 방식으로 분리한다.<br>완료: Linux GPU/driver/compiler/validation의 실제 receipt와 동일 장치 검증. | 선행: `P3-LNX-11`, `P3-BEN-01`<br>환경: Linux native probe 이식·실제 software/VVL·거부 대조 준비 검증 완료. 실제 Linux 물리 GPU/ICD·정확한 UUID와 SDK/SDL 동일 장치·driver/compiler·필수 validation 원본이 필요. software 성공은 최종 완료로 합산하지 않음.<br>근거: [PHASE3_LINUX_COMPARISON_PROBE_REPORT](PHASE3_LINUX_COMPARISON_PROBE_REPORT.md), [README.md](../benchmarks/linux_comparison/README.md), [linux_comparison.py](../tools/linux_comparison.py), [linux-comparison-probe.yml](../.github/workflows/linux-comparison-probe.yml), [linux-comparison-physical.yml](../.github/workflows/linux-comparison-physical.yml), [summary.json](../benchmarks/reports/2026-10-27-phase3-linux-comparison-probe/summary.json), [receipt.json](../benchmarks/reports/validation/2026-10-27-phase3-linux-comparison-probe/native/linux-comparison-evidence/receipt.json) |
| [ ] `P3-BEN-08`<br>미완료 · 필수 | **Linux benchmark build 이식**<br>공통 benchmark/의존성/설치 SDK 소비자의 Windows 의존성을 분리한다.<br>완료: 선택된 Vulkan 비교 프로그램의 native Linux build·실행 probe. | 선행: `P3-BEN-07`<br>환경: 4종 소비자 native Linux build·software probe/대표 validation smoke·거부 대조 준비 검증 완료. 선행 P3-BEN-07의 실제 물리 GPU qualification과 같은 환경의 build/probe가 있어야 최종 완료. 전체 Linux 사례·성능 비교는 별도 미완료.<br>근거: [PHASE3_LINUX_WORKLOAD_BUILD_REPORT](PHASE3_LINUX_WORKLOAD_BUILD_REPORT.md), [README.md](../benchmarks/linux_workloads/README.md), [linux_workloads.py](../tools/linux_workloads.py), [port_linux_benchmarks.py](../tools/port_linux_benchmarks.py), [summary.json](../benchmarks/reports/2026-10-28-phase3-linux-workloads/summary.json), [verify.py](../benchmarks/reports/validation/2026-10-28-phase3-linux-workloads/verify.py) |
| [ ] `P3-BEN-09`<br>미완료 · 필수 | **SDK/SDL 동일 draw GPU timestamp 비교**<br>사용자 선택으로 P3-BEN-04에서 분리한 GPU 계측. 같은 draw 구간의 native GPU timestamp 경로와 정확한 장치/queue 귀속을 양 SDK에서 검증한다.<br>완료: GPU 전용 process의 실제 counter·valid bits/period·availability/fence·동일 구간과 계측 영향/참조 검증. CPU wait/total을 GPU 시간으로 대체하지 않음. | 선행: `P3-BEN-04`<br>환경: 고정 SDL 공개 API에 timestamp가 없어 별도 native 계측 경로와 실제 GPU가 필요<br>근거: [PHASE3_DRAW_SCOPE](PHASE3_DRAW_SCOPE.md) |

### P3-CMP — 추가 Vulkan 경쟁 SDK

| 체크 · ID · 구분 | 작업 / 완료 기준 | 선행 · 환경 · 근거 |
|---|---|---|
| [x] `P3-CMP-01`<br>완료 · 필수 | **경쟁 SDK 지원·동등 계약 표**<br>고정 LLGL/Diligent/bgfx/Forge 등의 실제 Vulkan 지원·라이선스·작업 대응을 확인한다.<br>완료: 우선 추가 비교 대상과 나머지 대상의 지원/제외 이유·선택 작업 ID. 필수 요구 자동 축소 금지. | 선행: `P3-SCP-01`, `P3-BEN-01`<br>근거: [PHASE3_COMPETITOR_CONTRACT_REPORT](PHASE3_COMPETITOR_CONTRACT_REPORT.md), [selection.json](../benchmarks/phase3_comparison/competitors/selection.json), [source-review.json](../benchmarks/phase3_comparison/competitors/source-review.json), [summary.json](../benchmarks/reports/validation/2026-10-28-phase3-competitor-contracts/summary.json) |
| [x] `P3-CMP-02`<br>완료 · 필수 | **LLGL Vulkan build·장치 probe**<br>고정 원본으로 독립 Vulkan adapter를 구성한다.<br>완료: 실제 backend/GPU 식별, 입력 감사·최소 참조 실행. | 선행: `P3-CMP-01`<br>환경: LLGL Vulkan 지원 native 환경<br>근거: [PHASE3_LLGL_VULKAN_PROBE_REPORT](PHASE3_LLGL_VULKAN_PROBE_REPORT.md), [summary.json](../benchmarks/reports/validation/2026-10-28-phase3-llgl-vulkan/summary.json), [verify.py](../benchmarks/reports/validation/2026-10-28-phase3-llgl-vulkan/verify.py) |
| [x] `P3-CMP-03`<br>완료 · 필수 | **LLGL 업로드 correctness adapter**<br>기존 GraphicsSDK/SDL의 업로드 작업 의미·완료 범위를 맞춘다.<br>완료: 전체 참조·native validation·지원 차이와 정당한 비교 가능 여부 기록. | 선행: `P3-CMP-02`, `P3-BEN-02`<br>환경: LLGL Vulkan 실제 GPU<br>근거: [PHASE3_LLGL_UPLOAD_REPORT](PHASE3_LLGL_UPLOAD_REPORT.md), [PHASE3_LLGL_UPLOAD_SCOPE](PHASE3_LLGL_UPLOAD_SCOPE.md), [llgl-transfer-src-profile.json](../benchmarks/phase3_comparison/competitors/llgl-transfer-src-profile.json), [summary.json](../benchmarks/reports/validation/2026-10-28-phase3-llgl-upload/summary.json) |
| [x] `P3-CMP-04`<br>완료 · 필수 | **LLGL draw/instancing correctness adapter**<br>기존 GraphicsSDK/SDL의 draw/instancing 작업 의미·완료 범위를 맞춘다.<br>완료: 전체 참조·native validation·지원 차이와 정당한 비교 가능 여부 기록. | 선행: `P3-CMP-02`, `P3-BEN-02`<br>환경: LLGL Vulkan 실제 GPU<br>근거: [PHASE3_LLGL_DRAW_REPORT](PHASE3_LLGL_DRAW_REPORT.md), [llgl-transfer-src-draw.json](../benchmarks/phase3_comparison/competitors/llgl-transfer-src-draw.json), [summary.json](../benchmarks/reports/validation/2026-10-28-phase3-llgl-draw/summary.json) |
| [ ] `P3-CMP-05`<br>미완료 · 필수 | **LLGL PSO correctness adapter**<br>기존 GraphicsSDK/SDL의 PSO 작업 의미·완료 범위를 맞춘다.<br>완료: 전체 참조·native validation·지원 차이와 정당한 비교 가능 여부 기록. | 선행: `P3-CMP-02`, `P3-BEN-02`<br>환경: LLGL Vulkan 실제 GPU |
| [ ] `P3-CMP-06`<br>미완료 · 필수 | **LLGL buffer/texture 생성 correctness adapter**<br>기존 GraphicsSDK/SDL의 buffer/texture 생성 작업 의미·완료 범위를 맞춘다.<br>완료: 전체 참조·native validation·지원 차이와 정당한 비교 가능 여부 기록. | 선행: `P3-CMP-02`, `P3-BEN-02`<br>환경: LLGL Vulkan 실제 GPU |
| [ ] `P3-CMP-07`<br>미완료 · 필수 | **LLGL 업로드 비교 실행**<br>승인된 업로드 계약을 같은 환경에서 반복 측정한다.<br>완료: 현재 제품/SDL/LLGL의 fresh 원본·통계·환경 일치 및 차이 분석. | 선행: `P3-CMP-03`, `P3-BEN-03`<br>환경: 세 SDK가 같은 GPU/driver/workload를 실행하는 환경 |
| [ ] `P3-CMP-08`<br>미완료 · 필수 | **LLGL draw/instancing 비교 실행**<br>승인된 draw/instancing 계약을 같은 환경에서 반복 측정한다.<br>완료: 현재 제품/SDL/LLGL의 fresh 원본·통계·환경 일치 및 차이 분석. | 선행: `P3-CMP-04`, `P3-BEN-04`<br>환경: 세 SDK가 같은 GPU/driver/workload를 실행하는 환경 |
| [ ] `P3-CMP-09`<br>미완료 · 필수 | **LLGL PSO 비교 실행**<br>승인된 PSO 계약을 같은 환경에서 반복 측정한다.<br>완료: 현재 제품/SDL/LLGL의 fresh 원본·통계·환경 일치 및 차이 분석. | 선행: `P3-CMP-05`, `P3-BEN-05`<br>환경: 세 SDK가 같은 GPU/driver/workload를 실행하는 환경 |
| [ ] `P3-CMP-10`<br>미완료 · 필수 | **LLGL 자원 생성 비교 실행**<br>승인된 자원 생성 계약을 같은 환경에서 반복 측정한다.<br>완료: 현재 제품/SDL/LLGL의 fresh 원본·통계·환경 일치 및 차이 분석. | 선행: `P3-CMP-06`, `P3-BEN-06`<br>환경: 세 SDK가 같은 GPU/driver/workload를 실행하는 환경 |
| [ ] `P3-CMP-11`<br>미완료 · 범위 결정 후 | **Diligent Vulkan 비교 확대**<br>선택된 작업을 correctness adapter와 측정 작업으로 추가 분할한다.<br>완료: 선택 workload별 실제 참조·반복 측정·의존성 감사 또는 지원 불가 근거. | 선행: `P3-CMP-01`<br>환경: Diligent의 실제 지원 Vulkan 환경<br>범위 결정: `P3-CMP-01` |
| [ ] `P3-CMP-12`<br>미완료 · 범위 결정 후 | **bgfx Vulkan 비교 확대**<br>선택된 작업을 correctness adapter와 측정 작업으로 추가 분할한다.<br>완료: 선택 workload별 실제 참조·반복 측정·의존성 감사 또는 지원 불가 근거. | 선행: `P3-CMP-01`<br>환경: bgfx의 실제 지원 Vulkan 환경<br>범위 결정: `P3-CMP-01` |
| [ ] `P3-CMP-13`<br>미완료 · 범위 결정 후 | **The Forge Vulkan 비교 확대**<br>선택된 작업을 correctness adapter와 측정 작업으로 추가 분할한다.<br>완료: 선택 workload별 실제 참조·반복 측정·의존성 감사 또는 지원 불가 근거. | 선행: `P3-CMP-01`<br>환경: The Forge의 실제 지원 Vulkan 환경<br>범위 결정: `P3-CMP-01` |

### P3-LBEN — Linux 동일 작업 측정

| 체크 · ID · 구분 | 작업 / 완료 기준 | 선행 · 환경 · 근거 |
|---|---|---|
| [ ] `P3-LBEN-01`<br>미완료 · 필수 | **Linux 업로드 비교**<br>현재 제품·SDL·선택 경쟁 SDK의 업로드 계약을 Linux에서 실행한다.<br>완료: Linux 전체 참조와 validation/performance 분리·fresh 반복 통계. Windows 값 재사용 금지. | 선행: `P3-BEN-08`, `P3-CMP-01`, `P3-BEN-03`<br>환경: 동일 Linux GPU/driver의 지원 SDK 조합 |
| [ ] `P3-LBEN-02`<br>미완료 · 필수 | **Linux draw/instancing 비교**<br>현재 제품·SDL·선택 경쟁 SDK의 draw/instancing 계약을 Linux에서 실행한다.<br>완료: Linux 전체 참조와 validation/performance 분리·fresh 반복 통계. Windows 값 재사용 금지. | 선행: `P3-BEN-08`, `P3-CMP-01`, `P3-BEN-04`<br>환경: 동일 Linux GPU/driver의 지원 SDK 조합 |
| [ ] `P3-LBEN-03`<br>미완료 · 필수 | **Linux PSO 비교**<br>현재 제품·SDL·선택 경쟁 SDK의 PSO 계약을 Linux에서 실행한다.<br>완료: Linux 전체 참조와 validation/performance 분리·fresh 반복 통계. Windows 값 재사용 금지. | 선행: `P3-BEN-08`, `P3-CMP-01`, `P3-BEN-05`<br>환경: 동일 Linux GPU/driver의 지원 SDK 조합 |
| [ ] `P3-LBEN-04`<br>미완료 · 필수 | **Linux 자원 생성 비교**<br>현재 제품·SDL·선택 경쟁 SDK의 자원 생성 계약을 Linux에서 실행한다.<br>완료: Linux 전체 참조와 validation/performance 분리·fresh 반복 통계. Windows 값 재사용 금지. | 선행: `P3-BEN-08`, `P3-CMP-01`, `P3-BEN-06`<br>환경: 동일 Linux GPU/driver의 지원 SDK 조합 |

### P3-GATE — 최종 Phase 3 수용

| 체크 · ID · 구분 | 작업 / 완료 기준 | 선행 · 환경 · 근거 |
|---|---|---|
| [ ] `P3-GATE-01`<br>미완료 · 필수 | **현재 지원 구성 통합 회귀**<br>Windows/Linux 지원표·headless/WSI·공통 RHI·필수 debug marker·미지원 응답과 조건부 제외 이유를 현재 source로 검증한다.<br>완료: 필수 구성의 실제 참조·validation·수명·설치 소비자와 선택된 조건부 작업 처리 근거. | 선행: `P3-SCP-02`, `P3-LNX-12`, `P3-LNX-13`, `P3-WSI-08`, `P3-CAP-01`, `P3-LNX-14`, `P3-LNX-15` |
| [ ] `P3-GATE-02`<br>미완료 · 필수 | **최종 비교·감사·한계 보고서**<br>현재 제품과 지원 경쟁 조합의 원본·통계·라이선스 입력을 종합한다.<br>완료: 새 측정과 과거 자료가 분리된 비교표, 미지원·미측정·환경 한계 및 재현 명령. | 선행: `P3-BEN-03`, `P3-BEN-04`, `P3-BEN-09`, `P3-BEN-05`, `P3-BEN-06`, `P3-CMP-07`, `P3-CMP-08`, `P3-CMP-09`, `P3-CMP-10`, `P3-LBEN-01`, `P3-LBEN-02`, `P3-LBEN-03`, `P3-LBEN-04` |
| [ ] `P3-GATE-03`<br>미완료 · 필수 | **Phase 3 완료 판정**<br>필수 작업·지원 범위·조건부 처리·근거를 확인하고 단계 수용을 기록한다.<br>완료: P3 필수 작업 완료와 선택 작업의 완료/지원 제외 근거. 보류 Phase 2·미채택 최적화를 완료로 바꾸지 않음. | 선행: `P3-GATE-01`, `P3-GATE-02` |

## Phase 4 — Metal

후속 단계. 실제 Mac compile/runtime/performance를 구분하며 환경 부재를 완료로 표시하지 않는다.

### P4-MTL — Metal 세부 실행 순서

| 체크 · ID · 구분 | 작업 / 완료 기준 | 선행 · 환경 · 근거 |
|---|---|---|
| [ ] `P4-MTL-01`<br>미완료 · 필수 | **Mac 환경·native toolchain 확인**<br>실제 Mac/compiler/GPU/Metal 검증 범위를 기록한다.<br>완료: 실제 환경 receipt, 가용하지 않으면 runtime 수용 미완료. | 선행: `P3-GATE-03`<br>환경: 실제 Mac toolchain/GPU; compile/runtime/measurement를 구분 |
| [ ] `P4-MTL-02`<br>미완료 · 필수 | **Metal 장치·queue·submission**<br>Objective-C++ 내부 구현과 C++ 공개 API를 연결한다.<br>완료: 실제 장치·제출·오류 계약 실행. | 선행: `P4-MTL-01`<br>환경: 실제 Mac toolchain/GPU; compile/runtime/measurement를 구분 |
| [ ] `P4-MTL-03`<br>미완료 · 필수 | **Metal buffers·동기화·수명**<br>Upload/device/readback·완료 보유와 버전 계약 구현.<br>완료: 전체 buffer 참조와 stale/수명 회귀 통과. | 선행: `P4-MTL-02`<br>환경: 실제 Mac toolchain/GPU; compile/runtime/measurement를 구분 |
| [ ] `P4-MTL-04`<br>미완료 · 필수 | **Metal textures·전송·pass**<br>공통 색/깊이 texture와 pass 의미를 구현한다.<br>완료: 전체 texel/padding·Clear/Load 참조. | 선행: `P4-MTL-03`<br>환경: 실제 Mac toolchain/GPU; compile/runtime/measurement를 구분 |
| [ ] `P4-MTL-05`<br>미완료 · 필수 | **Metal graphics·입력·binding**<br>사전 검증 shader fixture로 common draw/depth/MRT/binding 구현.<br>완료: 동일 이미지·vertex/index/바인딩·수명 참조. | 선행: `P4-MTL-04`<br>환경: 실제 Mac toolchain/GPU; compile/runtime/measurement를 구분 |
| [ ] `P4-MTL-06`<br>미완료 · 필수 | **Metal 화면 출력**<br>지원되는 Mac 표면·drawable·완료·resize/종료를 구현한다.<br>완료: 실제 출력·참조·drawable 수명 수용. | 선행: `P4-MTL-05`<br>환경: 실제 Mac toolchain/GPU; compile/runtime/measurement를 구분 |
| [ ] `P4-MTL-07`<br>미완료 · 필수 | **Metal native validation·회귀**<br>지원되는 native validation과 오류 controls를 구성한다.<br>완료: Mac 실제 실행 결과와 진단 한계 명시. | 선행: `P4-MTL-06`<br>환경: 실제 Mac toolchain/GPU; compile/runtime/measurement를 구분 |
| [ ] `P4-MTL-08`<br>미완료 · 필수 | **동일 Metal 경쟁 비교**<br>지원 SDK와 같은 GPU/workload 계약으로 비교한다.<br>완료: 참조 일치·분리 측정·라이선스 감사. | 선행: `P4-MTL-07`<br>환경: 실제 Mac toolchain/GPU; compile/runtime/measurement를 구분 |
| [ ] `P4-MTL-09`<br>미완료 · 필수 | **Mac 설치 소비자 수용**<br>설치된 SDK로 독립 C++ 소비자 build/runtime 검사.<br>완료: 실제 find_package·native 참조 실행과 입력 감사. | 선행: `P4-MTL-08`<br>환경: 실제 Mac toolchain/GPU; compile/runtime/measurement를 구분 |

## Phase 5 — Shader pipeline

DXC DXIL/SPIR-V·MSL·일반 reflection·cache. 현재 제한된 SPIR-V drawing과 구분.

### P5-SHD — Shader pipeline

| 체크 · ID · 구분 | 작업 / 완료 기준 | 선행 · 환경 · 근거 |
|---|---|---|
| [ ] `P5-SHD-01`<br>미완료 · 필수 | **고정 shader toolchain·지원 계약**<br>DXC/SPIRV-Cross 버전·라이선스·입력/출력 계약 고정.<br>완료: 실제 tool receipt와 지원 target/version 표. | 선행: `P4-MTL-09`<br>환경: 지원 target의 실제 compiler/runtime |
| [ ] `P5-SHD-02`<br>미완료 · 필수 | **DXC → DXIL**<br>기존 DXBC bootstrap과 구분된 DXIL compile 경로.<br>완료: 실제 D3D12 shader 사용·진단·참조 이미지. | 선행: `P5-SHD-01`<br>환경: 지원 target의 실제 compiler/runtime |
| [ ] `P5-SHD-03`<br>미완료 · 필수 | **DXC → SPIR-V**<br>Vulkan target용 compile 및 외부 semantic validation 연결.<br>완료: 실제 Vulkan shader 사용·동일 참조와 잘못된 입력 거부. | 선행: `P5-SHD-02`<br>환경: 지원 target의 실제 compiler/runtime |
| [ ] `P5-SHD-04`<br>미완료 · 필수 | **SPIRV-Cross → MSL**<br>Metal target 변환과 실제 Mac 사용 연결.<br>완료: 실제 Metal compile/runtime 참조와 변환 한계. | 선행: `P5-SHD-03`<br>환경: 지원 target의 실제 compiler/runtime |
| [ ] `P5-SHD-05`<br>미완료 · 필수 | **일반 reflection·binding 검증**<br>현재 제한된 SPIR-V framing 검사를 일반 reflection으로 확장.<br>완료: descriptor/stage/layout 일치와 잘못된 shader/layout 거부. | 선행: `P5-SHD-04`<br>환경: 지원 target의 실제 compiler/runtime |
| [ ] `P5-SHD-06`<br>미완료 · 필수 | **Compile 진단·cache**<br>오류 위치/target diagnostics와 명시적 cache key·무효화 구현.<br>완료: cold/warm·변경 입력·손상 cache의 정확성 검증. | 선행: `P5-SHD-05`<br>환경: 지원 target의 실제 compiler/runtime |
| [ ] `P5-SHD-07`<br>미완료 · 필수 | **Pipeline cache 계약·구현**<br>지원 backend cache key·수명·무효화 의미 정의.<br>완료: 실제 hit/miss와 shader/layout/device 변경 검증. | 선행: `P5-SHD-06`<br>환경: 지원 target의 실제 compiler/runtime |
| [ ] `P5-SHD-08`<br>미완료 · 필수 | **Shader/PSO 공정 측정**<br>Compile/cache와 pipeline 생성 구간을 분리해 비교.<br>완료: 같은 target·cache 상태·참조를 가진 반복 수치. | 선행: `P5-SHD-07`<br>환경: 지원 target의 실제 compiler/runtime |

## Phase 6 — GPU 자원 최적화

완료된 후보 관측과 남은 정책/성능/채택을 구분한다. 이 목록 전체를 Phase 3 선행 gate로 만들지 않는다.

### P6-EXP — 완료된 최적화 후보 관측 — 제품 채택과 구분

| 체크 · ID · 구분 | 작업 / 완료 기준 | 선행 · 환경 · 근거 |
|---|---|---|
| [x] `P6-EXP-01`<br>완료 · 후속 최적화 | **Persistent mapping 실험·기각**<br>해당 fingerprint의 관측·정확성 증거를 보존. 제품 승격·물리 메모리 수용 의미 아님.<br>완료: 연결된 보고서의 당시 플랫폼·입력·실행 범위에서 확인됨. | 선행: 없음<br>근거: [PHASE3_VULKAN_UPLOAD_MEMORY_REPORT](PHASE3_VULKAN_UPLOAD_MEMORY_REPORT.md) |
| [x] `P6-EXP-02`<br>완료 · 후속 최적화 | **Host-cached Upload 선호 실험·기각**<br>해당 fingerprint의 관측·정확성 증거를 보존. 제품 승격·물리 메모리 수용 의미 아님.<br>완료: 연결된 보고서의 당시 플랫폼·입력·실행 범위에서 확인됨. | 선행: 없음<br>근거: [PHASE3_VULKAN_UPLOAD_MEMORY_REPORT](PHASE3_VULKAN_UPLOAD_MEMORY_REPORT.md) |
| [x] `P6-EXP-03`<br>완료 · 후속 최적화 | **고정 64KiB pool 실험·미채택**<br>해당 fingerprint의 관측·정확성 증거를 보존. 제품 승격·물리 메모리 수용 의미 아님.<br>완료: 연결된 보고서의 당시 플랫폼·입력·실행 범위에서 확인됨. | 선행: 없음<br>근거: [PHASE3_VULKAN_BUFFER_POOL_REPORT](PHASE3_VULKAN_BUFFER_POOL_REPORT.md) |
| [x] `P6-EXP-04`<br>완료 · 후속 최적화 | **적응형 pool 실험·미채택**<br>해당 fingerprint의 관측·정확성 증거를 보존. 제품 승격·물리 메모리 수용 의미 아님.<br>완료: 연결된 보고서의 당시 플랫폼·입력·실행 범위에서 확인됨. | 선행: 없음<br>근거: [PHASE3_VULKAN_ADAPTIVE_BUFFER_POOL_REPORT](PHASE3_VULKAN_ADAPTIVE_BUFFER_POOL_REPORT.md) |
| [x] `P6-EXP-05`<br>완료 · 후속 최적화 | **현재 후보 혼합 churn·희소 점유 검증**<br>해당 fingerprint의 관측·정확성 증거를 보존. 제품 승격·물리 메모리 수용 의미 아님.<br>완료: 연결된 보고서의 당시 플랫폼·입력·실행 범위에서 확인됨. | 선행: 없음<br>근거: [VULKAN_POOL_CHURN_REPORT](VULKAN_POOL_CHURN_REPORT.md) |
| [x] `P6-EXP-06`<br>완료 · 후속 최적화 | **현재 후보 OOM·exact fallback 전체 재시험**<br>해당 fingerprint의 관측·정확성 증거를 보존. 제품 승격·물리 메모리 수용 의미 아님.<br>완료: 연결된 보고서의 당시 플랫폼·입력·실행 범위에서 확인됨. | 선행: 없음<br>근거: [VULKAN_POOL_REBASE_FAULT_REPORT](VULKAN_POOL_REBASE_FAULT_REPORT.md) |

### P6-POOL — 소형 buffer pool 후보의 남은 결정

| 체크 · ID · 구분 | 작업 / 완료 기준 | 선행 · 환경 · 근거 |
|---|---|---|
| [ ] `P6-POOL-01`<br>미완료 · 후속 최적화 | **예약량 목표·workload 계약**<br>관측 6.2배 사례를 포함해 허용 비용·same-survivor 비교를 명시.<br>완료: 임의 우위 목표 대신 workload·bytes·지연의 측정/채택 기준. | 선행: `P6-EXP-06` |
| [ ] `P6-POOL-02`<br>미완료 · 후속 최적화 | **예약량 제한 후보 구현**<br>block 크기·성장·대상 임계값 후보를 새 격리 source로 구현.<br>완료: 정책 diff·source receipt와 기존 후보/제품 보존. | 선행: `P6-POOL-01` |
| [ ] `P6-POOL-03`<br>미완료 · 후속 최적화 | **변경 후보의 정확성·장애 회귀**<br>바뀐 fingerprint에 CPU/GPU/혼합 보유/OOM/fallback를 다시 실행.<br>완료: 실제 새 후보의 참조·수명·rollback 통과. | 선행: `P6-POOL-02` |
| [ ] `P6-POOL-04`<br>미완료 · 후속 최적화 | **현재 후보 fresh 성능 비교**<br>동일 payload·생존자 분포·환경에서 dedicated/후보/지원 경쟁 비교.<br>완료: 예약량·할당 calls·create/release 지연의 새 반복 통계. | 선행: `P6-POOL-03` |
| [ ] `P6-POOL-05`<br>미완료 · 후속 최적화 | **후보 채택 또는 기각**<br>정확성·비용을 근거로 기본값/opt-in/기각을 결정.<br>완료: 결정 이유·회귀·API/통계 영향·최종 제품 증거. 채택을 전제하지 않음. | 선행: `P6-POOL-04` |

### P6-OPT — 다른 최적화 — 병목 증거 후 개별 진행

| 체크 · ID · 구분 | 작업 / 완료 기준 | 선행 · 환경 · 근거 |
|---|---|---|
| [ ] `P6-OPT-01`<br>미완료 · 후속 최적화 | **Upload ring**<br>별도 baseline/병목·정확성 계약을 먼저 만들고 구현/측정 ID로 추가 분할한다.<br>완료: 새 후보별 참조·수명 회귀와 전후 실제 수치, 채택/기각 이유. | 선행: `P5-SHD-08` |
| [ ] `P6-OPT-02`<br>미완료 · 후속 최적화 | **Frame-in-flight slots**<br>별도 baseline/병목·정확성 계약을 먼저 만들고 구현/측정 ID로 추가 분할한다.<br>완료: 새 후보별 참조·수명 회귀와 전후 실제 수치, 채택/기각 이유. | 선행: `P5-SHD-08` |
| [ ] `P6-OPT-03`<br>미완료 · 후속 최적화 | **Deferred destruction**<br>별도 baseline/병목·정확성 계약을 먼저 만들고 구현/측정 ID로 추가 분할한다.<br>완료: 새 후보별 참조·수명 회귀와 전후 실제 수치, 채택/기각 이유. | 선행: `P5-SHD-08` |
| [ ] `P6-OPT-04`<br>미완료 · 후속 최적화 | **Descriptor cache/pool**<br>별도 baseline/병목·정확성 계약을 먼저 만들고 구현/측정 ID로 추가 분할한다.<br>완료: 새 후보별 참조·수명 회귀와 전후 실제 수치, 채택/기각 이유. | 선행: `P5-SHD-08` |
| [ ] `P6-OPT-05`<br>미완료 · 후속 최적화 | **Query/render-pass 등 cache 재사용**<br>별도 baseline/병목·정확성 계약을 먼저 만들고 구현/측정 ID로 추가 분할한다.<br>완료: 새 후보별 참조·수명 회귀와 전후 실제 수치, 채택/기각 이유. | 선행: `P5-SHD-08` |
| [ ] `P6-OPT-06`<br>미완료 · 후속 최적화 | **동기화·command recording 비용**<br>별도 baseline/병목·정확성 계약을 먼저 만들고 구현/측정 ID로 추가 분할한다.<br>완료: 새 후보별 참조·수명 회귀와 전후 실제 수치, 채택/기각 이유. | 선행: `P5-SHD-08` |

## Phase 7 — 독립 SDK 배포

Windows 설치 기반은 있음. 전체 지원 플랫폼의 배포·감사·CI·릴리스 수용은 미완료.

### P7-PKG — 독립 SDK 배포

| 체크 · ID · 구분 | 작업 / 완료 기준 | 선행 · 환경 · 근거 |
|---|---|---|
| [x] `P7-PKG-01`<br>완료 · 필수 | **Windows 설치·외부 소비자 기반**<br>현재 Windows install/find_package smoke와 좁은 package 검증. 전체 다중 플랫폼 배포 아님.<br>완료: 연결된 보고서의 당시 플랫폼·입력·실행 범위에서 확인됨. | 선행: 없음<br>근거: [BUILD](BUILD.md) |
| [ ] `P7-PKG-02`<br>미완료 · 필수 | **배포 OS/configuration·API 계약 고정**<br>지원 platform/compiler/backend·version/ABI 범위를 정리.<br>완료: 지원/미지원 표와 릴리스 수용 기준. | 선행: `P5-SHD-08` |
| [ ] `P7-PKG-03`<br>미완료 · 필수 | **Linux release payload**<br>Phase 3 설치 검증과 별도로 배포 payload·symbols·의존성 구성.<br>완료: 정확한 package member/notice·외부 소비자 검사. | 선행: `P7-PKG-02` |
| [ ] `P7-PKG-04`<br>미완료 · 필수 | **Mac release payload**<br>실제 Mac library/header/metadata와 배포 전제 정리.<br>완료: 실제 package·consumer·runtime 수용. | 선행: `P7-PKG-03` |
| [ ] `P7-PKG-05`<br>미완료 · 필수 | **공통 배포 감사·무결성**<br>OS/config별 실제 compiler/linker closure·출처·notice·추가파일 검사.<br>완료: 누락/변경/추가 payload 거부와 재현 receipt. | 선행: `P7-PKG-04` |
| [ ] `P7-PKG-06`<br>미완료 · 필수 | **지원 구성 CI·release smoke**<br>native build/CPU/software/GPU 검사 차이를 명확히 분리.<br>완료: 실제 CI 결과와 hardware 미실행 범위 명시. | 선행: `P7-PKG-05` |
| [ ] `P7-PKG-07`<br>미완료 · 필수 | **릴리스 최종 수용·문서**<br>예제·설치·API·재현 명령·알려진 제한을 종합한다.<br>완료: 지원 범위별 증거와 versioned artifact·release notes. | 선행: `P7-PKG-06` |
