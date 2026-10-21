# Phase 1 GPU 메모리 acceptance 완료

2026-10-21. 남은 Windows D3D12 GPU 메모리 검증을 완료해 **Phase 1 foundation 판정이
통과**했다. 이전 LLGL baseline과 SDL3 비교 증거의 모든 해시를 재검사한 뒤 새 메모리
증거를 결합했다. 자체 SDK의 미완료 API와 다른 플랫폼 구현은 이후 단계에 남아 있다.

- [최종 완료 판정](../benchmarks/reports/2026-10-21-phase1-gpu-memory-final/acceptance.json)
- [관측 표](../benchmarks/reports/2026-10-21-phase1-gpu-memory-final/TABLE.md)
- [메모리 peak·검증용 transient 결과](../benchmarks/reports/2026-10-21-phase1-gpu-memory-final/memory.json)
- [자원 lifetime 목록](../benchmarks/reports/2026-10-21-phase1-gpu-memory-final/lifetimes.json)
- [대상 프로세스 이벤트 원본](../benchmarks/reports/2026-10-21-phase1-gpu-memory-final/process-events.jsonl)
- [raw ETL·export·프로세스·소스 ZIP 해시](../benchmarks/reports/2026-10-21-phase1-gpu-memory-final/evidence.json)

## 수집과 권한

이전 WPR 시도는 정책 오류 `0xc5585011`과 관리자 실행 취소로 끝났다. 이번에는 사용자가
새 관리자 실행을 명시적으로 승인한 후 `capture_phase1_gpu_lifetime.ps1`을 실행했다.
무작위 이름의 전용 ETW file session 하나만 시작·종료했다. 기존 WPR/kernel/global session,
Windows 정책, 그룹 구성은 바꾸지 않았고 종료 후 전용 session이 남지 않았음을 확인했다.

공급자는 Microsoft-Windows-DxgKrnl, keyword는 Base/Resource/Memory/References의 `0xc5`,
level5, buffer1MiB×32–128이다. 설치된 Windows/WPT manifest에서 event ID·version·필드를
확인했다. 수집은 fresh child process 시작 전부터 종료 후까지 감싸며 5개 작업을 순서대로
실행했다. 일반 workload는 validation OFF / CPU-only 120표본과 기존 pre/post reference를
사용하고, positive control은 debug device와 전체 buffer readback을 켰다.

첫 탐색 수집은 GPU 작업 자체가 성공했으나 Windows PowerShell의 Start-Process 반환 객체에서
ExitCode가 null로 읽혀 중단됐다. 자기 session은 정상 종료했고 탐색 ETL은 로컬에 보존했다.
자식 프로세스를 직접 소유하는 Diagnostics.Process 및 비동기 stdout/stderr 수집으로 수정한 뒤
새 출력 경로에서 최종 5개 프로세스 모두 명시적인 exit code 0을 확인했다. 탐색 수집을 최종
완료 증거로 사용하지 않았다.

## 이벤트 분석

