# Mixed-size / occupancy / retained-recording pool contracts

The reviewed adaptive slab is rebased onto the current ownership-hardened product
in a new `out/` source/install tree. All 29 existing creation guards remain, and the
new pool block allocation also acquires its output only on success. Pool range and
block policy headers are exact historical bytes. Product sources, public ABI,
historical candidates and dedicated-allocation default remain unchanged.

An independent installed-SDK consumer exercises:

- `mixed`: 16 rounds, 96 live buffers, 24 boundary sizes from 1 to 65,537 bytes,
  changing size classes and 1/2, 1/3, 1/4 survival patterns with periodic drains.
- `sparse`: four rounds of 31 buffers per 64/256/1024/4096-byte request size; keep
  one lease per actual native memory block, refill holes and reset after empty.
- `held`: eight rounds of ended recordings retaining 24 dropped wrappers across
  writes to 72 replacements, then dropped submitted recordings and full readback.

Every round injects one bind OOM (-2/-1 alternating), verifies exact rollback and
continues with valid resources. This is simulated failure, never real heap pressure.
The existing immutable own fault layer is included in a new TU that exports
read-only complete buffer binding snapshots. Native requirements/dedicated flags
are queried downstream; the driver does not receive synthetic invalid handles.

`Model.hpp` compares actual bindings with first-free-slot and live-block growth
policy, per memory type/stride. It checks exact live owners, alignment, compatible
memory bits, nonoverlap, no host sharing, explicit block reclamation and public
statistics. Native identity and slot checks are separate from full GPU data checks.
Helpers are dedicated Upload/Readback buffers and are released before stable
checkpoints. Retained submission checkpoints deliberately do not poll SDK status.
They assert SDK ownership before public fence wait, not that the GPU is still busy.

The sparse pattern is defined by native blocks: the dedicated product keeps more
logical survivors than the pool. It is an ownership/occupancy contract, not a fair
performance workload. Mixed and held have fixed matching logical input sequences.
No timings, physical residency or release acceptance are inferred from the trace.

[Report](../../docs/VULKAN_POOL_CHURN_REPORT.md) records the observed 6.2x sparse
reservation and why the candidate remains unadopted. Full rebase allocation-OOM
fallback retesting, policy tradeoffs and new matched performance comparisons remain
separate; the earlier isolated fault study is not relabeled as this candidate.
