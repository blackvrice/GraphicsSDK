# Linux 세 SDK correctness 통합·물리 GPU 준비 연결

2026-11-01. 동일 커밋의 GraphicsSDK·SDL·승인 LLGL Linux software correctness 원본을 통합했다.
**13개 입력 × 3 SDK × 일반/필수 validation = 78개 정상 실행**과 **core/sync 제어 8개·거부 제어 38개**,
총 **workload·제어 124개 프로세스**를 재검사했다. qualification·최소 probe·대표 smoke는 이 합계에 중복 합산하지 않는다.
**전체 이미지 24,240개·버퍼 24,024개·2,918,751,264바이트**의 기존 개별 검증과 연결한다.
성능 표본은 0이며 물리 GPU 수용·세 SDK Linux 성능 비교·Phase 3 완료는 미완료다.

## 새 통합 기능

[`linux_readiness_report.py`](../tools/linux_readiness_report.py)는 SDK/SDL 전체 matrix와
LLGL 업로드·draw/instancing·PSO·자원 원본의 **기존 repository validator를 먼저 각각 재생**한다.
통과한 다섯 receipt만 통합하며 다음 일치·충돌 조건을 검사한다.

- 모든 native source revision과 qualification/probe revision이 일치한다.
- SDK/SDL과 LLGL의 qualification 전체가 같고, 네 LLGL 소비자의 승인 build/probe가 동일하다.
- 공통 source 파일의 raw hash, frozen/runtime 파일의 path·크기·hash에 충돌이 없다.
- 소비자 사이에서도 실제 프로세스 구간이 직렬이며 입력·SDK·validation의 78개 조합이 누락·중복되지 않는다.
- workload 계약 ID와 전체 readback·제어 합계가 맞고 software·성능 미수용 상태를 유지한다.

개별 검증을 통합 요약만으로 대체하지 않는다. 다른 커밋·qualification·CPU·driver 결과를 같은 실행으로 섞으면 거부한다.
13개 입력은 upload 4종·draw 3종·PSO 2종·자원 4종이다. 코드·binary·compiler 입력·license·실제 runtime과
각 개별 전체 참조·native 제어는 원래 validator가 검사하고, 통합기는 그 결과의 공통 귀속과 완전성을 추가 확인한다.

## 같은 커밋의 실제 원본

