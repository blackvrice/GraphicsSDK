"""Replay historical contracts against their hashed source ZIP, never today's checkout.

Only test fixtures bind a runner's ROOT to this temporary, byte-exact source snapshot.
Production validators retain their current-source fingerprint checks unchanged.
"""
import hashlib
import json
import pathlib
import tempfile
import zipfile
from unittest import mock

ROOT = pathlib.Path(__file__).resolve().parents[1]
LOCK = ROOT / 'tests/recorded_sources.json'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def restore(root, entry, destination):
    archive = root / entry['archive']['path']
    if sha(archive.read_bytes()) != entry['archive']['sha256']:
        raise ValueError('Historical source archive changed')
    paths = [row['path'] for row in entry['files']]
    if not paths or len(paths) != len(set(paths)):
        raise ValueError('Historical source file set is empty or duplicated')
    with zipfile.ZipFile(archive) as z:
        if len(z.infolist()) != len(set(z.namelist())):
            raise ValueError('Duplicate archive member')
        for row in entry['files']:
            name = pathlib.PurePosixPath(row['path'])
            path = destination / name
            if name.is_absolute() or '\\' in row['path'] or '..' in name.parts or not path.resolve().is_relative_to(destination.resolve()):
                raise ValueError('Unsafe historical source path')
            info = z.getinfo(row['path'])
            if info.file_size > 2_000_000 or (info.external_attr >> 16) & 0o170000 == 0o120000:
                raise ValueError('Invalid historical source member')
            data = z.read(info)
            if sha(data) != row['sha256']:
                raise ValueError('Historical source member changed')
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
    fingerprint = sha(''.join(row['path'] + '|' + row['sha256'] + '\n' for row in sorted(entry['files'], key=lambda r:r['path'])).encode())
    if fingerprint != entry['source_fingerprint']:
        raise ValueError('Historical source fingerprint differs')


def bind(test_class, module, key):
    entry = json.loads(LOCK.read_text(encoding='utf-8'))['sources'][key]
    temp = tempfile.TemporaryDirectory(prefix='gfx-recorded-source-')
    test_class.addClassCleanup(temp.cleanup)
    destination = pathlib.Path(temp.name).resolve()
    restore(ROOT, entry, destination)
    patcher = mock.patch.object(module, 'ROOT', destination)
    patcher.start()
    test_class.addClassCleanup(patcher.stop)
    if module.fingerprint() != entry['source_fingerprint']:
        raise ValueError('Validator source selection differs from historical manifest')
    test_class.recorded_source = entry['source_fingerprint']
