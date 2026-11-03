# Replay data with current validators; never execute archived source/native code.
import pathlib
import sys
import tempfile
HERE=pathlib.Path(__file__).resolve().parent
ROOT=HERE.parents[3]
sys.path.insert(0,str(ROOT/'tools'))
import macos_metal_backend as c
import linux_readiness_report as bundle
import linux_llgl_probe as p


def replay():
    (ROOT/'out').mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(dir=ROOT/'out',prefix='metal-graphics-replay-') as td:
        destination=pathlib.Path(td);bundle.unpack(HERE/'evidence.zip',p.read(HERE/'inventory.json'),destination)
        p.require({x.name for x in destination.iterdir()}=={'matrix'},'Unexpected evidence root');folder=destination/'matrix'
        p.require({x.name for x in folder.iterdir()}=={'arm64','x86_64'},'Architecture evidence differs')
        result={};commit=None;fingerprint=None
        for arch in ('arm64','x86_64'):
            root=folder/arch;p.require({x.name for x in root.iterdir()}=={'native','python'},'Unexpected architecture evidence')
            value=c.check(root/'native',root/'python');r=p.read(root/'native/receipt.json');py=p.read(root/'python/receipt.json')
            p.exact(value,dict(architecture=arch,**c.SCOPE_V4));p.exact(py,dict(tests=1501))
            commit=commit or r['source_commit'];fingerprint=fingerprint or r['SDK_source_fingerprint']
            p.require(commit==r['source_commit'] and fingerprint==r['SDK_source_fingerprint'],'Mixed source/SDK revisions')
            result[arch]=dict(result=value,Python_contracts=py['tests'],native=p.read(root/'native/native.json'),consumer=p.read(root/'native/consumer.json'),buffer_native=p.read(root/'native/buffer-native.json'),buffer_consumer=p.read(root/'native/buffer-consumer.json'),texture_native=p.read(root/'native/texture-native.json'),texture_consumer=p.read(root/'native/texture-consumer.json'),graphics_native=p.read(root/'native/graphics-native.json'),graphics_consumer=p.read(root/'native/graphics-consumer.json'),
                inventory=p.read(root/'native/availability/receipt.json')['inventory'],source_inputs=len(r['source_inputs']),
                compile_units=len(p.read(root/'native/sdk-commands.json')),installed_headers=len(r['installed_headers']),
                OS=(root/'native/availability/core/commands/os.stdout').read_text(),compiler=(root/'native/availability/core/commands/compiler.stdout').read_text(),
                xcode=(root/'native/availability/core/commands/xcode.stdout').read_text(),sdk_version=(root/'native/availability/core/commands/sdk-version.stdout').read_text())
        return dict(source_commit=commit,SDK_source_fingerprint=fingerprint,architectures=result,**c.SCOPE_V4)


def verify():
    manifest=p.read(HERE/'manifest.json');actual={x.relative_to(HERE).as_posix() for x in HERE.rglob('*') if x.is_file() and x!=HERE/'manifest.json' and '__pycache__' not in x.parts}
    p.require(actual=={x['path'] for x in manifest['files']},'Archive inventory differs')
    for item in manifest['files']:
        path=p.safe(HERE,item['path']);p.require(path.stat().st_size==item['bytes'] and p.sha(path)==item['sha256'],'Archive byte drift')
    value=replay();p.require(p.same(value,p.read(HERE/'summary.json')['replay']),'Summary/replay differs')
    print('PASS native SDK Metal graphics + installed consumer on macOS arm64 + x86_64; each 1501 Python contracts; 36 full draw readbacks per architecture; no physical/validation/performance acceptance')
    return value


if __name__=='__main__':verify()
