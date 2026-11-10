import hashlib
import json
import pathlib
import stat
import sys
import tempfile
import unittest
import zipfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'tools'))
import linux_release_candidate as release


class LinuxReleaseCandidateTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = pathlib.Path(self.temp.name)
        self.source = self.root / 'audit'
        self.source.mkdir()
        version = 'set(PACKAGE_VERSION "0.0.1")\n'
        self.files = {
            f'include/graphics/{p.name}': b'header'
            for p in (release.ROOT / 'include/graphics').glob('*.hpp')
        }
        self.files.update({
            release.LIBRARY: b'archive',
            release.NOTICE: b'notice',
            'share/GraphicsSDK/LICENSE': b'MIT',
            release.VERSION_FILE: version.encode(),
            release.CONFIG + 'GraphicsSDKConfig.cmake': b'config',
            release.CONFIG + 'GraphicsSDKTargets.cmake': b'targets',
            release.CONFIG + 'GraphicsSDKTargets-release.cmake': b'release targets',
        })
        self.zip_path = self.source / release.PACKAGE
        self.write_zip(self.files)
        payload = [
            dict(path=name, bytes=len(data), sha256=hashlib.sha256(data).hexdigest())
            for name, data in sorted(self.files.items())
        ]
        rows = {row['path']: row for row in payload}
        self.receipt = dict(
            schema_version=1, task='P3-LNX-12', hardware_GPU_accepted=False, required_validation_this_run=False,
            source_commit='a' * 40, native_SDK_source_fingerprint='b' * 64,
            platform=dict(machine='x86_64', os_release=dict(ID='ubuntu', VERSION_ID='24.04')),
            archive_members={'Vulkan.cpp.o': 'c' * 64}, payload=payload,
            SDK_library={k: rows[release.LIBRARY][k] for k in ('bytes', 'sha256')},
            notice=rows[release.NOTICE],
        )
        self.write_build_profile()

    def write_zip(self, files, symlink=None):
        with zipfile.ZipFile(self.zip_path, 'w') as archive:
            for name, data in sorted(files.items()):
                info = zipfile.ZipInfo(name)
                info.external_attr = ((stat.S_IFLNK if name == symlink else stat.S_IFREG) | 0o644) << 16
                archive.writestr(info, data)

    def write_build_profile(self, **changed):
        flags = dict(release.FLAGS, CMAKE_CXX_COMPILER='g++-14', CMAKE_CXX_FLAGS_RELEASE='-O3 -DNDEBUG')
        flags.update(changed)
        cache = self.source / release.DIAGNOSTICS[0]
        cache.write_text(''.join(f'{k}:STRING={v}\n' for k, v in flags.items()), encoding='utf-8')
        compiler = self.source / release.DIAGNOSTICS[1]
        compiler.write_text('g++-14 (Ubuntu test fixture) 14.2.0\n', encoding='utf-8')
        self.receipt['diagnostics'] = [self.audit_record(path) for path in (cache, compiler)]

    def audit_record(self, path):
        return dict(path=f'out/linux-package-evidence/{path.name}', bytes=path.stat().st_size,
                    sha256=release.sha(path))

    def inspect(self, digest=None, xcb=False):
        self.receipt['package'] = self.audit_record(self.zip_path)
        receipt = self.source / 'receipt.json'
        receipt.write_text(json.dumps(self.receipt), encoding='utf-8')
        return release.inspect_audit(self.source, digest or release.sha(receipt), xcb)

    def test_version_name_and_manifest_binding(self):
        stem, _, _, identity, _ = self.inspect()
        self.assertEqual(stem, f'GraphicsSDK-0.0.1-{release.PROFILE}-{"a"*12}-{release.sha(self.zip_path)[:12]}')
        out = self.root / 'output'
        out.mkdir()
        zip_path = out / f'{stem}.zip'
        zip_path.write_bytes(self.zip_path.read_bytes())
        symbols = out / f'{stem}.symbols.txt'
        symbols.write_bytes(b'link symbols')
        manifest = dict(schema_version=1, **identity, package=release.record(zip_path), symbols=release.record(symbols),
                        smoke=dict(cpu=True, software='lavapipe', validation=False, adapters=1, assertions=361,
                                   transfers=24, verified_bytes=28521273, direct_needed=['libc.so.6']))
        path = out / 'manifest.json'
        path.write_text(json.dumps(manifest), encoding='utf-8')
        release.check(stem, identity, out)
        zip_path.write_bytes(b'replacement')
        manifest['package'] = release.record(zip_path)
        path.write_text(json.dumps(manifest), encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'pinned audit'):
            release.check(stem, identity, out)

    def test_wrong_receipt_pin_rejected(self):
        with self.assertRaisesRegex(ValueError, 'receipt hash'):
            self.inspect(digest='0' * 64)

    def test_xcb_profile_rejected(self):
        self.write_build_profile(GFX_VULKAN_XCB='ON')
        with self.assertRaisesRegex(ValueError, 'build profile'):
            self.inspect()

    def test_xcb_requires_audited_private_inputs(self):
        self.write_build_profile(GFX_VULKAN_XCB='ON')
        with self.assertRaisesRegex(ValueError, 'XCB compiler inputs'):
            self.inspect(xcb=True)
        self.receipt['actual_inputs'] = [
            dict(category='system', path='/usr/include/xcb/xcb.h'),
            dict(category='vendor', path='/repo/.deps/Vulkan-Headers/include/vulkan/vulkan_xcb.h'),
        ]
        stem, _, _, identity, _ = self.inspect(xcb=True)
        self.assertIn(release.XCB_PROFILE, stem)
        self.assertEqual(identity['build_flags']['GFX_VULKAN_XCB'], 'ON')

    def test_xcb_requires_full_display_smoke(self):
        smoke = dict(cpu=True, software='lavapipe', validation=False, adapters=1, assertions=361,
                     transfers=24, verified_bytes=28521273, direct_needed=['libc.so.6'])
        with self.assertRaisesRegex(ValueError, 'Display evidence'):
            release.check_smoke(smoke, xcb=True)
        smoke['display'] = dict(server='Xvfb', presents=3, resizes=1, verified_bytes=3836,
                                human_display_accepted=False, direct_needed=['libxcb.so.1'])
        release.check_smoke(smoke, xcb=True)
        with self.assertRaisesRegex(ValueError, 'Display evidence'):
            release.check_smoke(smoke)
        smoke['display']['resizes'] = 0
        with self.assertRaisesRegex(ValueError, 'XCB present'):
            release.check_smoke(smoke, xcb=True)

    def test_extra_archive_entry_rejected(self):
        self.write_zip(dict(self.files, **{'../outside': b'extra'}))
        with self.assertRaisesRegex(ValueError, 'extra or duplicate'):
            self.inspect()

    def test_changed_archive_member_rejected(self):
        self.write_zip(dict(self.files, **{release.LIBRARY: b'mutate!'}))
        with self.assertRaisesRegex(ValueError, 'Changed ZIP member'):
            self.inspect()

    def test_symlink_member_rejected(self):
        self.write_zip(self.files, symlink=release.LIBRARY)
        with self.assertRaisesRegex(ValueError, 'member type'):
            self.inspect()


if __name__ == '__main__':
    unittest.main()
