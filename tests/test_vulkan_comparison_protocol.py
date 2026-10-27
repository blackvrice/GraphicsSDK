import copy
import json
import pathlib
import sys
import unittest

sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'tools'))
import run_vulkan_comparison as v
import test_vulkan_upload as upload_fixture
import test_vulkan_draw as draw_fixture
import test_vulkan_pso as pso_fixture
import test_vulkan_allocation as allocation_fixture


class ComparisonProtocol(unittest.TestCase):
    def setUp(self):
        self.plan=v.read(v.PLAN)
        self.context=dict(expected_gpu='GPU',SDL_commit='c'*40,SDK_source_fingerprint='b'*64,
                          profile_fingerprints={n:'a'*64 for n in v.NAMES},validation_library='C:/runtime/VkLayer_khronos_validation.dll')

    def payload(self,n,validation=False):
        modules=dict(upload=upload_fixture,draw=draw_fixture,pso=pso_fixture,allocation=allocation_fixture)
        names=dict(upload='VulkanUploadContracts',draw='VulkanDrawContracts',pso='VulkanPsoContracts',allocation='VulkanAllocationContracts')
        fixture=getattr(modules[n],names[n])('test_matching_completed_payload');fixture.setUp();r=copy.deepcopy(fixture.r)
        r['native_vendor_id']=r['environment']['vendor_id']=4318
        # The allocation fixture exercises textures. Qualification uses buffer1.
        if n=='allocation':
            r.update(scenario='buffer1',width=0,height=0,texel_bytes=0,verified_images=0,verified_buffers=4,verified_bytes=4096*4,
                     buffer_bytes=4096,resource_kind='buffer',usage_policy='sdl_vertex_buffer')
        self.context['identity']={k:copy.deepcopy(r[k]) for k in v.MODULES[n].IDENTITY_KEYS}
        mode=self.plan['validation' if validation else 'performance'];total=mode['warmup']+mode['samples']
        for k,x in list(r.items()):
            if k.startswith(('verified_','created_','released_','used_','submitted_')) and type(x) is int:r[k]=x*total//4
        r.update(warmup=mode['warmup'],sample_count=mode['samples'],validation=validation,
                 loaded_validation_layer=self.context['validation_library'] if validation else '')
        r['samples_ms']=(r['samples_ms']*((mode['samples']+2)//3))[:mode['samples']]
        if n=='draw':r['CPU_intervals_ms']=(r['CPU_intervals_ms']*((mode['samples']+2)//3))[:mode['samples']]
        return r

    def job(self,n,kind='measurement'):
        return dict(profile=n,sdk='sdl',scenario=self.plan['performance']['scenarios'][n],kind=kind,repetition=0)

    def validate(self,n,r,validation=False,stderr=None):
        return v.validate_output(self.job(n,'validation' if validation else 'measurement'),json.dumps(r).encode(),
                                 stderr if stderr is not None else b'VK_LAYER_KHRONOS_validation VkLayer_khronos_validation.dll' if validation else b'',self.context,self.plan)

    def test_fixed_qualification_plan(self):v.validate_plan(self.plan)

    def test_schedule_gates_before_any_measurement(self):
        jobs=v.schedule(self.plan);self.assertEqual(len(jobs),66)
        self.assertEqual([j['kind'] for j in jobs[:26]],['validation']*26)
        self.assertTrue(all(j['kind'] in ('missing-layer','wrong-target') for j in jobs[26:42]))
        self.assertTrue(all(j['kind']=='measurement' for j in jobs[42:]))
        self.assertEqual(len({(j['profile'],j['sdk'],j['scenario']) for j in jobs[:26]}),26)

    def test_adjacent_pairs_and_alternation_each_repetition(self):
        measured=[j for j in v.schedule(self.plan) if j['kind']=='measurement']
        for i in range(0,len(measured),2):
            a,b=measured[i:i+2];self.assertEqual((a['profile'],a['repetition']),(b['profile'],b['repetition']));self.assertEqual({a['sdk'],b['sdk']},set(v.SDKS))
        for n in v.NAMES:
            order=[j['sdk'] for j in measured if j['profile']==n];self.assertEqual(order[0],order[3]);self.assertEqual(order[0],order[4]);self.assertNotEqual(order[0],order[2])

    def test_schedule_reproducible_and_unique(self):
        a=v.schedule(self.plan);self.assertEqual(a,v.schedule(copy.deepcopy(self.plan)));self.assertEqual(len({x['name'] for x in a}),66)

    def test_no_boolean_counts_or_historical_promotion(self):
        for section,key,value in [('validation','warmup',True),('performance','samples',True),('performance','warmup',0),('statistics','samples_removed',1),('statistics','validation_timings_excluded',False)]:
            p=copy.deepcopy(self.plan);p[section][key]=value
            with self.subTest(section=section,key=key),self.assertRaises(ValueError):v.validate_plan(p)
        for key,value in [('historical_timings_reused',True),('overall_SDK_ranking',True),('full_workload_tasks_completed',['P3-BEN-03']),('SDL_native_handle_verified',True)]:
            p=copy.deepcopy(self.plan);p[key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):v.validate_plan(p)

    def test_all_four_shapes_validate_in_both_process_modes(self):
        for n in v.NAMES:
            for validation in (False,True):
                with self.subTest(profile=n,validation=validation):r=self.payload(n,validation);self.assertEqual(self.validate(n,r,validation),r)

    def test_validation_library_must_be_the_pinned_path(self):
        r=self.payload('upload',True);r['loaded_validation_layer']='C:/other/VkLayer_khronos_validation.dll'
        with self.assertRaises(ValueError):self.validate('upload',r,True)

    def test_required_loader_trace_not_just_nonempty_layer_field(self):
        r=self.payload('upload',True)
        with self.assertRaises(ValueError):self.validate('upload',r,True,stderr=b'')

    def test_validation_error_not_accepted_as_clean_reference(self):
        r=self.payload('draw',True)
        with self.assertRaises(ValueError):self.validate('draw',r,True,stderr=b'VK_LAYER_KHRONOS_validation VkLayer_khronos_validation.dll SYNC-HAZARD-WRITE-AFTER-WRITE')

    def test_performance_cannot_load_validation(self):
        r=self.payload('upload');r['loaded_validation_layer']=self.context['validation_library']
        with self.assertRaises(ValueError):self.validate('upload',r)

    def test_full_readback_and_current_sources_required(self):
        for key,value in [('verified_bytes',0),('readback_verified',False),('source_fingerprint','d'*64),('SDK_source_fingerprint','d'*64),('native_driver_version',999)]:
            r=self.payload('allocation');r[key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):self.validate('allocation',r)

    def test_control_cannot_accept_crash_or_results(self):
        j=self.job('upload','missing-layer');v.validate_control(j,1,b'',b'FAIL: VK_LAYER_KHRONOS_validation is not installed native=0')
        for code,out,err in [(0,b'',b'VK_LAYER_KHRONOS_validation is not installed'),(1,b'{}',b'VK_LAYER_KHRONOS_validation is not installed'),(-1,b'',b'crash'),(1,b'',b'Device lost')]:
            with self.subTest(code=code,out=out),self.assertRaises(ValueError):v.validate_control(j,code,out,err)

    def test_exact_target_control(self):
        j=self.job('upload','wrong-target');v.validate_control(j,1,b'',b'FAIL: Selected GPU differs from expected GPU\n')
        with self.assertRaises(ValueError):v.validate_control(j,1,b'',b'FAIL: Other GPU error')

    def test_inherited_validation_and_gpu_selection_removed(self):
        inherited=dict(PATH='system',VK_INSTANCE_LAYERS='other',VVL_LOG='x',SDL_GPU_DRIVER='bad',GFX_EXPECT_VULKAN_LAYER='bad',vk_driver_files='intel-only')
        clean,validation,e=v.environments(pathlib.Path('/campaign'),pathlib.Path('/runtime/VkLayer_khronos_validation.dll'),inherited)
        self.assertEqual(clean['PATH'],'system');self.assertEqual(set(clean)-{'PATH'},{'VK_LOADER_LAYERS_DISABLE','VK_LAYER_PATH','VK_LAYER_SETTINGS_PATH'})
        self.assertEqual(validation['VK_INSTANCE_LAYERS'],'VK_LAYER_KHRONOS_validation');self.assertEqual(validation['VK_KHRONOS_VALIDATION_VALIDATE_SYNC'],'true')
        self.assertEqual(len(e['removed_inherited_keys']),5)

    def test_cross_platform_path_evidence_and_exact_nonpath_values(self):
        self.assertTrue(v.same_overrides({'VK_LAYER_PATH':r'C:\Runtime','VK_LOADER_DEBUG':'layer'},{'VK_LAYER_PATH':'c:/runtime','VK_LOADER_DEBUG':'layer'}))
        self.assertFalse(v.same_overrides({'VK_LAYER_PATH':'c:/runtime','VK_LOADER_DEBUG':'all'},{'VK_LAYER_PATH':'c:/runtime','VK_LOADER_DEBUG':'layer'}))

    def test_linear_percentiles_mad_and_all_outliers_retained(self):
        s=v.scalar_stats([[1,2,3],[2,3,100]])
        self.assertEqual(s['p50_ms'],2.5);self.assertEqual(s['MAD_ms'],.5);self.assertAlmostEqual(s['p95_ms'],75.75);self.assertAlmostEqual(s['p99_ms'],95.15)
        self.assertEqual(s['process_medians_ms'],[2,3]);self.assertEqual(s['samples'],6);self.assertEqual(s['max_ms'],100);self.assertEqual(s['samples_removed'],0)

    def test_nonfinite_negative_boolean_or_empty_samples_rejected(self):
        for runs in ([],[[]],[[float('nan')]],[[float('inf')]],[[True]],[[-1]],[["1"]]):
            with self.subTest(runs=runs),self.assertRaises(ValueError):v.scalar_stats(runs)

    def test_summary_excludes_validation_and_preserves_each_column(self):
        rows=[]
        for n in v.NAMES:
            for sdk in v.SDKS:
                for _ in range(3):rows.append(dict(job={**self.job(n), 'sdk':sdk},result=self.payload(n)))
                r=self.payload(n,True);r['samples_ms']=[999999]*3;rows.append(dict(job={**self.job(n,'validation'),'sdk':sdk},result=r))
        groups=v.summarize(rows,self.plan);self.assertEqual(len(groups),8)
        for g in groups:
            for s in g['statistics'].values():self.assertEqual(s['samples'],36);self.assertLess(s['max_ms'],999999)
        draw=next(g for g in groups if g['profile']=='draw');self.assertEqual(draw['statistics']['record_ms']['max_ms'],.6)
        self.assertEqual(draw['statistics']['total_ms']['max_ms'],3)

    def test_incomplete_independent_process_group_rejected(self):
        with self.assertRaises(ValueError):v.summarize([],self.plan)


if __name__=='__main__':unittest.main()
