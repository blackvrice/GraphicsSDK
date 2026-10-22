# Phase 2 충돌 시 제한된 다중 스레드 문맥·스택 수집

2026-10-22. 원래 메모리 stress에서 발생한 Intel access violation을 추가로 조사할 수 있도록
자체 child debugger를 확장했다. 기존에는 오류가 난 스레드의 문맥·스택만 수집했다. 이제 같은
second-chance debug stop에서 다른 살아 있는 스레드도 수집한다. **원래 충돌 원인과 장시간
GBV 메모리 gate는 여전히 미완료다.** 이번 짧은 진단에서 충돌하지 않은 것을 해결로 세지 않는다.

SDK 구현·API·기본 검증 설정은 그대로다. SDK fingerprint는
`50fe75fea32a5444531868da9f1a225a05573d78ba12c52bb622a1ea80d35edf`이며, 기존 library와
11개 SDK ZIP의 byte hash를 보존했다. 새 SDK 패키지를 만들지 않았다.

## 수집 계약

[Windows debug event 계약](https://learn.microsoft.com/en-us/windows/win32/debug/debugging-events)에
따라 이벤트 처리 중 child의 모든 스레드가 정지한다. 추가 SuspendThread/ResumeThread는
사용하지 않는다. debug event로 등록·종료를 추적한 live thread 중 fault thread를 먼저 수집하고,
나머지는 thread ID 순서로 선택한다. 총 **16개 스레드, 각 최대 64개 frame**으로 제한한다.
이는 frame/개수의 상한이며 DbgHelp 함수 자체의 실행 시간을 선점하는 deadline이 아니다.
기존 own-child watchdog과 Python 외부 deadline, DEBUG_ONLY_THIS_PROCESS 범위를 유지한다.

각 fault마다 하나의 local symbol session을 만들고 여러 스레드가 공유한 뒤 정리한다.
심볼 서버·다른 프로세스 attach·전역 WER/TDR/driver 정책 변경은 없다. faulting thread의 PC는
`exception_pc`, 다른 정지 PC는 `thread_pc`다. RIP=0의 module은 null이며 caller candidate는
명시적으로 표시한다. 다른 worker가 대기 중인 위치를 crash 원인으로 단정하지 않는다.
[StackWalk64](https://learn.microsoft.com/en-us/windows/win32/api/dbghelp/nf-dbghelp-stackwalk64)는
복사한 CONTEXT를 받아 원본 fault context와 선택적 minidump 정보를 보존한다.

schema 2는 context/frame에 snapshot ID를 붙이고 `thread_snapshot`에 observed/captured/omitted
스레드 수, 성공한 context 수, 실제 frame 수, symbol session 상태를 기록한다.
`all_threads_captured=true`는 생략이 없고 선택한 모든 context를 읽었을 때만 가능하다.
전체 stack unwind·모든 symbol 해석·원인 규명을 보증하는 값은 아니다. GetThreadContext 실패는
Win32 오류와 available=false로 남기고 스택을 만들어내지 않는다. 정상 종료와 timeout에는
실제로 없던 exception snapshot을 추가하지 않는다.

parser는 기존 schema 1 자료를 계속 읽되 schema 2의 행을 schema 1로 낮춰 숨기는 결과를 거부한다.
현재 native control runner와 메모리 capture wrapper는 schema 2 collector를 요구한다.
행 누락·중복/다른 thread·다른 snapshot·범위 밖 순서·불연속 frame·상한 완화·잘못된 완전성
주장을 거부한다. 수집 완료와 memory acceptance는 구별하며 후자는 언제나 false다.

## 실제 검증

기존 handled exception, Unicode/빈 문자열/quote/backslash 인자, exit 7, null-call과 local dump,
unhandled breakpoint, timeout/reap, 1,000개 debug string의 64KiB 한도, overwrite 거부를 유지했다.
새로운 두 fixture는 main과 waiter들이 대기하는 동안 별도 worker에서 실제 null call을 한다.

- waiter 3개 대조: 실제 live thread 8개 모두 context를 얻었으며 46개 frame을 수집했다.
  세 waiter의 `fixtureWaiter` symbol과 fault worker의 caller candidate를 독립적으로 확인했다.
- waiter 24개 대조: 실제 live thread 29개 중 fault를 포함한 16개 context/94개 frame을 수집하고
  13개 생략을 명시했다. `all_threads_captured=false`이며 오류 종료 코드를 그대로 전달했다.

두 실제 access violation은 fixture 자체에서 의도적으로 만든 양성 대조다. Intel runtime의
원래 access violation 재현으로 세지 않는다. 선택적 minidump는 local `out/`에만 보존하고
Git, 검증 원본·source archive와 SDK 배포에서 제외한다.

Core CTest **34/34**, Python **169/169**가 통과했다. 추가 Python 10개는 누락/과장/상한/상관관계
오류를 거부하며, available=false인 context를 명시한 부분 수집도 완전 수집으로 오인하지 않는다.
Native control **9개 및 overwrite 거부**가 통과했다. NVIDIA/Intel full GBV와 WARP에서
현재 public SDK의 5초 + 1초 warmup 진단을 직렬로 실행했다. 이 실행의 실제 frame/reference 수와
결과는 [요약](../benchmarks/reports/2026-10-22-phase2-thread-snapshot/summary.json)에 보존한다.
시간·작업량이 원래 memory policy에 미달하므로 장시간 통과로 분석하지 않는다.

SDK 소스를 바꾸지 않아 기존 72개 common correctness·6개 D3D12-OFF·7개 presentation 및
441개 창 이미지 campaign을 이번에 다시 실행하지 않았다. 이전 결과와 이번 Core 재검사를
구분한다. 원래 실패한 GBV policy·관측·source archive와 이전 SDK 배포본은 그대로다.

## 감사와 재현

새 [crash inventory](../licenses/generated/windows-sdk-thread-snapshot-crash-release/inventory.json)는
28 compiler-read inputs/1 component와 8개 executable linker closure를 확인한다. 이전 의존성인
Windows DbgHelp를 사용하며 외부 package를 추가하지 않았다. 새 symbol-bearing 도구를 빌드하고
SDK를 재빌드하거나 재포장하지 않았다. `.exe/.dll/.lib/.pdb/.dmp/.nupkg`는 자체 source ZIP과
Git 실행 원본에 넣지 않는다. 실행에 사용한 binary/PDB는 local에 두고 해시만 기록한다.

```powershell
cmake --build out/crash-capture --config Release --parallel 1
python tools/crash_capture/validate.py --output out/thread-controls-new
python tools/capture_memory_failure.py --adapter <fresh-id> --mode validation --diagnostic-seconds 5 --output out/thread-memory-new
```

[고정 input·실행 원본](../benchmarks/reports/validation/2026-10-22-phase2-thread-snapshot/)과
[해시·소스 ZIP 근거](../benchmarks/reports/2026-10-22-phase2-thread-snapshot/)를 함께 보존한다.
장시간 GBV memory 증가, 원래 Intel AV, 육안 표시/실제 DPI/예기치 않은 TDR·hot-unplug 및
Vulkan/Linux/Metal/macOS 검증은 남아 있다. 이 작업은 다음 충돌에서 얻을 수 있는 근거를 확장한다.
