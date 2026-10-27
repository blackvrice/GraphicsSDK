import copy
import json
import pathlib
import sys
import tempfile
import unittest
from unittest import mock
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'tools'))
import linux_comparison as v

def device(kind=4):
    uuid='a'*32;value=14695981039346656037
    for b in bytes.fromhex(uuid):value=((value^b)*1099511628211)&((1<<64)-1)
    return dict(name='GPU',device_uuid=uuid,driver_uuid='b'*32,adapter_id=str(value),vendor_id=4318,device_id=1,api_version_raw=4198400,driver_version_raw=1,device_type=kind,device_luid=None)

class LinuxComparison(unittest.TestCase):
    def setUp(self):
        self.d=device();self.ctx=dict(inventory=[self.d],device_uuid=self.d['device_uuid'],pci=[],probe_fingerprint='1'*64,SDK_source_fingerprint='2'*64,SDL_commit='3'*40,
            environment=dict(platform='Linux',kernel='kernel',machine='x86_64',cpu='CPU',ram_bytes=1000,compiler='GCC 14',pointer_bits=64),loader_path='/lib/libvulkan.so.1',validation_library='/runtime/libVkLayer_khronos_validation.so')
    def result(self,sdk='sdl',validation=False):
        return dict(schema_version=1,task='P3-BEN-07',contract='linux_native_uuid_public_name_probe_v1',sdk=sdk,mode='software-diagnostic',probe=True,validation=validation,logical_device_created=True,
            source_fingerprint=self.ctx['probe_fingerprint'],SDK_source_fingerprint=self.ctx['SDK_source_fingerprint'],SDL_commit=self.ctx['SDL_commit'],identity_attribution='unique_exact_name_to_native_vulkan_not_SDL_native_handle',SDL_native_handle_verified=False,
            selected=copy.deepcopy(self.d),inventory=copy.deepcopy(self.ctx['inventory']),environment=copy.deepcopy(self.ctx['environment']),loader_path=self.ctx['loader_path'],loaded_validation_layer=self.ctx['validation_library'] if validation else '',
            runtime_modules=[self.ctx['loader_path']]+([self.ctx['validation_library']] if validation else []),validation_error_count=0,sample_count=0,samples_ms=[],gpu_ms=None,readback_verified=False,benchmark_accepted=False)
    def validate(self,r):return v.validate_result(r,dict(sdk=r['sdk'],mode=r['mode'],validation=r['validation']),self.ctx)
    def test_linux_uuid_without_windows_luid(self):self.assertEqual(v.identity(self.d),self.d)
    def test_large_id_requires_exact_string(self):
        for value in (int(self.d['adapter_id']),'1',True):
            d=copy.deepcopy(self.d);d['adapter_id']=value
            with self.subTest(value=value),self.assertRaises(ValueError):v.identity(d)
    def test_invalid_native_metadata_rejected(self):
        for key,value in [('device_uuid','0'*32),('driver_uuid','b'*31),('device_type',True),('driver_version_raw',0),('api_version_raw',4194304),('device_luid','0:1')]:
            d=copy.deepcopy(self.d);d[key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):v.identity(d)
    def test_exact_software_selection(self):self.assertEqual(v.select([self.d],self.d['device_uuid'],'software-diagnostic',[]),self.d)
    def test_missing_and_duplicate_uuid_rejected(self):
        for rows,uuid in [([],self.d['device_uuid']),([self.d],'c'*32),([self.d,self.d],self.d['device_uuid'])]:
            with self.subTest(rows=rows),self.assertRaises(ValueError):v.select(rows,uuid,'software-diagnostic',[])
    def test_duplicate_public_names_rejected(self):
        other=device();other['device_uuid']='c'*32;value=14695981039346656037
        for b in bytes.fromhex(other['device_uuid']):value=((value^b)*1099511628211)&((1<<64)-1)
        other['adapter_id']=str(value)
        with self.assertRaises(ValueError):v.select([self.d,other],self.d['device_uuid'],'software-diagnostic',[])
    def test_software_never_physical(self):
        with self.assertRaises(ValueError):v.select([self.d],self.d['device_uuid'],'physical',[])
    def test_physical_model_requires_pci_driver(self):
        d=device(2);pci=[dict(vendor_id=4318,device_id=1,driver='nvidia')]
        self.assertEqual(v.select([d],d['device_uuid'],'physical',pci),d)
        for bad in ([],[dict(vendor_id=4318,device_id=1,driver='virtio-pci')]):
            with self.assertRaises(ValueError):v.select([d],d['device_uuid'],'physical',bad)
    def test_virtual_and_other_devices_rejected(self):
        for kind in (0,3):
            d=device(kind)
            with self.subTest(kind=kind),self.assertRaises(ValueError):v.select([d],d['device_uuid'],'physical',[])
    def test_both_sdks_and_validation_modes(self):
        for sdk in v.SDKS:
            for val in (False,True):self.assertEqual(self.validate(self.result(sdk,val))['validation'],val)
    def test_timing_and_hardware_claims_rejected(self):
        for key,value in [('SDL_native_handle_verified',True),('gpu_ms',1),('sample_count',1),('samples_ms',[1]),('readback_verified',True),('benchmark_accepted',True),('logical_device_created',False),('validation_error_count',1)]:
            r=self.result();r[key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):self.validate(r)
    def test_sources_driver_and_environment_must_match(self):
        for key,value in [('SDK_source_fingerprint','f'*64),('source_fingerprint','f'*64),('SDL_commit','f'*40),('loader_path','/other/loader')]:
            r=self.result();r[key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):self.validate(r)
        r=self.result();r['environment']['kernel']='different'
        with self.assertRaises(ValueError):self.validate(r)
        r=self.result();r['selected']['driver_uuid']='f'*32
        with self.assertRaises(ValueError):self.validate(r)
    def test_validation_contamination_and_wrong_layer(self):
        r=self.result();r['runtime_modules'].append(self.ctx['validation_library'])
        with self.assertRaises(ValueError):self.validate(r)
        r=self.result(validation=True);r['loaded_validation_layer']='/other/layer.so'
        with self.assertRaises(ValueError):self.validate(r)
        r=self.result(validation=True);r['runtime_modules'].pop()
        with self.assertRaises(ValueError):self.validate(r)
    def test_schedule_has_two_fresh_repeats_and_six_controls(self):
        for mode in ('physical','software-diagnostic'):
            jobs=v.jobs(mode);self.assertEqual(len(jobs),14);self.assertEqual(len({j['name'] for j in jobs}),14)
            self.assertTrue(all(j['kind']=='probe' for j in jobs[:8]));self.assertEqual({j['kind'] for j in jobs[8:]},{'missing-layer','wrong-uuid','wrong-kind'})
            for sdk in v.SDKS:
                for val in (False,True):self.assertEqual({j['repetition'] for j in jobs[:8] if j['sdk']==sdk and j['validation']==val},{0,1})
    def test_controls_need_specific_failure_and_no_result(self):
        messages=dict(zip(('wrong-uuid','missing-layer','wrong-kind'),(b'FAIL: Exact UUID absent or ambiguous',b'FAIL: Required comparison validation layer absent',b'FAIL: Requested probe kind differs from native device type')))
        for kind,message in messages.items():
            v.control(dict(kind=kind),1,b'',message)
            for code,stdout,stderr in ((0,b'',message),(1,b'{}',message),(1,b'',b'crash')):
                with self.subTest(kind=kind),self.assertRaises(ValueError):v.control(dict(kind=kind),code,stdout,stderr)
    def test_validation_environment_is_child_only_and_plain_is_clean(self):
        out=pathlib.Path('/out');plain=v.overrides(out,'/driver.json','/runtime/layer.so',False);val=v.overrides(out,'/driver.json','/runtime/layer.so',True)
        self.assertNotIn('VK_INSTANCE_LAYERS',plain);self.assertEqual(val['VK_INSTANCE_LAYERS'],'VK_LAYER_KHRONOS_validation');self.assertEqual(plain['SDL_VIDEODRIVER'],'offscreen')
        self.assertEqual(val['VK_DRIVER_FILES'],plain['VK_DRIVER_FILES'])
    def test_software_receipt_cannot_promote_completion(self):
        with tempfile.TemporaryDirectory() as temp:
            p=pathlib.Path(temp);r=dict(status='completed',task='P3-BEN-07',mode='software-diagnostic',inputs_unchanged=True,physical_probe_qualified=True,P3_BEN_07_completed=True,workload_accepted=False,performance_accepted=False,SDL_native_handle_verified=False,measurement_samples=0)
            v.save(p/'receipt.json',r)
            with self.assertRaisesRegex(ValueError,'promoted'):v.check(p)

    def test_packages_must_resolve_to_fresh_roots(self):
        with tempfile.TemporaryDirectory() as td:
            root=pathlib.Path(td);install=root/'install';sdl=root/'sdl'
            c=dict(CMAKE_BUILD_TYPE='Release',GraphicsSDK_DIR=str(install/'lib/cmake/GraphicsSDK'),SDL3_DIR=str(sdl))
            v.packages(c,install,sdl)
            for key,value in [('CMAKE_BUILD_TYPE','Debug'),('GraphicsSDK_DIR',str(root/'old-sdk')),('SDL3_DIR',str(root/'old-sdl'))]:
                bad=dict(c);bad[key]=value
                with self.subTest(key=key),self.assertRaises(ValueError):v.packages(bad,install,sdl)

    def test_real_cmake_cache_syntax(self):
        with tempfile.TemporaryDirectory() as td:
            path=pathlib.Path(td)/'CMakeCache.txt';path.write_text('// comment\n# comment\nSDL_STATIC:BOOL=ON\nSDL_SHARED:BOOL=OFF\n')
            self.assertEqual(v.cache(path),dict(SDL_STATIC='ON',SDL_SHARED='OFF'))

if __name__=='__main__':unittest.main()
