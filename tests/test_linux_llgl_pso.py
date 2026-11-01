"""PSO contracts and adversarial receipts; these CPU tests are not native GPU evidence."""
import copy
import hashlib
import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'tools'))
import linux_llgl_pso as d
import port_linux_llgl_pso as port


class PSOPort(unittest.TestCase):
    def sources(self):
        return [(d.ROOT/n).read_text(encoding='utf-8') for n in (
            'benchmarks/llgl_vulkan_pso/main.cpp','benchmarks/llgl_vulkan_pso/Support.hpp',
            'benchmarks/llgl_vulkan_upload/Context.hpp','benchmarks/llgl_vulkan_draw/Layer.cpp',
            'benchmarks/llgl_vulkan_upload/main.cpp','benchmarks/llgl_vulkan_draw/main.cpp')]

    def test_lifecycle_control_and_native_forwarding_bodies_preserved(self):
        original,support,context,layer,upload,draw=self.sources()
        main,adapted,platform,generated,changes=port.translate(original,support,context,layer,upload,draw)
        self.assertEqual(original[original.index('struct Totals'):original.index('int main(')],
                         main[main.index('struct Totals'):main.index('int main(')])
        self.assertEqual(support[support.index('std::string validationControl('):],adapted[adapted.index('std::string validationControl('):])
        self.assertEqual(layer.replace('__declspec(dllexport)','__attribute__((visibility("default")))'),generated)
        self.assertIn('renderer.reset();query.close();',platform);self.assertIn('GFX_LLGL_PSO_KIND',platform)
        self.assertIn('RTLD_NOLOAD',adapted);self.assertIn('s.createdInstances==2',adapted)

    def test_frozen_source_drift_fails_closed(self):
        sources=self.sources()
        for i,old in [(0,'#include "Support.hpp"'),(1,'#include <bcrypt.h>'),(2,'desc.flags=LLGL::RenderSystemFlags::PreferNVIDIA;'),(3,'__declspec(dllexport)')]:
            changed=sources.copy();changed[i]=changed[i].replace(old,'changed')
            with self.subTest(i=i),self.assertRaises(ValueError):port.translate(*changed)

    def test_deterministic_confined_read_only_generation(self):
        before=[d.sha(d.ROOT/n) for n in port.OWN]
        with tempfile.TemporaryDirectory(dir=d.ROOT/'out') as td:
            a=port.generate(pathlib.Path(td));self.assertEqual(a,port.generate(pathlib.Path(td)))
            self.assertFalse(a['workload_bodies_changed']);self.assertEqual(set(a['generated']),{'main.cpp','Layer.cpp','Platform.hpp','SupportLinux.hpp'})
        self.assertEqual(before,[d.sha(d.ROOT/n) for n in port.OWN])
        with self.assertRaises(ValueError):port.generate(d.ROOT/'invalid-output')


