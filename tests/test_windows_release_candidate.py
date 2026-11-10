"""Exercise Windows SDK ZIP verification without the native toolchain."""

import json
import pathlib
import sys
import tempfile
import unittest
import zipfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'tools'))
import windows_release_candidate as release


class WindowsReleaseCandidate(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.output = pathlib.Path(tmp.name)
        self.manifest = dict(schema_version=1, platform='windows-x64', configuration='Release', version='0.0.1',
                             source_commit='a' * 40, SDK_source_fingerprint='b' * 64, packages={})
        for profile in release.PROFILES:
            with tempfile.TemporaryDirectory() as prefix:
                prefix = pathlib.Path(prefix)
                member = prefix / release.LIBRARY
                member.parent.mkdir()
                member.write_text(profile, encoding='utf-8')
                entry = dict(bytes=member.stat().st_size, sha256=release.sha(member))
                payload = {release.LIBRARY: entry}
                archive = self.output / (profile + '.zip')
                release.package_zip(prefix, archive, [dict(path=release.LIBRARY, **entry)])
                self.set_package(profile, archive, payload)

    def set_package(self, profile, archive, payload):
        name = release.candidate_name(self.manifest['version'], profile, self.manifest['source_commit'],
                                      release.sha(archive))
        archive = archive.rename(self.output / name)
        self.manifest['packages'][profile] = dict(package=release.record(archive), payload=payload)

    def write_manifest(self):
        path = self.output / release.MANIFEST
        path.write_text(json.dumps(self.manifest), encoding='utf-8')
        return release.sha(path)

    def test_pinned_pair_and_wrong_manifest_hash(self):
        digest = self.write_manifest()
        self.assertEqual(release.check(self.output, digest), self.manifest)
        with self.assertRaisesRegex(ValueError, 'Pinned candidate manifest'):
            release.check(self.output, '0' * 64)

    def test_profile_swap_is_rejected(self):
        digest = self.write_manifest()
        d3d12, vulkan = (self.output / self.manifest['packages'][p]['package']['name'] for p in release.PROFILES)
        d3d12.write_bytes(vulkan.read_bytes())
        with self.assertRaisesRegex(ValueError, 'Missing or changed file'):
            release.check(self.output, digest)

    def test_extra_member_is_rejected_even_with_matching_outer_hash(self):
        row = self.manifest['packages']['vulkan']
        archive = self.output / row['package']['name']
        with zipfile.ZipFile(archive, 'a') as package:
            package.writestr('lib/vulkan-1.dll', b'unexpected runtime')
        self.set_package('vulkan', archive, row['payload'])
        with self.assertRaisesRegex(ValueError, 'Missing, extra or duplicate ZIP entry'):
            release.check(self.output, self.write_manifest())


if __name__ == '__main__':
    unittest.main()
