"""Replay the archived Metal comparison data using the current checker."""

import pathlib
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[3]
sys.path.insert(0, str(ROOT/'tools'))

from linux_comparison import read, require, sha
from linux_llgl_probe import same
from linux_readiness_report import unpack
from macos_core import exact
from macos_metal_comparison import check


SDL_NOTICES = {
    'src/hidapi/LICENSE.txt': '7d3b087c34f35d4d538e3bcddd1ff8f66e92f9ef336881999482800ddf840913',
    'src/hidapi/LICENSE-bsd.txt': '30eb1bef29b46f8ba7ab8b416035dbd93cb034a45481dd97815b944284582cd2',
    'src/hidapi/LICENSE-gpl3.txt': '8ceb4b9ee5adedde47b31e975c1d90c73ad27b6b165a1dcd80c7c545eb65b903',
    'src/hidapi/LICENSE-orig.txt': 'fb5436aa63d1b71a8dfbf74ecaf1a5b4e1ec4df7f80074d11fec99284f69ca5f',
    'src/video/yuv2rgb/LICENSE': 'f7d74004cc75778cae46c5f0139f244d67c07ef64cf51937e8272352197dcb18',
}


def inspect(matrix, source, run_id):
    require({p.name for p in matrix.iterdir()} == {'arm64', 'x86_64', 'runner.py', 'SDL-notices'}, 'Comparison matrix differs')
    notices = matrix/'SDL-notices'
    require({p.relative_to(notices).as_posix():sha(p) for p in notices.rglob('*') if p.is_file()} == SDL_NOTICES,
            'SDL bundled notices differ from the pinned tree')
    values = {}
    for arch in ('arm64', 'x86_64'):
        folder = matrix/arch
        receipt = read(folder/'receipt.json')
        exact(receipt, dict(architecture=arch, source_commit=source, github_run_id=run_id))
        sources = {p['path']:p['sha256'] for p in receipt['frozen_inputs']}
        archived = {'tools/macos_metal_comparison.py': matrix/'runner.py'}
        archived.update({f'benchmarks/macos_metal_comparison/{name}':folder/name
                         for name in ('Draw.metal', 'Compare.mm', 'CMakeLists.txt')})
        for path, artifact in archived.items():
            require([digest for name,digest in sources.items() if name.endswith('/'+path)] == [sha(artifact)],
                    f'Comparison source snapshot differs: {path}')
        values[arch] = check(folder)
    require(len({r['SDK_source_fingerprint'] for r in values.values()}) == 1, 'Mixed SDK revisions')
    return values


def verify():
    summary = read(HERE/'summary.json')
    exact(summary, dict(schema_version=1, scope='macos-metal-draw-diagnostic-v1', external_trust_signature=False))
    archive = HERE/'evidence.zip'
    require(sha(archive) == summary['archive_sha256'], 'Comparison ZIP bytes differ')
    (ROOT/'out').mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(dir=ROOT/'out', prefix='metal-cmp-') as tmp:
        folder = pathlib.Path(tmp)
        unpack(archive, summary['files'], folder)
        require({p.name for p in folder.iterdir()} == {'matrix'}, 'Unexpected archive root')
        result = inspect(folder/'matrix', summary['source_commit'], summary['github_run_id'])
    require(same(result, summary['architectures']), 'Summary differs from raw evidence')
    print('PASS GraphicsSDK, SDL3 and LLGL Metal draw images, CPU intervals and upstream notices on arm64 and x86_64')


if __name__ == '__main__':
    verify()
