"""Check the source-review metadata and frozen P3-CMP-01 selection contract.

Offline checks validate recorded metadata, not upstream source authenticity or GPU
execution. --live also reads the five clean pinned checkouts and reference cache.
No SDK is compiled, loaded, or measured by this tool.
"""
import argparse
import hashlib
import json
import pathlib
import re
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[1]
DIRECTORY = ROOT / 'benchmarks/phase3_comparison/competitors'
CHECKOUTS = dict(llgl='LLGL', sdl='SDL', diligent_core='DiligentCore', bgfx='bgfx', the_forge='TheForge')
CANDIDATES = set(CHECKOUTS) | {'nvrhi', 'dawn', 'sandbox'}
POLICY = dict(source_review_only=True, native_build_accepted=False,
              correctness_accepted=False, performance_accepted=False,
              historical_timings_reused=False, overall_SDK_ranking=False,
              root_license_is_complete_build_audit=False,
              automatic_requirement_reduction=False,
              GPU_timestamp_pending_task='P3-BEN-09')
REQUIREMENTS = [
    'fresh_pinned_build_input_license_audit', 'exact_backend_device_driver_queue_identity',
    'actual_core_and_sync_layer_load', 'missing_layer_and_wrong_target_negative_controls',
    'separate_validation_and_performance_processes', 'full_changing_readback_every_sample',
    'public_API_only_workload_operations', 'no_native_Vulkan_workload_shortcuts',
    'same_SPIRV_target_formats_geometry_and_payload', 'record_API_version_and_layout_differences',
    'fresh_three_SDK_interleaved_runs_same_GPU_driver', 'retain_all_samples_and_process_medians',
    'no_physical_memory_or_native_allocation_claim_from_public_release',
]


def read(path):
    return json.loads(path.read_text(encoding='utf-8'))


def digest(data):
    return hashlib.sha256(data).hexdigest()


def canonical(value):
    return digest(json.dumps(value, sort_keys=True, separators=(',', ':'), ensure_ascii=False).encode())


def require(value, message):
    if not value:
        raise ValueError(message)


def safe_path(value):
    path = pathlib.PurePosixPath(value)
    require(isinstance(value, str) and value and not path.is_absolute() and
            '..' not in path.parts and '\\' not in value and ':' not in value,
            'Unsafe source path')
    return path


def git(checkout, *args):
    return subprocess.check_output(['git', '--no-optional-locks', '-c', 'core.preloadIndex=false',
                                    '-C', str(checkout), *args])


def source_bytes(record, root=ROOT):
    safe_path(record['path'])
    sdk = record['sdk']
    if record['origin'] == 'clean_pinned_git_blob':
        require(sdk in CHECKOUTS, 'Unknown checkout')
        return git(root / '.deps' / CHECKOUTS[sdk], 'show', record['commit'] + ':' + record['path'])
    require(record['origin'] == 'pinned_reference_cache_LF', 'Unknown source origin')
    require(sdk in {'nvrhi', 'dawn', 'sandbox'}, 'Unexpected cached reference')
    return (root / '.deps/references' / sdk / record['path']).read_bytes().replace(b'\r\n', b'\n')


