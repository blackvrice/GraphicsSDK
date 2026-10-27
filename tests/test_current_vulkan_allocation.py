import copy
import json
import pathlib
import sys
import unittest

sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'tools'))
import run_current_vulkan_allocation as v
import test_vulkan_allocation as fixture


class CurrentAllocation(unittest.TestCase):
    def setUp(self):
        self.plan=v.read(v.PLAN)
        self.context=dict(expected_gpu='GPU',SDL_commit='c'*40,SDK_source_fingerprint='b'*64,
                          profile_fingerprints={'allocation':'a'*64},validation_library='C:/runtime/VkLayer_khronos_validation.dll')

    def result(self,scenario='buffer1',validation=False,sdk='sdl'):
        f=fixture.VulkanAllocationContracts('test_matching_completed_payload');f.setUp();r=copy.deepcopy(f.r)
        r['native_vendor_id']=r['environment']['vendor_id']=4318
        self.context['identity']={k:copy.deepcopy(r[k]) for k in v.protocol.MODULES['allocation'].IDENTITY_KEYS}
        mode=self.plan['validation' if validation else 'performance'];count=self.plan['work'][scenario];total=mode['warmup']+mode['samples']
        texture=scenario.startswith('texture');w,h,bpp=v.SHAPES[scenario];resources=total*count
        r.update(sdk=sdk,scenario=scenario,warmup=mode['warmup'],sample_count=mode['samples'],validation=validation,
                 width=w,height=h,texel_bytes=bpp,buffer_bytes=0 if texture else 4096,resource_kind='texture' if texture else 'buffer',
                 verified_images=resources if texture else 0,verified_buffers=0 if texture else resources,
                 verified_resources=resources,verified_bytes=(w*h*bpp if texture else 4096)*resources,resources_per_iteration=count,
                 created_resources=resources,released_resource_handles=resources,used_resources=resources,
                 usage_policy='sampled_rgba8_2d' if texture else 'sdl_vertex_buffer' if sdk=='sdl' else 'sdk_general_device_buffer',
                 loaded_validation_layer=self.context['validation_library'] if validation else '')
        r['samples_ms']=(r['samples_ms']*((mode['samples']+2)//3))[:mode['samples']]
        return r

    def job(self,scenario='buffer1',sdk='sdl',kind='measurement',repetition=0):
        return dict(profile='allocation',scenario=scenario,sdk=sdk,kind=kind,repetition=repetition)

    def validate(self,r,validation=False):
        return v.protocol.validate_output(self.job(r['scenario'],r['sdk'],'validation' if validation else 'measurement'),json.dumps(r).encode(),
               b'VK_LAYER_KHRONOS_validation VkLayer_khronos_validation.dll' if validation else b'',self.context,self.plan)

    def rows(self):
        return [dict(job=self.job(s,sdk,repetition=rep),result=self.result(s,sdk=sdk)) for s in self.plan['scenarios'] for sdk in v.SDKS for rep in range(5)]

    def test_full_resource_scope_and_sample_budget(self):
        v.validate_plan(self.plan);jobs=v.schedule(self.plan);self.assertEqual(len(jobs),52)
        self.assertEqual(sum(j['kind']=='measurement' for j in jobs)*30,1200)

    def test_validation_and_controls_finish_before_timing(self):
        jobs=v.schedule(self.plan);self.assertEqual([j['kind'] for j in jobs[:8]],['validation']*8)
        self.assertTrue(all(j['kind'] in ('missing-layer','wrong-target') for j in jobs[8:12]));self.assertTrue(all(j['kind']=='measurement' for j in jobs[12:]))

    def test_all_scenarios_have_five_repetitions_for_both_sdks(self):
        rows=v.schedule(self.plan)[12:]
        for s in self.plan['scenarios']:
            for sdk in v.SDKS:self.assertEqual({j['repetition'] for j in rows if j['scenario']==s and j['sdk']==sdk},set(range(5)))

    def test_seeded_adjacent_sdk_pairs_alternate(self):
        a=v.schedule(self.plan);self.assertEqual(a,v.schedule(copy.deepcopy(self.plan)));self.assertEqual(len({j['name'] for j in a}),52)
        rows=a[12:]
        for i in range(0,len(rows),2):
            x,y=rows[i:i+2];self.assertEqual((x['scenario'],x['repetition']),(y['scenario'],y['repetition']));self.assertEqual({x['sdk'],y['sdk']},set(v.SDKS))
        for s in self.plan['scenarios']:
            order=[j['sdk'] for j in rows if j['scenario']==s];self.assertEqual(order[0],order[4]);self.assertNotEqual(order[0],order[2])

    def test_resource_sizes_counts_and_usage_cannot_drift(self):
        for section,key,value in [('work','texture1000',999),('work','buffer1',True),('resources','buffer_bytes',4095),('resources','texture_shape',[64,37,4]),('resources','usage_policy',{})]:
            p=copy.deepcopy(self.plan);p[section][key]=value
            with self.subTest(section=section,key=key),self.assertRaises(ValueError):v.validate_plan(p)

    def test_cpu_and_allocation_boundaries_are_explicit(self):
        for key,value in [('allocation_policy','dedicated'),('native_allocation_count_verified',True),('allocator_cache_state_known',True),('GPU_memory_reclamation_verified',True),('completed_workload_tasks',['P3-BEN-06','P3-BEN-07'])]:
            p=copy.deepcopy(self.plan);p[key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):v.validate_plan(p)
        for key,value in [('GPU_timestamp_measurement',True),('category','end_to_end'),('includes',['GPU_use'])]:
            p=copy.deepcopy(self.plan);p['timing'][key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):v.validate_plan(p)

    def test_frozen_samples_and_columns(self):
        for key,value in [('samples',12),('warmup',True),('repetitions',3)]:
            p=copy.deepcopy(self.plan);p['performance'][key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):v.validate_plan(p)
        p=copy.deepcopy(self.plan);p['timing']['columns'].reverse()
        with self.assertRaises(ValueError):v.validate_plan(p)

    def test_four_scenarios_modes_and_sdks(self):
        for s in self.plan['scenarios']:
            for validation in (False,True):
                for sdk in v.SDKS:
                    r=self.result(s,validation,sdk)
                    with self.subTest(scenario=s,validation=validation,sdk=sdk):self.assertEqual(self.validate(r,validation),r)

    def test_every_created_resource_is_released_and_used(self):
        for key,value in [('resources_per_iteration',999),('created_resources',34999),('released_resource_handles',34999),('used_resources',34999),('verified_resources',34999),('verified_images',34999),('verified_bytes',0)]:
            r=self.result('texture1000');r[key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):self.validate(r)

    def test_no_timing_or_cache_or_reclamation_promotion(self):
        for key,value in [('gpu_ms',.5),('GPU_use_in_timing',True),('GPU_memory_reclamation_verified',True),('allocation_policy','dedicated'),('native_allocation_count_verified',True),('usage_policy','other')]:
            r=self.result();r[key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):self.validate(r)

    def test_source_driver_and_validation_contamination(self):
        for key,value in [('source_fingerprint','f'*64),('native_driver_version',99),('loaded_validation_layer',self.context['validation_library'])]:
            r=self.result();r[key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):self.validate(r)

    def test_invalid_intervals_and_zero_creation_rejected(self):
        for bad in (-1,True,float('nan'),float('inf'),0):
            r=self.result();r['samples_ms'][0][0]=bad
            with self.subTest(bad=bad),self.assertRaises(ValueError):v.validate_intervals(r,30)
        r=self.result();r['samples_ms'][0].append(1)
        with self.assertRaises(ValueError):v.validate_intervals(r,30)

    def test_zero_release_allowed_without_reclamation_claim(self):
        r=self.result();r['samples_ms'][0][1]=0;v.validate_intervals(r,30);self.validate(r)

    def test_both_columns_keep_150_samples_and_process_medians(self):
        groups=v.summarize(self.rows(),self.plan);self.assertEqual(len(groups),8)
        for g in groups:
            self.assertEqual(set(g['statistics']),{'create_ms','public_release_ms'})
            for s in g['statistics'].values():self.assertEqual((s['processes'],s['samples'],s['samples_removed']),(5,150,0));self.assertEqual(len(s['process_medians_ms']),5)
            self.assertEqual(g['statistics']['create_ms']['p50_ms'],2);self.assertEqual(g['statistics']['public_release_ms']['p50_ms'],.2)

    def test_validation_warmup_exclusion_and_outlier_retention(self):
        rows=self.rows();r=self.result(validation=True);r['samples_ms']=[[999999,999999]]*3;rows.append(dict(job=self.job(kind='validation'),result=r))
        rows[0]['result']['samples_ms'][0]=[999,.1]
        groups=v.summarize(rows,self.plan);self.assertEqual(groups[0]['statistics']['create_ms']['max_ms'],999)
        self.assertTrue(all(g['statistics']['create_ms']['max_ms']<999999 for g in groups))
        rows=self.rows();rows[0]['result']['samples_ms'] += [[1,1]]*5
        with self.assertRaises(ValueError):v.summarize(rows,self.plan)

    def test_missing_duplicate_repetitions_rejected(self):
        rows=self.rows();rows[0]['job']['repetition']=1
        with self.assertRaises(ValueError):v.summarize(rows,self.plan)
        rows=self.rows();rows.pop()
        with self.assertRaises(ValueError):v.summarize(rows,self.plan)

    def test_exact_native_arguments(self):
        p=dict(binaries=dict(allocation=dict(path='allocation.exe')))
        self.assertEqual(v.arguments(self.job('texture1000'),p,self.plan,'GPU'),['allocation.exe','sdl','texture1000','high','5','30','0','GPU'])
        self.assertEqual(v.arguments(self.job(kind='missing-layer'),p,self.plan,'GPU')[4:7],['1','3','1'])
        self.assertEqual(v.arguments(self.job(kind='wrong-target'),p,self.plan,'GPU')[-1],'GraphicsSDK BEN06 absent GPU')

    def test_buffer_usage_difference_and_full_buffer_readback(self):
        for sdk,usage in [('graphicssdk','sdk_general_device_buffer'),('sdl','sdl_vertex_buffer')]:
            r=self.result('buffer1000',sdk=sdk);self.assertEqual(r['usage_policy'],usage);self.validate(r)
            for key,value in [('usage_policy','sampled_rgba8_2d'),('buffer_bytes',4095),('verified_buffers',34999),('verified_images',1),('verified_bytes',1),('created_resources',True)]:
                bad=copy.deepcopy(r);bad[key]=value
                with self.subTest(sdk=sdk,key=key),self.assertRaises(ValueError):self.validate(bad)


if __name__=='__main__':unittest.main()
