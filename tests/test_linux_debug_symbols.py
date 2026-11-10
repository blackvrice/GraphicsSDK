import hashlib
import json
import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'tools'))
import linux_debug_symbols as debug
from linux_release_candidate import LIBRARY, PROFILE


class LinuxDebugSymbolsTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = pathlib.Path(self.temp.name)
        self.header = 'include/graphics/Graphics.hpp'
        self.full = b'full archive with debug information'
        self.stripped = b'linkable archive'
        self.identity = dict(profile=PROFILE, release_cxx_flags='-O3 -DNDEBUG -g -ffile-prefix-map=/repo=.',
                             archive_members={'Vulkan.cpp.o': 'a' * 64})
        self.original = {name: self.row(name, data) for name, data in
                         ((self.header, b'header'), (LIBRARY, self.full))}
        self.stem = 'GraphicsSDK-0.0.1-linux-x86_64-vulkan-headless-release-deadbeef1234-123456789abc'

    @staticmethod
    def row(name, data):
        return dict(path=name, bytes=len(data), sha256=hashlib.sha256(data).hexdigest())

    def candidate(self, header=b'header', debug_archive=None):
        runtime, companion, output = (self.root / name for name in ('runtime', 'debug', 'output'))
        output.mkdir()
        if debug_archive is None:
            debug_archive = self.full
        runtime_files = {self.header: header, LIBRARY: self.stripped}
        debug_files = {LIBRARY: debug_archive}
        runtime_rows = [self.row(name, data) for name, data in sorted(runtime_files.items())]
        for folder, files, name in ((runtime, runtime_files, 'runtime.zip'), (companion, debug_files, 'debug.zip')):
            for member, data in files.items():
                path = folder / member
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(data)
            debug.package_zip(folder, self.root / name, [self.row(key, files[key]) for key in sorted(files)])
        runtime_file = self.root / 'runtime.zip'
        name = debug.artifact_stem(self.stem, debug.sha(runtime_file))
        runtime_file.rename(output / f'{name}.zip')
        (self.root / 'debug.zip').rename(output / f'{name}.debug.zip')
        manifest = dict(schema_version=1, source=self.identity, runtime=debug.record(output / f'{name}.zip'),
                        debug=debug.record(output / f'{name}.debug.zip'), runtime_payload=runtime_rows,
                        runtime_archive_members={'Vulkan.cpp.o': 'b' * 64}, global_symbols_sha256='c' * 64,
                        linked_sdk_dwarf=dict(runtime=False, debug_relink=True),
                        smoke=dict(cpu=True, software='lavapipe', validation=False, adapters=1, assertions=361,
                                   transfers=24, verified_bytes=28521273, direct_needed=['libc.so.6']))
        (output / 'manifest.json').write_text(json.dumps(manifest), encoding='utf-8')
        return output

    def test_audited_split_pair(self):
        debug.check(self.stem, self.original, self.identity, self.candidate())

    def test_rejects_missing_release_debug_flags(self):
        with self.assertRaisesRegex(ValueError, 'mapped source root'):
            debug.source_map(dict(release_cxx_flags='-O3 -DNDEBUG'))

    def test_rejects_unchanged_runtime_archive(self):
        with self.assertRaisesRegex(ValueError, 'differ and be smaller'):
            debug.runtime_inventory(self.original, self.original[LIBRARY])

    def test_rejects_changed_runtime_header_even_with_updated_manifest(self):
        with self.assertRaisesRegex(ValueError, 'outside the audited library'):
            debug.check(self.stem, self.original, self.identity, self.candidate(header=b'changed'))

    def test_rejects_replacement_debug_archive_even_with_updated_manifest(self):
        wrong = b'x' * len(self.full)
        with self.assertRaisesRegex(ValueError, 'Changed ZIP member'):
            debug.check(self.stem, self.original, self.identity, self.candidate(debug_archive=wrong))


if __name__ == '__main__':
    unittest.main()
