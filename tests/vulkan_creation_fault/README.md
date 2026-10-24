# Product Vulkan creation ownership tests

This CPU-only executable includes the actual product implementation in its test TU,
then injects exact-signature PFNs into private native tables. It exposes no test
switches or new public API. It loads no Vulkan driver and makes no GPU claims.
`Stubs.hpp` contains own stub bodies with function signatures generated from the
reviewed Vulkan-Headers registry by the recorded `make-stubs.py`.

Twenty-seven cases exercise failed outputs, destruction of earlier successful
objects and a clean retry through the actual buffer/image, command/query/fence,
shader/pipeline, render-pass/framebuffer, descriptor and swapchain implementation.
Failures return poisoned outputs except APIs whose contract requires null outputs
(command/descriptor arrays and graphics pipelines). Pool-owned children are released
with their parent. The fake ledger catches invalid cleanup instead of calling a driver.

The same TU compiles once against the verified original product snapshot and once
against current product sources, with their matching static installations. Compiler
read evidence proves which implementation/private headers each executable consumed.
Both runs create/destroy 254 valid fake objects and leave no remainder. The old run
detects 21 invalid cleanup attempts; the new run has zero. Each run has 270 assertions.

Instance/device/debug-messenger/Win32-surface creation output routing is statically
audited and the generic helper is CPU-tested across integer/pointer handle shapes;
their loader-level failure is not injected here. Normal live-device/validation/WSI
behavior is covered by the product CTests. Separate explicit-layer tests cover real
NVIDIA/Intel buffer memory/bind errors and full GPU readback. These are distinct kinds
of evidence. Device loss, real heap exhaustion and general error handling remain separate.

[Report](../../docs/VULKAN_CREATION_OWNERSHIP_REPORT.md) links builds, raw results,
source before/after hashes, dependency audit and source ZIP.
