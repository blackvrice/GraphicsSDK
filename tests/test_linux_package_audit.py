import pathlib
import sys
import tempfile
import unittest
import zipfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'tools'))
import audit_linux_package as audit


class LinuxPackageAuditTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = pathlib.Path(self.temp.name).resolve()
        self.source = self.root / 'a b.hpp'
        self.source.write_bytes(b'header')

    def test_gnu_continuation_and_escaped_space(self):
        self.assertEqual(audit.dependencies('target.o: a\\ b.hpp \\\n a\\ b.hpp\n', self.root), [self.source])

    def test_missing_dependency_rejected(self):
        with self.assertRaises(ValueError):
            audit.dependencies('target.o: absent.hpp\n', self.root)

    def test_missing_target_rejected(self):
        with self.assertRaises(ValueError):
            audit.dependencies('a b.hpp', self.root)

    def test_unknown_external_input_rejected(self):
        with self.assertRaises(ValueError):
            audit.audit_dependencies([self.source], 'consumer', set(), set(), set(), {})

    def test_consumer_repository_header_rejected(self):
        with self.assertRaises(ValueError):
            audit.audit_dependencies([self.source], 'consumer', {self.source}, set(), set(), {})

    def test_consumer_vendor_header_rejected(self):
        with self.assertRaises(ValueError):
            audit.audit_dependencies([self.source], 'consumer', set(), set(), set(), {self.source: audit.sha(self.source)})

    def test_installed_header_accepted(self):
        row, = audit.audit_dependencies([self.source], 'consumer', set(), {self.source}, set(), {})
        self.assertEqual(row['category'], 'installed')
        self.assertEqual(row['sha256'], audit.sha(self.source))

    def fixture(self):
        header = self.root / 'source/Graphics.hpp'
        header.parent.mkdir()
        header.write_bytes(b'public')
        prefix = self.root / 'prefix'
        files = {'include/graphics/Graphics.hpp': b'public', 'lib/libGraphicsSDKCore.a': b'archive',
                 'share/GraphicsSDK/LICENSE': b'MIT', 'share/GraphicsSDK/THIRD_PARTY_NOTICES.txt': b'notice'}
        files.update({'lib/cmake/GraphicsSDK/' + n: b'config' for n in ('GraphicsSDKTargets.cmake', 'GraphicsSDKTargets-release.cmake', 'GraphicsSDKConfig.cmake', 'GraphicsSDKConfigVersion.cmake')})
        for name, data in files.items():
            path = prefix / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        return prefix, [header], 'notice', audit.sha(prefix / 'lib/libGraphicsSDKCore.a')

    def test_missing_notice_rejected(self):
        args = self.fixture()
        (args[0] / 'share/GraphicsSDK/THIRD_PARTY_NOTICES.txt').unlink()
        with self.assertRaises(ValueError):
            audit.payload(*args)

    def test_extra_runtime_binary_rejected(self):
        args = self.fixture()
        (args[0] / 'lib/libvulkan.so').write_bytes(b'external')
        with self.assertRaises(ValueError):
            audit.payload(*args)

    def test_header_tamper_rejected(self):
        args = self.fixture()
        (args[0] / 'include/graphics/Graphics.hpp').write_bytes(b'tampered')
        with self.assertRaises(ValueError):
            audit.payload(*args)

    def test_stale_archive_rejected(self):
        args = self.fixture()
        (args[0] / 'lib/libGraphicsSDKCore.a').write_bytes(b'stale')
        with self.assertRaises(ValueError):
            audit.payload(*args)

    def test_notice_tamper_rejected(self):
        args = self.fixture()
        (args[0] / 'share/GraphicsSDK/THIRD_PARTY_NOTICES.txt').write_bytes(b'tampered')
        with self.assertRaises(ValueError):
            audit.payload(*args)

    def test_exact_payload_zip(self):
        args = self.fixture()
        rows = audit.payload(*args)
        destination = self.root / 'package.zip'
        audit.package_zip(args[0], destination, rows)
        with zipfile.ZipFile(destination) as archive:
            self.assertEqual(len(archive.namelist()), 8)


if __name__ == '__main__':
    unittest.main()
