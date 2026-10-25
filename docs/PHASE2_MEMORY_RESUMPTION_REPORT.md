# Phase 2 — 장시간 메모리 수용 재개 결과

2026-10-25 · `P2-ACC-01` 사용자 명시 재개 · 실행 소스 커밋 `a93af3b`.

**NVIDIA GPU 15분과 WARP 5분은 기존 메모리 안정화 기준을 통과했으나, full GBV는 워밍업 중 512MiB 증가 한도로 중단됐다. 전체 세 모드 수용은 실패이며 `P2-ACC-01`은 필수 미완료로 유지한다.** 재개 전의 실패 기록·정책은 그대로 보존하고, Intel AV 원인 조사 `P2-ACC-02`는 재개하지 않았다.

[요약·해시](../benchmarks/reports/2026-10-25-phase2-memory-resumed/summary.json), [전체 재개 receipt](../benchmarks/reports/validation/2026-10-25-phase2-memory-resumed/native/resumption.json), [실행 전후 환경 비교](../benchmarks/reports/validation/2026-10-25-phase2-memory-resumed/native/environment-comparison.json), [실제 source ZIP manifest](../benchmarks/reports/validation/2026-10-25-phase2-memory-resumed/source-manifest.json)에 연결한다.

## 준비와 원래 기준 보존

기존 `out/core`의 검사 실행 파일은 현재 SDK fingerprint와 달랐다. 옛 executable/library를 별도 `out/memory-resume-prior-build`에 복사하고 해시를 보존한 뒤 현재 SDK로 다시 빌드했다. 새 설치 prefix와 설치 SDK 소비자인 presentation 열거 도구도 별도 경로에 빌드했다. 이번에는 SDK C++·공개 API·root `CMakeLists.txt`를 수정하지 않았다. 재빌드와 install·열거 원본은 [setup](../benchmarks/reports/validation/2026-10-25-phase2-memory-resumed/setup/)에 보존한다.

새 열거에서 NVIDIA GTX 1660 SUPER의 ID는 `96751`, Intel UHD 770은 `107212`, Microsoft WARP는 `107125`였다. 이 값은 현재 OS 세션의 관측이다. NVIDIA 두 모드와 별도 WARP를 명시적으로 선택했고 Intel에서는 스트레스를 실행하지 않았다. 각 workload의 요청·SDK·독립 DXGI observer·footer ID를 대조했다.

기존 [memory policy](../tools/memory_stress_policy.json)는 GPU 900초·full GBV 300초·WARP 300초, 각 warmup 최소 30초, 측정 checkpoint 최대 간격 15초, 측정 처리량 최소 8fps를 요구한다. 후반 절반의 private/handle/GPU local/nonlocal drift·증가율·변동폭을 검사한다. Private growth guard는 기존 기본값 **512MiB**, 실제 샘플 간격은 5초로 유지했다. Guard는 baseline 대비 증가량이며 절대 process memory 상한이나 연속 peak 측정이 아니다.

```text
SDK source fingerprint: 2e511b8e53dbef4bbb2bf3d86ab4b19cf03e8765111de2b3dad4fe45fa551401
Unchanged policy SHA-256: 377e0136d26f15f2262d624ca4cee0d1fb629fff4ebee7d34ba031f5919233ba
Selected NVIDIA driver: 32.0.16.2101, date 2026-10-19
```

세 모드는 같은 frozen binary/library/source/policy로 순차 실행했다. GPU/GBV는 같은 NVIDIA, WARP는 다른 software ID를 사용했다. 다른 로컬 SDK 빌드나 GPU 검사를 병행하지 않았다. 입력과 기록된 OS·runtime·driver·boot·CPU/BIOS/microcode·GPU metadata는 실행 전후 같았다. 이 비교는 관측 필드의 동일성이며 모든 전력·온도·시스템 상태를 인증한 것은 아니다.

## 실제 실행 결과

| 모드 | 요청 측정 / 실제 측정 | 검증 frames / 전체 images | Native exit / runner exit | 기존 메모리 판정 |
|---|---|---:|---|---|
| NVIDIA GPU | 900초 / 900.041초 | 98,088 / 294,264 | 0 / 0 | **통과** |
| NVIDIA full GBV | 300초 / 0초, warmup 중 elapsed 9.398초에 중단 | 608 / 1,824 | 2 / 1 | **실패: private growth guard** |
| WARP | 300초 / 300.024초 | 62,728 / 188,184 | 0 / 0 | **통과** |

합계 161,424 frames, 484,272 전체 이미지, 42,316,333,056 bytes의 buffer 참조를 비교했다. 전체 frame의 MRT0/MRT1·D32 texel·buffer 내용을 검사하고 매 drain의 SDK object/resource/allocation/pending/reference 계수 0을 확인했다. 세 workload 모두 schema 3이며 native/공개 memory-budget snapshot 비교를 통과했다.

