"""Regressions for the common checks on top of platform-specific ZIP verification."""

import hashlib
import pathlib
import sys
import tempfile
import unittest
import zipfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'tools'))
import release_candidate_audit as audit


class ReleaseCandidateAudit(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.root = pathlib.Path(tmp.name)

    def test_manifest_rejects_wrong_pin_and_duplicate_json_keys(self):
        path = self.root / 'manifest.json'
        valid = b'{"schema_version": 1}'
        path.write_bytes(valid)
        self.assertEqual(audit.pinned_manifest(self.root, hashlib.sha256(valid).hexdigest()), {'schema_version': 1})
        with self.assertRaisesRegex(ValueError, 'manifest SHA-256 differs'):
            audit.pinned_manifest(self.root, '0' * 64)
        duplicate = b'{"schema_version": 1, "schema_version": 1}'
        path.write_bytes(duplicate)
        with self.assertRaisesRegex(ValueError, 'Duplicate JSON field'):
            audit.pinned_manifest(self.root, hashlib.sha256(duplicate).hexdigest())

    def windows_sdk(self, name, version):
        members = {key: b'fixture' for key in audit.metal.PAYLOAD_FILES - {audit.metal.LIBRARY}}
        members[audit.windows.LIBRARY] = b'windows archive'
        members[audit.linux.NOTICE] = b'pinned Apache-2.0 notices'
        members[audit.macos.VERSION_FILE] = b'set(PACKAGE_VERSION "0.0.1")\r\n'
        members[audit.macos.VERSION_HEADER] = f'inline constexpr std::string_view version = "{version}";\r\n'.encode()
        path = self.root / name
        with zipfile.ZipFile(path, 'w') as archive:
            for key, data in members.items():
                archive.writestr(key, data)
        return path, {key: dict(bytes=len(data), sha256=hashlib.sha256(data).hexdigest())
                      for key, data in members.items()}

    def test_vulkan_notice_is_required_for_the_profile(self):
        path, payload = self.windows_sdk('sdk.zip', '0.0.1')
        result = audit.inspect_sdk(path, payload, audit.windows.LIBRARY, '0.0.1', third_party=True)
        self.assertEqual(set(result['notices']), {audit.metal.NOTICE, audit.linux.NOTICE})
        del payload[audit.linux.NOTICE]
        with self.assertRaisesRegex(ValueError, 'required notice differs'):
            audit.inspect_sdk(path, payload, audit.windows.LIBRARY, '0.0.1', third_party=True)

    def test_version_must_match_public_header_and_cmake_config(self):
        path, payload = self.windows_sdk('sdk.zip', '0.0.2')
        with self.assertRaisesRegex(ValueError, 'version differs from the installed'):
            audit.inspect_sdk(path, payload, audit.windows.LIBRARY, '0.0.1', third_party=True)

    def test_xcb_manifest_cannot_fill_headless_slot_even_when_pinned(self):
        source = dict(version='0.0.1', source_commit='a' * 40, sdk_source_fingerprint='b' * 64,
                      profile=audit.linux.XCB_PROFILE, build_flags=dict(audit.linux.FLAGS, GFX_VULKAN_XCB='ON'))
        with self.assertRaisesRegex(ValueError, 'Linux split profile'):
            audit.linux_profile(self.root, {'source': source}, xcb=False)


if __name__ == '__main__':
    unittest.main()
