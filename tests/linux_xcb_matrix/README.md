# Installed-SDK XCB integrated output matrix

`linux-xcb-matrix.yml` builds one Core installation, then five external consumers:
present, lifetime, resize, events and this multi-window consumer. All use the same
software ICD and SDK fingerprint. Normal and pinned core/synchronization validation
are separate jobs. Logs/receipts are uploaded; external binaries are not.

The new consumer keeps two independently sized XCB windows and swapchains alive
on one device. It acquires both images before alternating independent clear,
scissored draw and blend references. Each of six pairs (requested buffers 2/3/4,
FIFO versus IMMEDIATE in opposite windows) covers peer rendering during held-image
resize rejection, held-recording rejection, independent resize, stale input/submit
rejection, chain teardown and replacement. Every returned texel and available row
padding byte is checked, with 102 full image references total. Actual native mode,
selected-mode image counts, completion fences and object closure are observed.

The campaign also reruns present format/mode unsupported controls, pending GPU
lifetime, resize retirement/retry and WM/event/loss controls on the same installed
Core. Validation-only isolated raw Vulkan controls detect core VUIDs, a recording
synchronization hazard, sync-disabled behavior and callback counting. Those raw
instances use the same pinned layer/ICD but do not create XCB surfaces; invalid
commands are never submitted. Expected control errors are kept separate from
zero-error SDK output campaigns, including their native teardown logs.

This scope is X11/XCB with Xvfb/Openbox and a software ICD. It does not establish
physical scanout, user display acceptance, Wayland/XWayland, other window managers,
driver-induced loss, threaded API use, throughput or long-duration memory behavior.
Requested buffer counts are minimum requests, not an assertion of actual counts.

Build consumers as in the workflow, then run:

```sh
python3 tools/run_linux_xcb_campaign.py --consumer-root out/wsi-consumers \
  --sdk-library out/wsi-install/lib/libGraphicsSDKCore.a \
  --icd /path/to/lvp_icd.json --output out/fresh-wsi-evidence \
  --validation-dir /path/to/pinned/layers
```

Omit `--validation-dir` for the normal campaign. Output must be fresh. Test-only
proxies and host-signaled GPU gates are external fixtures, not SDK API features.
