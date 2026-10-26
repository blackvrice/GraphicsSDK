# Installed SDK XCB event loop

Linux/XCB example with caller-owned window and connection. Build the SDK with
`GFX_ENABLE_VULKAN=ON`, `GFX_VULKAN_XCB=ON`, install it, then configure this directory
with `CMAKE_PREFIX_PATH` pointing to that installation.

```sh
cmake -S examples/xcb_presentation -B out/xcb-example -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++-14 -DCMAKE_PREFIX_PATH="$PWD/out/installed-sdk"
cmake --build out/xcb-example --parallel 1
out/xcb-example/gfx_xcb_window --software
```

Requires a running X11 display and libxcb. `--validation` requires the reviewed
external validation runtime. Omit `--software` to request hardware; support remains
limited to the exact adapter/window system combination actually validated.

The example clears an asymmetric blue background at a capped 60 frames per second.
Map/Unmap and WM_STATE events pause/resume rendering. Paused states block in poll
rather than retrying acquire or resize. WM_DELETE_WINDOW drains SDK resources before
destroying the caller-owned window. DestroyNotify and connection errors terminate
the loop. The connection allocation stays alive until all SDK children are released.

Out-of-date requests an explicit resize. Surface loss discards the chain so the next
attempt creates a fresh native surface. If that replacement also reports loss before
a successful frame, the example exits with an error rather than retrying forever.
The SDK does not consume the application's X event queue or own its window manager.

`openbox.xml` is only a minimal configuration for private Xvfb CI. The SDK and example
do not require Openbox in normal use. Automated protocol delivery is separate from a
person clicking a close/minimize button or accepting physical monitor output.
