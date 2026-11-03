"""PSO timing/observation contracts; synthetic metadata is never GPU evidence."""
import copy
import json
import pathlib
import sys
import tempfile
import unittest
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'tools'))
import linux_pso_campaign as c
import test_linux_upload_campaign as upload_fixture


class PSOCampaign(unittest.TestCase):
    def setUp(self):self.plan=c.read(c.ROOT/c.PLAN)

    def context(self,physical=False):
        fixture=upload_fixture.UploadCampaign();fixture.setUp();r=fixture.context(physical)
        correct=c.read(c.ROOT/'benchmarks/reports/validation/2026-11-01-phase3-linux-llgl-pso/native/receipt.json')
        correct['probe']=r['llgl_correctness']['probe'];correct['source_commit']=r['readiness']['source_commit']
        if physical:
            correct.update(mode='physical',physical_guard_only=False,physical_pso_correctness_verified=True,software_pso_correctness_verified=False,P3_BEN_08_completed=False)
            correct['adaptation']['mode']='physical'
        r.update(llgl_correctness=correct,plan=copy.deepcopy(self.plan),reference=c.reference(),
            shader_header_sha256=r['readiness']['shader_header_sha256'])
        return r

    def output(self,job,r):
        validation,observed,warmup,samples=c.options(job);a=c.shared.qualification(r);d=a['selected'];kind=job['kind']
        control=kind in c.correctness.TOKENS;probe=kind in ('probe','observe');special=control or probe
        count=0 if special else c.WORK[job['scenario']];images=0 if special else warmup+samples
        intervals=[[1+i,2+i] for i in range(samples)]
        v=dict(schema_version=1,sdk=job['sdk'],api='vulkan',configuration='Release',category='cpu_api',
            contract='vulkan_pipeline_create_public_release_use_checked_v1',scenario=job['scenario'],
            validation=validation,probe=probe,warmup=warmup,sample_count=samples,gpu_ms=None,
            device_name=d['name'],device_uuid=d['device_uuid'],driver_uuid=d['driver_uuid'],device_luid=None,
            native_vendor_id=d['vendor_id'],native_device_id=d['device_id'],native_driver_version=d['driver_version_raw'],native_api_version=d['api_version_raw'],
            width=65,height=37,texel_bytes=4,pipelines_per_iteration=count,created_pipelines=count*images,
            released_pipeline_handles=count*images,used_pipeline_regions=count*images,includes_shader_object_create_release=True,
            GPU_use_in_timing=False,GPU_memory_reclamation_verified=False,cache_state='uncontrolled_driver_cache_repeated_public_creation',
            verified_images=images,verified_bytes=9620*images,readback_verified=not special,shader_header_sha256=r['shader_header_sha256'],columns=c.COLUMNS,
            loaded_validation_layer=a['validation_library'] if validation else '',environment=copy.deepcopy(a['environment']),
            samples_ms=intervals)
        if job['sdk']!='llgl':
            v.update(preference='high',source_fingerprint=r['readiness']['profile_fingerprints']['pso'],SDK_source_fingerprint=a['SDK_source_fingerprint'],
                SDL_commit=a['SDL_commit'],identity_attribution=c.shared.w.ATTRIBUTION,requested_instance_api_version=4198400)
            v['environment'].update(gpu=d['name'],software_renderer=r['mode']=='software-diagnostic',device_uuid=d['device_uuid'],driver_uuid=d['driver_uuid'],device_luid=None)
        else:
            prior=r['llgl_correctness']['probe'];native=next(x['result'] for x in prior['runs'] if x['job']['kind']=='normal')
            modules={x['path'] for x in a['runtime_libraries']}|{prior['binaries'][n]['path'] for n in ('core','backend')}|{r['crypto']['path']}
            if validation:modules.add(a['validation_library'])
            if observed:modules.add(r['observer']['path'])
            v.update(task='P3-LBEN-03-pso-timer',mode=r['mode'],profile_id=c.p.PROFILE,LLGL_commit=prior['LLGL_commit'],
                source_fingerprint=r['adaptation']['source_fingerprint'],identity_attribution='actual_LLGL_native_handles_and_native_UUID',
                native_handle_verified=True,queue_family=native['queue_family'],queue_index=0,queue_flags=native['queue_flags'],loader_api_version=native['loader_api_version'],
                loader_path=a['loader_path'],LLGL_path=prior['binaries']['core']['path'],backend_path=prior['binaries']['backend']['path'],
                control=control,control_diagnostic=c.correctness.TOKENS[kind] if control else '',submitted_control_work=0,delivered_errors=1 if control else 0,
                native_workload_shortcuts=False,observer_enabled=observed,instance_API_request_observed=observed,
                observed_instance_api_version=native['loader_api_version'] if observed else 0,observer_live_instances_after_close=0,observer_live_devices_after_close=0,
                scissor=[5,3,55,31],vertices_per_pipeline=3,first_vertex=0,first_instance=0,
                created_shader_handles=count*images*2,released_shader_handles=count*images*2,native_pipeline_count_verified=False,driver_cache_state_known=False,
                requested_instance_api_version=None,clear_strategy='automatic_offscreen_pass_plus_full_attachment_clear',
                texture_bind_policy='color_attachment_sampled_copy_src',frame_sha256=r['reference'][job['scenario']] if observed and not special else [],runtime_modules=sorted(modules))
            counts=dict(native_draw_calls=images*count,native_forwarded_draw_calls=images*count,native_instances=images*count,
                native_vertices=3*images*count,native_bad_arguments=0,native_min_instances=1 if images else 0,native_max_instances=1 if images else 0)
            v.update({k:x if observed else None for k,x in counts.items()})
        return v

    def test_frozen_contract_and_cache_scope(self):
        c.validate_plan(self.plan)
        for key in ('native_pipeline_count_verified','driver_cache_state_known','GPU_memory_reclamation_verified'):
            bad=copy.deepcopy(self.plan);bad[key]=True
            with self.assertRaises(ValueError):c.validate_plan(bad)
        for mutate in (lambda p:p['performance'].update(samples=3),lambda p:p['performance'].update(repetitions=True),lambda p:p['work'].update(pso8=7),lambda p:p.update(columns=['create_ms'])):
            bad=copy.deepcopy(self.plan);mutate(bad)
            with self.assertRaises(ValueError):c.validate_plan(bad)

    def test_schedule_gates_and_five_distinct_adjacent_orders(self):
        jobs=c.schedule(self.plan,'physical');self.assertEqual(len(jobs),61)
        self.assertEqual(len({j['name'] for j in jobs}),61)
        self.assertFalse(any(j['kind']=='measurement' for j in jobs[:31]));self.assertTrue(all(j['kind']=='measurement' for j in jobs[31:]))
        self.assertEqual(sum(j['kind'] not in c.SUCCESS for j in jobs),17)
        for scenario in c.SHAPES:
            orders=[]
            for rep in range(5):
                group=[j for j in jobs if j['kind']=='measurement' and j['scenario']==scenario and j['repetition']==rep]
                self.assertEqual({j['sdk'] for j in group},set(c.SDKS));orders.append(tuple(j['sdk'] for j in group))
                positions=[jobs.index(j) for j in group];self.assertEqual(positions,list(range(min(positions),min(positions)+3)))
            self.assertEqual(len(set(orders)),5)
        self.assertEqual(jobs,c.schedule(copy.deepcopy(self.plan),'physical'))

    def test_all_successful_modes_and_reference_totals(self):
        for physical in (False,True):
            r=self.context(physical);c.validate_inputs(r['readiness'],r['llgl_correctness'],r['mode'])
            values=[]
            for j in c.schedule(self.plan,r['mode']):
                if j['kind'] in c.SUCCESS:values.append(c.validate_output(self.output(j,r),j,r))
            self.assertEqual((sum(v['verified_images'] for v in values),sum(v['verified_bytes'] for v in values)),c.FULL_TOTALS if physical else c.SMOKE_TOTALS)

    def test_software_never_exports_statistics_or_measurements(self):
        jobs=c.schedule(self.plan,'software-diagnostic');self.assertEqual(len(jobs),37)
        self.assertEqual(sum(j['kind']=='smoke' for j in jobs),6)
        self.assertEqual(c.summarize([dict(job=j) for j in jobs],'software-diagnostic'),[])
        with self.assertRaises(ValueError):c.summarize([dict(job=dict(kind='measurement'))],'software-diagnostic')

    def test_measured_processes_are_observer_and_validation_free(self):
        r=self.context(True)
        for j in c.schedule(self.plan,'physical'):
            env=c.environment(j,r);args=c.arguments(j,r)
            if j['kind']=='measurement':
                self.assertNotIn('LD_DEBUG',env);self.assertNotIn('VK_INSTANCE_LAYERS',env);self.assertEqual(c.options(j)[2:],(5,30))
                if j['sdk']=='llgl':self.assertEqual(args[2],'plain')
            if j['sdk']=='llgl' and j['kind']=='validation':
                self.assertEqual(args[2],'validation');self.assertEqual(env['VK_INSTANCE_LAYERS'],'VK_LAYER_GFX_llgl_draw:VK_LAYER_KHRONOS_validation')
            if j['kind']=='audit':self.assertEqual(args[2],'observe');self.assertEqual(c.options(j)[2:],(1,3))

    def test_invalid_intervals_and_lifecycle_are_rejected(self):
        r=self.context(True)
        for sdk in c.SDKS:
            j=next(j for j in c.schedule(self.plan,'physical') if j['kind']=='measurement' and j['sdk']==sdk)
            for value in ([float('nan'),1],[0,1],[True,1],[1,-1],[1,float('inf')],[1]):
                v=self.output(j,r);v['samples_ms'][0]=value
                with self.subTest(sdk=sdk,value=value),self.assertRaises(ValueError):c.validate_output(v,j,r)
            for key,value in [('created_pipelines',1),('released_pipeline_handles',1),('used_pipeline_regions',1),('GPU_use_in_timing',True),('GPU_memory_reclamation_verified',True),('shader_header_sha256','f'*64)]:
                v=self.output(j,r);v[key]=value
                with self.subTest(sdk=sdk,key=key),self.assertRaises(ValueError):c.validate_output(v,j,r)

    def test_native_counts_full_hashes_and_plain_nulls(self):
        r=self.context();j=next(j for j in c.schedule(self.plan,r['mode']) if j['kind']=='audit' and j['scenario']=='pso8')
        for key,value in [('native_draw_calls',31),('native_forwarded_draw_calls',31),('native_vertices',95),('created_shader_handles',63),('frame_sha256',[]),('scissor',[0,0,65,37]),('native_pipeline_count_verified',True),('driver_cache_state_known',True)]:
            v=self.output(j,r);v[key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):c.validate_output(v,j,r)
        j=next(j for j in c.schedule(self.plan,r['mode']) if j['kind']=='smoke' and j['sdk']=='llgl')
        v=self.output(j,r);v['native_draw_calls']=4
        with self.assertRaises(ValueError):c.validate_output(v,j,r)

    def test_exact_native_failures_required(self):
        r=self.context();tokens={'wrong-uuid':b'Exact UUID absent or ambiguous','wrong-kind':c.correctness.policy.wrong_kind_message(r['mode']),
            'missing-layer':b'Required comparison validation layer absent','sync-disabled':b'Expected validation control diagnostic absent',
            'missing-pipeline':b'Public pipeline creation count differs','skip-release':b'Public pipeline release count differs',
            'wrong-pipeline':b'Full changing PSO stripe reference mismatch','skip-region':b'Native PSO draw count/arguments differ',
            'wrong-scissor':b'Full changing PSO stripe reference mismatch','corrupt-readback':b'Full changing PSO stripe reference mismatch',
            'observer-disabled':b'Required runtime module absent or ambiguous'}
        for j in c.schedule(self.plan,r['mode']):
            if j['sdk']!='llgl' or j['kind'] in c.SUCCESS:continue
            token=tokens[j['kind']];c.rejected(j,1,b'',token,r)
            for code,out,err in [(True,b'',token),(0,b'',token),(1,b'{}',token),(1,b'',b'unrelated')]:
                with self.assertRaises(ValueError):c.rejected(j,code,out,err,r)

    def test_controlled_o2_and_non_guard_same_revision_prerequisites(self):
        for mutate in (lambda r:r['readiness'].pop('controlled_build'),lambda r:r['llgl_correctness'].update(source_commit='b'*40),lambda r:r['llgl_correctness'].update(physical_guard_only=True),lambda r:r['llgl_correctness'].update(physical_pso_correctness_verified=False)):
            r=self.context(True);mutate(r)
            with self.assertRaises(ValueError):c.validate_inputs(r['readiness'],r['llgl_correctness'],'physical')
        r=self.context()
        with self.assertRaises(ValueError):c.validate_inputs(r['readiness'],r['llgl_correctness'],'physical')

    def test_both_statistics_columns_keep_five_processes_and_outliers(self):
        rows=[dict(job=j,result=dict(samples_ms=[[i+1,0.5] for i in range(30)])) for j in c.schedule(self.plan,'physical') if j['kind']=='measurement']
        groups=c.summarize(rows,'physical');self.assertEqual(len(groups),12)
        for g in groups:self.assertEqual((g['statistics']['processes'],g['statistics']['samples'],g['statistics']['samples_removed']),(5,150,0))
        rows[0]['result']['samples_ms'][0][0]=10000
        self.assertTrue(any(g['statistics']['max_ms']==10000 for g in c.summarize(rows,'physical')))
        for bad in (rows[:-1],rows+[rows[0]]):
            with self.assertRaises(ValueError):c.summarize(bad,'physical')
        rows[0]['result']['samples_ms'][0][1]=float('nan')
        with self.assertRaises(ValueError):c.summarize(rows,'physical')

    def test_shared_execution_checks_nested_intervals_before_following_measurement(self):
        r=self.context();j=next(j for j in c.schedule(self.plan,r['mode']) if j['kind']=='smoke' and j['sdk']=='llgl');v=self.output(j,r)
        row=dict(returncode=0,started=0,finished=30);c.validate_success(row,json.dumps(v).encode(),b'',j,r)
        row['finished']=0.001
        with self.assertRaises(ValueError):c.validate_success(row,json.dumps(v).encode(),b'',j,r)
        row['finished']=30
        with self.assertRaises(ValueError):c.validate_success(row,json.dumps(v).encode(),b'VkLayer_ activated',j,r)

    def test_scope_cannot_promote_software_or_change_counts_and_shader(self):
        for physical in (False,True):
            r=self.context(physical);r.update(schema_version=1,task=c.TASK,status='completed',inputs_unchanged=True,
                campaign_kind='physical-campaign' if physical else 'software-smoke',performance_accepted=physical,
                measurement_samples=900 if physical else 0,diagnostic_rows=24 if physical else 42,
                physical_GPU_accepted=False,P3_LBEN_03_completed=False,historical_timings_reused=False,overall_SDK_ranking=False,
                GPU_timestamp_measurement=False,SDL_native_handle_verified=False,SDK_SDL_measurement_module_snapshot_observed=False)
            c.validate_scope(r)
            for key,value in [('performance_accepted',not physical),('measurement_samples',True),('diagnostic_rows',0),('shader_header_sha256','f'*64),('P3_LBEN_03_completed',True),('SDK_SDL_measurement_module_snapshot_observed',True)]:
                bad=copy.deepcopy(r);bad[key]=value
                with self.subTest(key=key),self.assertRaises(ValueError):c.validate_scope(bad)

    def test_runtime_layer_contamination_is_rejected(self):
        r=self.context(True);j=next(j for j in c.schedule(self.plan,'physical') if j['kind']=='measurement' and j['sdk']=='llgl')
        for path in (r['observer']['path'],c.shared.qualification(r)['validation_library'],'/tmp/inject.so'):
            v=self.output(j,r);v['runtime_modules']=sorted(v['runtime_modules']+[path])
            with self.assertRaises(ValueError):c.validate_output(v,j,r)


if __name__=='__main__':unittest.main()

