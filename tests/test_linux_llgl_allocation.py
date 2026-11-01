"""Resource contracts and adversarial receipts; these CPU tests are not native GPU evidence."""
import copy
import hashlib
import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'tools'))
import linux_llgl_allocation as d
import port_linux_llgl_allocation as port


class ResourcePort(unittest.TestCase):
    def sources(self):
        return [(d.ROOT/n).read_text(encoding='utf-8') for n in (
            'benchmarks/llgl_vulkan_allocation/main.cpp','benchmarks/llgl_vulkan_pso/main.cpp','benchmarks/llgl_vulkan_pso/Support.hpp',
            'benchmarks/llgl_vulkan_upload/Context.hpp','benchmarks/llgl_vulkan_draw/Layer.cpp',
            'benchmarks/llgl_vulkan_upload/main.cpp','benchmarks/llgl_vulkan_draw/main.cpp')]

    def test_lifecycle_control_and_native_forwarding_bodies_preserved(self):
        original,pso,support,context,layer,upload,draw=self.sources()
        main,adapted,platform,generated,changes=port.translate(original,pso,support,context,layer,upload,draw)
        self.assertEqual(original[original.index('struct Totals'):original.index('int main(')],
                         main[main.index('struct Totals'):main.index('int main(')])
        self.assertEqual(support[support.index('std::string validationControl('):],adapted[adapted.index('std::string validationControl('):])
        self.assertEqual(layer.replace('__declspec(dllexport)','__attribute__((visibility("default")))'),generated)
        self.assertIn('renderer.reset();query.close();',platform);self.assertIn('GFX_LLGL_ALLOCATION_KIND',platform)
        self.assertIn('RTLD_NOLOAD',adapted);self.assertIn('s.createdInstances==2',adapted)

    def test_frozen_source_drift_fails_closed(self):
        sources=self.sources()
        for i,old in [(0,'#include "../llgl_vulkan_pso/Support.hpp"'),(2,'#include <bcrypt.h>'),(3,'desc.flags=LLGL::RenderSystemFlags::PreferNVIDIA;'),(4,'__declspec(dllexport)')]:
            changed=sources.copy();changed[i]=changed[i].replace(old,'changed')
            with self.subTest(i=i),self.assertRaises(ValueError):port.translate(*changed)

    def test_deterministic_confined_read_only_generation(self):
        before=[d.sha(d.ROOT/n) for n in port.OWN]
        with tempfile.TemporaryDirectory(dir=d.ROOT/'out') as td:
            a=port.generate(pathlib.Path(td));self.assertEqual(a,port.generate(pathlib.Path(td)))
            self.assertFalse(a['workload_bodies_changed']);self.assertEqual(set(a['generated']),{'main.cpp','Layer.cpp','Platform.hpp','SupportLinux.hpp'})
        self.assertEqual(before,[d.sha(d.ROOT/n) for n in port.OWN])
        with self.assertRaises(ValueError):port.generate(d.ROOT/'invalid-output')