class PSOContracts(unittest.TestCase):
    def setUp(self):
        probe=d.read(d.ROOT/'benchmarks/reports/validation/2026-10-30-phase3-linux-llgl-probe/native/receipt.json')
        self.r=dict(probe=probe,adaptation=dict(source_fingerprint='a'*64),reference=d.references(),shader_sha256='b'*64,
            binary=dict(path='/build/linux_llgl_pso'),crypto=dict(path='/usr/lib/libcrypto.so.3'),
            observer=dict(path='/build/libVkLayer_gfx_llgl_draw.so'),output_directory='/out/matrix')

    def result(self,job):
        a=self.r['probe']['qualification'];selected=a['selected'];native=next(x['result'] for x in self.r['probe']['runs'] if x['job']['kind']=='normal')
        control=job['kind'] in d.TOKENS;count=0 if control else d.SHAPES[job['scenario']]
        modules={x['path'] for x in a['runtime_libraries']}|{self.r['probe']['binaries'][n]['path'] for n in ('core','backend')}|{self.r[n]['path'] for n in ('crypto','observer')}
        if job['validation']:modules.add(a['validation_library'])
        return dict(schema_version=1,task='P3-LBEN-03-LLGL-readiness',sdk='llgl',api='vulkan',configuration='Release',
            contract='vulkan_pipeline_create_public_release_use_checked_v1',category='cpu_api',source_fingerprint='a'*64,
            shader_header_sha256='b'*64,LLGL_commit=self.r['probe']['LLGL_commit'],profile_id=d.p.PROFILE,dependency_profile=d.p.PROFILE,unmodified_upstream=False,
            selected=copy.deepcopy(selected),environment=copy.deepcopy(a['environment']),mode='software-diagnostic',device_name=selected['name'],
            device_uuid=selected['device_uuid'],driver_uuid=selected['driver_uuid'],device_luid=None,native_vendor_id=selected['vendor_id'],
            native_device_id=selected['device_id'],native_driver_version=selected['driver_version_raw'],native_api_version=selected['api_version_raw'],
            native_handle_verified=True,queue_family=native['queue_family'],queue_index=0,queue_flags=native['queue_flags'],loader_api_version=native['loader_api_version'],
            instance_API_request_observed=True,observed_instance_api_version=native['loader_api_version'],loader_path=a['loader_path'],
            LLGL_path=self.r['probe']['binaries']['core']['path'],backend_path=self.r['probe']['binaries']['backend']['path'],
            loaded_validation_layer=a['validation_library'] if job['validation'] else '',validation=job['validation'],scenario=job['scenario'],control=control,
            control_diagnostic=d.TOKENS[job['kind']] if control else '',submitted_control_work=0,delivered_errors=1 if control else 0,
            width=65,height=37,texel_bytes=4,clear_strategy='automatic_offscreen_pass_plus_full_attachment_clear',texture_bind_policy='color_attachment_sampled_copy_src',
            scissor=[5,3,55,31],vertices_per_pipeline=3,first_vertex=0,first_instance=0,pipelines_per_iteration=count,
            created_pipelines=count*4,released_pipeline_handles=count*4,used_pipeline_regions=count*4,created_shader_handles=count*8,released_shader_handles=count*8,
            includes_shader_object_create_release=True,GPU_use_in_timing=False,GPU_memory_reclamation_verified=False,native_pipeline_count_verified=False,
            driver_cache_state_known=False,cache_state='uncontrolled_driver_cache_repeated_public_creation',
            warmup=0 if control else 1,sample_count=0 if control else 3,verified_images=0 if control else 4,verified_bytes=0 if control else 38480,
            readback_verified=not control,frame_sha256=[] if control else d.references()[job['scenario']],native_draw_calls=4*count,native_forwarded_draw_calls=4*count,
            native_instances=4*count,native_vertices=12*count,native_bad_arguments=0,
            observer_path=self.r['observer']['path'],observer_live_instances_after_close=0,observer_live_devices_after_close=0,
            performance_samples=0,samples_ms=[],gpu_ms=None,physical_GPU_accepted=False,P3_LBEN_03_completed=False,
            performance_accepted=False,native_workload_shortcuts=False,runtime_modules=sorted(modules))

    def test_complete_two_cases_both_modes_and_core_sync(self):
        for job in d.jobs()[:6]:
            with self.subTest(job=job):d.validate_result(self.result(job),job,self.r)
        values=[self.result(j) for j in d.jobs()[:4]]
        self.assertEqual(sum(v['verified_images'] for v in values),16);self.assertEqual(sum(v['verified_bytes'] for v in values),153920)
        self.assertEqual(len(d.jobs()),17);self.assertEqual(len({j['name'] for j in d.jobs()}),17)

    def test_cpu_reference_matches_frozen_cross_platform_contract(self):
        import llgl_vulkan_pso as windows
        expected={name:[hashlib.sha256(windows.reference(i,count)).hexdigest() for i in range(4)] for name,count in d.SHAPES.items()}
        self.assertEqual(d.references(),expected)
        self.assertNotEqual(expected['pso1'],expected['pso8'])

    def test_public_pipeline_and_shader_lifecycle_counts_are_required(self):
        self.reject_changes([(key,-1) for key in ('pipelines_per_iteration','created_pipelines','released_pipeline_handles','used_pipeline_regions','created_shader_handles','released_shader_handles')])

    def test_driver_cache_native_pipeline_count_and_memory_claims_rejected(self):
        self.reject_changes([('GPU_memory_reclamation_verified',True),('native_pipeline_count_verified',True),('driver_cache_state_known',True),('cache_state','cold'),('includes_shader_object_create_release',False),('GPU_use_in_timing',True)])

    def reject_changes(self,changes,jobs=None):
        for job in jobs or d.jobs()[:4]:
            for key,value in changes:
                result=self.result(job);result[key]=value
                with self.subTest(job=job,key=key),self.assertRaises(ValueError):d.validate_result(result,job,self.r)

    def test_actual_draw_forwarded_instances_and_vertices_required(self):
        self.reject_changes([(k,-1) for k in ('native_draw_calls','native_forwarded_draw_calls','native_instances','native_vertices')]+[('native_bad_arguments',1)])

    def test_full_changing_images_and_arguments_required(self):
        self.reject_changes([('frame_sha256',[]),('verified_images',3),('verified_bytes',1),('readback_verified',False),('scissor',[6,3,55,31]),('first_vertex',1),('first_instance',1),('vertices_per_pipeline',6)])

    def test_source_shader_target_and_actual_instance_api_bound(self):
        self.reject_changes([('source_fingerprint','f'*64),('shader_header_sha256','f'*64),('device_uuid','0'*32),('device_luid','fake'),('native_handle_verified',False),('queue_index',1),('observed_instance_api_version',0),('instance_API_request_observed',False)])

    def test_observer_must_be_loaded_and_native_objects_retired(self):
        self.reject_changes([('observer_path','/tmp/fake.so'),('observer_live_instances_after_close',1),('observer_live_devices_after_close',1)])
        job=d.jobs()[0];value=self.result(job);value['runtime_modules'].remove(self.r['observer']['path'])
        with self.assertRaises(ValueError):d.validate_result(value,job,self.r)

    def test_hardware_performance_or_boolean_counter_cannot_pass(self):
        self.reject_changes([('P3_LBEN_03_completed',True),('physical_GPU_accepted',True),('performance_accepted',True),('unmodified_upstream',True),('performance_samples',False),('samples_ms',[1]),('gpu_ms',1),('native_draw_calls',True)])

    def test_validation_controls_cannot_render_or_hide_errors(self):
        self.reject_changes([('control_diagnostic','wrong'),('delivered_errors',0),('submitted_control_work',1),('native_draw_calls',1),('verified_images',1)],d.jobs()[4:6])
        self.reject_changes([('delivered_errors',1)])

    def test_eleven_intended_rejection_controls(self):
        for job,token in zip(d.jobs()[6:],(
            b'Required comparison validation layer absent',b'Exact UUID absent or ambiguous',b'Software diagnostics only',
            b'Full changing PSO stripe reference mismatch',b'Expected validation control diagnostic absent',
            b'Public pipeline creation count differs',b'Public pipeline release count differs',
            b'Full changing PSO stripe reference mismatch',b'Native PSO draw count/arguments differ',
            b'Full changing PSO stripe reference mismatch',b'Required runtime module absent or ambiguous')):
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
