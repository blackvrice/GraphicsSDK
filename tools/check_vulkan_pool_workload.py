"""Replay the Phase 6 sparse pool workload without a Vulkan device."""

import argparse
import pathlib

from linux_comparison import ROOT, read, require, save


ARCHIVE = ROOT/'benchmarks/results/2026-10-24-vulkan-pool-churn/results.json'
SIZES = (64, 256, 1024, 4096)
INITIAL = 31
SURVIVORS = (0, 1, 3, 7, 15)
REFILL = 8
ROUNDS = 4
MAX_BLOCK_BYTES = 64 * 1024
CAPPED_SLOTS = 4


def model(slot_limit=None):
    """First-free slots, live-block growth and immediate empty-block release."""
    blocks = {size: [] for size in SIZES}
    allocation_calls = 0

    def allocate(size, ordinal):
        nonlocal allocation_calls
        group = blocks[size]
        for capacity, live in group:
            if len(live) < capacity:
                live.add(ordinal)
                return
        largest = max((capacity for capacity, _ in group), default=0)
        limit = min(slot_limit or MAX_BLOCK_BYTES//size, MAX_BLOCK_BYTES//size)
        group.append((min(max(1, 2*largest), limit), {ordinal}))
        allocation_calls += 1

    def checkpoint():
        live = [(size, capacity, len(ids)) for size, group in blocks.items() for capacity, ids in group]
        required = sum(size*count for size, _, count in live)
        return dict(buffers=sum(count for _, _, count in live), allocations=len(live),
                    reserved=sum(size*capacity for size, capacity, _ in live), required=required,
                    payload=required, allocation_calls=allocation_calls)

    for size in SIZES:
        for ordinal in range(INITIAL):
            allocate(size, ordinal)
    full = checkpoint()
    for size, group in blocks.items():
        for _, live in group:
            live.intersection_update(SURVIVORS)
        blocks[size] = [(capacity, live) for capacity, live in group if live]
    sparse = checkpoint()
    for ordinal in range(INITIAL, INITIAL + REFILL):
        for size in SIZES:
            allocate(size, ordinal)
    refilled = checkpoint()
    for group in blocks.values():
        group.clear()
    empty = checkpoint()
    allocate(SIZES[-1], 0)
    singleton = checkpoint()
    blocks[SIZES[-1]].clear()
    return dict(full=full, sparse=sparse, refilled=refilled, empty=empty,
                reset_singleton=singleton, reset_empty=checkpoint())


def verify_archive(archive, adaptive, dedicated):
    seen = set()
    for entry in archive['runs']:
        run = entry['result']
        if run['profile'] != 'sparse':
            continue
        name = entry['name']
        candidate = run['candidate']
        require(name not in seen and tuple(run['strides']) == SIZES, f'{name}: sparse archive identity')
        seen.add(name)
        rows = {(row['round'], row['label']): row for row in run['checkpoints']}
        require(len(rows) == len(run['checkpoints']), f'{name}: duplicate checkpoint')
        expected = {'full': (adaptive if candidate else dedicated)['full']}
        if candidate:
            expected.update({'one-per-block': adaptive['sparse'], 'holes-refilled': adaptive['refilled'],
                             'empty': adaptive['empty'], 'reset-singleton': adaptive['reset_singleton'],
                             'reset-empty': adaptive['reset_empty']})
        for index in range(ROUNDS):
            for label, values in expected.items():
                row = rows[index, label]
                require(all(row[key] == values[key] for key in ('buffers', 'allocations', 'reserved', 'required',
                                                               'payload')), f'{name}: {label} round {index} differs')
            if not candidate:
                require(rows[index, 'one-per-block']['buffers'] == dedicated['full']['buffers'],
                        f'{name}: old dedicated survivor schedule differs')
    require(seen == {f'{variant}-gpu{gpu}-v{validation}-sparse' for variant in ('product', 'candidate')
                     for gpu in range(2) for validation in range(2)}, 'Missing historical sparse runs')
    return len(seen)


def check():
    dedicated, adaptive, capped = model(1), model(), model(CAPPED_SLOTS)
    historical_runs = verify_archive(read(ARCHIVE), adaptive, dedicated)
    for checkpoint in dedicated:
        require(all(dedicated[checkpoint][key] == adaptive[checkpoint][key] == capped[checkpoint][key]
                    for key in ('buffers', 'required', 'payload')), f'{checkpoint}: logical workload differs')
    require(capped['sparse']['reserved'] == 3*capped['sparse']['required'] and
            capped['refilled']['allocation_calls'] == capped['full']['allocation_calls'] <
            dedicated['refilled']['allocation_calls'], 'Four-slot budget or refill reuse differs')
    return dict(schema_version=1, historical_runs=historical_runs, rounds=ROUNDS, sizes=list(SIZES),
                initial_per_size=INITIAL, survivor_ordinals=list(SURVIVORS), refill_per_size=REFILL,
                capped_slots=CAPPED_SLOTS, model=dict(dedicated=dedicated, historical_adaptive=adaptive,
                                                    four_slot=capped))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=pathlib.Path, help='Write the workload and CPU model checkpoints as JSON')
    args = parser.parse_args()
    result = check()
    if args.output:
        save(args.output, result)
    print(f"PASS {result['historical_runs']} archived sparse runs and matched dedicated/adaptive/four-slot model")


if __name__ == '__main__':
    main()
