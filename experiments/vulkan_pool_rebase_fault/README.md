# Rebased allocator OOM / exact fallback contracts

This profile links the unchanged rebased adaptive candidate (`40a467f7…`). It does
not modify the product, candidate, historical fault consumer, layer or churn tools.
The original 15-case fault project is freshly built against that installation.
Seven new mixed-context cases use a separate consumer and the existing complete
binding snapshot layer. Native successful handles are never wrapped.

All new cases keep 4KiB and 64-byte pool guards plus an 8KiB standalone guard.
They cover pool host/device OOM with exact standalone fallback, two allocation
failures, unknown error without fallback, and fallback bind failure with an ended
recording retaining a dropped guard wrapper. The two reuse cases keep a second
4KiB block half occupied: a first target reuses its free slot without consuming a
pending allocation OOM, then a second target grows, consumes it and falls back.

The binding observer verifies unchanged guard handles/ranges, the reused offset,
exact new 4KiB standalone fallback, rollback and an 8KiB growth retry after failure.
GPU copies/readbacks check all payloads, including the retained recording, before
explicit cleanup. Every cycle ends with public/native ownership zero. Synthetic
failed allocation outputs are deliberately poisoned and the own layer guards
invalid frees/binds before they could reach VVL or the driver.

The full fresh matrix is 15 legacy + 7 new cases, each with three cycles on exact
NVIDIA/Intel adapters and validation OFF/ON. The offline reviewer reconstructs
the full command matrix, checks raw output and actual binaries, native interval
counts/bytes, DXGI/Vulkan LUID, binding bounds/nonoverlap and retry behavior. JSON
scalar types are preserved. Negative controls must fail without result JSON.

This is bounded synthetic fault evidence, not real OOM, device-loss recovery,
physical memory reclamation, long-duration acceptance or performance measurement.
The previous 6.2x sparse reservation observation still applies; no pool promotion.
See [the report](../../docs/VULKAN_POOL_REBASE_FAULT_REPORT.md).
