"""Replay target-only virtual-memory ranges; never accept a process memory gate.

Input is the frozen target-event export from the fault investigation, not raw API
arguments: RELEASE events must contain their actual nonzero released range.
"""
import argparse
import collections
import gzip
import hashlib
import json
import pathlib
import re

from analyze_memory_stress import require, unique_object

ROOT = pathlib.Path(__file__).resolve().parents[1]
ORIGINS = ('gbv_resource_info_command_buffer', 'gbv_resource_info_other', 'other')
MAX_EVENT_BYTES = 256 * 1024 * 1024


def integer(value, low, high):
    return type(value) is int and low <= value <= high


def load_json(path):
    return json.loads(path.read_text(encoding='utf-8'), object_pairs_hook=unique_object)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def symbol_index(data):
    require(isinstance(data, dict), 'Invalid symbol document')
    identities = data.get('public_PDB_identity_matches')
    require(isinstance(identities, list) and len(identities) == 2, 'Missing exact public PDB identities')
    names = set()
    for entry in identities:
        require(isinstance(entry, dict), 'Invalid public PDB identity')
        name = entry.get('module')
        require(name in ('d3d12core.dll', 'd3d12sdklayers.dll') and name not in names
                and type(entry.get('sym_type')) is int and entry['sym_type'] == 3
                and entry.get('pdb_redistributed') is False
                and integer(entry.get('pdb_age'), 1, 2**32-1)
                and re.fullmatch(r'[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}', entry.get('pdb_guid', ''))
                and all(re.fullmatch(r'[0-9a-f]{64}', entry.get(k, '')) for k in ('pdb_sha256', 'image_sha256')),
                'Invalid/duplicate public PDB identity')
        names.add(name)
    result = {}
    frames = data.get('unique_frames')
    require(isinstance(frames, list), 'Missing symbol frames')
    for frame in frames:
        require(isinstance(frame, dict), 'Invalid symbol frame')
        name, offset = frame.get('module'), frame.get('offset')
        require(isinstance(name, str) and name.lower() in names and integer(offset, 0, 2**32-1), 'Invalid symbol frame')
        key = (name.lower(), offset)
        require(key not in result and type(frame.get('resolved')) is bool, 'Duplicate/invalid symbol frame')
        if frame['resolved']:
            require(isinstance(frame.get('symbol'), str) and frame['symbol'], 'Missing resolved symbol')
        result[key] = frame
    require(result, 'Empty symbol table')
    return result


def origin(event, symbols):
    found = set()
    require(isinstance(event.get('stack'), list), 'Missing event stack (use [] if unavailable)')
    for frame in event['stack']:
        require(isinstance(frame, dict), 'Invalid event frame')
        name = frame.get('module')
        if name is None:
            continue
        require(isinstance(name, str) and integer(frame.get('offset'), 0, 2**32-1), 'Invalid module-relative frame')
        if name.lower() in ('d3d12core.dll', 'd3d12sdklayers.dll'):
            key = (name.lower(), frame['offset'])
            require(key in symbols, 'Runtime frame absent from exact-offset symbol table')
            symbol = symbols[key]
            if symbol['resolved']:
                found.add((name.lower(), symbol['symbol']))
    info = ('d3d12sdklayers.dll', 'NDebug::CGBVDataController::AddResourceInfo') in found
    cb = ('d3d12core.dll', 'NDXGI::CDevice::AllocateCB_0022') in found
    return ORIGINS[0] if info and cb else ORIGINS[1] if info else ORIGINS[2]


class Ranges:
    """Disjoint half-open intervals tagged by the first observed live commit.

    Recommit does not transfer attribution. Decommit/release split intervals and
    remove only overlap. A later commit in a retired hole starts a new lifetime.
    No reservation baseline or zero-sized release extent is invented.
    """
    def __init__(self):
        self.live = []

    def commit(self, start, end, tag, index):
        cursor, added = start, []
        for left, right, _, _ in self.live:
            if right <= cursor:
                continue
            if left >= end:
                break
            if cursor < left:
                added.append((cursor, min(left, end), tag, index))
            cursor = max(cursor, right)
        if cursor < end:
            added.append((cursor, end, tag, index))
        self.live = sorted(self.live + added)
        return sum(right-left for left, right, _, _ in added)

    def free(self, start, end):
        remaining, removed = [], collections.Counter()
        for left, right, tag, index in self.live:
            lo, hi = max(start, left), min(end, right)
            if lo >= hi:
                remaining.append((left, right, tag, index))
                continue
            removed[tag] += hi-lo
            if left < lo:
                remaining.append((left, lo, tag, index))
            if hi < right:
                remaining.append((hi, right, tag, index))
        self.live = remaining
        return removed

    def totals(self):
        totals = dict.fromkeys(ORIGINS, 0)
        for left, right, tag, _ in self.live:
            totals[tag] += right-left
        return totals


