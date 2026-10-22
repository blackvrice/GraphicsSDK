# Own-child Windows x64 crash capture

This standalone diagnostic launches a new process under `DEBUG_ONLY_THIS_PROCESS`.
It cannot attach to an existing process and changes no global debugger, WER, TDR or
driver policy. It keeps own-child kill-on-debugger-exit enabled. A finite watchdog
terminates and reaps a timed-out child; Windows API failures produce a rejected
capture rather than a success footer.

```powershell
cmake -S tools/crash_capture -B out/crash-capture -G "Visual Studio 17 2022" -A x64
cmake --build out/crash-capture --config Release --parallel 1
python tools/crash_capture/validate.py --output out/crash-controls-new
out/presentation/Release/gfx_presentation.exe --list-adapters
python tools/capture_memory_failure.py --adapter <fresh-hardware-id> --output out/crash-memory-new
```

The fixture controls handled exceptions, a real null function call, an unhandled
application breakpoint, exit code 7, timeout, bounded debugger text and exact
empty/Unicode/quote/trailing-backslash argument forwarding. The native collector
consumes only the initial loader breakpoint. It passes other first-chance events
back to the child and records second-chance faults without swallowing them. It
returns the child's exit code, including access violation, or 125 on collector
failure. Timeout terminates the child with 124. JSONL footer fields distinguish
child exit codes from collector errors.

On an unhandled fault it records exception code/address/parameters/thread,
loaded module image ranges and the faulting thread's x64 context and up to 64 stack
frames. It uses local symbols in the child executable directory, never a symbol
server. Missing private runtime/driver PDBs remain unresolved. For RIP=0 the fault
module stays null; the stack-top candidate return address is labelled explicitly.
A caller candidate or nearby stack frame alone does not identify the root cause.

Schema 2 also records other live threads at that same second-chance debug stop.
Windows suspends the child's threads during a debug event; the collector does not
add SuspendThread/ResumeThread calls. It prioritizes the faulting thread, then takes
ascending thread IDs up to a fixed total of 16, each with at most 64 frames. The fault
PC stays `exception_pc`; other stopped PCs are `thread_pc`, never fault attribution.
One local symbol session is shared across the snapshot and cleaned up afterward.

`thread_snapshot` records observed/captured/omitted counts, available contexts,
frame count and symbol availability. Each context/frame carries a snapshot ID.
`all_threads_captured` requires no omitted threads and every selected context to be
available; it does not certify complete unwind/symbol resolution or a crash cause.
Failed contexts retain their Win32 error and have no invented stack. No snapshot is
synthesized for normal exits or watchdog termination. The parser keeps historical
schema 1 support, but current runners refuse a stale schema 1 collector. Real worker
faults with blocked waiters and an oversized thread set exercise these contracts.

Debugger strings use the advertised byte count, stop at their first terminator,
and have a total 64KiB read budget. Invalid UTF-16 is marked and converted with
replacement; optional text cannot terminate exception collection. The total
message count includes messages omitted by this text budget. This log is not the
SDK's unfiltered ERROR/CORRUPTION counter or proof of zero native errors.

`--dump <fresh-local-path>` optionally writes a normal minidump with thread and
unloaded-module information from the external collector, while the process is
stopped at its unhandled fault. It contains diagnostic process state and remains
local; neither SDK packages, Git evidence nor source archives include `.dmp` files.
The capture wrapper records its hash and location only. Existing output/dump files
are refused. A requested dump write failure rejects the capture.

`gfx_memory_stress_symbols` compiles the existing workload with Release `/O2 /MD`,
plus `/Zi` and linker `/DEBUG`, linking the unchanged core library. Its PDB improves
own workload stack resolution; it supplies no private SDK/runtime/driver symbols.
The wrapper freezes sources, executable/PDB/core hashes and exact public adapter ID,
retains the default 512MiB growth guard and full GBV/state tracking. Native completed,
guard-aborted, timed-out, crashed and collector-failed outcomes remain distinct.
Every outcome has `memory_acceptance=false`: debugger instrumentation and a new
binary are separate evidence, never a replacement for the normal long memory gate.
Module file hashes are disk observations after exit, not hashes of mapped memory.

[Debug events](https://learn.microsoft.com/en-us/windows/win32/debug/debugging-events),
[byte-length contract](https://learn.microsoft.com/en-us/windows/win32/api/minwinbase/ns-minwinbase-output_debug_string_info),
[StackWalk64](https://learn.microsoft.com/en-us/windows/win32/api/dbghelp/nf-dbghelp-stackwalk64),
[MiniDumpWriteDump](https://learn.microsoft.com/en-us/windows/win32/api/minidumpapiset/nf-minidumpapiset-minidumpwritedump).
