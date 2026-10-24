# Guarded Vulkan buffer allocation failure study

This own, explicit Vulkan layer injects bounded allocation/bind errors in isolated
child processes. It does not exhaust GPU memory or cause device loss/TDR. No driver,
system settings, vendor code or historical experiment is modified.

The pre-fix SDK is the frozen adaptive candidate. The new candidate copies that
reviewed snapshot and changes only buffer allocation ownership: the returned memory
handle enters `Allocation` after `VK_SUCCESS`. Failed output handles are discarded.
See `candidate.patch` and `tools/prepare_vulkan_buffer_pool_fault.py`.

`FaultPlan.hpp` permits up to two allocation errors and one bind error per arm.
Plans arm only in the failure phase; rearming an unconsumed plan is rejected.
Allocation errors deliberately return a non-null invalid handle. The layer catches
an attempted free of such an unowned handle before it reaches validation/the driver.
This guard is part of the regression control, not a production error recovery path.
New and existing bindings also check memory type, alignment, range and nonoverlap;
freeing memory with bound buffers is rejected. No VkBuffer or VkDeviceMemory is wrapped.

Fifteen cases cover clean control, host/device pool OOM with exact fallback, double
and mixed OOM, non-OOM propagation, fresh/growing/reused-slot bind OOM, fallback bind
failure, dedicated/Upload/Readback OOM, and an ended recording retaining a slot.
Each case has three failure/retry cycles with distinct full GPU payload readbacks.
Existing guard data must survive the error. Per-phase native attempts/successes,
request bytes, slot reuse, object/byte conservation, final release and teardown are
checked. Deferred cleanup at device destruction is not accepted as leak recovery.

The existing product SDK remains unchanged and the allocator is not adopted. The
same direct-output ownership pattern in product creation paths needs a separate
product-wide audit/fix; this patch is confined to the candidate buffer path. Broader
mixed-size/occupancy/churn and performance acceptance also remain open. No new
performance, physical residency or long memory acceptance is claimed. Historical
Intel AV investigation remains deferred.

[Report](../../docs/PHASE3_VULKAN_BUFFER_POOL_FAULT_REPORT.md) links the serial builds,
raw process records, guarded pre-fix evidence, dependency audit, source ZIP and verifier.
Reproduction requires new source/build/install/result paths; current snapshots are
preserved. `make-layer.py` derives a separate layer from own prior diagnostic code.
