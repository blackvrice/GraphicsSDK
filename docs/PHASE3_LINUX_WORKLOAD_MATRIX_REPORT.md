# Linux SDK·SDL 전체 입력 software 검증 준비

2026-10-30. Linux SDK·SDL의 **13개 입력 × 2 SDK × 일반/필수 validation, 52개 프로세스**를
실제 Linux software ICD에서 실행하고 저장 원본을 Windows에서 재검사했다.
전체 이미지 **16,160개**, 버퍼 **16,016개**, **1,945,834,176바이트**의 changing readback이 통과했다.
`P3-LBEN-01`~`04`의 software 준비 증거이며 **실제 Linux GPU 성능 비교 완료는 아니다**.
LLGL은 이번 matrix에 포함하지 않는다. BEN-07·08, LBEN-01~04의 상태는 todo를 유지한다.

## 실행 입력·환경

실행 revision은 `30cfbf664086501808005be87d62601833f8dd6f`이며
[실제 Linux 실행](https://github.com/blackvrice/GraphicsSDK/actions/runs/40998663996)이 통과했다.
원래 Windows C++ 소비자와 SDK 제품 소스는 변경하지 않았다. 기존 결정적 Linux 변환의
5개 platform adaptation, 설치 SDK, 고정 static SDL, 원래 workload·timer·reference 본문을 재사용했다.
이번 revision에서 새 SDK/SDL qualification과 workload build를 실행했으며 과거 binary를 내려받아 대신 실행하지 않았다.

| 입력 | 관측 값 |
|---|---|
| OS / compiler | Ubuntu hosted Linux x86_64, kernel `7.0.0-1017-azure`, GCC 14.2.0 |
| CPU / RAM | AMD EPYC 7763, 16,760,344,576 bytes |
| Native 장치 | `llvmpipe (LLVM 20.1.2, 256 bits)`, CPU 종류 4 |
| Device / driver UUID | `6d65736132352e322e382d3075627500` / `6c6c766d706970655555494400000000` |
| Adapter ID / LUID | 문자열 `5892941257450743403` / JSON null |
| SDK fingerprint | `fdcb274bf57381a3b426f3c22b420d6719c3ba37e4ba038b6902a4b2bebb6f79` |
| SDL pin | `3b2bc951500ca6a1f428af77e036e732d9a8afe3` |
| Vulkan-Headers | `b379292b2ab6df5771ba9870d53cf8b2c9295daf` |
| Validation | 고정 VVL 1.3.290, 기존 lock/profile, core + sync/submit-time 요청 |

장치는 같은 revision의 BEN-07 probe와 BEN-08 build receipt로 연결한다. 이름·native UUID·driver·
compiler·SDK/SDL source가 일치해야 하며, software CPU 종류만 이 준비 도구에 허용한다.
SDL은 공개 이름과 유일한 native 장치를 연결한다. SDL renderer의 native handle을 직접 관측한 것으로 확대하지 않는다.
각 프로세스의 실제 ICD/loader library load를 확인하고 파일 해시는 collection 중 원본과 대조했다.
일반 실행에는 validation library가 없어야 하며 필수 validation 실행에는 정확한 고정 library가 있어야 한다.

## 전체 입력의 실제 결과

각 프로세스는 warmup 1회와 diagnostic 3회를 수행한다. warmup도 전체 reference 검사에 포함한다.
다음 합계는 **새 matrix 52개 프로세스만** 센 것이며 선행 probe·대표 smoke·거부 controls와 합산하지 않았다.

| 작업 | 전체 입력 | 프로세스 | 전체 이미지 | 전체 버퍼 | 검증 bytes |
|---|---|---:|---:|---:|---:|
| Upload | odd, upload1080, upload4k, upload4k16f | 16 | 64 | 0 | 1,725,389,120 |
| Draw | draw1, draw1000, instanced1000 | 12 | 48 | 0 | 461,760 |
| PSO | pso1, pso8 | 8 | 32 | 0 | 307,840 |
| Allocation | buffer1, buffer1000, texture1, texture1000 | 16 | 16,016 | 16,016 | 219,675,456 |

모든 입력에서 전체 changing payload와 readback 일치를 확인한다. Draw는 호출·instance·vertex·
scissor와 CPU 구간 분해를 검사한다. PSO는 공개 생성·해제와 실제 사용 region 144개를,
allocation은 공개 생성·해제·사용·전체 참조 자원 32,032개를 검사한다. 이는 wrapper가 보고하는 공개 작업과
reference의 검증이며, native allocation 수·물리 회수·driver cache 상태를 직접 관측한 것은 아니다.
기존 SDK general buffer / SDL vertex-buffer 사용 정책 차이도 유지한다.

진단 시간 행 **156개**는 raw stdout에 그대로 남긴다. 이 도구는 percentile·SDK 순위·성능 표본을 생성하지 않는다.
일반 실행의 시간도 software correctness 진단이며, hardware 성능이나 Windows 결과와 섞지 않는다.
CPU fence wall time은 GPU timestamp가 아니다. SDL upload의 기본 instance API 요청과
draw/PSO/allocation의 명시적 Vulkan 1.1 요청 차이도 보존했다.

선행 qualification과 대표 workload 준비에 있는 missing-layer·wrong-UUID·wrong-kind controls도 보존한다.
새 matrix는 정상 전체 입력 52개를 추가하며 별도 native core/sync positive error controls를 다시 실행하지 않는다.
정상 실행의 validation 메시지·종료 결과·정확한 library load와 설정을 검사한 범위를 기록한다.
GPU-assisted·실제 화면·물리 GPU·장시간 메모리·Intel 원인 수용을 추가하지 않는다.

## 보존과 재검사

[`linux_workload_matrix.py`](../tools/linux_workload_matrix.py)는 실행 명령·환경 override·PID·직렬 구간·
stdout/stderr·full result·동결 입력과 선행 qualification을 보존한다. 실행 전 및 최종 source/build/runtime
해시가 그대로여야 한다. 실패·timeout은 실패 receipt와 해당 원본을 남기며 완료로 승인하지 않는다.
실행 소스 59파일과 프로젝트 LICENSE를 source ZIP에 넣었다. 외부 executable/library/driver는 재배포하지 않는다.

[요약](../benchmarks/reports/validation/2026-10-30-phase3-linux-workload-matrix/summary.json),
[원본 receipt](../benchmarks/reports/validation/2026-10-30-phase3-linux-workload-matrix/native/receipt.json),
[실행 소스 ZIP](../benchmarks/reports/validation/2026-10-30-phase3-linux-workload-matrix/native/source.zip),
[archive manifest](../benchmarks/reports/validation/2026-10-30-phase3-linux-workload-matrix/manifest.json),
[저장 증거 재검사](../benchmarks/reports/validation/2026-10-30-phase3-linux-workload-matrix/verify.py)를 연결한다.
재검사기는 repository validator를 사용하며 ZIP 코드나 저장 executable을 실행하지 않는다.
manifest hash는 외부 신뢰 서명이 아니며 offline 재검사는 실제 GPU 재실행을 대신하지 않는다.

새 회귀 테스트 **17개**와 두 OS의 전체 Python 테스트 **각 1,264개**가 통과했다.
[Windows 원본](../benchmarks/reports/validation/2026-10-30-phase3-linux-workload-matrix/python-windows/receipt.json)과
[Linux 원본](../benchmarks/reports/validation/2026-10-30-phase3-linux-workload-matrix/python-linux/receipt.json)은
실행 revision `30cfbf6`와 해당 runner source hash를 기록한다.
Windows 임시 폴더의 짧은 경로명, Linux loader SONAME·실제 버전 파일명·`/./` 표기 차이를 보완했다.
수집 이후 Windows offline 재생에서 발견한 경로 구분자 차이도 validator에서 처리하며 raw receipt는 재작성하지 않았다.
최종 커밋 CI의 저장 증거 재검사는 현재 validator와 별도로 확인한다.

```text
# 같은 revision의 Linux software qualification/build를 먼저 실행한다.
python3 tools/linux_workload_matrix.py run --readiness out/linux-workloads-evidence --output out/NEW_MATRIX
python3 tools/linux_workload_matrix.py check --output out/NEW_MATRIX --live

# 보존 자료만 재검사: Windows/Linux에서 실행할 수 있다.
python tools/linux_workload_matrix.py check --output benchmarks/reports/validation/2026-10-30-phase3-linux-workload-matrix/native
python benchmarks/reports/validation/2026-10-30-phase3-linux-workload-matrix/verify.py
python tools/roadmap.py --check
```

## 남은 완료 조건

- `P3-LNX-15`, `P3-BEN-07`~`08`: 실제 Linux 물리 GPU·ICD·정확한 UUID와 동일 환경 qualification 필요.
- `P3-LBEN-01`~`04`: 물리 GPU에서 SDK·SDL·선택 LLGL 조합의 전체 correctness와 분리된 fresh 반복 성능 측정 필요.
- Linux LLGL 이식·프로필과 실제 지원 조합 검증은 이 SDK/SDL software matrix로 대체하지 않는다.
- `P3-WSI-08`, `P3-GATE-01`~`03`과 Phase 3 최종 수용도 별도 미완료다.