Windows의 [VidMm은 GPU 메모리 할당·해제를 관리](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/video-memory-management-and-gpu-scheduling)한다.
새 exporter는 OpenTrace/ProcessTrace의 raw QPC 시계와 설치된 OS의
[TdhGetPropertySize/TdhGetProperty](https://learn.microsoft.com/en-us/windows/win32/api/tdh/nf-tdh-tdhgetproperty)로
필드를 해독한다. 다른 OS의 바이트 offset을 추측해서 사용하지 않는다. 해당 OS에서 검토한
event version이나 필드가 다르면 분석은 실패한다.

| 데이터 | event ID / version | 검사와 의미 |
|---|---|---|
| AdapterAllocation 생성·해제 | 33/34, version3 | `(native adapter, global allocation handle)`로 생성·해제를 대응. 살아 있는 handle 중복, 누락, 크기 불일치, 종료 후 잔여 allocation을 거부 |
| AdapterAllocation rundown | 35, version3 | fresh process의 생성 경계가 빠진 상태이므로 대상 프로세스에서 발견되면 거부 |
| ProcessUsageChange | 367, version0 | local/nonlocal 별 old→new 사용량 전체를 이어 peak를 계산. 시작 old=0, 연속성, 종료 new=0을 확인 |
| ProcessCommitmentChange | 371, version0 | commitment를 별도 old→new series로 재구성. usage나 allocation 크기에 더하지 않음 |
| ProcessAllocation | 225/226, version1 | CPU mapping 이벤트로 보존하지만 GPU resident usage에 합산하지 않음 |

해제가 System 문맥 PID4에서 발생한 경우 생성 때의 `(adapter, handle)`로 원래 프로세스에
귀속시킨다. header PID만으로 필터링하면 이 해제를 잃기 때문에 payload ProcessId와 handle
수명을 사용한다. 5개 프로세스에서 **1,274개 allocation의 생성·해제를 전부 대응**했다.
그중 675개 해제는 생성 프로세스 밖의 문맥에서 발생했다. 사용량·commitment의 local/nonlocal
모든 change stream이 0에서 시작해 단절 없이 이어지고 종료 시 0으로 돌아왔다.

전체 exporter 입력은 10,707개 선택 이벤트이고, Git에 보존하는 대상 이벤트는 **9,176개**다.
header EventsLost, consumer EventsLost, BuffersLost가 모두 0이며 ProcessTrace 성공과 TDH
해독 실패 없음도 확인했다. 이벤트 손실, 시각 역전, 잘린 export, 알 수 없는 GPU node/segment,
다중 native adapter 또는 불완전한 수명/사용량 chain은 완료를 거부한다.

## 실제 관측과 transient positive control

| 프로세스 | local usage peak MiB | nonlocal usage peak MiB | 완전히 대응한 allocation |
|---|---:|---:|---:|
| positive_control | 79.941406 | 129.890625 | 211 |
| resource_inflight | 13.617188 | 30.281250 | 649 |
| multi_upload | 75.328125 | 43.031250 | 144 |
| video_4k | 67.390625 | 66.781250 | 135 |
| video_p010 | 91.203125 | 74.718750 | 135 |

검증 프로그램은 LLGL public API로 32MiB buffer 2개를 만들고 복사한 뒤 전체 bytes를 CPU
참조와 대조하고 해제한다. 3회 모두 두 GPU allocation의 겹친 수명이 before/live/after QPC
구간 안에서 완전히 대응됐다. GPU memory checkpoint는 생성 전과 해제 후에만 읽는다.

체크포인트의 local 최대값은 15.941406MiB였다. 이벤트 추적은 각 구간에서 79.933594,
79.941406, 79.941406MiB를 검출했다. 체크포인트가 놓친 양은 **63.992188 / 64 / 64MiB**다.
따라서 이번 완료는 체크포인트 최대값을 연속 peak로 바꿔 부른 결과가 아니다.

측정 범위는 **각 프로세스의 전체 수명에서 VidMm이 계상한 사용량 change event의 peak**다.
device/driver 준비, CPU reference용 readback과 teardown도 포함한다. allocation 크기의 합은
등록된 자원의 별도 지표이며 GPU resident usage에 합산하지 않는다. 이 값을 frame-only peak,
하드웨어 VRAM의 모든 순간 점유량, SDK 간 메모리 우위로 일반화하지 않는다. 이전 timing
캠페인은 그대로 보존하며 이번 trace의 실행 시간을 성능 비교 값으로 추가하지 않았다.

## 증거와 검증

원본 ETL(60MiB)과 전체 시스템 event export는 로컬 `out/phase1-gpu-lifetime-20261021-final/`에
보존했다. 공급자 수집 자체는 시스템 GPU 이벤트도 담으므로 raw ETL 전체를 원격 Git에
게시하지 않는다. 저장소에는 대상 프로세스의 이벤트·native 출력·수명 목록과 모든 raw 입력의
SHA256만 포함한다. 재분석은 현재 로컬 ETL로 가능하고, 다른 PC에서는 새 캡처가 필요하다.

새 native positive-control CTest 1개와 Python 전체 **56개**가 통과했다. 56개에는 누락·중복
allocation, System 문맥 해제, usage chain 단절, 이벤트 손실, 미지원 시계, checkpoint가 이미
관측한 peak와 구간 밖 수명을 거부하는 검증이 포함된다. 기존 공통 CTest 72개와 84개 반복
측정은 앞선 캠페인의 보존된 증거이며 이번에 다시 실행했다고 주장하지 않는다.

새 compiler-read/license inventory는 자체 exporter/control까지 **974개 입력/22개 구성요소**다.
Windows ETW/TDH는 시스템 전제조건이며 신규 외부 패키지는 없다. 기존 자체 SDK/API/SDK ZIP,
고정 LLGL/SDL source와 성능 benchmark 실행 파일은 바뀌지 않았다. 실제 runtime copy 해시,
새 inventory와 기존 SDK ZIP을 확인했다. 다른 플랫폼, SDL의 연속 GPU memory 비교,
장기 stress나 device-loss acceptance는 이번 완료 범위에 포함되지 않는다.

## 재현

저장소 루트에서 새 출력 경로를 사용한다. 관리자 수집은 사용자 승인된 Windows 관리자
프로세스에서만 실행한다. 다른 recording을 중단하거나 시스템 권한 설정을 바꾸지 않는다.

```powershell
cmake -S tools/gpu_trace -B out/gpu-trace -G "Visual Studio 17 2022" -A x64
cmake --build out/gpu-trace --config Release --parallel 1
ctest --test-dir out/gpu-trace -C Release --output-on-failure
./tools/capture_phase1_gpu_lifetime.ps1 -Output D:/source/GraphicsSDK/out/<new-capture>
python tools/analyze_gpu_memory_trace.py --capture out/<new-capture> --output benchmarks/reports/<new-report>
python tools/phase1_acceptance.py benchmarks/reports/<new-report>/acceptance-evidence.json
```

capture script는 성공한 자기 ETW 세션만 finally에서 중단한다. 기존 경로 덮어쓰기를 거부하고,
timeout이면 자기 benchmark child만 종료한다. 분석기는 원본을 바꾸지 않고 새 보고서를
생성한다. `phase1_acceptance.py` 자체는 여전히 증거 조합기이며 실제 ETL 해독·수명 검증은
새 exporter와 `analyze_gpu_memory_trace.py`가 담당한다.
