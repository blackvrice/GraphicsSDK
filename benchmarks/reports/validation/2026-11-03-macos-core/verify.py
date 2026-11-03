# Replay current validators against data; never execute archived source/native code.
import pathlib
import sys
import tempfile
HERE=pathlib.Path(__file__).resolve().parent
ROOT=HERE.parents[3]
sys.path.insert(0,str(ROOT/'tools'))
import macos_core as c
import linux_readiness_report as bundle
import linux_llgl_probe as p


def replay():
    (ROOT/'out').mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(dir=ROOT/'out',prefix='mac-replay-') as td:
        destination=pathlib.Path(td);bundle.unpack(HERE/'evidence.zip',p.read(HERE/'inventory.json'),destination)
        p.require({x.name for x in destination.iterdir()}=={'matrix'},'Unexpected outer evidence root')
        folder=destination/'matrix'
        p.require({x.name for x in folder.iterdir()}=={'arm64','x86_64'},'Architecture bundle differs')
        result={};commit=None;fingerprint=None
        for arch in ('arm64','x86_64'):
            root=folder/arch;p.require({x.name for x in root.iterdir()}=={'native','python'},'Unexpected architecture evidence root')
            value=c.check(root/'native',root/'python');r=p.read(root/'native/receipt.json')
            p.exact(value,dict(architecture=arch,CPU_CTests=11,installed_consumer_passed=True,Python_contracts=1465,**c.SCOPE))
            commit=commit or r['source_commit'];fingerprint=fingerprint or r['SDK_source_fingerprint']
            p.require(commit==r['source_commit'] and fingerprint==r['SDK_source_fingerprint'],'Mixed source/SDK revisions')
            result[arch]=dict(result=value,OS=(root/'native/commands/os.stdout').read_text(),
                compiler=(root/'native/commands/compiler.stdout').read_text(),xcode=(root/'native/commands/xcode.stdout').read_text(),
                sdk_version=(root/'native/commands/sdk-version.stdout').read_text(),sdk_path=r['sdk_path'],runner_image=r['runner_image'],
                core_compile_units=len(p.read(root/'native/core-commands.json')),consumer_compile_units=len(p.read(root/'native/consumer-commands.json')),
                installed_headers=len(r['installed_headers']),source_inputs=len(r['source_inputs']),native_artifacts=[x for x in r['frozen_inputs'] if x['path'].endswith(('.a','/macos_core_consumer'))])
        return dict(source_commit=commit,SDK_source_fingerprint=fingerprint,architectures=result,**c.SCOPE)


def verify():
    manifest=p.read(HERE/'manifest.json')
    actual={x.relative_to(HERE).as_posix() for x in HERE.rglob('*') if x.is_file() and x!=HERE/'manifest.json' and '__pycache__' not in x.parts}
    p.require(actual=={x['path'] for x in manifest['files']},'Archive inventory differs')
    for item in manifest['files']:
        path=p.safe(HERE,item['path']);p.require(path.stat().st_size==item['bytes'] and p.sha(path)==item['sha256'],'Archive bytes differ')
    value=replay();p.require(p.same(value,p.read(HERE/'summary.json')['replay']),'Summary/replay differs')
    print('PASS native macOS arm64 + x86_64: each 11 CPU CTests, installed consumer, 1465 Python contracts; no GPU/Metal acceptance')
    return value


if __name__=='__main__':verify()
