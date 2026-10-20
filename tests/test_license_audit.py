import importlib.util
import json
import pathlib
import tempfile
import unittest
import zipfile

SPEC = importlib.util.spec_from_file_location("license_audit", pathlib.Path(__file__).resolve().parents[1] / "tools/audit_licenses.py")
audit = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(audit)


class CompilerEvidenceTests(unittest.TestCase):
    def test_utf16_batched_sources_and_headers(self):
        data = '^D:\\src\\a.cpp|D:\\src\\b.cpp\r\nD:\\src\\header.h\r\nD:\\src\\header.h\r\n'
        self.assertEqual(audit.parse_tlog(data.encode("utf-16")), [r'D:\src\a.cpp', r'D:\src\b.cpp', r'D:\src\header.h'])

    def test_missing_trace_is_not_empty_success(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaises(audit.AuditError):
                audit.read_trace(pathlib.Path(directory), "core")

    def test_unknown_linked_library_is_not_covered_by_header_inventory(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            known = root / audit.ARTIFACTS["core"]
            self.assertEqual(audit.reviewed_library(root, known), "reviewed_project_artifact")
            with self.assertRaises(audit.AuditError):
                audit.reviewed_library(root, root / "third_party/new_dependency.lib")

    def test_notices_inside_amalgamated_header_and_at_end(self):
        text = '/* normal header */\nint a;\n/* SPDX-License-Identifier: Apache-2.0 */\n' + 'int b;\n' * 300 + '// Copyright author\n// Permission is hereby granted\n'
        blocks = audit.source_notices(text)
        self.assertTrue(any("Apache-2.0" in block for block in blocks))
        self.assertTrue(any("Permission is hereby granted" in block for block in blocks))
        self.assertFalse(any("normal header" in block for block in blocks))

    def test_pin_refresh_requires_new_review(self):
        policy = {"reviewed_commits": {"sdl": "old"}}
        with self.assertRaises(audit.AuditError):
            audit.verify_pins(policy, {"sdl": {"commit": "new"}})

    def test_specific_license_precedes_project_default(self):
        policy = {"components": [
            {"id": "hid", "sdk": "sdl", "patterns": ["src/hidapi/*"], "selected": "BSD-3-Clause"},
            {"id": "root", "sdk": "sdl", "patterns": ["*"], "selected": "Zlib"}
        ]}
        selected = audit.component_for(policy, "sdl", "SRC/HIDAPI/windows/hid.c")
        self.assertEqual(selected["selected"], "BSD-3-Clause")
        with self.assertRaises(audit.AuditError):
            audit.component_for(policy, "unreviewed", "anything.h")

    def test_diligent_checksum_keeps_rsa_terms_and_nvapi_binary_is_explicit(self):
        policy = json.loads((audit.ROOT / "licenses/diligent-policy.json").read_text())
        checksum = audit.component_for(policy, "diligent_core", "ThirdParty/GPUOpenShaderUtils/DXBCChecksum.cpp")
        self.assertIn("LicenseRef-RSA-MD5", checksum["selected"])
        self.assertEqual(audit.component_for(policy, "diligent_nvapi", "amd64/nvapi64.lib")["selected"], "MIT")
        dxc = audit.component_for(policy, "diligent_core", "ThirdParty/DirectXShaderCompiler/dxc/dxcapi.h")
        self.assertEqual(dxc["selected"], "NCSA")

    def test_d3dx12_origin_accepts_only_recorded_body(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            origin = root / "licenses/provenance/d3dx12-origin.json"
            source = root / ".deps/LLGL/sources/Renderer/Direct3D12/D3DX12/d3dx12.h"
            origin.parent.mkdir(parents=True); source.parent.mkdir(parents=True)
            origin.write_text(json.dumps({"sha256_lf_stripped": audit.sha256(b"// copyright\nint body;")}))
            source.write_bytes(b"\r\n// copyright\r\nint body;\r\n")
            audit.verify_d3dx12(root)
            source.write_bytes(b"// copyright\nint different;")
            with self.assertRaises(audit.AuditError):
                audit.verify_d3dx12(root)


class PackageTests(unittest.TestCase):
    def setUp(self):
        self.required = {"share/GraphicsSDK/LICENSE": b"own MIT notice", "lib/GraphicsSDKCore.lib": b"known core"}
        self.entries = list(self.required.items())

    def test_known_sdk_payload(self):
        audit.validate_package_entries(self.entries, self.required)

    def test_competitor_dll_cannot_hide_in_sdk_package(self):
        with self.assertRaises(audit.AuditError):
            audit.validate_package_entries(self.entries + [("bin/LLGL.dll", b"foreign")], self.required)

    def test_unreviewed_header_cannot_hide_under_public_include(self):
        with self.assertRaises(audit.AuditError):
            audit.validate_package_entries(self.entries + [("include/graphics/Foreign.hpp", b"foreign")], self.required)

    def test_missing_or_changed_license_and_library(self):
        for name in self.required:
            with self.subTest(name=name), self.assertRaises(audit.AuditError):
                audit.validate_package_entries([(key, value) for key, value in self.entries if key != name], self.required)
            with self.subTest(changed=name), self.assertRaises(audit.AuditError):
                audit.validate_package_entries([(key, b"changed" if key == name else value) for key, value in self.entries], self.required)

    def test_archive_traversal_and_windows_aliases(self):
        for name in ("../outside.dll", "/outside.dll", r"include\graphics\Foreign.hpp", "C:/outside.dll", "lib/../../outside.dll"):
            with self.subTest(name=name), self.assertRaises(audit.AuditError):
                audit.validate_package_entries(self.entries + [(name, b"foreign")], self.required)
        with self.assertRaises(audit.AuditError):
            audit.validate_package_entries(self.entries + [("LIB/GRAPHICSSDKCORE.LIB", b"known core")], self.required)

    def test_unsafe_directory_cannot_be_skipped_by_zip_reader(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            payload = {"LICENSE": b"own MIT", audit.ARTIFACTS["core"]: b"known library"}
            for name in ("GraphicsSDKConfig.cmake", "GraphicsSDKConfigVersion.cmake", "GraphicsSDKTargets.cmake", "GraphicsSDKTargets-release.cmake"):
                payload["out/install/lib/cmake/GraphicsSDK/" + name] = b"package export"
            for name, data in payload.items():
                path = root / name; path.parent.mkdir(parents=True, exist_ok=True); path.write_bytes(data)
            archive_path = root / "unsafe.zip"
            with zipfile.ZipFile(archive_path, "w") as archive:
                archive.writestr("../outside/", b"")
            with self.assertRaisesRegex(audit.AuditError, "Unsafe archive member"):
                audit.audit_package(root, archive_path)


class SnapshotTests(unittest.TestCase):
    def test_changed_notice_is_detected_without_overwriting(self):
        with tempfile.TemporaryDirectory() as directory:
            output = pathlib.Path(directory) / "audit"
            files = {"inventory.json": "{}\n", "THIRD_PARTY_NOTICES.txt": "copyright\n"}
            audit.write_or_check(output, files, False)
            audit.write_or_check(output, files, True)
            (output / "THIRD_PARTY_NOTICES.txt").write_text("removed")
            with self.assertRaises(audit.AuditError):
                audit.write_or_check(output, files, True)
            self.assertEqual((output / "THIRD_PARTY_NOTICES.txt").read_text(), "removed")
            with self.assertRaises(audit.AuditError):
                audit.write_or_check(output, files, False)


if __name__ == "__main__":
    unittest.main()
