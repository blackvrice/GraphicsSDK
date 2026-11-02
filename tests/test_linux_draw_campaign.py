"""Draw timing/observation contracts; synthetic metadata is never GPU evidence."""
import copy
import json
import pathlib
import sys
import tempfile
import unittest
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'tools'))
import linux_draw_campaign as c
import test_linux_upload_campaign as upload_fixture


class DrawCampaign(unittest.TestCase):
    def setUp(self):self.plan=c.read(c.ROOT/c.PLAN)

    def context(self,physical=False):
        fixture=upload_fixture.UploadCampaign();fixture.setUp();r=fixture.context(physical)
        correct=c.read(c.ROOT/'benchmarks/reports/validation/2026-11-01-phase3-linux-llgl-draw/native/receipt.json')
        correct['probe']=r['llgl_correctness']['probe'];correct['source_commit']=r['readiness']['source_commit']
        if physical:
            correct.update(mode='physical',physical_guard_only=False,physical_draw_correctness_verified=True,software_draw_correctness_verified=False,P3_BEN_08_completed=False)
            correct['adaptation']['mode']='physical'
        r.update(llgl_correctness=correct,plan=copy.deepcopy(self.plan),reference=c.reference(),
            shader_header_sha256=r['readiness']['shader_header_sha256'])
        return r

    def output(self,job,r):
        validation,observed,warmup,samples=c.options(job);a=c.shared.qualification(r);d=a['selected'];kind=job['kind']
        control=kind in c.correctness.TOKENS;probe=kind in ('probe','observe');special=control or probe
        calls,instances=(0,0) if special else c.WORK[job['scenario']];images=0 if special else warmup+samples
        intervals=[[1+i,2+i,3+i,6+3*i] for i in range(samples)]
        v=dict(schema_version=1,sdk=job['sdk'],api='vulkan',configuration='Release',category='end_to_end',
            contract='vulkan_scissored_fullscreen_triangle_draw_fence_each_sample_readback_v1',scenario=job['scenario'],
            validation=validation,probe=probe,warmup=warmup,sample_count=samples,gpu_ms=None,
            device_name=d['name'],device_uuid=d['device_uuid'],driver_uuid=d['driver_uuid'],device_luid=None,
            native_vendor_id=d['vendor_id'],native_device_id=d['device_id'],native_driver_version=d['driver_version_raw'],native_api_version=d['api_version_raw'],
            width=65,height=37,texel_bytes=4,scissor=[5,3,55,31],vertices_per_instance=3,first_vertex=0,first_instance=0,
            draw_calls_per_frame=calls,instances_per_call=instances,vertices_per_frame=3*calls*instances,submitted_draw_calls=images*calls,
            verified_images=images,verified_bytes=9620*images,readback_verified=not special,shader_header_sha256=r['shader_header_sha256'],columns=c.COLUMNS,
            loaded_validation_layer=a['validation_library'] if validation else '',environment=copy.deepcopy(a['environment']),
            samples_ms=[row[3] for row in intervals],CPU_intervals_ms=intervals)
        if job['sdk']!='llgl':
            v.update(preference='high',source_fingerprint=r['readiness']['profile_fingerprints']['draw'],SDK_source_fingerprint=a['SDK_source_fingerprint'],
                SDL_commit=a['SDL_commit'],identity_attribution=c.shared.w.ATTRIBUTION,requested_instance_api_version=4198400)
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
                requested_instance_api_version=None,clear_strategy='automatic_offscreen_pass_plus_full_attachment_clear',
                texture_bind_policy='color_attachment_sampled_copy_src',frame_sha256=r['reference'] if observed and not special else [],runtime_modules=sorted(modules))
            counts=dict(native_draw_calls=images*calls,native_forwarded_draw_calls=images*calls,native_instances=images*calls*instances,
                native_vertices=3*images*calls*instances,native_bad_arguments=0,native_min_instances=instances if images else 0,native_max_instances=instances if images else 0)
            v.update({k:x if observed else None for k,x in counts.items()})
        return v

    def test_plan_preserves_frozen_draw_geometry_intervals_and_repetitions(self):
        c.validate_plan(self.plan)
        for mutate in (lambda p:p['performance'].update(samples=3),lambda p:p['performance'].update(repetitions=True),
                       lambda p:p['geometry']['scissor'].__setitem__(0,6),lambda p:p['work']['draw1000'].__setitem__(0,999),
                       lambda p:p.update(columns=['total_ms']),lambda p:p.update(GPU_timestamp_measurement=True)):
            p=copy.deepcopy(self.plan);mutate(p)
            with self.assertRaises(ValueError):c.validate_plan(p)

    def test_schedule_gates_all_measurements_and_uses_distinct_adjacent_orders(self):
        jobs=c.schedule(self.plan,'physical');self.assertEqual(len(jobs),78);self.assertEqual(len({j['name'] for j in jobs}),78)
        self.assertFalse(any(j['kind']=='measurement' for j in jobs[:33]));self.assertTrue(all(j['kind']=='measurement' for j in jobs[33:]))
        self.assertEqual(sum(j['kind'] not in c.SUCCESS for j in jobs),15)
        for scenario in c.SHAPES:
            orders=[]
            for rep in range(5):
                group=[j for j in jobs if j['kind']=='measurement' and j['scenario']==scenario and j['repetition']==rep]
                self.assertEqual({j['sdk'] for j in group},set(c.SDKS));orders.append(tuple(j['sdk'] for j in group))
                pos=[jobs.index(j) for j in group];self.assertEqual(pos,list(range(min(pos),min(pos)+3)))
            self.assertEqual(len(set(orders)),5)
        self.assertEqual(jobs,c.schedule(copy.deepcopy(self.plan),'physical'))

    def test_software_smoke_never_exports_statistics_or_measurements(self):
        jobs=c.schedule(self.plan,'software-diagnostic');self.assertEqual(len(jobs),42)
        self.assertEqual(sum(j['kind']=='smoke' for j in jobs),9)
        self.assertEqual(c.summarize([dict(job=j) for j in jobs],'software-diagnostic'),[])
        with self.assertRaises(ValueError):c.summarize([dict(job=dict(kind='measurement'))],'software-diagnostic')

    def test_all_positive_outputs_have_complete_geometry_and_native_observation(self):
        for physical in (False,True):
            r=self.context(physical);c.validate_inputs(r['readiness'],r['llgl_correctness'],r['mode'])
            for j in c.schedule(self.plan,r['mode']):
                if j['kind'] in c.SUCCESS:c.validate_output(self.output(j,r),j,r)

    def test_physical_requires_matched_o2_same_target_and_non_guard_correctness(self):
        for mutate in (lambda r:r['readiness'].pop('controlled_build'),lambda r:r['llgl_correctness'].update(source_commit='b'*40),
                       lambda r:r['llgl_correctness'].update(physical_guard_only=True),lambda r:r['llgl_correctness'].update(physical_draw_correctness_verified=False)):
            r=self.context(True);mutate(r)
            with self.assertRaises(ValueError):c.validate_inputs(r['readiness'],r['llgl_correctness'],'physical')
        r=self.context()
        with self.assertRaises(ValueError):c.validate_inputs(r['readiness'],r['llgl_correctness'],'physical')

    def test_measurements_have_no_observer_vvl_or_loader_trace(self):
        r=self.context(True)
        for j in c.schedule(self.plan,'physical'):
            env=c.environment(j,r);args=c.arguments(j,r)
            if j['kind']=='measurement':
                self.assertNotIn('LD_DEBUG',env);self.assertNotIn('VK_INSTANCE_LAYERS',env)
                self.assertEqual(c.options(j)[2:],(5,30))
                if j['sdk']=='llgl':self.assertEqual(args[2],'plain')
            if j['sdk']=='llgl' and j['kind']=='validation':
                self.assertEqual(args[2],'validation');self.assertEqual(env['VK_INSTANCE_LAYERS'],'VK_LAYER_GFX_llgl_draw:VK_LAYER_KHRONOS_validation')
            if j['kind']=='audit':self.assertEqual(args[2],'observe');self.assertEqual(c.options(j)[2:],(1,3))

    def test_intervals_must_partition_positive_totals_and_reject_nonfinite_bool(self):
        r=self.context(True)
        for sdk in c.SDKS:
            j=next(j for j in c.schedule(self.plan,'physical') if j['kind']=='measurement' and j['sdk']==sdk)
            for key,value in [('samples_ms',[True]*30),('samples_ms',[float('nan')]*30),('CPU_intervals_ms',[]),
                              ('CPU_intervals_ms',[[1,2,3,7]]*30),('CPU_intervals_ms',[[True,2,3,6]]*30),('CPU_intervals_ms',[[1,-1,3,3]]*30)]:
                v=self.output(j,r);v[key]=value
                with self.subTest(sdk=sdk,key=key),self.assertRaises(ValueError):c.validate_output(v,j,r)

    def test_identity_geometry_shader_and_full_readback_cannot_be_reduced(self):
        r=self.context(True)
        for sdk in c.SDKS:
            j=next(j for j in c.schedule(self.plan,'physical') if j['kind']=='measurement' and j['sdk']==sdk)
            for key,value in [('device_uuid','0'*32),('driver_uuid','f'*32),('shader_header_sha256','f'*64),('verified_images',True),
                              ('verified_bytes',0),('draw_calls_per_frame',999),('scissor',[6,3,55,31]),('first_vertex',True),('warmup',1)]:
                v=self.output(j,r);v[key]=value
                with self.subTest(sdk=sdk,key=key),self.assertRaises(ValueError):c.validate_output(v,j,r)

    def test_native_counts_are_observed_only_in_diagnostics(self):
        r=self.context(True)
        for kind in ('audit','validation','measurement'):
            j=next(j for j in c.schedule(self.plan,'physical') if j['kind']==kind and j['sdk']=='llgl')
            for key,value in [('native_draw_calls',0 if kind!='measurement' else 1),('native_instances',True),('native_bad_arguments',1),('observer_live_devices_after_close',1)]:
                v=self.output(j,r);v[key]=value
                with self.subTest(kind=kind,key=key),self.assertRaises(ValueError):c.validate_output(v,j,r)
        j=next(j for j in c.schedule(self.plan,'physical') if j['kind']=='audit')
        v=self.output(j,r);v['frame_sha256']=v['frame_sha256'][:-1]
        with self.assertRaises(ValueError):c.validate_output(v,j,r)

    def test_runtime_snapshot_rejects_layer_contamination_or_unknown_modules(self):
        r=self.context(True);j=next(j for j in c.schedule(self.plan,'physical') if j['kind']=='measurement' and j['sdk']=='llgl')
        for path in (r['observer']['path'],c.shared.qualification(r)['validation_library'],'/tmp/inject.so'):
            v=self.output(j,r);v['runtime_modules']=sorted(v['runtime_modules']+[path])
            with self.assertRaises(ValueError):c.validate_output(v,j,r)

    def test_specific_count_scissor_corruption_and_layer_failures_required(self):
        r=self.context(True)
        tokens={'wrong-uuid':b'Exact UUID absent or ambiguous','wrong-kind':b'Physical qualification only','missing-layer':b'Required comparison validation layer absent',
            'sync-disabled':b'Expected validation control diagnostic absent','wrong-call-count':b'Native draw count/arguments differ from contract',
            'wrong-instance-count':b'Native draw count/arguments differ from contract','wrong-scissor':b'Full changing draw reference mismatch',
            'corrupt-readback':b'Full changing draw reference mismatch','observer-disabled':b'Required runtime module absent or ambiguous'}
        for j in c.schedule(self.plan,'physical'):
            if j['sdk']!='llgl' or j['kind'] in c.SUCCESS:continue
            token=tokens[j['kind']];c.rejected(j,1,b'',token,r)
            for code,out,err in [(0,b'',token),(True,b'',token),(1,b'{}',token),(1,b'',b'unrelated')]:
                with self.subTest(kind=j['kind']),self.assertRaises(ValueError):c.rejected(j,code,out,err,r)

    def test_all_four_statistics_columns_retain_every_process_and_outlier(self):
        rows=[]
        for j in c.schedule(self.plan,'physical'):
            if j['kind']!='measurement':continue
            intervals=[[j['repetition']*30+i+1,2,3,j['repetition']*30+i+6] for i in range(30)]
            rows.append(dict(job=j,result=dict(CPU_intervals_ms=intervals,samples_ms=[x[3] for x in intervals])))
        groups=c.summarize(rows,'physical');self.assertEqual(len(groups),36)
        for g in groups:
            self.assertEqual((g['statistics']['processes'],g['statistics']['samples'],g['statistics']['samples_removed']),(5,150,0))
        groups_for_first=[g for g in groups if g['scenario']==rows[0]['job']['scenario'] and g['sdk']==rows[0]['job']['sdk']]
        self.assertEqual({g['column'] for g in groups_for_first},set(c.COLUMNS))
        rows[0]['result']['CPU_intervals_ms'][0][0]=10000;rows[0]['result']['CPU_intervals_ms'][0][3]=10005;rows[0]['result']['samples_ms'][0]=10005
        selected=next(g for g in c.summarize(rows,'physical') if g['scenario']==rows[0]['job']['scenario'] and g['sdk']==rows[0]['job']['sdk'] and g['column']=='total_ms')
        self.assertEqual(selected['statistics']['max_ms'],10005)
        with self.assertRaises(ValueError):c.summarize(rows[:-1],'physical')
        with self.assertRaises(ValueError):c.summarize(rows+[rows[0]],'physical')

    def test_reference_totals_include_audits_validation_and_warmups(self):
        for physical,expected in ((False,c.SMOKE_TOTALS),(True,c.FULL_TOTALS)):
            r=self.context(physical);values=[self.output(j,r) for j in c.schedule(self.plan,r['mode']) if j['kind'] in c.SUCCESS]
            self.assertEqual((sum(v['verified_images'] for v in values),sum(v['verified_bytes'] for v in values)),expected)

    def test_frozen_draw_and_complete_timed_loop_bytes_preserved(self):
        sources={n:(c.ROOT/n).read_text(encoding='utf-8') for n in c.port.OWN}
        for mode in ('physical','software-diagnostic'):
            outputs,_=c.port.translate(sources,mode);old=sources['benchmarks/llgl_vulkan_draw_compare/Draw.hpp']
            self.assertEqual(old[old.index('struct Draw {'):],outputs['Draw.hpp'][outputs['Draw.hpp'].index('struct Draw {'):])
            old=sources['benchmarks/llgl_vulkan_draw_compare/main.cpp'];a=old.index('        for(unsigned i=0;');b=old.index('adapter.close();}',a)+len('adapter.close();}')
            self.assertIn(old[a:b],outputs['main.cpp'])
        bad=copy.deepcopy(sources);bad['benchmarks/llgl_vulkan_draw_compare/Identity.hpp']=bad['benchmarks/llgl_vulkan_draw_compare/Identity.hpp'].replace('unsigned number(','changed(')
        with self.assertRaises(ValueError):c.port.translate(bad,'physical')

    def test_generation_is_mode_bound_confined_and_source_readonly(self):
        before=[c.sha(c.ROOT/n) for n in c.port.OWN]
        with tempfile.TemporaryDirectory(dir=c.ROOT/'out') as td:
            root=pathlib.Path(td);soft=c.port.generate(root/'soft','software-diagnostic');phys=c.port.generate(root/'physical','physical')
            self.assertNotEqual(soft['source_fingerprint'],phys['source_fingerprint']);self.assertEqual(phys,c.port.generate(root/'physical','physical'))
            self.assertFalse(phys['timed_loop_changed'])
        self.assertEqual(before,[c.sha(c.ROOT/n) for n in c.port.OWN])
        with self.assertRaises(ValueError):c.port.generate(c.ROOT/'forbidden','physical')

    def test_scope_cannot_promote_software_or_change_shader_and_measurement_type(self):
        for physical in (False,True):
            r=self.context(physical);r.update(schema_version=1,task=c.TASK,status='completed',inputs_unchanged=True,
                campaign_kind='physical-campaign' if physical else 'software-smoke',performance_accepted=physical,
                measurement_samples=1350 if physical else 0,diagnostic_rows=36 if physical else 63,
                physical_GPU_accepted=False,P3_LBEN_02_completed=False,historical_timings_reused=False,overall_SDK_ranking=False,
                GPU_timestamp_measurement=False,SDL_native_handle_verified=False,SDK_SDL_measurement_module_snapshot_observed=False)
            c.validate_scope(r)
            for key,value in [('performance_accepted',not physical),('measurement_samples',True),('diagnostic_rows',0),
                              ('P3_LBEN_02_completed',True),('shader_header_sha256','f'*64),('GPU_timestamp_measurement',True),
                              ('SDK_SDL_measurement_module_snapshot_observed',True)]:
                bad=copy.deepcopy(r);bad[key]=value
                with self.subTest(key=key,physical=physical),self.assertRaises(ValueError):c.validate_scope(bad)

    def test_shared_execution_dispatches_draw_validation_before_later_measurements(self):
        r=self.context(True);row=dict(returncode=0,started=0,finished=30)
        j=next(j for j in c.schedule(self.plan,'physical') if j['kind']=='audit');v=self.output(j,r)
        c.validate_success(row,json.dumps(v).encode(),b'',j,r)
        v['native_draw_calls']=999
        with self.assertRaises(ValueError):c.validate_success(row,json.dumps(v).encode(),b'',j,r)
        j=next(j for j in c.schedule(self.plan,'physical') if j['kind']=='measurement' and j['sdk']=='llgl');v=self.output(j,r)
        with self.assertRaises(ValueError):c.validate_success(row,json.dumps(v).encode(),b'VkLayer_ activated',j,r)
        v['CPU_intervals_ms'][0][0]=1000
        with self.assertRaises(ValueError):c.validate_success(row,json.dumps(v).encode(),b'',j,r)


if __name__=='__main__':unittest.main()
