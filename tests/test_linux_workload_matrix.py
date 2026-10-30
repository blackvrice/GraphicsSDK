"""Full-case Linux diagnostic contracts; generated fixtures are not native evidence."""
import copy
import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]/'tools'))
import linux_workload_matrix as m


class MatrixContracts(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.ready = m.read(m.ROOT/'benchmarks/reports/validation/2026-10-28-phase3-linux-workloads/native/linux-workloads-evidence/receipt.json')
        cls.spec = m.read(m.ROOT/m.CONTRACT)

    def setUp(self):
        self.r = dict(readiness=copy.deepcopy(self.ready), contract=copy.deepcopy(self.spec))

    def result(self, job):
        p, sdk, name = job['profile'], job['sdk'], job['scenario']
        value = copy.deepcopy(next(row['result'] for row in self.ready['runs']
                                  if row['job'] == next(j for j in m.w.schedule()
                                      if j['profile'] == p and j['sdk'] == sdk and j['kind'] == 'validation-smoke')))
        shape = self.spec['profiles'][p]['shapes'][name]
        value.update(scenario=name, validation=job['validation'], loaded_validation_layer=
                     self.ready['qualification']['validation_library'] if job['validation'] else '')
        value.update(zip(('width', 'height', 'texel_bytes'), shape))
        images, buffers = 4, 0
        if p == 'draw':
            calls, instances = self.spec['profiles'][p]['work'][name]
            value.update(draw_calls_per_frame=calls, instances_per_call=instances,
                         submitted_draw_calls=4*calls, vertices_per_frame=3*calls*instances)
        if p == 'pso':
            count = self.spec['profiles'][p]['work'][name]
            value.update(pipelines_per_iteration=count, created_pipelines=4*count,
                         released_pipeline_handles=4*count, used_pipeline_regions=4*count)
        if p == 'allocation':
            count = self.spec['profiles'][p]['work'][name]; texture = name.startswith('texture')
            images, buffers = (4*count, 0) if texture else (0, 4*count)
            value.update(resource_kind='texture' if texture else 'buffer', resources_per_iteration=count,
                created_resources=4*count, released_resource_handles=4*count, used_resources=4*count,
                verified_resources=4*count, verified_buffers=buffers, buffer_bytes=0 if texture else 4096,
                usage_policy='sampled_rgba8_2d' if texture else 'sdl_vertex_buffer' if sdk == 'sdl' else 'sdk_general_device_buffer')
        value.update(verified_images=images, verified_bytes=(images+buffers)*(shape[0]*shape[1]*shape[2] if images else 4096))
        return value

    def jobs(self, profile=None, scenario=None, validation=None):
        return [j for j in m.schedule(self.spec) if (profile is None or j['profile'] == profile)
                and (scenario is None or j['scenario'] == scenario)
                and (validation is None or j['validation'] == validation)]

    def test_all_13_cases_both_sdks_and_modes(self):
        jobs = self.jobs()
        self.assertEqual(len(jobs), 52)
        self.assertEqual(len({j['name'] for j in jobs}), 52)
        self.assertEqual(len({(j['profile'], j['scenario']) for j in jobs}), 13)
        for j in jobs:
            with self.subTest(j=j['name']):
                m.validate_result(self.result(j), j, self.r)

    def test_all_shape_counters_include_4k16f_and_1000_resources(self):
        j = self.jobs(scenario='upload4k16f')[0]
        self.assertEqual(self.result(j)['verified_bytes'], 4*3840*2160*8)
        j = self.jobs(scenario='texture1000')[0]
        self.assertEqual(self.result(j)['verified_images'], 4000)
        j = self.jobs(scenario='buffer1000')[0]
        self.assertEqual(self.result(j)['verified_buffers'], 4000)

    def test_native_identity_and_source_cannot_drift(self):
        for key, val in [('device_uuid', '0'*32), ('driver_uuid', '0'*32), ('device_luid', 'fake'),
                         ('source_fingerprint', 'a'*64), ('native_api_version', 0),
                         ('SDK_source_fingerprint', 'b'*64), ('SDL_commit', 'c'*40),
                         ('identity_attribution', 'actual_native_handle'), ('gpu_ms', 1)]:
            j = self.jobs()[0]; value = self.result(j); value[key] = val
            with self.subTest(key=key), self.assertRaises(ValueError):
                m.validate_result(value, j, self.r)

    def test_wrong_scenario_shape_or_work_count(self):
        for j in self.jobs():
            for key in ('width', 'height', 'texel_bytes', 'verified_images', 'verified_bytes'):
                value = self.result(j); value[key] += 1
                with self.subTest(job=j['name'], key=key), self.assertRaises(ValueError):
                    m.validate_result(value, j, self.r)

    def test_instance_and_draw_counts_are_independent(self):
        for name, key in [('draw1000', 'draw_calls_per_frame'), ('instanced1000', 'instances_per_call'),
                          ('draw1000', 'submitted_draw_calls'), ('instanced1000', 'vertices_per_frame')]:
            j = self.jobs(scenario=name)[0]; value = self.result(j); value[key] = 1
            with self.subTest(name=name, key=key), self.assertRaises(ValueError):
                m.validate_result(value, j, self.r)

    def test_pso_and_resource_use_release_and_readback(self):
        for p, keys in [('pso', ['created_pipelines', 'released_pipeline_handles', 'used_pipeline_regions']),
                        ('allocation', ['created_resources', 'released_resource_handles', 'used_resources', 'verified_resources', 'verified_buffers'])]:
            for j in self.jobs(profile=p):
                for key in keys:
                    value = self.result(j); value[key] += 1
                    with self.subTest(job=j['name'], key=key), self.assertRaises(ValueError):
                        m.validate_result(value, j, self.r)

    def test_validation_mode_requires_actual_layer_metadata(self):
        for j in self.jobs():
            value = self.result(j); value['loaded_validation_layer'] = '' if j['validation'] else '/unexpected/layer.so'
            with self.subTest(job=j['name']), self.assertRaises(ValueError):
                m.validate_result(value, j, self.r)

    def test_times_remain_finite_cpu_diagnostics(self):
        for p in m.w.port.PROFILES:
            j = self.jobs(profile=p)[0]
            for bad in (-1, True, float('nan'), float('inf')):
                value = self.result(j)
                if p in ('pso', 'allocation'):
                    value['samples_ms'][0][1] = bad
                else:
                    value['samples_ms'][0] = bad
                with self.subTest(profile=p, bad=bad), self.assertRaises(ValueError):
                    m.validate_result(value, j, self.r)

    def test_draw_partition_not_a_gpu_timestamp(self):
        j = self.jobs(profile='draw')[0]; value = self.result(j)
        value['CPU_intervals_ms'][0][0] += 1
        with self.assertRaises(ValueError):
            m.validate_result(value, j, self.r)

    def test_cache_or_reclamation_claim_rejected(self):
        for p, key, val in [('pso', 'cache_state', 'cold'), ('allocation', 'native_allocation_count_verified', True),
                            ('allocation', 'GPU_memory_reclamation_verified', True), ('allocation', 'usage_policy', 'same_native_usage')]:
            j = self.jobs(profile=p)[0]; value = self.result(j); value[key] = val
            with self.subTest(key=key), self.assertRaises(ValueError):
                m.validate_result(value, j, self.r)

    def test_boolean_counters_and_wrong_rows_rejected(self):
        for key, val in [('verified_images', True), ('sample_count', True), ('warmup', True),
                         ('samples_ms', [1]), ('scissor', [True, 3, 55, 31])]:
            j = self.jobs(profile='draw')[0]; value = self.result(j); value[key] = val
            with self.subTest(key=key), self.assertRaises(ValueError):
                m.validate_result(value, j, self.r)

    def test_scope_cannot_promote_software_or_diagnostic_timing(self):
        r = dict(self.r, schema_version=1, task='P3-LBEN-readiness', mode='software-diagnostic',
            physical_GPU_accepted=False, performance_accepted=False, performance_samples=0,
            P3_BEN_07_completed=False, P3_BEN_08_completed=False, P3_LBEN_completed=False,
            LLGL_accepted=False, overall_SDK_ranking=False, diagnostic_timings_excluded=True)
        m.validate_scope(r)
        for key in ('physical_GPU_accepted', 'performance_accepted', 'P3_LBEN_completed', 'LLGL_accepted',
                    'P3_BEN_07_completed', 'P3_BEN_08_completed', 'overall_SDK_ranking'):
            with self.subTest(key=key), self.assertRaises(ValueError):
                m.validate_scope(dict(r, **{key: True}))
        for val in (1, False):
            with self.subTest(samples=val), self.assertRaises(ValueError):
                m.validate_scope(dict(r, performance_samples=val))

    def test_physical_or_other_device_cannot_enter_software_matrix(self):
        r = dict(self.r)
        for kind in (0, 1, 2, 3, True):
            r['readiness']['qualification']['selected']['device_type'] = kind
            with self.subTest(kind=kind), self.assertRaises(ValueError):
                m.validate_scope(r)

    def test_duplicate_json_fields_rejected(self):
        with self.assertRaises(ValueError):
            m.strict_json('{"sample_count":3,"sample_count":0}')

    def test_output_paths_stay_within_evidence(self):
        with tempfile.TemporaryDirectory() as td:
            folder = pathlib.Path(td); (folder/'ok').write_bytes(b'ok')
            self.assertEqual(m.safe(folder, 'ok'), folder/'ok')
            for bad in ('../ok', '/ok', 'C:/ok', 'bad\\ok'):
                with self.subTest(bad=bad), self.assertRaises(ValueError):
                    m.safe(folder, bad)

    def test_actual_loader_and_layer_trace(self):
        a = dict(runtime_libraries=[dict(path='/usr/lib/libvulkan.so.1'), dict(path='/usr/lib/liblvp.so')],
                 validation_library='/runtime/libVkLayer_khronos_validation.so')
        plain = b'calling init: /lib/libvulkan.so.1\ncalling init: /lib/liblvp.so\n'
        self.assertFalse(m.loaded_libraries(plain, a))
        self.assertTrue(m.loaded_libraries(plain+b'calling init: /runtime/libVkLayer_khronos_validation.so\n', a))
        for trace in (b'', plain+b'calling init: /other/libVkLayer_khronos_validation.so\n'):
            with self.assertRaises(ValueError):
                m.loaded_libraries(trace, a)


if __name__ == '__main__':
    unittest.main()
