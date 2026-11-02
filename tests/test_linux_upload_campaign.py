"""Linux upload timer/campaign contracts. Synthetic fixtures are not GPU evidence."""
import copy
import hashlib
import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'tools'))
import linux_upload_campaign as c


class UploadCampaign(unittest.TestCase):
    def setUp(self):
        self.plan=c.read(c.ROOT/c.PLAN)

    def context(self,physical=False):
        ready=c.read(c.ROOT/'benchmarks/reports/validation/2026-10-30-phase3-linux-workload-matrix/native/readiness/receipt.json')
        llgl=c.read(c.ROOT/'benchmarks/reports/validation/2026-10-30-phase3-linux-llgl-upload/native/receipt.json')
        llgl['source_commit']=ready['source_commit'];llgl['probe']['qualification']=copy.deepcopy(ready['qualification'])
        mode='physical' if physical else 'software-diagnostic'
        if physical:
            a=ready['qualification'];d=a['selected'];d['device_type']=2
            a.update(mode='physical',physical_probe_qualified=True,P3_BEN_07_completed=True,inventory=[copy.deepcopy(d)],
                pci=[dict(vendor_id=d['vendor_id'],device_id=d['device_id'],driver='amdgpu')])
            a['controlled_build']=dict(name=c.build_policy.NAME,projects={k:{} for k in ('sdk','sdl','probe')})
            ready['controlled_build']=dict(name=c.build_policy.NAME,projects=dict(consumer={}))
            llgl['probe']['qualification']=copy.deepcopy(a)
            llgl['probe'].update(mode='physical',physical_guard_only=False,physical_probe_qualified=True,P3_BEN_08_completed=False)
            llgl.update(mode='physical',physical_guard_only=False,physical_upload_correctness_verified=True,software_upload_correctness_verified=False,P3_BEN_08_completed=False)
            llgl['adaptation']['mode']='physical'
        return dict(mode=mode,readiness=ready,llgl_correctness=llgl,plan=copy.deepcopy(self.plan),
            output_directory='/out/campaign',binary=dict(path='/build/linux_upload_compare'),observer=dict(path='/build/libVkLayer_gfx_llgl_draw.so'),
            crypto=dict(path='/usr/lib/libcrypto.so.3'),adaptation=dict(source_fingerprint='a'*64),reference=c.read(c.ROOT/c.lu.REFERENCE))

    def output(self,job,r):
        validation,observed,warmup,samples=c.options(job);a=c.qualification(r);d=a['selected']
        control=job['kind'] in c.lu.TOKENS;probe=job['kind'] in ('probe','observe');special=control or probe
        shape=[0,0,0] if special else c.SHAPES[job['scenario']];images=0 if special else warmup+samples
        env=copy.deepcopy(a['environment'])
        v=dict(schema_version=1,sdk=job['sdk'],api='vulkan',configuration='Release',category='end_to_end',
            contract='vulkan_upload_cpu_payload_to_texture_fence_complete_each_sample_readback_v1',scenario=job['scenario'],
            validation=validation,probe=probe,warmup=warmup,sample_count=samples,gpu_ms=None,
            device_name=d['name'],device_uuid=d['device_uuid'],driver_uuid=d['driver_uuid'],device_luid=None,
            native_vendor_id=d['vendor_id'],native_device_id=d['device_id'],native_driver_version=d['driver_version_raw'],native_api_version=d['api_version_raw'],
            width=shape[0],height=shape[1],texel_bytes=shape[2],verified_images=images,verified_bytes=c.math.prod(shape)*images,readback_verified=not special,
            loaded_validation_layer=a['validation_library'] if validation else '',environment=env,samples_ms=[1.5+i/10 for i in range(samples)])
        if job['sdk']!='llgl':
            env.update(gpu=d['name'],software_renderer=r['mode']=='software-diagnostic',device_uuid=d['device_uuid'],driver_uuid=d['driver_uuid'],device_luid=None)
            v.update(preference='high',source_fingerprint=r['readiness']['profile_fingerprints']['upload'],SDK_source_fingerprint=a['SDK_source_fingerprint'],
                SDL_commit=a['SDL_commit'],identity_attribution=c.w.ATTRIBUTION)
        else:
            prior=r['llgl_correctness']['probe'];native=next(x['result'] for x in prior['runs'] if x['job']['kind']=='normal')
            modules={x['path'] for x in a['runtime_libraries']}|{prior['binaries'][n]['path'] for n in ('core','backend')}|{r['crypto']['path']}
            if validation:modules.add(a['validation_library'])
            if observed:modules.add(r['observer']['path'])
            v.update(task='P3-LBEN-01-upload-campaign',mode=r['mode'],profile_id=c.p.PROFILE,LLGL_commit=prior['LLGL_commit'],
                source_fingerprint=r['adaptation']['source_fingerprint'],identity_attribution='actual_LLGL_native_handles_and_native_UUID',
                native_handle_verified=True,queue_family=native['queue_family'],queue_index=0,queue_flags=native['queue_flags'],loader_api_version=native['loader_api_version'],
                loader_path=a['loader_path'],LLGL_path=prior['binaries']['core']['path'],backend_path=prior['binaries']['backend']['path'],
                control=control,control_diagnostic=c.lu.TOKENS[job['kind']] if control else '',submitted_control_work=0,delivered_errors=1 if control else 0,
                row_stride=0,layer_stride=0,native_workload_shortcuts=False,observer_enabled=observed,instance_API_request_observed=observed,
                observed_instance_api_version=native['loader_api_version'] if observed else 0,observer_live_instances_after_close=0,observer_live_devices_after_close=0,
                payload_sha256=r['reference']['hashes'][job['scenario']] if validation and not special else [],runtime_modules=sorted(modules))
        return v

    def test_frozen_plan_rejects_reduced_work_bool_aliases_or_new_fields(self):
        c.validate_plan(self.plan)
        for section,key,value in [('performance','samples',3),('performance','repetitions',1),('performance','warmup',True),
                                  ('smoke','statistics_exported',True),('validation','required_core_and_sync',False)]:
            p=copy.deepcopy(self.plan);p[section][key]=value
            with self.subTest(section=section,key=key),self.assertRaises(ValueError):c.validate_plan(p)
        p=copy.deepcopy(self.plan);p['extra']=True
        with self.assertRaises(ValueError):c.validate_plan(p)

    def test_full_schedule_separates_all_gates_and_keeps_five_distinct_sdk_orders(self):
        jobs=c.schedule(self.plan,'physical');self.assertEqual(len(jobs),90);self.assertEqual(len({j['name'] for j in jobs}),90)
        self.assertTrue(all(j['kind']!='measurement' for j in jobs[:30]));self.assertTrue(all(j['kind']=='measurement' for j in jobs[30:]))
        self.assertEqual(sum(j['kind'] not in c.SUCCESS for j in jobs),12)
        for scenario in c.SHAPES:
            orders=[]
            for rep in range(5):
                group=[j for j in jobs if j['kind']=='measurement' and j['scenario']==scenario and j['repetition']==rep]
                self.assertEqual({j['sdk'] for j in group},set(c.SDKS));orders.append(tuple(j['sdk'] for j in group))
                positions=[jobs.index(j) for j in group];self.assertEqual(positions,list(range(min(positions),min(positions)+3)))
            self.assertEqual(len(set(orders)),5)
        self.assertEqual(jobs,c.schedule(copy.deepcopy(self.plan),'physical'))

    def test_smoke_does_not_silently_run_or_summarize_full_measurements(self):
        jobs=c.schedule(self.plan,'software-diagnostic');self.assertEqual(len(jobs),42)
        self.assertEqual(sum(j['kind']=='smoke' for j in jobs),12);self.assertFalse(any(j['kind']=='measurement' for j in jobs))
        self.assertEqual(c.summarize([dict(job=j) for j in jobs],'software-diagnostic'),[])
        with self.assertRaises(ValueError):c.summarize([dict(job=dict(kind='measurement'))],'software-diagnostic')

    def test_physical_requires_same_revision_complete_qualified_correctness(self):
        for physical in (False,True):
            r=self.context(physical);c.validate_inputs(r['readiness'],r['llgl_correctness'],r['mode'])
        for action in (lambda r:r['llgl_correctness'].update(source_commit='b'*40),
                       lambda r:r['llgl_correctness']['probe']['qualification']['environment'].update(kernel='foreign'),
                       lambda r:r['llgl_correctness'].update(physical_guard_only=True),
                       lambda r:r['llgl_correctness'].update(physical_upload_correctness_verified=False)):
            r=self.context(True);action(r)
            with self.assertRaises(ValueError):c.validate_inputs(r['readiness'],r['llgl_correctness'],'physical')
        r=self.context()
        with self.assertRaises(ValueError):c.validate_inputs(r['readiness'],r['llgl_correctness'],'physical')
        for key in ('controlled_build','machine'):
            r=self.context(True);a=r['readiness']['qualification']
            if key=='controlled_build':a.pop(key)
            else:a['environment'][key]='aarch64'
            r['llgl_correctness']['probe']['qualification']=copy.deepcopy(a)
            with self.subTest(key=key),self.assertRaises(ValueError):c.validate_inputs(r['readiness'],r['llgl_correctness'],'physical')

    def test_measurement_environment_has_no_loader_trace_observer_or_validation(self):
        r=self.context(True)
        for j in c.schedule(self.plan,'physical'):
            env=c.environment(j,r)
            if j['kind']=='measurement':
                self.assertNotIn('LD_DEBUG',env);self.assertNotIn('VK_INSTANCE_LAYERS',env)
                self.assertEqual(env['VK_LAYER_PATH'].replace('\\','/'),'/out/campaign/empty')
                self.assertEqual(c.options(j)[2:],(5,30))
            if j['kind']=='observe':self.assertEqual(env['VK_INSTANCE_LAYERS'],'VK_LAYER_GFX_llgl_draw')
            if j['kind']=='validation':self.assertEqual(c.options(j)[2:],(1,3))
        for j in c.schedule(self.plan,'software-diagnostic'):
            if j['kind']=='smoke':self.assertNotIn('LD_DEBUG',c.environment(j,self.context()))

    def test_exact_uuid_and_wrong_kind_controls_never_fallback(self):
        for physical in (False,True):
            r=self.context(physical)
            for j in c.schedule(self.plan,r['mode']):
                args=c.arguments(j,r);env=c.environment(j,r)
                if j['kind']=='wrong-uuid':
                    self.assertEqual(args[1] if j['sdk']=='llgl' else env['GFX_COMPARISON_UUID'],'0'*32)
                if j['kind']=='wrong-kind':
                    self.assertEqual(env['GFX_LLGL_UPLOAD_COMPARE_KIND' if j['sdk']=='llgl' else 'GFX_COMPARISON_MODE'],
                                     'software-diagnostic' if physical else 'physical')

    def test_all_successful_output_shapes_modes_and_controls_match(self):
        for physical in (False,True):
            r=self.context(physical)
            for j in c.schedule(self.plan,r['mode']):
                if j['kind'] in c.SUCCESS:c.validate_output(self.output(j,r),j,r)

    def test_output_rejects_short_readback_identity_changes_and_timing_aliases(self):
        r=self.context(True)
        for sdk in c.SDKS:
            j=next(j for j in c.schedule(self.plan,'physical') if j['kind']=='measurement' and j['sdk']==sdk)
            for key,value in [('verified_images',True),('verified_bytes',0),('device_uuid','0'*32),('driver_uuid','b'*32),
                              ('sample_count',3),('warmup',1),('samples_ms',[1]*29),('samples_ms',[True]*30),('samples_ms',[float('nan')]*30),
                              ('loaded_validation_layer','foreign'),('source_fingerprint','foreign')]:
                v=self.output(j,r);v[key]=value
                with self.subTest(sdk=sdk,key=key),self.assertRaises(ValueError):c.validate_output(v,j,r)

    def test_llgl_observer_and_native_runtime_are_separate_from_measurements(self):
        r=self.context(True)
        for kind in ('observe','measurement','validation'):
            j=next(j for j in c.schedule(self.plan,'physical') if j['kind']==kind and j['sdk']=='llgl')
            for key,value in [('native_handle_verified',False),('queue_index',True),('runtime_modules',[]),('observer_live_devices_after_close',1)]:
                v=self.output(j,r);v[key]=value
                with self.subTest(kind=kind,key=key),self.assertRaises(ValueError):c.validate_output(v,j,r)
        j=next(j for j in c.schedule(self.plan,'physical') if j['kind']=='measurement' and j['sdk']=='llgl')
        for path in (r['observer']['path'],c.qualification(r)['validation_library'],'/tmp/inject.so'):
            v=self.output(j,r);v['runtime_modules']=sorted(v['runtime_modules']+[path])
            with self.assertRaises(ValueError):c.validate_output(v,j,r)

    def test_full_validation_hashes_and_specific_host_control_are_required(self):
        r=self.context();j=next(j for j in c.schedule(self.plan,r['mode']) if j['kind']=='validation' and j['sdk']=='llgl')
        v=self.output(j,r);v['payload_sha256']=v['payload_sha256'][:-1]
        with self.assertRaises(ValueError):c.validate_output(v,j,r)
        j=next(j for j in c.schedule(self.plan,r['mode']) if j['kind']=='core-control')
        for key,value in [('control_diagnostic',''),('delivered_errors',0),('submitted_control_work',1)]:
            v=self.output(j,r);v[key]=value
            with self.assertRaises(ValueError):c.validate_output(v,j,r)

    def test_stats_retain_all_five_processes_all_samples_and_outliers(self):
        jobs=c.schedule(self.plan,'physical');rows=[]
        for j in jobs:
            if j['kind']=='measurement':rows.append(dict(job=j,result=dict(samples_ms=[30*j['repetition']+i+1 for i in range(30)])))
        groups=c.summarize(rows,'physical');self.assertEqual(len(groups),12)
        for group in groups:
            s=group['statistics'];self.assertEqual((s['processes'],s['samples'],s['samples_removed']),(5,150,0))
            self.assertEqual(s['p50_ms'],75.5);self.assertAlmostEqual(s['p95_ms'],142.55)
            self.assertEqual(s['process_medians_ms'],[15.5,45.5,75.5,105.5,135.5])
        with self.assertRaises(ValueError):c.summarize(rows[:-1],'physical')
        with self.assertRaises(ValueError):c.summarize(rows+[rows[0]],'physical')
        rows[0]['result']['samples_ms'][-1]=10000
        changed=rows[0]['job']
        group=next(x for x in c.summarize(rows,'physical') if x['scenario']==changed['scenario'] and x['sdk']==changed['sdk'])
        self.assertEqual(group['statistics']['max_ms'],10000)

    def test_port_preserves_exact_timed_loop_and_public_upload_body(self):
        sources={n:(c.ROOT/n).read_text(encoding='utf-8') for n in c.port.OWN}
        for mode in ('software-diagnostic','physical'):
            generated,_=c.port.translate(sources,mode);old=sources['benchmarks/llgl_vulkan_upload_compare/main.cpp']
            a=old.index('        for(unsigned i=0;');b=old.index('upload.close();}',a)+len('upload.close();}')
            self.assertIn(old[a:b],generated['main.cpp']);self.assertIn('bench::Clock::now()',generated['main.cpp'])
            self.assertNotIn('GetModuleHandle',str(generated));self.assertIn('observerPresent()',generated['main.cpp'])
        bad=copy.deepcopy(sources);bad['benchmarks/llgl_vulkan_upload_compare/main.cpp']=old.replace('auto env=bench::environment(gpu);','changed')
        with self.assertRaises(ValueError):c.port.translate(bad,'physical')

    def test_generation_fingerprint_is_mode_bound_readonly_and_confined(self):
        before=[c.sha(c.ROOT/n) for n in c.port.OWN]
        with tempfile.TemporaryDirectory(dir=c.ROOT/'out') as td:
            out=pathlib.Path(td);a=c.port.generate(out/'soft','software-diagnostic');b=c.port.generate(out/'physical','physical')
            self.assertNotEqual(a['source_fingerprint'],b['source_fingerprint']);self.assertFalse(a['timed_loop_changed'])
            self.assertEqual(b,c.port.generate(out/'physical','physical'))
        self.assertEqual(before,[c.sha(c.ROOT/n) for n in c.port.OWN])
        with self.assertRaises(ValueError):c.port.generate(c.ROOT/'forbidden','physical')

    def test_scope_cannot_promote_software_or_use_boolean_counters(self):
        for physical in (False,True):
            r=self.context(physical)
            r.update(schema_version=1,task='P3-LBEN-01-upload-campaign',status='completed',inputs_unchanged=True,
                campaign_kind='physical-campaign' if physical else 'software-smoke',performance_accepted=physical,
                measurement_samples=1800 if physical else 0,diagnostic_rows=36 if physical else 72,
                physical_GPU_accepted=False,P3_LBEN_01_completed=False,historical_timings_reused=False,overall_SDK_ranking=False,
                GPU_timestamp_measurement=False,SDL_native_handle_verified=False,SDK_SDL_measurement_module_snapshot_observed=False)
            c.validate_scope(r)
            for key,value in [('measurement_samples',True),('physical_GPU_accepted',True),('P3_LBEN_01_completed',True),
                              ('SDL_native_handle_verified',True),('SDK_SDL_measurement_module_snapshot_observed',True),
                              ('performance_accepted',not physical),('overall_SDK_ranking',True)]:
                bad=copy.deepcopy(r);bad[key]=value
                with self.subTest(physical=physical,key=key),self.assertRaises(ValueError):c.validate_scope(bad)

    def test_expected_full_readbacks_include_warmups_and_both_run_roles(self):
        for physical,images,total in ((False,96,2588083680),(True,2148,57908372340)):
            r=self.context(physical);values=[self.output(j,r) for j in c.schedule(self.plan,r['mode']) if j['kind'] in c.SUCCESS]
            self.assertEqual(sum(v['verified_images'] for v in values),images)
            self.assertEqual(sum(v['verified_bytes'] for v in values),total)

    def test_native_diagnostics_are_rejected_before_later_processes_can_run(self):
        r=self.context(True);jobs=c.schedule(self.plan,'physical');row=dict(returncode=0,started=0,finished=10)
        j=next(j for j in jobs if j['kind']=='validation' and j['sdk']=='llgl')
        raw=c.json.dumps(self.output(j,r)).encode();c.validate_success(row,raw,b'',j,r)
        with self.assertRaises(ValueError):c.validate_success(row,raw,b'Validation Error: injected',j,r)
        j=next(j for j in jobs if j['kind']=='measurement' and j['sdk']=='sdl');raw=c.json.dumps(self.output(j,r)).encode()
        with self.assertRaises(ValueError):c.validate_success(row,raw,b'VkLayer_khronos_validation activated',j,r)
        with self.assertRaises(ValueError):c.validate_success(dict(row,finished=0.001),raw,b'',j,r)
        j=next(j for j in jobs if j['kind']=='core-control');raw=c.json.dumps(self.output(j,r)).encode()
        with self.assertRaises(ValueError):c.validate_success(row,raw,b'',j,r)


if __name__=='__main__':unittest.main()