class ResourceContracts(unittest.TestCase):
    def setUp(self):
        probe=d.read(d.ROOT/'benchmarks/reports/validation/2026-10-30-phase3-linux-llgl-probe/native/receipt.json')
        self.r=dict(probe=probe,adaptation=dict(source_fingerprint='a'*64),reference=d.references(),shader_sha256='b'*64,
            binary=dict(path='/build/linux_llgl_allocation'),crypto=dict(path='/usr/lib/libcrypto.so.3'),
            observer=dict(path='/build/libVkLayer_gfx_llgl_draw.so'),output_directory='/out/matrix')

    def result(self,job):
        a=self.r['probe']['qualification'];selected=a['selected'];native=next(x['result'] for x in self.r['probe']['runs'] if x['job']['kind']=='normal')
        control=job['kind'] in d.TOKENS;count=0 if control else d.SHAPES[job['scenario']];texture=job['scenario'].startswith('texture');total=count*4
        modules={x['path'] for x in a['runtime_libraries']}|{self.r['probe']['binaries'][n]['path'] for n in ('core','backend')}|{self.r[n]['path'] for n in ('crypto','observer')}
        if job['validation']:modules.add(a['validation_library'])
        return dict(schema_version=1,task='P3-LBEN-04-LLGL-readiness',sdk='llgl',api='vulkan',configuration='Release',
            contract='vulkan_resource_create_public_release_full_readback_v1',category='cpu_api',source_fingerprint='a'*64,
            LLGL_commit=self.r['probe']['LLGL_commit'],profile_id=d.p.PROFILE,dependency_profile=d.p.PROFILE,unmodified_upstream=False,
            selected=copy.deepcopy(selected),environment=copy.deepcopy(a['environment']),mode='software-diagnostic',device_name=selected['name'],
            device_uuid=selected['device_uuid'],driver_uuid=selected['driver_uuid'],device_luid=None,native_vendor_id=selected['vendor_id'],
            native_device_id=selected['device_id'],native_driver_version=selected['driver_version_raw'],native_api_version=selected['api_version_raw'],
            native_handle_verified=True,queue_family=native['queue_family'],queue_index=0,queue_flags=native['queue_flags'],loader_api_version=native['loader_api_version'],
            instance_API_request_observed=True,observed_instance_api_version=native['loader_api_version'],loader_path=a['loader_path'],
            LLGL_path=self.r['probe']['binaries']['core']['path'],backend_path=self.r['probe']['binaries']['backend']['path'],
            loaded_validation_layer=a['validation_library'] if job['validation'] else '',validation=job['validation'],scenario=job['scenario'],control=control,
            control_diagnostic=d.TOKENS[job['kind']] if control else '',submitted_control_work=0,delivered_errors=1 if control else 0,
            resource_kind='none' if control else 'texture' if texture else 'buffer',width=65 if texture else 0,height=37 if texture else 0,
            texel_bytes=4 if texture else 0,buffer_bytes=4096 if not control and not texture else 0,
            resources_per_iteration=count,created_resources=total,released_resource_handles=total,used_resources=total,verified_resources=total,
            unique_native_resources_checked=total,native_resource_handles_verified=not control,
            verified_images=total if texture else 0,verified_buffers=total if not texture else 0,verified_bytes=total*(9620 if texture else 4096),readback_verified=not control,
            allocation_policy='public_device_resource_default_policy',usage_policy='none' if control else 'sampled_rgba8_2d' if texture else 'llgl_vertex_buffer_copy_src_dst',
            buffer_cpu_access_flags=0,initial_data_supplied=False,copy_submission_policy='separate_upload_download_completed_fence_idle',
            staging_in_create_release_boundary=False,row_stride=0,layer_stride=0,GPU_use_in_timing=False,GPU_memory_reclamation_verified=False,
            native_allocation_count_verified=False,allocator_cache_state_known=False,warmup=0 if control else 1,sample_count=0 if control else 3,
            payload_sha256=[] if control else self.r['reference']['sha256'][job['scenario']],native_draw_calls=0,
            observer_path=self.r['observer']['path'],observer_live_instances_after_close=0,observer_live_devices_after_close=0,
            performance_samples=0,samples_ms=[],gpu_ms=None,physical_GPU_accepted=False,P3_LBEN_04_completed=False,
            performance_accepted=False,native_workload_shortcuts=False,runtime_modules=sorted(modules))

    def test_complete_four_cases_both_modes_and_core_sync(self):
        for job in d.jobs()[:10]:
            with self.subTest(job=job):d.validate_result(self.result(job),job,self.r)
        values=[self.result(j) for j in d.jobs()[:8]]
        self.assertEqual(sum(v['verified_images'] for v in values),8008);self.assertEqual(sum(v['verified_bytes'] for v in values),109837728)
        self.assertEqual(sum(v['verified_buffers'] for v in values),8008)
        self.assertEqual(len(d.jobs()),23);self.assertEqual(len({j['name'] for j in d.jobs()}),23)

    def test_frozen_cpu_reference_has_changing_resource_and_iteration_inputs(self):
        import llgl_vulkan_allocation as original
        for name in ('buffer1000','texture1000'):
            for i in range(4):
                for j in (0,1,999):
                    data=original.reference(i,j)[:4096 if name.startswith('buffer') else 9620]
                    self.assertEqual(self.r['reference']['sha256'][name][i][j],hashlib.sha256(data).hexdigest())
        self.assertNotEqual(self.r['reference']['sha256']['buffer1000'][0][0],self.r['reference']['sha256']['buffer1000'][0][1])

    def test_public_resource_native_identity_and_full_count_required(self):
        self.reject_changes([(key,-1) for key in ('resources_per_iteration','created_resources','released_resource_handles','used_resources','verified_resources','unique_native_resources_checked')]+[('native_resource_handles_verified',False)])

    def test_allocator_cache_allocation_count_memory_and_staging_claims_rejected(self):
        self.reject_changes([('GPU_memory_reclamation_verified',True),('native_allocation_count_verified',True),('allocator_cache_state_known',True),('staging_in_create_release_boundary',True),('initial_data_supplied',True),('buffer_cpu_access_flags',1),('GPU_use_in_timing',True)])

    def reject_changes(self,changes,jobs=None):
        for job in jobs or d.jobs()[:8]:
            for key,value in changes:
                result=self.result(job);result[key]=value
                with self.subTest(job=job,key=key),self.assertRaises(ValueError):d.validate_result(result,job,self.r)

    def test_full_resource_and_iteration_hash_matrix_required(self):
        for job in d.jobs()[:8]:
            value=self.result(job);value['payload_sha256']=[]
            with self.assertRaises(ValueError):d.validate_result(value,job,self.r)
            value=copy.deepcopy(self.result(job));value['payload_sha256'][0][-1]='0'*64
            with self.assertRaises(ValueError):d.validate_result(value,job,self.r)
            value=copy.deepcopy(self.result(job));value['payload_sha256'].reverse()
            with self.assertRaises(ValueError):d.validate_result(value,job,self.r)

    def test_full_images_buffers_bytes_and_zero_draws_required(self):
        self.reject_changes([('verified_images',-1),('verified_buffers',-1),('verified_bytes',1),('readback_verified',False),('row_stride',256),('layer_stride',1),('native_draw_calls',1)])

    def test_source_shape_target_and_actual_instance_api_bound(self):
        self.reject_changes([('source_fingerprint','f'*64),('device_uuid','0'*32),('device_luid','fake'),('native_handle_verified',False),('queue_index',1),('resource_kind','other'),('usage_policy','other'),('width',999),('observed_instance_api_version',0),('instance_API_request_observed',False)])

    def test_observer_must_be_loaded_and_native_objects_retired(self):
        self.reject_changes([('observer_path','/tmp/fake.so'),('observer_live_instances_after_close',1),('observer_live_devices_after_close',1)])
        job=d.jobs()[0];value=self.result(job);value['runtime_modules'].remove(self.r['observer']['path'])
        with self.assertRaises(ValueError):d.validate_result(value,job,self.r)

    def test_hardware_performance_or_boolean_counter_cannot_pass(self):
        self.reject_changes([('P3_LBEN_04_completed',True),('physical_GPU_accepted',True),('performance_accepted',True),('unmodified_upstream',True),('performance_samples',False),('samples_ms',[1]),('gpu_ms',1),('native_draw_calls',True)])

    def test_validation_controls_cannot_render_or_hide_errors(self):
        self.reject_changes([('control_diagnostic','wrong'),('delivered_errors',0),('submitted_control_work',1),('native_draw_calls',1),('verified_images',1)],d.jobs()[8:10])
        self.reject_changes([('delivered_errors',1)])

    def test_thirteen_intended_rejection_controls(self):
        tokens=(b'Required comparison validation layer absent',b'Exact UUID absent or ambiguous',b'Software diagnostics only',
            b'Public resource creation count differs',b'Duplicate live public resource handle',b'Public resource release count differs',
            b'Full changing resource readback mismatch',b'Full changing resource readback mismatch',b'Full changing resource readback mismatch',
            b'Full changing resource readback mismatch',b'Incomplete full resource readback',b'Required runtime module absent or ambiguous',b'Expected validation control diagnostic absent')
        self.assertEqual(len(d.jobs()[10:]),len(tokens))
        for job,token in zip(d.jobs()[10:],tokens):
            d.rejected(job,1,b'',token)
            for code,out,err in [(0,b'',token),(1,b'{}',token),(1,b'',b'unrelated')]:
                with self.subTest(job=job),self.assertRaises(ValueError):d.rejected(job,code,out,err)

    def test_observer_validation_and_sync_off_environment_is_explicit(self):
        for job in d.jobs():
            env=d.environment(job,self.r)
            self.assertEqual('VK_LAYER_GFX_llgl_draw' in env.get('VK_INSTANCE_LAYERS',''),job['kind']!='missing-observer')
        env=d.environment(next(j for j in d.jobs() if j['kind']=='sync-disabled'),self.r)
        self.assertEqual(env['VK_KHRONOS_VALIDATION_VALIDATE_SYNC'],'false');self.assertIn('sync-off',env['VK_LAYER_SETTINGS_PATH'])

    def test_both_actual_compiler_units_and_dependency_union_required(self):
        files=[dict(path='/fixture/'+str(i),bytes=1,sha256='a'*64,component=c,
            **({'redistributed':False} if c=='system' else {'selected_license':'BSD-3-Clause' if c=='llgl' else 'MIT'}))
            for i,c in enumerate(('own','generated','llgl','vulkan_headers','system'))]
        raw='file.o: '+' '.join(x['path'] for x in files)+'\n'
        command=dict(command='g++ -O2 -DNDEBUG -c /fixture/0 -o file.o',executed_command='g++ -O2 -DNDEBUG -MD -MT file.o -MF file.d -c /fixture/0 -o file.o')
        dep=dict(raw=raw,bytes=len(raw),sha256=hashlib.sha256(raw.encode()).hexdigest(),dependencies=[x['path'] for x in files])
        value=dict(files=files,compile_commands=[command,command],depfiles=[dep,dep],actual_verbose_commands_verified=True);d.validate_audit(value)
        for change in (lambda v:v['compile_commands'].pop(),lambda v:v['depfiles'][0].update(raw='bad'),lambda v:v.update(actual_verbose_commands_verified=False),lambda v:v['files'][4].update(redistributed=True)):
            bad=copy.deepcopy(value);change(bad)
            with self.assertRaises(ValueError):d.validate_audit(bad)


if __name__=='__main__':unittest.main()
