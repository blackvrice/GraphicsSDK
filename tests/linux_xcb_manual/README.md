# P3-WSI-08 preparation only

This installed-SDK consumer displays a red rectangle moving across a blue background
at a 60 fps cap. It uses the existing fullscreen fixture/scissor draw and caller-owned
XCB event loop. It is a test consumer, not an installed SDK example or benchmark.
The SDK/Core implementation is unchanged.

Build the SDK with Vulkan/XCB enabled, install it, and build this directory against
that installation (reviewed Vulkan-Headers are still required in `.deps/`).

```sh
cmake -S tests/linux_xcb_manual -B out/manual-consumer -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++-14 \
  -DCMAKE_PREFIX_PATH="$PWD/out/manual-install"
cmake --build out/manual-consumer --parallel 1
```

On the actual Linux X11 desktop, use the explicit physical ICD and native identity
probe to inspect the available UUID-derived decimal IDs. The tool selects exactly
one requested ID and kind; it does not fall back to another GPU.

```sh
VK_DRIVER_FILES=/absolute/path/to/icd.json \
  out/manual-consumer/identity/gfx_linux_vulkan_device selection
python3 tools/linux_xcb_manual.py run \
  --binary out/manual-consumer/gfx_xcb_manual \
  --proxy out/manual-consumer/proxy/libvulkan.so.1 \
  --identity-binary out/manual-consumer/identity/gfx_linux_vulkan_device \
  --sdk-library out/manual-install/lib/libGraphicsSDKCore.a \
  --icd /absolute/path/to/icd.json --adapter-id EXACT_DECIMAL_ID \
  --display-route local-monitor --output out/fresh-manual-session
```

Use `--display-route remote-desktop` when observing through a remote desktop. Record
the route and monitor/compositor/environment details; a remote view does not prove
physical monitor scanout. Wayland sessions are rejected pending a separate scope.
Pass `--software` only for an explicitly selected CPU renderer, and
`--validation-dir /path/to/reviewed/layers` for the pinned validation runtime.
The existing loader observer runs only in forwarding mode, without fault injection.

Within five minutes, verify the session token in the window title, animation,
resize, minimize for at least two seconds then restore, maximize/restore,
cover/uncover, responsiveness, and finally close with the window's X button.
The window and connection remain caller-owned through GPU cleanup. A 500ms paused
watchdog enforces the overall deadline without issuing frames while inactive.

`human-observation.json` contains empty observer/environment/statement/check fields,
bound to the session ID, raw native stdout SHA-256, SDK fingerprint and device UUID.
Record the person's actual observations and direct statements there. Keep all raw
files together, then run:

```sh
python3 tools/linux_xcb_manual.py check --output out/fresh-manual-session
```

`check` validates the session and binding. Even a filled form does not authenticate
human observation or grant P3-WSI-08/P3-LNX-15 acceptance. A reviewer must compare
the real person's statements, display route, native events and runtime identity.
Native state checkpoints are supporting evidence, not proof of button-click origin,
maximize versus ordinary resize, unobscured output, motion visibility or scanout.

CI uses the distinct `smoke` stage with one software ICD in private Xvfb/Openbox.
It captures all pixels/padding of twelve animated 129x73 frames, verifies motion,
native present completion and cleanup, then exits at the frame limit. It generates
an empty human form; all human/hardware/task acceptance flags remain false.
An interrupted, lost/recovered or timed-out session is not an ordinary successful
manual capture. No Windows/WSL, driver or desktop settings are installed or changed.