def validate(plan, review, root=ROOT):
    locks = read(root / 'benchmarks/competitors/sources.lock.json')
    require(plan['schema_version'] == review['schema_version'] == 1 and
            plan['task'] == review['task'] == 'P3-CMP-01', 'Wrong contract')
    require(plan['policy'] == POLICY, 'Source review cannot grant runtime/build acceptance or drop requirements')
    require(plan['required_before_measurement'] == REQUIREMENTS, 'Missing equivalence requirement')
    require(plan['source_review_sha256'] == canonical(review), 'Source review metadata changed')
    require(set(plan['candidates']) == CANDIDATES, 'Candidate set changed')
    require(plan['primary_additional_comparator'] == 'llgl', 'Primary comparator differs')
    require(plan['baseline_comparators'] == ['graphicssdk', 'sdl'], 'Baseline differs')
    require(plan['next_task'] == 'P3-CMP-02', 'Build/probe gate must come first')
    require(review['reviewed_on'] == '2026-10-28' and review['GPU_executed'] is False,
            'Review date/execution differs')
    records = {r['id']: r for r in review['files']}
    require(len(records) == len(review['files']) and records, 'Duplicate/empty source records')
    for record in records.values():
        sdk = record['sdk']
        safe_path(record['path'])
        require(sdk in CANDIDATES and record['commit'] == locks[sdk]['commit'], 'Source pin drift')
        require(record['origin'] == ('clean_pinned_git_blob' if sdk in CHECKOUTS else 'pinned_reference_cache_LF'),
                'Source provenance drift')
        url = 'https://github.com/' + locks[sdk]['repository'] + '/blob/' + record['commit'] + '/' + record['path']
        require(record['url'] == url, 'Source URL is not immutable/pinned')
        require(re.fullmatch('[0-9a-f]{64}', record['sha256']) and
                re.fullmatch('[0-9a-f]{40}', record['git_blob_sha1']), 'Missing source hashes')
        require(record['bytes'] > 0 and record['anchors'], 'Empty source evidence')
        require(all(a['token'] and a['lines'] and all(type(n) is int and n > 0 for n in a['lines'])
                    for a in record['anchors']), 'Missing source locations')
    licenses = dict(llgl='BSD-3-Clause', sdl='Zlib', diligent_core='Apache-2.0',
                    bgfx='BSD-2-Clause', the_forge='Apache-2.0', nvrhi='MIT', dawn='BSD-3-Clause',
                    sandbox='unconfirmed_project_root')
    for sdk, candidate in plan['candidates'].items():
        require(candidate['repository'] == locks[sdk]['repository'] and
                candidate['commit'] == locks[sdk]['commit'], 'Candidate pin drift')
        require(candidate['root_license'] == licenses[sdk], 'Root license classification drift')
        expected = 'baseline' if sdk == 'sdl' else 'selected_next' if sdk == 'llgl' else 'deferred_review'
        require(candidate['selection'] == expected and candidate['reason'].strip() and
                candidate['workload_caveat'].strip(), 'Selection/reason missing')
        require(candidate['evidence'] and all(r in records and records[r]['sdk'] == sdk
                                            for r in candidate['evidence']), 'Missing candidate evidence')
        if sdk != 'sandbox':
            require(any(records[r]['role'] == 'root_license' for r in candidate['evidence']), 'Missing root license input')
        require(candidate['build_inputs_approved'] is False, 'Old input audit cannot approve new Vulkan inputs')
    require(plan['candidates']['sandbox']['task_ids'] == [] and
            review['sandbox_root_license_paths'] == [], 'Sandbox license/scope requires a new review')
    expected_tasks = dict(llgl=[f'P3-CMP-{i:02}' for i in range(2, 11)], sdl=[f'P3-BEN-{i:02}' for i in range(1, 10)],
                          diligent_core=['P3-CMP-11'], bgfx=['P3-CMP-12'], the_forge=['P3-CMP-13'], nvrhi=[], dawn=[], sandbox=[])
    require(all(plan['candidates'][n]['task_ids'] == ids for n, ids in expected_tasks.items()), 'Task mapping drift')
    require(set(plan['workloads']) == {'upload', 'draw', 'pso', 'allocation'}, 'Four workloads required')
    for index, name in enumerate(('upload', 'draw', 'pso', 'allocation'), 3):
        item = plan['workloads'][name]
        require(item['baseline_path'] == f'benchmarks/phase3_comparison/{name}.json', 'Wrong baseline path')
        require(item['baseline_semantic_sha256'] == canonical(read(root / item['baseline_path'])), 'Baseline workload drift')
        require(item['correctness_task'] == f'P3-CMP-{index:02}' and
                item['measurement_task'] == f'P3-CMP-{index+4:02}', 'Wrong workload task')
        require(item['LLGL_API_plan'] and item['differences_to_record'] and item['evidence'] and
                all(r in records and records[r]['sdk'] == 'llgl' for r in item['evidence']), 'Missing API mapping')
    require(plan['common_contract_semantic_sha256'] == canonical(read(root / 'benchmarks/phase3_comparison/contract.json')),
            'Common workload contract drift')
    require(plan['LLGL_build_requirements'] == dict(LLGL_BUILD_RENDERER_VULKAN='ON',
            LLGL_VK_ENABLE_GPU_ASSISTED_VALIDATION='OFF', LLGL_VK_ENABLE_SPIRV_REFLECT='OFF',
            other_renderers='OFF', Release=True, input_audit_task='P3-CMP-02'), 'Build requirements drift')
    return plan


def validate_live(review, root=ROOT):
    locks = read(root / 'benchmarks/competitors/sources.lock.json')
    for sdk, directory in CHECKOUTS.items():
        checkout = root / '.deps' / directory
        require(git(checkout, 'rev-parse', 'HEAD').decode().strip() == locks[sdk]['commit'], 'Live checkout pin drift: ' + sdk)
        require(not git(checkout, 'status', '--porcelain', '--untracked-files=no').strip(), 'Dirty source checkout: ' + sdk)
    for record in review['files']:
        data = source_bytes(record, root)
        require(digest(data) == record['sha256'] and len(data) == record['bytes'], 'Live source hash drift: ' + record['id'])
        blob = hashlib.sha1(b'blob ' + str(len(data)).encode() + b'\0' + data).hexdigest()
        require(blob == record['git_blob_sha1'], 'Live blob drift')
        lines = data.decode('utf-8-sig').splitlines()
        for anchor in record['anchors']:
            require(anchor['lines'] == [i for i, line in enumerate(lines, 1) if anchor['token'] in line],
                    'Live API/source anchor drift: ' + record['id'])
    return len(review['files'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--live', action='store_true')
    args = parser.parse_args()
    review = read(DIRECTORY / 'source-review.json')
    validate(read(DIRECTORY / 'selection.json'), review)
    if args.live:
        print('Live pinned source files checked:', validate_live(review))
    print('P3-CMP-01 contract valid; source review only; next: P3-CMP-02; no GPU acceptance')


if __name__ == '__main__':
    main()
