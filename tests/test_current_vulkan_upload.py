import copy
import json
import pathlib
import sys
import unittest

sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'tools'))
import run_current_vulkan_upload as v
import test_vulkan_upload as fixture


class CurrentUpload(unittest.TestCase):
    def setUp(self):
        self.plan=v.read(v.PLAN)
        self.context=dict(expected_gpu='GPU',SDL_commit='c'*40,SDK_source_fingerprint='b'*64,
                          profile_fingerprints={'upload':'a'*64},validation_library='C:/runtime/VkLayer_khronos_validation.dll')

    def result(self,scenario='odd',validation=False):
        f=fixture.VulkanUploadContracts('test_matching_completed_payload');f.setUp();r=copy.deepcopy(f.r)
        r['native_vendor_id']=r['environment']['vendor_id']=4318
        self.context['identity']={k:copy.deepcopy(r[k]) for k in v.protocol.MODULES['upload'].IDENTITY_KEYS}
        mode=self.plan['validation' if validation else 'performance'];w,h,bpp=v.SHAPES[scenario]
        r.update(scenario=scenario,width=w,height=h,texel_bytes=bpp,warmup=mode['warmup'],sample_count=mode['samples'],validation=validation,
                 verified_images=mode['warmup']+mode['samples'],verified_bytes=w*h*bpp*(mode['warmup']+mode['samples']),
                 samples_ms=[float(i+1) for i in range(mode['samples'])],loaded_validation_layer=self.context['validation_library'] if validation else '')
        return r

    def job(self,scenario='odd',sdk='sdl',kind='measurement',repetition=0):
        return dict(profile='upload',scenario=scenario,sdk=sdk,kind=kind,repetition=repetition)

    def validate(self,r,validation=False):
        return v.protocol.validate_output(self.job(r['scenario'],kind='validation' if validation else 'measurement'),json.dumps(r).encode(),
               b'VK_LAYER_KHRONOS_validation VkLayer_khronos_validation.dll' if validation else b'',self.context,self.plan)

    def rows(self):
        return [dict(job=self.job(s,sdk,repetition=rep),result=self.result(s)) for s in self.plan['scenarios'] for sdk in v.SDKS for rep in range(5)]

    def test_full_upload_scope_and_sample_budget(self):
        v.validate_plan(self.plan);jobs=v.schedule(self.plan)
        self.assertEqual(len(jobs),52);self.assertEqual(sum(j['kind']=='measurement' for j in jobs)*30,1200)

    def test_all_validation_and_controls_precede_timing(self):
        jobs=v.schedule(self.plan);self.assertEqual([j['kind'] for j in jobs[:8]],['validation']*8)
        self.assertTrue(all(j['kind'] in ('missing-layer','wrong-target') for j in jobs[8:12]))
        self.assertTrue(all(j['kind']=='measurement' for j in jobs[12:]))

    def test_all_four_shapes_have_five_independent_repetitions(self):
        jobs=[j for j in v.schedule(self.plan) if j['kind']=='measurement']
        for s in self.plan['scenarios']:
            for sdk in v.SDKS:self.assertEqual({j['repetition'] for j in jobs if j['scenario']==s and j['sdk']==sdk},set(range(5)))

    def test_adjacent_sdk_pairs_alternate_and_order_is_repeatable(self):
        a=v.schedule(self.plan);self.assertEqual(a,v.schedule(copy.deepcopy(self.plan)));self.assertEqual(len({j['name'] for j in a}),52)
        rows=a[12:]
        for i in range(0,len(rows),2):
            x,y=rows[i:i+2];self.assertEqual((x['scenario'],x['repetition']),(y['scenario'],y['repetition']));self.assertEqual({x['sdk'],y['sdk']},set(v.SDKS))
        for s in self.plan['scenarios']:
            order=[j['sdk'] for j in rows if j['scenario']==s]
            self.assertEqual(order[0],order[4]);self.assertNotEqual(order[0],order[2])

    def test_scope_does_not_drop_odd_or_half_float(self):
        for scenario in ('odd','upload4k16f'):
            p=copy.deepcopy(self.plan);p['scenarios'].remove(scenario)
            with self.subTest(scenario=scenario),self.assertRaises(ValueError):v.validate_plan(p)

    def test_sample_counts_types_and_order_are_frozen(self):
        for section,key,value in [('performance','samples',3),('performance','warmup',False),('performance','repetitions',3),('performance','seed',0),('validation','warmup',True),('validation','required_core_and_sync',False)]:
            p=copy.deepcopy(self.plan);p[section][key]=value
            with self.subTest(section=section,key=key),self.assertRaises(ValueError):v.validate_plan(p)

    def test_no_timestamp_or_boundary_promotion(self):
        for key,value in [('GPU_timestamp_measurement',True),('includes',['payload_generation']),('excludes',[]),('category','gpu_timestamp')]:
            p=copy.deepcopy(self.plan);p['timing'][key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):v.validate_plan(p)

    def test_cannot_accept_other_workloads_or_reuse_history(self):
        for key,value in [('completed_workload_tasks',['P3-BEN-03','P3-BEN-04']),('historical_timings_reused',True),('overall_SDK_ranking',True),('SDL_native_handle_verified',True)]:
            p=copy.deepcopy(self.plan);p[key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):v.validate_plan(p)

    def test_full_image_each_shape_and_mode(self):
        for scenario in self.plan['scenarios']:
            for validation in (False,True):
                r=self.result(scenario,validation)
                with self.subTest(scenario=scenario,validation=validation):self.assertEqual(self.validate(r,validation),r)

    def test_incomplete_large_or_half_float_readback_rejected(self):
        for scenario in ('upload1080','upload4k','upload4k16f'):
            r=self.result(scenario);r['verified_bytes']-=1
            with self.subTest(scenario=scenario),self.assertRaises(ValueError):self.validate(r)

    def test_wrong_format_dimensions_sources_or_gpu_rejected(self):
        for key,value in [('texel_bytes',4),('width',1920),('source_fingerprint','f'*64),('SDK_source_fingerprint','f'*64),('native_driver_version',999)]:
            r=self.result('upload4k16f');r[key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):self.validate(r)

    def test_validation_timing_contamination_rejected(self):
        r=self.result();r['loaded_validation_layer']=self.context['validation_library']
        with self.assertRaises(ValueError):self.validate(r)

    def test_warmup_and_reference_generation_not_samples(self):
        r=self.result();r['samples_ms'] += [0.1]*5
        with self.assertRaises(ValueError):self.validate(r)

    def test_group_statistics_are_all_150_raw_samples(self):
        groups=v.summarize(self.rows(),self.plan);self.assertEqual(len(groups),8)
        for g in groups:
            s=g['statistics'];self.assertEqual((s['processes'],s['samples'],s['samples_removed']),(5,150,0));self.assertEqual(s['p50_ms'],15.5);self.assertEqual(s['MAD_ms'],7.5)
            self.assertEqual(s['process_medians_ms'],[15.5]*5)

    def test_validation_values_excluded_from_statistics(self):
        rows=self.rows();r=self.result(validation=True);r['samples_ms']=[999999]*3;rows.append(dict(job=self.job(kind='validation'),result=r))
        self.assertTrue(all(g['statistics']['max_ms']==30 for g in v.summarize(rows,self.plan)))

    def test_duplicate_or_missing_process_repetition_rejected(self):
        rows=self.rows();rows[0]['job']['repetition']=1
        with self.assertRaises(ValueError):v.summarize(rows,self.plan)
        rows=self.rows();rows.pop()
        with self.assertRaises(ValueError):v.summarize(rows,self.plan)

    def test_nonpositive_boolean_nonfinite_values_rejected(self):
        for bad in (0,-1,True,float('nan'),float('inf')):
            rows=self.rows();rows[0]['result']['samples_ms'][0]=bad
            with self.subTest(bad=bad),self.assertRaises(ValueError):v.summarize(rows,self.plan)

    def test_controls_and_workload_arguments(self):
        prepared=dict(binaries=dict(upload=dict(path='binary.exe')))
        self.assertEqual(v.arguments(self.job(),prepared,self.plan,'GPU'),['binary.exe','sdl','odd','high','5','30','0','GPU'])
        self.assertEqual(v.arguments(self.job(kind='missing-layer'),prepared,self.plan,'GPU')[4:7],['1','3','1'])
        self.assertEqual(v.arguments(self.job(kind='wrong-target'),prepared,self.plan,'GPU')[-1],'GraphicsSDK BEN03 absent GPU')


if __name__=='__main__':unittest.main()
