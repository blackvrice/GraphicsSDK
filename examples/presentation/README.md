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

Default: hardware, VSync on, validation and native loss injection off.
Use `--mode validation` for full GPU validation, `--mode warp` for WARP,
`--no-vsync` for Present interval zero, or `--frames 60` for a bounded normal-loop
smoke. Interval zero does not request tearing or prove its presence/absence.
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
python tools/audit_licenses.py --profile sdk-presentation --package out/GraphicsSDK-0.0.1-dev-windows-x64-memory-stress.zip --output out/my-new-presentation-inventory
```

Six serial fresh-process CTests cover hardware, hardware GBV, WARP × VSync on/off.
They briefly show their own window and manipulate it programmatically. Each checks
41 full RGBA backbuffer images before Present, padded readback rows, odd/changing
client extents, nine minimize/restore cycles, coalesced sizes, hide/show, native
removal while active and while minimized, recovery and safe close. Only self-test
enables native RemoveDevice. Missing support fails rather than passing as a skip.

The Python runner checks event coverage and four actual argument/output rejections;
it freezes source, installed headers/library and binary hashes across execution.
The inventory checks compiler/linker inputs and exact installed SDK identity.
No new dependency download is required.

For remaining manual acceptance, run normally, drag window borders, minimize and
restore via the taskbar, maximize/restore, cover/uncover and close via Esc/title bar.
Check resumed content, responsiveness and client extent. With differently scaled
monitors, move between them and verify content after actual WM_DPICHANGED. Record
adapter/display/DPI configuration and observations. No human visual acceptance or
physical DPI transition is claimed by the automatic checks. Unexpected driver
TDR/hot-unplug and the original GBV memory plateau remain separate open gates.

See the [measured report](../../docs/PHASE2_PRESENTATION_REPORT.md).
