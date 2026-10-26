# Phase 2 — Intel AV 조사 재개

2026-10-25~26 KST · 사용자 지시로 `P2-ACC-02` 재개 · SDK 기준 커밋 `59c28fc`.

**현재 Intel의 일반 실행과 심볼 수집 실행은 각각 300초 측정+30초 warmup을 정상 종료했으며, 과거 미처리 `0xc0000005`는 재현되지 않았다. 원인은 여전히 미확정이고 작업은 필수 미완료다.** 비재현을 해결이나 하드웨어 원인 확정으로 판정하지 않는다.

[요약·해시](../benchmarks/reports/2026-10-25-phase2-intel-crash-resumed/summary.json), [전체 실행 receipt](../benchmarks/reports/validation/2026-10-25-phase2-intel-crash-resumed/native/resumption.json), [로컬 심볼 해석 확인](../benchmarks/reports/validation/2026-10-25-phase2-intel-crash-resumed/setup/symbol-resolution.json), [source manifest](../benchmarks/reports/validation/2026-10-25-phase2-intel-crash-resumed/source-manifest.json)에 연결한다.

현재 SDK source fingerprint는 `2e511b8e53dbef4bbb2bf3d86ab4b19cf03e8765111de2b3dad4fe45fa551401`이다. SDK C++·공개 API·root CMake를 변경하지 않았다. 새 Intel 열거 ID `107212`는 현재 OS 세션에 한정된다.

조사용 SDK는 별도 경로에서 Release `/O2 /Ob2 /MD /Zi`로 빌드했다. 수집용 workload는 이 SDK library를 명시적으로 링크하고 `/DEBUG` PDB를 생성했다. 일반 실행은 기존 최적화 Release binary를 사용한다. 두 실행은 소스가 같아도 다른 binary·계측 조건이므로 하나의 동일한 실행으로 합치지 않는다.

수집기는 자체 자식만 디버깅하며 미처리 예외의 context·최대 16 thread stack·module을 기록한다. 정상/실제 충돌/메모리 guard/시간 초과/수집 실패를 구분한다. 9개 실제 제어 검사와 덮어쓰기 거부를 통과했다. 제어용 의도적 AV는 Intel workload의 재현으로 세지 않는다. 덤프·PDB·binary는 로컬에 두며 Git·source ZIP에는 넣지 않는다.

## 실행 결과

일반 실행과 수집 실행 각각 full GBV·state tracking, 300초 측정+30초 warmup, 5초 checkpoint, 기본 512MiB 증가 guard를 요청했다. 원래 메모리 policy는 변경하지 않았다. SDK 빌드나 다른 로컬 GPU 검사를 병행하지 않았다.

| 조건 | 실제 elapsed / 측정 | 검증 frames / 전체 images | Native / wrapper exit | 미처리 AV |
|---|---|---:|---|---|
| 일반 Release, debugger 없음 | 330.397초 / 300.081초 | 14,856 / 44,568 | 0 / 1 | 재현 안 됨 |
| 별도 SDK 심볼 Release, 자체 자식 debugger | 330.095초 / 300.012초 | 7,848 / 23,544 | 0 / 0 | 재현 안 됨 |

합계 22,704 frames, 68,112 전체 이미지, 5,951,717,376 buffer bytes의 참조를 검사했다. 두 native footer의 validation error는 0이고 statistics contract와 모든 drain의 SDK 보유 계수 0을 확인했다. Guard 중단이나 watchdog timeout은 없었다. 수집기는 loader breakpoint와 처리된 C++ 예외 `0xe06d7363`를 기록했으며, second chance는 0이었다. AV 예외 기록이나 새 실제 Intel 덤프는 생성되지 않았다.

일반 wrapper exit 1은 native 충돌이 아닌 **private 메모리 안정화 기준 실패**다. 수집 wrapper exit 0도 수집의 정상 종료만 뜻한다. 같은 원래 분석기로 수집 실행을 별도 대조해도 private 안정화 기준은 실패했다. 일반 실행은 baseline 183.016MiB→633.445MiB(+450.430MiB), 수집 실행은 122.555MiB→294.406MiB(+171.852MiB)였다. 둘 다 baseline 대비 512MiB guard 이내이며, guard 이내라는 사실이 tail plateau 수용을 뜻하지 않는다.