GPU와 WARP는 각각 181/61개의 drained 측정 표본과 91/31개의 tail 표본을 확보했다. Full GBV만 GPU validation·state tracking이 모두 켜져 있었고 오류 계수는 0이었다. GPU/WARP의 오류 필드 0을 full GBV 실행 성공으로 간주하지 않는다.

### 통과한 두 모드의 tail 수치

| Private memory 지표 | NVIDIA GPU | WARP | 기존 허용치 |
|---|---:|---:|---:|
| Median drift | +3.627MiB | −0.063MiB | ≤32MiB |
| 분당 증가율 | +0.658MiB | +0.093MiB | ≤8MiB |
| 표본 변동폭 | 7.199MiB | 2.473MiB | ≤128MiB |

NVIDIA GPU의 tail GPU local/nonlocal은 약 11.262MiB/28.137MiB로 drift·증가율·변동폭이 모두 0이었다. Handles는 tail 변동폭 2, median drift 0이었다. WARP의 handles 및 GPU local/nonlocal 변동은 0이며 후자는 native/공개 query가 모두 성공해 관측한 실제 0이다. 누락 query를 0으로 합성하지 않았다. 전체 기준은 원래 분석기로 다시 검사했다.

### Full GBV 실패

GBV process private baseline은 **134.160MiB**, 중단·final checkpoint는 **650.879MiB**, 증가량은 **516.719MiB**였다. Baseline 대비 512MiB를 넘은 뒤 완전히 검증·drain된 8-frame 회차에서 native exit 2와 `private_growth_limit` footer를 남겼다. Overshoot를 포함한 유한 checkpoint 관측이며 정확한 연속 최대값은 아니다.

Warmup 30초를 채우지 못해 measurement start·300초 측정·tail plateau 검사는 성립하지 않는다. Full GBV를 끄거나 warmup/한도/기준을 바꾸지 않았고, 짧은 참조 성공이나 정상 오류 계수 0으로 통과시키지 않았다. GBV 실패 뒤에도 WARP를 별도 process에서 끝까지 실행했다.

SDK 보유 계수 0과 참조·budget mapping의 일치는 확인했지만 process private 증가의 내부 원인까지 확정한 것은 아니다. SDK/runtime/driver/하드웨어 중 원인을 특정하거나 과거 Intel AV가 해결됐다고 판정하지 않는다. 이전 [장치 수명](PHASE2_DEVICE_LIFETIME_REPORT.md)·[native runtime 비교](PHASE2_RUNTIME_COMPARISON_REPORT.md) 관측도 이번 수용 성공으로 재분류하지 않는다.

## 검증·소스 보존·후속 상태

원본 JSONL/footer/exit와 각 single-mode receipt를 독립 대조했다. Single-mode runner 자체의 campaign `accepted=false`는 세 모드 전체를 단독 수용하지 않는 기존 규칙이다. 그 안의 mode 분석은 GPU/WARP true·GBV false이며, 세 mode·장치 ID를 검증해 합친 최종 결과도 false다. 원래 `analyze()`가 aborted GBV를 거부하는 것도 확인했다.

실행 전후 모든 frozen 입력 해시를 실제 파일과 대조했고, 환경 비교도 다시 계산했다. Source ZIP은 SDK fingerprint 대상 27개 파일과 사용한 own harness·policy·열거 소비자·실행 wrapper를 원래 Windows 바이트 그대로 보존한다. 외부 prerequisite·runtime·compiled executable/library는 ZIP에 묶지 않았으며 binary identity는 [별도 해시](../benchmarks/reports/validation/2026-10-25-phase2-memory-resumed/binary-identities.json)로 기록했다. 이전 source ZIP과 측정 원본은 변경하지 않았다.

Memory 관련 Python 회귀 **57개**와 로컬 전체 Python **654개**가 오류·실패·건너뜀 0으로 통과했다. [원본 대조 도구](../benchmarks/reports/validation/2026-10-25-phase2-memory-resumed/verify_evidence.py)는 보존된 source ZIP·정책·native/runner·엄격한 거부·환경을 다시 검사한다. `--live-inputs`를 추가하면 당시 binary/도구가 남아 있는 `out`의 실제 frozen 해시까지 대조한다.

```sh
python benchmarks/reports/validation/2026-10-25-phase2-memory-resumed/verify_evidence.py
```

`P2-ACC-01`은 보류를 해제해 **필수 미완료**로 바꿨다. 남은 조건은 원래 신규 자원 생성 workload의 full GBV가 warmup과 300초를 마치고 기존 메모리 안정화 기준을 실제 통과하는 것이다. `P2-ACC-02`와 기존 Intel crash 자료는 별도 보류로 유지한다. Linux 물리 GPU `P3-LNX-15`, WSI, 새 성능 수용도 이번 실행과 별개다.

[보존한 source ZIP](../benchmarks/reports/sources/memory-resumed-2e511b8e53dbef4bbb2bf3d86ab4b19cf03e8765111de2b3dad4fe45fa551401-1720e2ab2123b1eb.zip)에는 실행 입력의 원래 바이트가 들어 있다.
