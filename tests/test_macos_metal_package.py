"""Reject foreign compiler/linker inputs when replaying macOS package evidence."""

import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]/'tools'))
import macos_metal_package as package


class MetalPackage(unittest.TestCase):
    def test_dependency_audit_allows_spaces_and_rejects_source_leakage(self):
        def deps(header):
            return f'consumer.o: #deps 2, deps mtime 100 (VALID)\n    /tmp/new source/Consumer.cpp\n    {header}\n\n'
        own = {'/tmp/new source/Consumer.cpp', '/tmp/new SDK/include/graphics/Device.hpp'}
        self.assertEqual(package.dependencies(deps('/tmp/new SDK/include/graphics/Device.hpp'), ['consumer.o'], own, ('/Xcode/SDK',)),
                         dict(own=2, system=0))
        for path in ('/repo/include/graphics/Device.hpp', '/Xcode/SDK/../../external/Device.hpp'):
            with self.assertRaises(ValueError):
                package.dependencies(deps(path), ['consumer.o'], own, ('/Xcode/SDK',))

    def test_link_map_must_use_relocated_archive(self):
        lib = '/tmp/new SDK/lib/libGraphicsSDKCore.a'
        text = f'# Object files:\n[  0] linker synthesized\n[  1] consumer.o\n[  2] {lib}(Backend.cpp.o)\n[  3] {lib}(Metal.cpp.o)\n# Sections:\n'
        self.assertEqual(package.linker(text, lib, 'consumer.o', '/Xcode/SDK', '/tmp/build'), ['Backend.cpp.o', 'Metal.cpp.o'])
        with self.assertRaises(ValueError):
            package.linker(text.replace(lib, '/repo/build/libGraphicsSDKCore.a'), lib, 'consumer.o', '/Xcode/SDK', '/tmp/build')

    def test_framework_export_cannot_capture_xcode_path(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder = pathlib.Path(tmp)
            for name in package.CONFIG_FILES:
                (folder/name).write_text('', encoding='utf-8')
            (folder/'GraphicsSDKTargets-release.cmake').write_text('${_IMPORT_PREFIX}/lib/libGraphicsSDKCore.a', encoding='utf-8')
            targets = folder/'GraphicsSDKTargets.cmake'
            targets.write_text('-framework Foundation;-framework QuartzCore;-framework Metal', encoding='utf-8')
            package.exports(folder)
            targets.write_text('-framework Foundation;-framework QuartzCore;/Applications/Xcode.app/Metal.framework', encoding='utf-8')
            with self.assertRaises(ValueError):
                package.exports(folder)
