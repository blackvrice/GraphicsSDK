import json
import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'tools'))
from run_benchmarks import forge_fingerprint, forge_runtime
from forge_license_profile import validate_inputs
from audit_licenses import component_for, ROOT


class ForgeContracts(unittest.TestCase):
    def test_legacy_nvapi_is_rejected_but_forge_fallback_helper_is_allowed(self):
        validate_inputs([r'D:\deps\TheForge\Common_3\Graphics\ThirdParty\OpenSource\nvapi\NvApiHelper.h'])
        for path in ('nvapi.h','nvapi_lite_common.h','amd64/nvapi64.lib'):
            with self.assertRaises(ValueError): validate_inputs(['/source/nvapi/'+path])

    def test_external_configuration_is_part_of_the_compiled_identity(self):
        with tempfile.TemporaryDirectory() as folder:
            root=pathlib.Path(folder); directory=root/'benchmarks/competitors'; directory.mkdir(parents=True)
            (directory/'forge-build.lock.json').write_text(json.dumps({'dependencies':{'the_forge':{'commit':'fixed'}}}))
            (root/'benchmarks/CMakeLists.txt').write_text('configuration')
            config=directory/'ForgeConfig.h'; config.write_text('no vendor extension')
            before=forge_fingerprint(root); config.write_text('changed vendor extension')
            self.assertNotEqual(before,forge_fingerprint(root))

    def test_runtime_and_gpu_configuration_copies_must_match_pinned_bytes(self):
        with tempfile.TemporaryDirectory() as folder:
            root=pathlib.Path(folder); exe=root/'build/gfx_bench.exe'
            sources={'amd_ags_x64.dll':'Common_3/Graphics/ThirdParty/OpenSource/ags/ags_lib/lib/amd_ags_x64.dll',
                     'dxcompiler.dll':'Common_3/Graphics/ThirdParty/OpenSource/DirectXShaderCompiler/bin/x64/dxcompiler.dll',
                     'forge-config/gpu.cfg':'Examples_3/Unit_Tests/src/01_Transformations/GPUCfg/gpu.cfg',
                     'forge-config/gpu.data':'Common_3/OS/Windows/pc_gpu.data'}
            for name,path in sources.items():
                for file in (exe.parent/name,root/'.deps/TheForge'/path):
                    file.parent.mkdir(parents=True,exist_ok=True); file.write_bytes(name.encode())
            self.assertEqual(len(forge_runtime(root,exe)),4)
            for name in sources:
                path=exe.parent/name; original=path.read_bytes(); path.write_bytes(b'changed')
                with self.assertRaises(RuntimeError): forge_runtime(root,exe)
                path.write_bytes(original)

    def test_vendor_terms_are_distinguished_from_forge_root_license(self):
        policy=json.loads((ROOT/'licenses/forge-policy.json').read_text(encoding='utf-8'))
        prefix='Common_3/Graphics/ThirdParty/OpenSource/'
        self.assertEqual(component_for(policy,'the_forge',prefix+'DirectXShaderCompiler/inc/dxcapi.h')['selected'],'NCSA AND MIT')
        self.assertEqual(component_for(policy,'the_forge',prefix+'ags/ags_lib/lib/amd_ags_x64.dll')['selected'],'MIT')
        self.assertIn('LicenseRef-',component_for(policy,'the_forge',prefix+'winpixeventruntime/Include/WinPixEventRuntime/pix3.h')['selected'])


if __name__=='__main__': unittest.main()
