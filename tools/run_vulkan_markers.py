"""Record real debug-utils labels and controlled missing-procedure responses."""
import argparse
import hashlib
import json
import os
import pathlib
import subprocess
import sys
from audit_licenses import ROOT, sdk_fingerprint


def require(value, message):
    if not value:
        raise ValueError(message)


def validate_rows(rows, fingerprint, validation, denied, require_software=False):
    require(rows and rows[-1].get('event') == 'result', 'Incomplete native result')
    result = rows[-1]
    adapters = [r for r in rows if r.get('event') == 'adapter']
    labels = [r for r in rows if r.get('event') == 'native_label']
    observers = [r for r in rows if r.get('event') == 'observer']
    require(result['source'] == fingerprint and result['validation'] is validation
            and result['denied_proc'] is denied and result['observed'] is True, 'Wrong source or execution mode')
    require(adapters and len(adapters) == result['adapters'] and len({a['adapter_id'] for a in adapters}) == len(adapters), 'Invalid adapters')
    require(not require_software or all(a['software'] is True for a in adapters), 'Software ICD required')
    expected = [] if denied else ['slice', '임시-marker-🙂', '', 'm'*200, 'inside-pass', 'reused']*len(adapters)
    require([r['text'] for r in labels] == expected and [r['index'] for r in labels] == list(range(len(labels)))
            and all(type(r['command']) is int and r['command'] > 0 for r in labels), 'Native label bytes/order/command mismatch')
    require(result['accepted_markers'] == result['native_calls'] == result['forwarded'] == len(expected)
            and result['assertions'] >= 30 and result['observer_errors'] == result['live_instances'] == result['live_devices'] == 0,
            'Missing forwarding, rejection or lifetime checks')
    require(len(observers) == 1 and observers[0]['debug_instances'] > 0 and observers[0]['lookups'] > 0
            and observers[0]['calls'] == observers[0]['forwarded'] == len(expected), 'Observer not active')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=pathlib.Path, required=True)
    parser.add_argument('--layer', type=pathlib.Path, required=True)
    parser.add_argument('--manifest-dir', type=pathlib.Path, required=True)
    parser.add_argument('--sdk-library', type=pathlib.Path, required=True)
    parser.add_argument('--output', type=pathlib.Path, required=True)
    parser.add_argument('--validation-dir', type=pathlib.Path)
    parser.add_argument('--icd', type=pathlib.Path)
    parser.add_argument('--require-software', action='store_true')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    paths = [args.binary, args.layer, args.sdk_library, args.manifest_dir/'VkLayer_gfx_marker.json', pathlib.Path(__file__),
             ROOT/'tests/VulkanMarkerTests.cpp', *sorted((ROOT/'tests/vulkan_marker').glob('*'))]
    if args.icd:
        paths.append(args.icd)
    frozen = {str(p.resolve()): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths if p.is_file()}
    fingerprint = sdk_fingerprint(ROOT)
    require(fingerprint.encode() in args.binary.read_bytes(), 'Stale SDK consumer')
    state = dict(SDK_source_fingerprint=fingerprint, cases=[], status='running', serialized=True,
                 source_base_commit=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
                 frozen_inputs=frozen, require_software=args.require_software)

    def save():
        (args.output/'campaign.json').write_text(json.dumps(state, indent=2)+'\n', encoding='utf-8')

    save()
    try:
        for validation in ([False, True] if args.validation_dir else [False]):
            for denied in (False, True):
                name = ('validation' if validation else 'normal') + ('-missing-proc' if denied else '')
                env = {k:v for k,v in os.environ.items() if not k.upper().startswith(('VK_', 'VVL_'))}
                env['VK_LOADER_LAYERS_DISABLE'] = '~implicit~'
                env['VK_INSTANCE_LAYERS'] = 'VK_LAYER_GFX_marker'
                env['VK_LAYER_PATH'] = str(args.manifest_dir.resolve()) + (os.pathsep+str(args.validation_dir.resolve()) if args.validation_dir else '')
                if args.icd:
                    env['VK_DRIVER_FILES'] = str(args.icd.resolve())
                command = [str(args.binary.resolve()), '--observe', str(args.layer.resolve())]
                if validation:
                    command.append('--validation')
                if denied:
                    command.append('--deny-proc')
                process = subprocess.run(command, env=env, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=120)
                (args.output/(name+'.stdout')).write_bytes(process.stdout)
                (args.output/(name+'.stderr')).write_bytes(process.stderr)
                row = dict(name=name, command=command, returncode=process.returncode,
                           environment={k:v for k,v in env.items() if k.startswith('VK_')})
                state['cases'].append(row); save()
                require(process.returncode == 0, 'Native failure: '+name)
                row['result'] = validate_rows([json.loads(l) for l in process.stdout.decode('utf-8').splitlines()],
                                              fingerprint, validation, denied, args.require_software)
                print('PASS '+name, flush=True); save()
        state['status'] = 'passed'
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        state.update(status='failed', error=str(error))
    state['inputs_unchanged'] = sdk_fingerprint(ROOT) == fingerprint and all(pathlib.Path(p).exists() and hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest() == h for p,h in frozen.items())
    save()
    return 0 if state['status'] == 'passed' and state['inputs_unchanged'] else 1


if __name__ == '__main__':
    sys.exit(main())