Native 실행 commit은 `7f684d898595530b54d04242f31f98979c146245`이며
[고정 VVL Linux CI](https://github.com/blackvrice/GraphicsSDK/actions/runs/41281016016)에서 새로 수집했다.
그 후 같은 CI에서 통합 도구의 `collect --live`와 `check --live`를 실행했다.
통합 도구 자체는 native 프로그램·ZIP의 Python 소스를 실행하지 않는다.
`native_code_executed_by_report=false`와 collector의 실제 source byte hash를 보존한다.

| 항목 | 새 원본·통합 기록 |
|---|---|
| OS / compiler | Linux x86_64, kernel `7.0.0-1017-azure`, GCC 14.2.0 |
| CPU / RAM | INTEL(R) XEON(R) PLATINUM 8573C, 16,759,308,288 bytes |
| Device / type | `llvmpipe (LLVM 20.1.2, 256 bits)`, 4 |
| Device / driver UUID | `6d65736132352e322e382d3075627500` / `6c6c766d706970655555494400000000` |
| SDK fingerprint | `fdcb274bf57381a3b426f3c22b420d6719c3ba37e4ba038b6902a4b2bebb6f79` |
| SDL pin | `3b2bc951500ca6a1f428af77e036e732d9a8afe3` |
| LLGL pin / profile | `649c0cc8fa608312c5e4b94dae409a5859fc7553` / `llgl-fixed-pin-transfer-src-v1` |
| Common qualification SHA-256 | `33f2e5567c41a57f2e6b9d2ae0a21570d439e2d3f541494514c2c261ab9d7524` |
| Collector source SHA-256 | `678938125830e004ae3217cde0dd330e8014a987b64165032f6db9859594d54e` |

SDK/SDL은 고유한 exact name을 독립 native UUID 조회에 귀속한 결과이며 SDL native handle을 직접 확인한 것은 아니다.
LLGL은 실제 native handle을 확인하고 draw/PSO는 실제 전달된 vkCmdDraw를 관찰한다.
이 차이를 coverage 행에 보존하고, 각 SDK의 전체 payload hash가 서로 같다고 추가 주장하지 않는다.
공통 계약의 완전한 사례·모드 수용과 개별 전체 참조 검증을 연결한다.

| 구성 | 정상 | core/sync | 거부 | 이미지 | 버퍼 | bytes |
|---|---:|---:|---:|---:|---:|---:|
| upload | 8 | 2 | 5 | 32 | 0 | 862694560 |
| draw | 6 | 2 | 9 | 24 | 0 | 230880 |
| pso | 4 | 2 | 11 | 16 | 0 | 153920 |
| allocation | 8 | 2 | 13 | 8008 | 8008 | 109837728 |
| matrix | 52 | 0 | 0 | 16160 | 16016 | 1945834176 |

SDK/SDL 원본의 **156개 CPU 진단 행**은 보존하지만 통합 통계·순위·성능 표본으로 사용하지 않는다.
timing 열은 통합 coverage와 합계에서 제외한다. native allocation count·driver/allocator cache·물리 회수와
실제 Linux GPU 수용을 이번 통합으로 완료했다고 표시하지 않는다.

## 자립 재검사 묶음

[bundle](../benchmarks/reports/validation/2026-11-01-phase3-linux-readiness/bundle/report.json)은
report JSON·생성 Markdown·handoff JSON·collector 원문·receipt·입력 inventory·evidence ZIP의 7파일로 구성한다.
ZIP에는 다섯 구성의 원본과 선행 qualification/probe closure **792파일**을 담았다.
source 파일 union **88개**와 frozen/runtime 파일 union **1,158개**를 대조했다.
외부 실행 binary는 추가하지 않았다. ZIP 내부의 source ZIP도 데이터로만 읽으며 코드를 import/실행하지 않는다.

검사기는 ZIP entry의 정확한 inventory·size/hash를 확인하고 임시 폴더에 펼쳐 repository validator를 실행한다.
경로 탈출·case alias·중복·symlink·누락·변조를 거부한다. runtime을 설치하지 않은 별도 checkout에서도 재검사할 수 있다.
수집 시 `--live`는 원래 native source/build/runtime의 고정 입력도 다시 확인하지만 새로운 GPU 실행은 하지 않는다.
manifest hash는 외부 신뢰 서명이 아니며 offline 재생은 GPU 재실행을 대신하지 않는다.

[요약](../benchmarks/reports/validation/2026-11-01-phase3-linux-readiness/summary.json),
[재검사 verifier](../benchmarks/reports/validation/2026-11-01-phase3-linux-readiness/verify.py),
[78개 coverage 행](../benchmarks/reports/validation/2026-11-01-phase3-linux-readiness/bundle/report.json),
[원본 inventory](../benchmarks/reports/validation/2026-11-01-phase3-linux-readiness/bundle/inventory.json)를 연결한다.
새 회귀 테스트 **18개**, [Windows·Linux 전체 CI](https://github.com/blackvrice/GraphicsSDK/actions/runs/41281016081)의
각 **1,361개** 테스트가 통과했다. 두 OS가 테스트한 collector source hash와 실제 CI collector 원문을 대조한다.
synthetic join/ZIP 테스트는 실제 native 실행 증거와 구분한다. 개발 중 과거 2ad3849 입력 재생은 별도 pilot이며
최종 보존 자료는 위 새 CI의 같은 revision 원본이다.

```text
python3 tools/linux_readiness_report.py collect --matrix out/linux-workload-matrix --upload out/linux-llgl-upload-evidence --draw out/linux-llgl-draw-evidence --pso out/linux-llgl-pso-evidence --allocation out/linux-llgl-allocation-evidence --output out/NEW_REPORT --live
python3 tools/linux_readiness_report.py check --output out/NEW_REPORT --live
python tools/linux_readiness_report.py check --output benchmarks/reports/validation/2026-11-01-phase3-linux-readiness/bundle
python benchmarks/reports/validation/2026-11-01-phase3-linux-readiness/verify.py
python tools/roadmap.py --check
```

## 남은 물리 GPU 실행·구현·수용

[handoff.json](../benchmarks/reports/validation/2026-11-01-phase3-linux-readiness/bundle/handoff.json)은
**미실행 준비 목록**이며 workflow를 자동 dispatch하지 않는다. 실제 GPU의 ICD·UUID는 해당 장비에서 새로 확보해야 한다.

| ID | 기존 준비 또는 추가 작업 | 미완료 이유 |
|---|---|---|
| P3-LNX-15 | `linux-hardware-acceptance.yml`, `icd`·`device_uuids`, self-hosted Linux/X64 `graphicssdk-gpu` | 실제 GPU 환경·원본 필요 |
| P3-BEN-07 | `linux-comparison-physical.yml`, `icd`·`device_uuid`, 같은 runner label | SDK/SDL 실제 물리 qualification 필요 |
| P3-BEN-08 | SDK/SDL physical 대표 소비자 준비는 기존 workflow에 있음. LLGL 현재 runner는 software-only guard | LLGL 물리 모드 준비·검증 구현과 실제 환경 필요 |
| P3-LBEN-01~04 | 현재 묶음은 correctness. Linux 세 SDK timed campaign·분리 실행·fresh 반복은 별도 준비 | 추가 구현과 같은 실제 GPU/driver의 새 측정 필요 |
| P3-WSI-08 | 실제 Linux desktop 표시·사용자 창 조작 | 사람의 관측 수용 필요 |
| P3-GATE-01~03 | 선행 실제 장치·표시·비교와 최종 검토 | Phase 3 수용 미완료 |

현재 사용 가능한 실제 Linux GPU/desktop 환경은 제공되지 않았다. software 준비 성공으로 이 조건을 채우지 않는다.
다음 개발은 **LLGL 실제 Linux GPU qualification 경로 준비**이며, 실제 장치 없이 완료 수용까지 체크하지 않는다.
기존 task 143개의 status와 Phase 3 미완료 상태를 유지했다.
