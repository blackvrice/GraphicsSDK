# Windowed presentation consumer

Win32/C++23 example using the installed `GraphicsSDK::Core` graphics API only.
The window shows a moving orange rectangle on a blue background. Resize, minimize,
restore and close run on the creating thread. Escape closes the window.

```powershell
cmake --install out/core --config Release --prefix out/install
cmake -S examples/presentation -B out/presentation -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=D:/source/GraphicsSDK/out/install
cmake --build out/presentation --config Release --parallel 1
./out/presentation/Release/gfx_presentation.exe
```

Default: hardware, VSync on, 60fps application cap, validation and native loss injection off.
`--list-adapters` alone prints fresh candidate IDs/metadata without creating a window.
Pass a listed decimal ID with `--adapter ID` for exact selection. Use `--mode gpu`
or `--mode validation` for hardware and `--mode warp` for WARP; the software kind
must match. Recovery retains an explicit ID. Missing IDs fail without fallback.
IDs are backend-scoped OS LUIDs, refreshed after reboot/topology changes; they are
not persistent hardware serials or list indices. Enumeration does not create devices
and device creation still checks required feature support.
Use `--mode validation` for full GPU validation, `--mode warp` for WARP,
`--no-vsync` for Present interval zero, or `--frames 60` for a bounded normal-loop
smoke. Interval zero does not request tearing or prove its presence/absence.
`--fps 1..240` changes the normal-loop cap independently of VSync. It is an upper
bound, not guaranteed display FPS. The example requires Windows 10 1803 or newer
for its private high-resolution waitable timer. It waits for timer or window input,
rechecks a monotonic deadline after input, and does not change global timer settings.
Late frames do not trigger catch-up bursts. Suspension resets the deadline for an
immediate first restored frame. A failed timer/wait is reported as an error.
`--output new.jsonl` creates a new file exclusively; it never replaces an old run.

Callbacks record client-pixel extent/suspension; the main loop drops the recorded
command's backbuffer references before resizing. Frames reuse one command list
and wait for their fence: this is an ownership example, not a frames-in-flight
optimization or performance benchmark. Minimized, zero-extent and hidden windows
do no GPU work; the interactive loop waits for messages. A covered visible window
is not classified as hidden; the SDK does not expose DXGI occlusion status yet.

Close keeps HWND alive until GPU work/resources are released. Observed DeviceLost
rebuilds device, pipeline and swapchain from CPU shader bytes. Failed recreation
prints the native reason and exits 1. It never forces an actual TDR or changes
system/driver configuration. The manifest requests PerMonitorV2 awareness.

```powershell
ctest --test-dir out/presentation -C Release --output-on-failure
python tools/run_presentation_validation.py --output benchmarks/reports/validation/my-new-presentation-run
python tools/run_adapter_validation.py --output benchmarks/reports/validation/my-new-explicit-adapter-run
python tools/audit_licenses.py --profile sdk-adapters --package out/GraphicsSDK-0.0.1-dev-windows-x64-adapters.zip --output out/my-new-adapter-inventory
```

Six serial fresh-process CTests cover hardware, hardware GBV, WARP × VSync on/off.
They briefly show their own window and manipulate it programmatically. Each checks
49 full RGBA backbuffer images before Present, padded readback rows, odd/changing
client extents, nine minimize/restore cycles, coalesced sizes, hide/show, native
removal while active and while minimized, recovery and safe close. Only self-test
enables native RemoveDevice. Missing support fails rather than passing as a skip.
Four tiny client extents (127×1, 129×2, 127×3, 257×9) each receive two full-image
checks. Positive extents always receive a positive in-bounds scissor, including
1×1 in the CPU policy test. Native nonclient window sizing imposes a minimum width.
The seventh CTest checks 2,278 geometry/clock boundary assertions without a GPU.

The Python runner checks event coverage and eight actual argument/output rejections;
it freezes source, installed headers/library and binary hashes across execution.
The inventory checks compiler/linker inputs and exact installed SDK identity.
No new dependency download is required.
It also runs actual 30/60fps caps and sends 40 wake messages plus close to its own
child window at 1fps. Only matching process ID and presentation class receive these
messages. Closing must interrupt the wait and must not render extra early frames.
Self-test image runs bypass pacing; normal-loop pacing is separate evidence.

For remaining manual acceptance, run normally, drag window borders, minimize and
restore via the taskbar, maximize/restore, cover/uncover and close via Esc/title bar.
Check resumed content, responsiveness and client extent. With differently scaled
monitors, move between them and verify content after actual WM_DPICHANGED. Record
adapter/display/DPI configuration and observations. No human visual acceptance or
physical DPI transition is claimed by the automatic checks. Unexpected driver
TDR/hot-unplug and the original GBV memory plateau remain separate open gates.

Normal sessions also flush `normal_session` and `window_state` checkpoints, recording
client extent, frame count, actual minimize/restore/maximize transitions and close
reason. These events and `interactive_exit` always retain `interactive_acceptance=false`.
Only a separate human observation tied to the exact native-log hash can establish
visual acceptance; `tools/manual_presentation_evidence.py` checks that observation
against effective resize, paused/resumed frames and safe close. The native program
cannot determine whether a person or automation generated an input message.

See the [measured report](../../docs/PHASE2_PRESENTATION_REPORT.md).
The [small-window and pacing report](../../docs/PHASE2_PRESENTATION_PACING_REPORT.md)
records the reproduced failure and current verification.
The [explicit-adapter report](../../docs/PHASE2_ADAPTER_REPORT.md) adds actual selection
and same-ID recovery on both hardware GPUs and WARP, including installed consumers.
