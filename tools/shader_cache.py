"""Content-addressed cache for validated offline DXC objects."""

import hashlib
import json
import os
import pathlib
import subprocess
import tempfile

from linux_comparison import read, require, save, sha
from shader_toolchain import FLAGS


MAGIC = b'GFX-DXC1'
MAX_OBJECT = 4 * 1024 * 1024
HEADER = len(MAGIC) + 64
PROFILES = {'vert': 'vs_6_0', 'frag': 'ps_6_0'}


def command_runner(out):
    commands = {}

    def invoke(name, *args, env=None, reject=False):
        args = [str(arg) for arg in args]
        process = subprocess.run(args, cwd=out, env=env, capture_output=True, timeout=900)
        (out/f'{name}.stdout').write_bytes(process.stdout)
        (out/f'{name}.stderr').write_bytes(process.stderr)
        commands[name] = dict(argv=args, exit_code=process.returncode)
        save(out/'commands.json', commands)
        require(process.returncode > 0 if reject else process.returncode == 0,
                f'{name}: unexpected exit {process.returncode}; see {out}/{name}.stderr')
        return process.stdout.decode('utf-8', errors='replace')

    return invoke


def digest(data):
    return hashlib.sha256(data).digest()


def cache_key(source, expanded, arguments, identity, target):
    record = dict(version=MAGIC.decode('ascii'), source=source, expanded=expanded,
                  arguments=arguments, toolchain=identity, target=target)
    return digest(json.dumps(record, sort_keys=True, separators=(',', ':')).encode('utf-8'))


def cache_read(path, key):
    try:
        with path.open('rb') as file:
            record = file.read(HEADER + MAX_OBJECT + 1)
    except FileNotFoundError:
        return 'miss', None
    prefix = MAGIC + key
    payload = record[HEADER:]
    if (record.startswith(prefix) and 0 < len(payload) <= MAX_OBJECT and
            digest(payload) == record[len(prefix):HEADER]):
        return 'hit', payload
    return 'repaired', None


def cache_write(path, key, payload):
    require(0 < len(payload) <= MAX_OBJECT, 'Shader object exceeds cache bounds')
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=path.parent, prefix='.pending-') as directory:
        pending = pathlib.Path(directory)/'entry'
        pending.write_bytes(MAGIC + key + digest(payload) + payload)
        os.replace(pending, path)


def compile_dxc(dxc, arguments, source, output, work, invoke, validate, *, target, cache=None, identity=None):
    intermediate = work/output.name
    name = output.stem

    def step(label, *args):
        try:
            invoke(label, *args)
        except ValueError as error:
            diagnostic = '\n'.join((output.parent/f'{label}.{stream}').read_text(
                encoding='utf-8', errors='replace').strip() for stream in ('stderr', 'stdout')).strip()
            raise ValueError(f'{target} {label}: {diagnostic or error}') from error

    status, entry, key, cached = 'disabled', None, None, None
    compile_input = source
    if cache is not None:
        require(identity, 'Cached compilation requires a toolchain identity')
        expanded = work/f'{name}.preprocessed.hlsl'
        step(name+'-preprocess', dxc, *arguments, '-P', '-Fi', expanded, source)
        key = cache_key(sha(source), sha(expanded), arguments, identity, target)
        entry = cache/(key.hex()+'.gfxshader')
        status, cached = cache_read(entry, key)
        compile_input = expanded
    if cached is None:
        step(name+'-compile', dxc, *arguments, compile_input, '-Fo', intermediate)
    else:
        intermediate.write_bytes(cached)
    validate(intermediate, step)
    if entry is not None and cached is None:
        with intermediate.open('rb') as file:
            cache_write(entry, key, file.read(MAX_OBJECT + 1))
    intermediate.replace(output)
    return dict(state=status, key=key.hex() if key is not None else None, object_sha256=sha(output))


