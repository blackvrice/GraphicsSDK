import copy
import json
import pathlib
import sys
import unittest

sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'tools'))
import run_current_vulkan_pso as v
import test_vulkan_pso as fixture


class CurrentPso(unittest.TestCase):
    def setUp(self):
        self.plan=v.read(v.PLAN)
        self.context=dict(expected_gpu='GPU',SDL_commit='c'*40,SDK_source_fingerprint='b'*64,
                          profile_fingerprints={'pso':'a'*64},validation_library='C:/runtime/VkLayer_khronos_validation.dll')

    def result(self,scenario='pso1',validation=False,sdk='sdl'):
        f=fixture.VulkanPsoContracts('test_matching_completed_payload');f.setUp();r=copy.deepcopy(f.r)
        r['native_vendor_id']=r['environment']['vendor_id']=4318
        self.context['identity']={k:copy.deepcopy(r[k]) for k in v.protocol.MODULES['pso'].IDENTITY_KEYS}
        mode=self.plan['validation' if validation else 'performance'];count=self.plan['work'][scenario];total=mode['warmup']+mode['samples']
        r.update(sdk=sdk,scenario=scenario,warmup=mode['warmup'],sample_count=mode['samples'],validation=validation,
                 verified_images=total,verified_bytes=65*37*4*total,pipelines_per_iteration=count,
                 created_pipelines=total*count,released_pipeline_handles=total*count,used_pipeline_regions=total*count,
                 loaded_validation_layer=self.context['validation_library'] if validation else '')
        r['samples_ms']=(r['samples_ms']*((mode['samples']+2)//3))[:mode['samples']]
        return r

    def job(self,scenario='pso1',sdk='sdl',kind='measurement',repetition=0):
        return dict(profile='pso',scenario=scenario,sdk=sdk,kind=kind,repetition=repetition)

    def validate(self,r,validation=False):
        return v.protocol.validate_output(self.job(r['scenario'],r['sdk'],'validation' if validation else 'measurement'),json.dumps(r).encode(),
               b'VK_LAYER_KHRONOS_validation VkLayer_khronos_validation.dll' if validation else b'',self.context,self.plan)

    def rows(self):
        return [dict(job=self.job(s,sdk,repetition=rep),result=self.result(s,sdk=sdk)) for s in self.plan['scenarios'] for sdk in v.SDKS for rep in range(5)]

    def test_full_pipeline_scope_and_sample_budget(self):
        v.validate_plan(self.plan);jobs=v.schedule(self.plan);self.assertEqual(len(jobs),28)
        self.assertEqual(sum(j['kind']=='measurement' for j in jobs)*30,600)

    def test_validation_and_controls_finish_before_timing(self):
        jobs=v.schedule(self.plan);self.assertEqual([j['kind'] for j in jobs[:4]],['validation']*4)
        self.assertTrue(all(j['kind'] in ('missing-layer','wrong-target') for j in jobs[4:8]));self.assertTrue(all(j['kind']=='measurement' for j in jobs[8:]))

    def test_all_scenarios_have_five_repetitions_for_both_sdks(self):
        rows=v.schedule(self.plan)[8:]
        for s in self.plan['scenarios']:
            for sdk in v.SDKS:self.assertEqual({j['repetition'] for j in rows if j['scenario']==s and j['sdk']==sdk},set(range(5)))

    def test_seeded_adjacent_sdk_pairs_alternate(self):
        a=v.schedule(self.plan);self.assertEqual(a,v.schedule(copy.deepcopy(self.plan)));self.assertEqual(len({j['name'] for j in a}),28)
        rows=a[8:]
        for i in range(0,len(rows),2):
            x,y=rows[i:i+2];self.assertEqual((x['scenario'],x['repetition']),(y['scenario'],y['repetition']));self.assertEqual({x['sdk'],y['sdk']},set(v.SDKS))
        for s in self.plan['scenarios']:
            order=[j['sdk'] for j in rows if j['scenario']==s];self.assertEqual(order[0],order[4]);self.assertNotEqual(order[0],order[2])

    def test_geometry_and_pipeline_counts_cannot_drift(self):
        for section,key,value in [('work','pso8',7),('geometry','scissor',[0,0,65,37]),('geometry','vertices_per_pipeline',4),('geometry','shape',[64,37,4])]:
            p=copy.deepcopy(self.plan);p[section][key]=value
            with self.subTest(section=section,key=key),self.assertRaises(ValueError):v.validate_plan(p)

    def test_cpu_and_cache_boundaries_are_explicit(self):
        for key,value in [('cache_state','cold'),('native_pipeline_count_verified',True),('driver_cache_state_known',True),('GPU_memory_reclamation_verified',True),('completed_workload_tasks',['P3-BEN-05','P3-BEN-06'])]:
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

    def test_two_scenarios_modes_and_sdks(self):
        for s in self.plan['scenarios']:
            for validation in (False,True):
                for sdk in v.SDKS:
                    r=self.result(s,validation,sdk)
                    with self.subTest(scenario=s,validation=validation,sdk=sdk):self.assertEqual(self.validate(r,validation),r)

    def test_every_created_pipeline_is_released_and_used(self):
        for key,value in [('pipelines_per_iteration',1),('created_pipelines',279),('released_pipeline_handles',279),('used_pipeline_regions',279),('verified_bytes',0),('shader_header_sha256','f'*64)]:
            r=self.result('pso8');r[key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):self.validate(r)

    def test_no_timing_or_cache_or_reclamation_promotion(self):
        for key,value in [('gpu_ms',.5),('GPU_use_in_timing',True),('GPU_memory_reclamation_verified',True),('cache_state','warm_hit'),('includes_shader_object_create_release',False)]:
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
        groups=v.summarize(self.rows(),self.plan);self.assertEqual(len(groups),4)
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
        p=dict(binaries=dict(pso=dict(path='pso.exe')))
        self.assertEqual(v.arguments(self.job('pso8'),p,self.plan,'GPU'),['pso.exe','sdl','pso8','high','5','30','0','GPU'])
        self.assertEqual(v.arguments(self.job(kind='missing-layer'),p,self.plan,'GPU')[4:7],['1','3','1'])
        self.assertEqual(v.arguments(self.job(kind='wrong-target'),p,self.plan,'GPU')[-1],'GraphicsSDK BEN05 absent GPU')


if __name__=='__main__':unittest.main()
