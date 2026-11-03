"""Allocation timing/observation contracts; synthetic metadata is never GPU evidence."""
import copy
import json
import pathlib
import sys
import tempfile
import unittest
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'tools'))
import linux_allocation_campaign as c
import test_linux_upload_campaign as upload_fixture


class AllocationCampaign(unittest.TestCase):
    def setUp(self):self.plan=c.read(c.ROOT/c.PLAN)

    def context(self,physical=False):
        fixture=upload_fixture.UploadCampaign();fixture.setUp();r=fixture.context(physical)
        correct=c.read(c.ROOT/'benchmarks/reports/validation/2026-11-01-phase3-linux-llgl-allocation/native/receipt.json')
        correct['probe']=r['llgl_correctness']['probe'];correct['source_commit']=r['readiness']['source_commit']
        if physical:
            correct.update(mode='physical',physical_guard_only=False,physical_allocation_correctness_verified=True,software_allocation_correctness_verified=False,P3_BEN_08_completed=False)
            correct['adaptation']['mode']='physical'
        r.update(llgl_correctness=correct,plan=copy.deepcopy(self.plan),reference=c.reference(),
            verified_buffers=0,verified_resources=0)
        return r

    def output(self,job,r):
        validation,observed,warmup,samples=c.options(job);a=c.shared.qualification(r);d=a['selected'];kind=job['kind']
        control=kind in c.correctness.TOKENS;probe=kind in ('probe','observe');special=control or probe
        count=0 if special else c.WORK[job['scenario']];texture=job['scenario'].startswith('texture');total=count*(warmup+samples)
        intervals=[[1+i,2+i] for i in range(samples)]
        v=dict(schema_version=1,sdk=job['sdk'],api='vulkan',configuration='Release',category='cpu_api',
            contract='vulkan_resource_create_public_release_full_readback_v1',scenario=job['scenario'],
            validation=validation,probe=probe,warmup=warmup,sample_count=samples,gpu_ms=None,
            device_name=d['name'],device_uuid=d['device_uuid'],driver_uuid=d['driver_uuid'],device_luid=None,
            native_vendor_id=d['vendor_id'],native_device_id=d['device_id'],native_driver_version=d['driver_version_raw'],native_api_version=d['api_version_raw'],
            resource_kind='none' if control else 'texture' if texture else 'buffer',width=65 if texture else 0,height=37 if texture else 0,
            texel_bytes=4 if texture else 0,buffer_bytes=4096 if not control and not texture else 0,resources_per_iteration=count,
            created_resources=total,released_resource_handles=total,used_resources=total,verified_resources=total,
            allocation_policy='public_device_resource_default_policy',GPU_use_in_timing=False,GPU_memory_reclamation_verified=False,
            native_allocation_count_verified=False,verified_images=total if texture else 0,verified_buffers=0 if texture else total,
            verified_bytes=total*(9620 if texture else 4096),readback_verified=not special,columns=c.COLUMNS,
            loaded_validation_layer=a['validation_library'] if validation else '',environment=copy.deepcopy(a['environment']),
            samples_ms=intervals)
        if job['sdk']!='llgl':
            v.update(preference='high',source_fingerprint=r['readiness']['profile_fingerprints']['allocation'],SDK_source_fingerprint=a['SDK_source_fingerprint'],
                SDL_commit=a['SDL_commit'],identity_attribution=c.shared.w.ATTRIBUTION,requested_instance_api_version=4198400,usage_policy='sampled_rgba8_2d' if texture else 'sdl_vertex_buffer' if job['sdk']=='sdl' else 'sdk_general_device_buffer')
            v['environment'].update(gpu=d['name'],software_renderer=r['mode']=='software-diagnostic',device_uuid=d['device_uuid'],driver_uuid=d['driver_uuid'],device_luid=None)
        else:
            prior=r['llgl_correctness']['probe'];native=next(x['result'] for x in prior['runs'] if x['job']['kind']=='normal')
            modules={x['path'] for x in a['runtime_libraries']}|{prior['binaries'][n]['path'] for n in ('core','backend')}|{r['crypto']['path']}
            if validation:modules.add(a['validation_library'])
            if observed:modules.add(r['observer']['path'])
            v.update(task=c.TASK,mode=r['mode'],profile_id=c.p.PROFILE,LLGL_commit=prior['LLGL_commit'],
                source_fingerprint=r['adaptation']['source_fingerprint'],identity_attribution='actual_LLGL_native_handles_and_native_UUID',
                native_handle_verified=True,queue_family=native['queue_family'],queue_index=0,queue_flags=native['queue_flags'],loader_api_version=native['loader_api_version'],
                loader_path=a['loader_path'],LLGL_path=prior['binaries']['core']['path'],backend_path=prior['binaries']['backend']['path'],
                control=control,control_diagnostic=c.correctness.TOKENS[kind] if control else '',submitted_control_work=0,delivered_errors=1 if control else 0,
                native_workload_shortcuts=False,observer_enabled=observed,instance_API_request_observed=observed,
                observed_instance_api_version=native['loader_api_version'] if observed else 0,observer_live_instances_after_close=0,observer_live_devices_after_close=0,
                unique_native_resources_checked=total,native_resource_handles_verified=not special,requested_instance_api_version=None,
                usage_policy='none' if control else 'sampled_rgba8_2d' if texture else 'llgl_vertex_buffer_copy_src_dst',
                buffer_cpu_access_flags=0,initial_data_supplied=False,copy_submission_policy='separate_upload_download_completed_fence_idle',
                staging_in_create_release_boundary=False,row_stride=0,layer_stride=0,allocator_cache_state_known=False,
                payload_sha256=copy.deepcopy(r['reference']['sha256'][job['scenario']]) if observed and not special else [],
                native_draw_calls=0 if observed else None,runtime_modules=sorted(modules))
        return v

    def test_frozen_resource_usage_and_scope_contract(self):
        c.validate_plan(self.plan)
        for key in ('native_allocation_count_verified','allocator_cache_state_known','GPU_memory_reclamation_verified'):
            bad=copy.deepcopy(self.plan);bad[key]=True
            with self.assertRaises(ValueError):c.validate_plan(bad)
        for mutate in (lambda p:p['performance'].update(samples=3),lambda p:p['performance'].update(repetitions=True),lambda p:p['work'].update(buffer1000=999),lambda p:p['resources'].update(buffer_bytes=4095),lambda p:p.update(columns=['create_ms'])):
            bad=copy.deepcopy(self.plan);mutate(bad)
            with self.assertRaises(ValueError):c.validate_plan(bad)

    def test_schedule_all_gates_before_five_distinct_adjacent_sdk_orders(self):
        jobs=c.schedule(self.plan,'physical');self.assertEqual(len(jobs),101);self.assertEqual(len({j['name'] for j in jobs}),101)
        self.assertFalse(any(j['kind']=='measurement' for j in jobs[:41]));self.assertTrue(all(j['kind']=='measurement' for j in jobs[41:]))
        self.assertEqual(sum(j['kind'] not in c.SUCCESS for j in jobs),19)
        for scenario in c.SHAPES:
            orders=[]
            for rep in range(5):
                group=[j for j in jobs if j['kind']=='measurement' and j['scenario']==scenario and j['repetition']==rep]
                self.assertEqual({j['sdk'] for j in group},set(c.SDKS));orders.append(tuple(j['sdk'] for j in group))
                positions=[jobs.index(j) for j in group];self.assertEqual(positions,list(range(min(positions),min(positions)+3)))
            self.assertEqual(len(set(orders)),5)
        self.assertEqual(jobs,c.schedule(copy.deepcopy(self.plan),'physical'))

    def test_all_successful_modes_and_entire_resource_totals(self):
        for physical in (False,True):
            r=self.context(physical);c.validate_inputs(r['readiness'],r['llgl_correctness'],r['mode'])
            values=[c.validate_output(self.output(j,r),j,r) for j in c.schedule(self.plan,r['mode']) if j['kind'] in c.SUCCESS]
            self.assertEqual((sum(v['verified_images'] for v in values),sum(v['verified_bytes'] for v in values)),c.FULL_TOTALS if physical else c.SMOKE_TOTALS)
            n=541541 if physical else 28028
            self.assertEqual(sum(v['verified_buffers'] for v in values),n);self.assertEqual(sum(v['verified_resources'] for v in values),2*n)

    def test_software_measurements_cannot_be_promoted(self):
        jobs=c.schedule(self.plan,'software-diagnostic');self.assertEqual(len(jobs),53);self.assertEqual(sum(j['kind']=='smoke' for j in jobs),12)
        self.assertEqual(c.summarize([dict(job=j) for j in jobs],'software-diagnostic'),[])
        with self.assertRaises(ValueError):c.summarize([dict(job=dict(kind='measurement'))],'software-diagnostic')

    def test_measured_processes_exclude_observer_vvl_and_loader_trace(self):
        r=self.context(True)
        for j in c.schedule(self.plan,'physical'):
            env=c.environment(j,r);args=c.arguments(j,r)
            if j['kind']=='measurement':
                self.assertNotIn('LD_DEBUG',env);self.assertNotIn('VK_INSTANCE_LAYERS',env);self.assertEqual(c.options(j)[2:],(5,30))
                if j['sdk']=='llgl':self.assertEqual(args[2],'plain')
            if j['sdk']=='llgl' and j['kind']=='validation':
                self.assertEqual(args[2],'validation');self.assertEqual(env['VK_INSTANCE_LAYERS'],'VK_LAYER_GFX_llgl_draw:VK_LAYER_KHRONOS_validation')

    def test_invalid_cpu_intervals_and_lifecycle_rejected(self):
        r=self.context(True)
        for sdk in c.SDKS:
            j=next(j for j in c.schedule(self.plan,'physical') if j['kind']=='measurement' and j['sdk']==sdk)
            for value in ([float('nan'),1],[0,1],[True,1],[1,-1],[1,float('inf')],[1]):
                v=self.output(j,r);v['samples_ms'][0]=value
                with self.subTest(sdk=sdk,value=value),self.assertRaises(ValueError):c.validate_output(v,j,r)
            for key,value in [('created_resources',1),('released_resource_handles',1),('used_resources',1),('verified_resources',1),('verified_buffers',1),('GPU_use_in_timing',True),('GPU_memory_reclamation_verified',True),('native_allocation_count_verified',True),('usage_policy','other')]:
                v=self.output(j,r);v[key]=value
                with self.subTest(sdk=sdk,key=key),self.assertRaises(ValueError):c.validate_output(v,j,r)

    def test_full_payload_matrix_native_handle_and_staging_policy_rejected_on_drift(self):
        r=self.context();j=next(j for j in c.schedule(self.plan,r['mode']) if j['kind']=='audit' and j['scenario']=='texture1000')
        for key,value in [('unique_native_resources_checked',3999),('native_resource_handles_verified',False),('payload_sha256',[]),('staging_in_create_release_boundary',True),('allocator_cache_state_known',True),('buffer_cpu_access_flags',1),('row_stride',260),('initial_data_supplied',True),('native_draw_calls',1),('width',64),('verified_bytes',1)]:
            v=self.output(j,r);v[key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):c.validate_output(v,j,r)
        v=self.output(j,r);v['payload_sha256'][-1][-1]='0'*64
        with self.assertRaises(ValueError):c.validate_output(v,j,r)

    def test_exact_rejections_not_unrelated_failures(self):
        r=self.context();tokens={'wrong-uuid':b'Exact UUID absent or ambiguous','wrong-kind':c.correctness.policy.wrong_kind_message(r['mode']),
            'missing-layer':b'Required comparison validation layer absent','sync-disabled':b'Expected validation control diagnostic absent',
            'missing-resource':b'Public resource creation count differs','alias-resource':b'Duplicate live public resource handle',
            'skip-release':b'Public resource release count differs','wrong-resource-payload':b'Full changing resource readback mismatch',
            'stale-iteration':b'Full changing resource readback mismatch','skip-copy':b'Full changing resource readback mismatch',
            'corrupt-readback':b'Full changing resource readback mismatch','short-readback':b'Incomplete full resource readback',
            'observer-disabled':b'Required runtime module absent or ambiguous'}
        for j in c.schedule(self.plan,r['mode']):
            if j['sdk']!='llgl' or j['kind'] in c.SUCCESS:continue
            token=tokens[j['kind']];c.rejected(j,1,b'',token,r)
            for code,out,err in [(True,b'',token),(0,b'',token),(1,b'{}',token),(1,b'',b'unrelated')]:
                with self.assertRaises(ValueError):c.rejected(j,code,out,err,r)

    def test_physical_requires_controlled_o2_non_guard_same_revision(self):
        for mutate in (lambda r:r['readiness'].pop('controlled_build'),lambda r:r['llgl_correctness'].update(source_commit='b'*40),lambda r:r['llgl_correctness'].update(physical_guard_only=True),lambda r:r['llgl_correctness'].update(physical_allocation_correctness_verified=False)):
            r=self.context(True);mutate(r)
            with self.assertRaises(ValueError):c.validate_inputs(r['readiness'],r['llgl_correctness'],'physical')
        r=self.context()
        with self.assertRaises(ValueError):c.validate_inputs(r['readiness'],r['llgl_correctness'],'physical')

    def test_statistics_preserve_both_columns_all_repetitions_and_outliers(self):
        rows=[dict(job=j,result=dict(samples_ms=[[i+1,0.5] for i in range(30)])) for j in c.schedule(self.plan,'physical') if j['kind']=='measurement']
        groups=c.summarize(rows,'physical');self.assertEqual(len(groups),24)
        for g in groups:self.assertEqual((g['statistics']['processes'],g['statistics']['samples'],g['statistics']['samples_removed']),(5,150,0))
        rows[0]['result']['samples_ms'][0][0]=10000
        self.assertTrue(any(g['statistics']['max_ms']==10000 for g in c.summarize(rows,'physical')))
        for bad in (rows[:-1],rows+[rows[0]]):
            with self.assertRaises(ValueError):c.summarize(bad,'physical')

    def test_shared_runner_checks_nested_intervals_before_later_processes(self):
        r=self.context();j=next(j for j in c.schedule(self.plan,r['mode']) if j['kind']=='smoke' and j['sdk']=='llgl');v=self.output(j,r)
        row=dict(returncode=0,started=0,finished=30);c.validate_success(row,json.dumps(v).encode(),b'',j,r)
        row['finished']=0.001
        with self.assertRaises(ValueError):c.validate_success(row,json.dumps(v).encode(),b'',j,r)
        row['finished']=30
        with self.assertRaises(ValueError):c.validate_success(row,json.dumps(v).encode(),b'VkLayer_ activated',j,r)

    def test_scope_and_buffer_totals_cannot_be_promoted(self):
        for physical in (False,True):
            r=self.context(physical);r.update(schema_version=1,task=c.TASK,status='completed',inputs_unchanged=True,
                campaign_kind='physical-campaign' if physical else 'software-smoke',performance_accepted=physical,
                measurement_samples=1800 if physical else 0,diagnostic_rows=48 if physical else 84,
                verified_buffers=541541 if physical else 28028,verified_resources=1083082 if physical else 56056,
                physical_GPU_accepted=False,P3_LBEN_04_completed=False,historical_timings_reused=False,overall_SDK_ranking=False,
                GPU_timestamp_measurement=False,SDL_native_handle_verified=False,SDK_SDL_measurement_module_snapshot_observed=False)
            c.validate_scope(r)
            for key,value in [('performance_accepted',not physical),('measurement_samples',True),('verified_buffers',1),('verified_resources',1),('P3_LBEN_04_completed',True),('SDK_SDL_measurement_module_snapshot_observed',True)]:
                bad=copy.deepcopy(r);bad[key]=value
                with self.subTest(key=key),self.assertRaises(ValueError):c.validate_scope(bad)

    def test_runtime_layer_contamination_and_plain_null_count(self):
        r=self.context(True);j=next(j for j in c.schedule(self.plan,'physical') if j['kind']=='measurement' and j['sdk']=='llgl')
        for path in (r['observer']['path'],c.shared.qualification(r)['validation_library'],'/tmp/inject.so'):
            v=self.output(j,r);v['runtime_modules']=sorted(v['runtime_modules']+[path])
            with self.assertRaises(ValueError):c.validate_output(v,j,r)
        v=self.output(j,r);v['native_draw_calls']=0
        with self.assertRaises(ValueError):c.validate_output(v,j,r)

    def test_complete_native_lifecycle_and_loop_are_preserved_in_both_modes(self):
        sources={n:(c.ROOT/n).read_text(encoding='utf-8') for n in c.port.OWN}
        for mode in ('physical','software-diagnostic'):
            out,_=c.port.translate(sources,mode);old=sources['benchmarks/llgl_vulkan_allocation_compare/Allocation.hpp']
            self.assertEqual(old[old.index('struct Allocation {'):],out['Allocation.hpp'][out['Allocation.hpp'].index('struct Allocation {'):])
            old=sources['benchmarks/llgl_vulkan_allocation_compare/main.cpp'];a=old.index('        for(unsigned i=0;');b=old.index('    auto runtime=modules();',a)
            self.assertIn(old[a:b],out['main.cpp']);self.assertIn('P3_LBEN_04_completed',out['Platform.hpp'])
        bad=copy.deepcopy(sources);key='benchmarks/llgl_vulkan_allocation_compare/Identity.hpp';bad[key]=bad[key].replace('unsigned number(','changed(')
        with self.assertRaises(ValueError):c.port.translate(bad,'physical')

    def test_generation_is_mode_bound_confined_deterministic_and_readonly(self):
        before=[c.sha(c.ROOT/n) for n in c.port.OWN]
        with tempfile.TemporaryDirectory(dir=c.ROOT/'out') as td:
            root=pathlib.Path(td);soft=c.port.generate(root/'soft','software-diagnostic');phys=c.port.generate(root/'physical','physical')
            self.assertNotEqual(soft['source_fingerprint'],phys['source_fingerprint']);self.assertEqual(phys,c.port.generate(root/'physical','physical'))
            self.assertFalse(phys['timed_loop_changed']);self.assertFalse(phys['workload_bodies_changed'])
        self.assertEqual(before,[c.sha(c.ROOT/n) for n in c.port.OWN])
        with self.assertRaises(ValueError):c.port.generate(c.ROOT/'forbidden','physical')


if __name__=='__main__':unittest.main()