모든 실행의 `memory_acceptance=false`를 유지한다. 별도 심볼·debugger가 실행 속도와 allocation timing을 바꿀 수 있으므로 증가량 차이로 SDK 결함이나 driver 원인을 특정하지 않는다. Debugger 문자열은 23,559개를 관측했지만 본문은 64KiB 예산 안에서 일부만 기록한다. 이 텍스트가 native ERROR/CORRUPTION 계수나 전체 메시지 보존을 대신하지 않는다.

## 환경·증거 검증

Intel UHD 770 driver `32.0.101.6129`, Windows `10.0.26220`, i7-14700K, BIOS F10a, microcode bytes `[47,1,0,0]`를 새로 관측했다. 각 실행 전후의 binary·도구·policy·OS/runtime·선택 driver·다른 GPU driver·CPU/BIOS/microcode·boot 관측이 일치했다. 일반/수집 binary와 library는 서로 다른 해시로 분리했다. 기록 필드의 일치가 전력·온도·RAM 안정성 등 전체 시스템 상태를 인증하는 것은 아니다.

처음 제한된 실행 환경에서는 읽기 전용 CIM 조회가 실패해 GPU workload를 시작하지 못했다. 실패 receipt를 보존한 뒤 접근 가능한 실행 환경에서 새 출력 경로로 재시도했다. Driver·BIOS·WER·TDR·시스템 정책은 변경하지 않았다.

실제 compiler/link 명령과 cache를 보존했고, 로컬 DbgHelp로 최종 workload PE/PDB를 열어 **2,429개 `gfx::` SDK 심볼**을 해석했다. 이는 SDK 심볼 준비의 확인이며 실제 충돌 스택이나 private driver 심볼 확보를 뜻하지 않는다. 두 binary와 관련 PDB/library 등 9개 로컬 파일은 해시만 보존했다.

새 library 선택·변경 검출 회귀 2개를 포함한 전체 Python **656개**가 실패·오류·건너뜀 0으로 통과했다. 테스트 동안 입력도 바뀌지 않았다. 기존 전체 Python 도구의 receipt는 도구의 원래 task 표기를 유지하며, 그 suite 자체가 GPU 검사나 AV 원인 규명을 수행했다는 뜻은 아니다.

Source ZIP은 SDK 27개 파일과 사용한 own harness·policy·검사·재현 명령 등 총 47개 파일의 원래 바이트를 보존한다. SDK 기준 커밋 이후의 이번 diagnostic 도구 변경도 ZIP 바이트·해시로 구분한다. 외부 runtime과 compiled binary·PDB·덤프는 포함하지 않았다. 원본 자료 73개와 ZIP 해시를 다시 대조하며, [검증 도구](../benchmarks/reports/validation/2026-10-25-phase2-intel-crash-resumed/verify_evidence.py)는 ZIP의 당시 분석기·policy를 복원해 현재 checkout의 줄바꿈이나 도구 변경과 분리한다.

```sh
python benchmarks/reports/validation/2026-10-25-phase2-intel-crash-resumed/verify_evidence.py
# 당시 로컬 binary/PDB가 남아 있으면 frozen input까지 추가 대조
python benchmarks/reports/validation/2026-10-25-phase2-intel-crash-resumed/verify_evidence.py --live-inputs
```

## 남은 원인 규명

현재 한 쌍의 실행으로 원래 비결정적 충돌이 없다고 보장할 수 없다. 원래 실행 시점의 fault context와 덤프도 여전히 없다. 동일 실행을 무기한 반복하거나 임의 SDK 수정을 원인 해결로 제출하지 않는다.

실제 AV가 생기면 새 덤프와 정확히 일치하는 binary/PDB를 보존하고 예외 종류·대상 주소·faulting PC·다른 thread·자원 수명을 대조한다. SDK/runtime/driver frame 하나나 기존 CL.exe 충돌만으로 원인을 단정하지 않는다. 재현 조건을 확보한 다음 최소 재현 및 한 변수씩의 환경 비교와 수정 전후 회귀로 확인해야 한다. Driver/정상성이 확인된 다른 장비 비교가 남은 선택지이며 이번 실행에서는 환경을 교체하지 않았다.

기존 [원인 조사](PHASE2_FAULT_INVESTIGATION_REPORT.md), [thread 수집](PHASE2_THREAD_SNAPSHOT_REPORT.md), [메모리 수용 재개](PHASE2_MEMORY_RESUMPTION_REPORT.md)는 원래 관측·실패를 보존한다. `P2-ACC-01`의 full GBV 메모리 실패와 `P3-LNX-15`의 Linux 물리 GPU 수용은 이번 AV 조사와 별개다.
