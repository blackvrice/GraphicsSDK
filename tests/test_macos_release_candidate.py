"""Check architecture and identity rejection for the macOS release candidate."""

import pathlib
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'tools'))
import macos_release_candidate as release


def member(name, data):
    long_name = name.encode('ascii') + b'\0'
    payload = long_name + data
    header = f'#1/{len(long_name):<13}{0:<12}{0:<6}{0:<6}{644:<8}{len(payload):<10}`\n'
    return header.encode('ascii') + payload + (b'\n' if len(payload) % 2 else b'')


def mach_object(arch, file_type=release.MH_OBJECT):
    return struct.pack('<8I', release.MH_MAGIC_64, release.CPU_TYPES[arch], 0, file_type, 0, 0, 0, 0)


def static_archive(arch, replacement=None):
    objects = dict.fromkeys(sorted(release.OBJECTS), mach_object(arch))
    if replacement is not None:
        objects['Metal.cpp.o'] = replacement
    return (release.ARCHIVE_MAGIC + member('__.SYMDEF', b'fixture')
            + b''.join(member(name, data) for name, data in objects.items()))


class MacOSReleaseCandidate(unittest.TestCase):
    def test_both_native_architectures(self):
        for arch in release.CPU_TYPES:
            with self.subTest(arch=arch):
                objects = release.archive_objects(static_archive(arch), arch)
                self.assertEqual(objects.keys(), release.OBJECTS)
                self.assertEqual(objects['Metal.cpp.o']['bytes'], release.MACH_HEADER_SIZE)

    def test_rejects_wrong_architecture_in_a_single_object(self):
        data = static_archive('arm64', mach_object('x86_64'))
        with self.assertRaisesRegex(ValueError, 'thin arm64.*Metal.cpp.o'):
            release.archive_objects(data, 'arm64')

    def test_rejects_truncated_duplicate_or_non_object_archive_members(self):
        data = static_archive('arm64')
        duplicate = member('Metal.cpp.o', mach_object('arm64'))
        for invalid in (data[:-2], data + duplicate, static_archive('arm64', mach_object('arm64', file_type=2))):
            with self.subTest(length=len(invalid)), self.assertRaises(ValueError):
                release.archive_objects(invalid, 'arm64')

    def test_rejects_a_receipt_that_does_not_match_its_pin(self):
        with tempfile.TemporaryDirectory() as temp:
            audit = pathlib.Path(temp)
            (audit / 'receipt.json').write_bytes(b'{}')
            with self.assertRaisesRegex(ValueError, 'receipt SHA-256 differs'):
                release.pin_receipt(audit, '0' * 64, 'arm64')

    def test_rejects_mixed_runs_before_writing_output(self):
        with tempfile.TemporaryDirectory() as temp:
            output = pathlib.Path(temp) / 'candidate'
            inputs = (None, None, pathlib.Path('unused.zip'))
            reports = [({'github_run_id': run}, {}) for run in ('1', '2')]
            with patch.object(release, 'inspect_package', side_effect=reports):
                with self.assertRaisesRegex(ValueError, 'must share one version, source, and CI run'):
                    release.candidate(inputs, inputs, output)
            self.assertFalse(output.exists())

    def test_cmake_version_must_be_unique(self):
        version = 'set(PACKAGE_VERSION "0.0.1")\n'
        self.assertEqual(release.package_version(version), '0.0.1')
        with self.assertRaisesRegex(ValueError, 'Expected one CMake package version'):
            release.package_version(version * 2)


if __name__ == '__main__':
    unittest.main()