def reconstruct(events, pid, symbols):
    require(integer(pid, 1, 2**32-1) and isinstance(events, list) and events, 'Missing target PID/events')
    ledger, timeline = Ranges(), []
    calls = collections.Counter(); added = collections.Counter(); retired = collections.Counter()
    peak = dict.fromkeys(ORIGINS, 0); peak_upper = dict.fromkeys(ORIGINS, 0)
    peak_total = 0; peak_total_upper = 0; peak_snapshot = None
    counts = collections.Counter(); group_start = {}; group_added = collections.Counter()
    previous_time = -1; same_time = []; tied_events = 0; unknown_free = 0

    def flush_group(index, time):
        nonlocal peak_total, peak_total_upper, peak_snapshot
        live = ledger.totals(); total = sum(live.values())
        upper = {k:group_start[k]+group_added[k] for k in ORIGINS}
        for name in ORIGINS:
            peak[name] = max(peak[name], live[name])
            peak_upper[name] = max(peak_upper[name], upper[name])
        row = dict(event_index=index,time_us=time,events_at_timestamp=len(same_time),
                   observed_live_bytes=total,origins=live,within_timestamp_peak_upper_bound_bytes=sum(upper.values()))
        timeline.append(row)
        peak_total_upper = max(peak_total_upper, sum(upper.values()))
        if peak_snapshot is None or total > peak_total:
            peak_total, peak_snapshot = total, row

    for index, event in enumerate(events):
        require(isinstance(event, dict) and integer(event.get('pid'), 1, 2**32-1) and event['pid'] == pid, 'Wrong target PID')
        time, start, end = (event.get(k) for k in ('time_us', 'base', 'end'))
        require(integer(time, 0, 2**63-1) and time >= previous_time, 'Invalid/inverted event time')
        require(integer(start, 4096, 2**64-1) and integer(end, start+1, 2**64)
                and start % 4096 == end % 4096 == 0
                and type(event.get('bytes')) is int and event['bytes'] == end-start, 'Invalid/empty/unaligned event range')
        if time == previous_time:
            tied_events += 1
            require(all(end <= left or start >= right for left, right in same_time), 'Overlapping same-time events have ambiguous order')
        else:
            if previous_time >= 0:
                flush_group(index-1, previous_time)
            same_time = []
            group_start = ledger.totals(); group_added = collections.Counter()
        same_time.append((start, end)); previous_time = time
        kind, flags = event.get('event'), event.get('flags')
        require((kind == 'VirtualAlloc' and flags in ('COMMIT', 'RESERVE', 'RESERVE COMMIT'))
                or (kind == 'VirtualFree' and flags in ('DECOMMIT', 'RELEASE')), 'Unsupported event/flags')
        counts[kind] += 1
        if kind == 'VirtualAlloc' and flags != 'RESERVE':
            tag = origin(event, symbols)
            calls[tag] += end-start
            introduced = ledger.commit(start, end, tag, index)
            added[tag] += introduced; group_added[tag] += introduced
        elif kind == 'VirtualFree':
            removed = ledger.free(start, end); retired.update(removed)
            unknown_free += end-start-sum(removed.values())
    flush_group(len(events)-1, previous_time)
    totals = ledger.totals()
    for name in ORIGINS:
        require(added[name]-retired[name] == totals[name], 'Range accounting disagreement')
    summary = dict(events=len(events),event_counts=dict(counts),pid=pid,diagnostic_only=True,memory_acceptance=False,
        scope='Only observed commit ranges minus observed frees; no initial-state/private-bytes/residency/VRAM/whole-process peak claim',
        attribution='First observed still-live commit; overlapping recommits preserve original tag',
        commit_call_bytes=dict(calls),introduced_observed_bytes=dict(added),
        overlapping_recommit_bytes={k:calls[k]-added[k] for k in ORIGINS},retired_observed_bytes=dict(retired),
        final_observed_live_bytes=sum(totals.values()),final_origins=totals,
        peak_observed_live_bytes=peak_total,peak_upper_bound_bytes=peak_total_upper,
        peak_scope='Lower bound at timestamp boundaries; upper bound allows all disjoint tied commits before frees',
        peak_snapshot=peak_snapshot,individual_origin_peaks=peak,individual_origin_peak_upper_bounds=peak_upper,
        free_bytes_outside_observed_live_ranges=unknown_free,disjoint_same_time_events=tied_events,
        surviving_ranges=[dict(base=a,end=b,origin=tag,commit_event_index=i) for a,b,tag,i in ledger.live])
    return summary, timeline