def compile_dxil(root, source, entry, stage, output, work, invoke, cache, identity):
    def validate(path, step):
        step(output.stem+'-validate', root/'bin/x64/dxv.exe', path)

    return compile_dxc(root/'bin/x64/dxc.exe', (*FLAGS, '-E', entry, '-T', PROFILES[stage]),
                       source, output, work, invoke, validate, target=f'DXIL {PROFILES[stage]}',
                       cache=cache, identity=identity)


def exercise_cache(out, build):
    """Use the normal compiler path with a nested include and a corrupt entry."""
    out.mkdir()
    source = out/'cache-fixture.hlsl'
    source.write_text('#include "middle.hlsli"\nfloat4 pixelMain() : SV_Target0 { return color(); }\n',
                      encoding='utf-8')
    (out/'middle.hlsli').write_text('#include "color.hlsli"\n', encoding='utf-8')
    include = out/'color.hlsli'
    include.write_text('float4 color() { return float4(1, 0.25, 0, 1); }\n', encoding='utf-8')
    cache = out/'objects'
    results = {name: build(source, name, cache) for name in ('cold', 'warm')}
    cold, warm = results['cold'], results['warm']
    require(cold['state'] == 'miss' and warm['state'] == 'hit' and
            cold['key'] == warm['key'] and cold['object_sha256'] == warm['object_sha256'],
            'Cold/warm shader cache outputs differ')
    (cache/(warm['key']+'.gfxshader')).write_bytes(b'truncated')
    repaired = results['repaired'] = build(source, 'repaired', cache)
    require(repaired['state'] == 'repaired' and repaired['key'] == cold['key'] and
            repaired['object_sha256'] == cold['object_sha256'], 'Corrupt shader object was not repaired')
    include.write_text('float4 color() { return float4(0, 1, 0, 1); }\n', encoding='utf-8')
    changed = results['include_changed'] = build(source, 'include-changed', cache)
    require(changed['state'] == 'miss' and changed['key'] != cold['key'] and
            changed['object_sha256'] != cold['object_sha256'], 'Nested include change reused a cached object')
    source.write_text('#include "middle.hlsli"\nfloat4 pixelMain() : SV_Target0 { return missingSymbol; }\n',
                      encoding='utf-8')
    try:
        build(source, 'invalid-source', cache)
    except ValueError as error:
        message = str(error)
        require('cache-fixture.hlsl:2:' in message and 'missingSymbol' in message,
                f'Missing original HLSL diagnostic: {message}')
        results['invalid_source'] = message
    else:
        raise ValueError('Invalid HLSL produced a cached object')
    require(len(list(cache.glob('*.gfxshader'))) == 2, 'Failed compilation published a cache entry')
    return results


def check_campaign(out, extension, target):
    rows = read(out/'results.json')
    commands = read(out/'commands.json')
    for key, label, state in (('cold', 'cold', 'miss'), ('warm', 'warm', 'hit'),
                              ('repaired', 'repaired', 'repaired'), ('include_changed', 'include-changed', 'miss')):
        row = rows[key]
        require(row['state'] == state and row['object_sha256'] == sha(out/f'{label}.{extension}') and
                commands[label+'-preprocess']['exit_code'] == commands[label+'-validate']['exit_code'] == 0,
                f'Unexpected cache evidence: {label}')
        require(label+'-compile' not in commands if state == 'hit'
                else commands[label+'-compile']['exit_code'] == 0, f'Unexpected compiler activity: {label}')
    require(rows['cold']['key'] == rows['warm']['key'] == rows['repaired']['key'] != rows['include_changed']['key'] and
            rows['cold']['object_sha256'] == rows['warm']['object_sha256'] == rows['repaired']['object_sha256'] !=
            rows['include_changed']['object_sha256'], 'Cache identity or output differs')
    require(commands['invalid-source-compile']['exit_code'] > 0 and
            rows['invalid_source'].startswith(target) and 'cache-fixture.hlsl:2:' in rows['invalid_source'] and
            'missingSymbol' in rows['invalid_source'] and not (out/f'invalid-source.{extension}').exists(),
            'Missing original-source, target or compiler error evidence')
