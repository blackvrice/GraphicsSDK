import copy
import json
import pathlib
import sys
import unittest

sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'tools'))
import run_current_vulkan_draw as v
import test_vulkan_draw as fixture


class CurrentDraw(unittest.TestCase):
    def setUp(self):
        self.plan=v.read(v.PLAN)
        self.context=dict(expected_gpu='GPU',SDL_commit='c'*40,SDK_source_fingerprint='b'*64,
                          profile_fingerprints={'draw':'a'*64},validation_library='C:/runtime/VkLayer_khronos_validation.dll')

    def result(self,scenario='draw1',validation=False,sdk='sdl'):
        f=fixture.VulkanDrawContracts('test_matching_completed_payload');f.setUp();r=copy.deepcopy(f.r)
        r['native_vendor_id']=r['environment']['vendor_id']=4318
        self.context['identity']={k:copy.deepcopy(r[k]) for k in v.protocol.MODULES['draw'].IDENTITY_KEYS}
        mode=self.plan['validation' if validation else 'performance'];calls,instances=self.plan['work'][scenario];total=mode['warmup']+mode['samples']
        r.update(sdk=sdk,scenario=scenario,warmup=mode['warmup'],sample_count=mode['samples'],validation=validation,
                 verified_images=total,verified_bytes=65*37*4*total,draw_calls_per_frame=calls,instances_per_call=instances,
                 vertices_per_frame=3*calls*instances,submitted_draw_calls=total*calls,
                 loaded_validation_layer=self.context['validation_library'] if validation else '')
        r['samples_ms']=(r['samples_ms']*((mode['samples']+2)//3))[:mode['samples']]
        r['CPU_intervals_ms']=(r['CPU_intervals_ms']*((mode['samples']+2)//3))[:mode['samples']]
        return r

    def job(self,scenario='draw1',sdk='sdl',kind='measurement',repetition=0):
        return dict(profile='draw',scenario=scenario,sdk=sdk,kind=kind,repetition=repetition)

    def validate(self,r,validation=False):
        return v.protocol.validate_output(self.job(r['scenario'],r['sdk'],'validation' if validation else 'measurement'),json.dumps(r).encode(),
               b'VK_LAYER_KHRONOS_validation VkLayer_khronos_validation.dll' if validation else b'',self.context,self.plan)

    def rows(self):
        return [dict(job=self.job(s,sdk,repetition=rep),result=self.result(s,sdk=sdk)) for s in self.plan['scenarios'] for sdk in v.SDKS for rep in range(5)]

    def test_full_draw_scope_and_sample_budget(self):
        v.validate_plan(self.plan);jobs=v.schedule(self.plan);self.assertEqual(len(jobs),40)
        self.assertEqual(sum(j['kind']=='measurement' for j in jobs)*30,900)

    def test_validation_and_controls_finish_before_timing(self):
        jobs=v.schedule(self.plan);self.assertEqual([j['kind'] for j in jobs[:6]],['validation']*6)
        self.assertTrue(all(j['kind'] in ('missing-layer','wrong-target') for j in jobs[6:10]));self.assertTrue(all(j['kind']=='measurement' for j in jobs[10:]))

    def test_every_scenario_has_five_repetitions_for_both_sdks(self):
        rows=v.schedule(self.plan)[10:]
        for s in self.plan['scenarios']:
            for sdk in v.SDKS:self.assertEqual({j['repetition'] for j in rows if j['scenario']==s and j['sdk']==sdk},set(range(5)))

    def test_sdk_pairs_alternate_and_schedule_reproduces(self):
        a=v.schedule(self.plan);self.assertEqual(a,v.schedule(copy.deepcopy(self.plan)));self.assertEqual(len({j['name'] for j in a}),40)
        rows=a[10:]
        for i in range(0,len(rows),2):
            x,y=rows[i:i+2];self.assertEqual((x['scenario'],x['repetition']),(y['scenario'],y['repetition']));self.assertEqual({x['sdk'],y['sdk']},set(v.SDKS))
        for s in self.plan['scenarios']:
            order=[j['sdk'] for j in rows if j['scenario']==s];self.assertEqual(order[0],order[4]);self.assertNotEqual(order[0],order[2])

    def test_geometry_work_and_shader_contract_cannot_drift(self):
        for section,key,value in [('work','draw1000',[999,1]),('work','instanced1000',[1000,1]),('geometry','scissor',[0,0,65,37]),('geometry','vertices_per_instance',4),('geometry','shape',[64,37,4])]:
            p=copy.deepcopy(self.plan);p[section][key]=value
            with self.subTest(section=section,key=key),self.assertRaises(ValueError):v.validate_plan(p)

    def test_scope_cannot_drop_instancing_or_hide_gpu_pending(self):
        for key,value in [('scenarios',['draw1','draw1000']),('GPU_timestamp_pending_task','done'),('completed_workload_tasks',['P3-BEN-04','P3-BEN-09']),('overall_SDK_ranking',True),('historical_timings_reused',True)]:
            p=copy.deepcopy(self.plan);p[key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):v.validate_plan(p)

    def test_cpu_wall_wait_is_not_gpu_timestamp(self):
        p=copy.deepcopy(self.plan);p['timing']['GPU_timestamp_measurement']=True
        with self.assertRaises(ValueError):v.validate_plan(p)
        r=self.result();r['gpu_ms']=.5
        with self.assertRaises(ValueError):self.validate(r)

    def test_sample_budget_and_column_order_are_frozen(self):
        for key,value in [('samples',12),('warmup',True),('repetitions',3)]:
            p=copy.deepcopy(self.plan);p['performance'][key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):v.validate_plan(p)
        p=copy.deepcopy(self.plan);p['timing']['columns'].reverse()
        with self.assertRaises(ValueError):v.validate_plan(p)

    def test_three_scenarios_two_modes_two_sdks(self):
        for s in self.plan['scenarios']:
            for validation in (False,True):
                for sdk in v.SDKS:
                    r=self.result(s,validation,sdk)
                    with self.subTest(scenario=s,validation=validation,sdk=sdk):self.assertEqual(self.validate(r,validation),r)

    def test_wrong_draw_instance_counts_and_reference_bytes_rejected(self):
        for key,value in [('draw_calls_per_frame',1000),('instances_per_call',1),('verified_bytes',0),('shader_header_sha256','f'*64),('submitted_draw_calls',0)]:
            r=self.result('instanced1000');r[key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):self.validate(r)

    def test_source_driver_and_validation_contamination_rejected(self):
        for key,value in [('source_fingerprint','f'*64),('native_driver_version',99),('loaded_validation_layer',self.context['validation_library'])]:
            r=self.result();r[key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):self.validate(r)

    def test_each_cpu_interval_partitions_primary_total(self):
        r=self.result();self.assertEqual(v.validate_intervals(r,30),r['CPU_intervals_ms'])
        r['CPU_intervals_ms'][0][0]+=.1
        with self.assertRaises(ValueError):v.validate_intervals(r,30)

    def test_nonfinite_negative_boolean_and_wrong_primary_rejected(self):
        for bad in (-1,True,float('nan'),float('inf')):
            r=self.result();r['CPU_intervals_ms'][0][0]=bad
            with self.subTest(bad=bad),self.assertRaises(ValueError):v.validate_intervals(r,30)
        r=self.result();r['samples_ms'][0]=0
        with self.assertRaises(ValueError):v.validate_intervals(r,30)

    def test_zero_component_can_be_valid_but_not_zero_total(self):
        r=self.result();r['CPU_intervals_ms'][0]=[0,.5,.5,1];v.validate_intervals(r,30)
        r['CPU_intervals_ms'][0]=[0,0,0,0];r['samples_ms'][0]=0
        with self.assertRaises(ValueError):v.validate_intervals(r,30)

    def test_all_four_columns_keep_150_samples_and_process_medians(self):
        groups=v.summarize(self.rows(),self.plan);self.assertEqual(len(groups),6)
        for g in groups:
            self.assertEqual(set(g['statistics']),set(self.plan['timing']['columns']))
            for s in g['statistics'].values():self.assertEqual((s['processes'],s['samples'],s['samples_removed']),(5,150,0));self.assertEqual(len(s['process_medians_ms']),5)
            self.assertEqual(g['statistics']['total_ms']['p50_ms'],2);self.assertEqual(g['statistics']['record_ms']['p50_ms'],.4)

    def test_validation_and_warmup_not_part_of_statistics(self):
        rows=self.rows();r=self.result(validation=True);r['samples_ms']=[999999]*3;rows.append(dict(job=self.job(kind='validation'),result=r))
        self.assertTrue(all(g['statistics']['total_ms']['max_ms']==3 for g in v.summarize(rows,self.plan)))
        rows=self.rows();rows[0]['result']['CPU_intervals_ms'] += [[1,1,1,3]]*5
        with self.assertRaises(ValueError):v.summarize(rows,self.plan)

    def test_missing_or_duplicate_independent_repetition_rejected(self):
        rows=self.rows();rows[0]['job']['repetition']=1
        with self.assertRaises(ValueError):v.summarize(rows,self.plan)
        rows=self.rows();rows.pop()
        with self.assertRaises(ValueError):v.summarize(rows,self.plan)

    def test_native_argv_has_correct_profile_mode_and_target(self):
        p=dict(binaries=dict(draw=dict(path='draw.exe')))
        self.assertEqual(v.arguments(self.job('draw1000'),p,self.plan,'GPU'),['draw.exe','sdl','draw1000','high','5','30','0','GPU'])
        self.assertEqual(v.arguments(self.job(kind='missing-layer'),p,self.plan,'GPU')[4:7],['1','3','1'])
        self.assertEqual(v.arguments(self.job(kind='wrong-target'),p,self.plan,'GPU')[-1],'GraphicsSDK BEN04 absent GPU')

    def test_roadmap_keeps_separate_required_gpu_gate(self):
        d=v.read(v.ROOT/'docs/ROADMAP_TASKS.json');tasks={t['id']:t for t in d['tasks']}
        self.assertEqual(tasks['P3-BEN-09']['gate'],'required');self.assertIn('P3-BEN-09',tasks['P3-GATE-02']['depends_on'])
        self.assertIn('docs/PHASE3_DRAW_SCOPE.md',tasks['P3-BEN-09']['evidence'])


if __name__=='__main__':unittest.main()