def read_events(path):
    with gzip.open(path, 'rb') as stream:
        data = stream.read(MAX_EVENT_BYTES+1)
    require(len(data) <= MAX_EVENT_BYTES and data.endswith(b'\n'), 'Oversized/truncated event export')
    return [json.loads(line, object_pairs_hook=unique_object) for line in data.decode('utf-8').splitlines()]


def analyze_manifest(manifest_path, symbols_path, root):
    frozen = {manifest_path: digest(manifest_path.read_bytes()), symbols_path: digest(symbols_path.read_bytes())}
    cases, symbol_data = load_json(manifest_path), load_json(symbols_path)
    require(isinstance(cases, list) and cases, 'Empty trace manifest')
    symbols = symbol_index(symbol_data); results, names = [], set()
    for case in cases:
        require(isinstance(case, dict), 'Invalid trace case')
        name = case.get('case')
        require(isinstance(name, str) and re.fullmatch(r'[a-zA-Z0-9_-]+', name) and name not in names, 'Invalid/duplicate case name')
        names.add(name)
        require(case.get('memory_acceptance') is False, 'Trace must be explicitly diagnostic')
        controller = case.get('controller', {})
        require(isinstance(controller, dict), 'Invalid controller metadata')
        require(all(type(controller.get(k)) is int and controller[k] == 0 for k in
                    ('started','stack_status','flags_status','stop_status','events_lost')), 'Controller failed/lost events')
        # This is the xperf exporter header format of this reviewed input schema.
        header = case.get('trace_header', '')
        require(isinstance(header, str) and re.search(r'(?:^|,)Events Lost: 0(?:,|$)', header)
                and re.search(r'(?:^|,)Buffers lost: 0(?:,|$)', header), 'Missing zero-loss export header')
        entry = case.get('filtered_events', {})
        require(isinstance(entry, dict), 'Invalid export metadata')
        relative = entry.get('path')
        require(isinstance(relative, str), 'Missing event path')
        path = (root/relative).resolve(); require(path.is_relative_to(root.resolve()), 'Event path outside evidence root')
        data = path.read_bytes()
        require(digest(data) == entry.get('sha256') and type(entry.get('bytes')) is int and len(data) == entry['bytes'], 'Changed target export')
        frozen[path] = digest(data)
        summary, timeline = reconstruct(read_events(path), case.get('pid'), symbols)
        counters = case.get('counters', {})
        require(isinstance(counters, dict) and all(integer(counters.get(k), 0, 2**64-1) for k in
                    ('traced_commit_call_bytes','VirtualAlloc_events','VirtualFree_events')), 'Invalid export counters')
        require(counters['traced_commit_call_bytes'] == sum(summary['commit_call_bytes'].values())
                and all(counters[k+'_events'] == summary['event_counts'].get(k, 0) for k in ('VirtualAlloc','VirtualFree')), 'Export/manifest counters disagree')
        results.append(dict(case=name,adapter=case.get('adapter'),input=entry,summary=summary,timeline=timeline))
    require(all(digest(p.read_bytes()) == sha for p,sha in frozen.items()), 'Analysis input changed')
    return results, {str(p):sha for p,sha in frozen.items()}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--trace-manifest', type=pathlib.Path, required=True)
    p.add_argument('--symbols', type=pathlib.Path, required=True)
    p.add_argument('--root', type=pathlib.Path, default=ROOT)
    p.add_argument('--output', type=pathlib.Path, required=True)
    args = p.parse_args()
    try:
        require(not args.output.exists(), 'Output already exists')
        results, frozen = analyze_manifest(args.trace_manifest.resolve(), args.symbols.resolve(), args.root.resolve())
        args.output.mkdir(parents=True, exist_ok=False)
        for case in results:
            timeline = case.pop('timeline')
            path = args.output/(case['case']+'-timeline.jsonl')
            data = ('\n'.join(json.dumps(row, separators=(',',':')) for row in timeline)+'\n').encode()
            path.write_bytes(data);case['timeline'] = dict(path=path.name,sha256=digest(data),bytes=len(data))
        result = dict(schema=1,diagnostic_only=True,memory_acceptance=False,input_hashes=frozen,cases=results)
        (args.output/'summary.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
        print(f'PASS: reconstructed {len(results)} target traces; diagnostic only, memory_acceptance=false')
        return 0
    except (ValueError,OSError,KeyError,TypeError,EOFError) as error:
        print('FAIL: '+str(error))
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
