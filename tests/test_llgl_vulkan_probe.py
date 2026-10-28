import copy
import importlib.util
import json
import pathlib
import shutil
import sys
import tempfile
import unittest

sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'tools'))
import llgl_vulkan_probe as v


class LLGLVulkanProbe(unittest.TestCase):
    def setUp(self):
        self.row=dict(schema_version=1,task='P3-CMP-02',sdk='llgl',api='vulkan',configuration='Release',
            LLGL_commit=v.read(v.ROOT/'benchmarks/competitors/sources.lock.json')['llgl']['commit'],
            source_fingerprint='a'*64,device_name='GPU',device_uuid='1'*32,driver_uuid='2'*32,device_luid='3'*16,
            native_vendor_id=4318,native_device_id=8644,native_driver_version=1,native_api_version=(1<<22|3<<12),
            queue_family=0,queue_index=0,queue_flags=15,loader_api_version=(1<<22|3<<12),native_handle_verified=True,
            validation=False,loaded_validation_layer='',operation='smoke',verified_bytes=12288,reference_rounds=3,
            performance_samples=0,gpu_ms=None,workload_correctness_accepted=False,performance_accepted=False,
            delivered_errors=0,instance_API_request_observed=False,runtime_modules=['system module'])

    def check(self,row=None,validation=False,operation='smoke',reference=None):
        return v.validate_probe(row or self.row,'GPU','a'*64,validation,operation,reference)

    def test_accepts_native_minimal_reference_without_performance(self):
        self.assertEqual(self.check()['device_uuid'],'1'*32)

    def test_identity_must_not_claim_reference_work(self):
        row=copy.deepcopy(self.row);row.update(operation='identity',verified_bytes=0,reference_rounds=0)
        self.check(row,operation='identity')
        row['verified_bytes']=4096
        with self.assertRaises(ValueError):self.check(row,operation='identity')

    def test_wrong_backend_target_or_source_rejected(self):
        for key,value in [('sdk','graphicssdk'),('api','d3d12'),('configuration','Debug'),('device_name','other'),('source_fingerprint','b'*64),('LLGL_commit','0'*40),('native_vendor_id',32902)]:
            row=copy.deepcopy(self.row);row[key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):self.check(row)

    def test_all_native_identifiers_required(self):
        for key in ('device_uuid','driver_uuid','device_luid'):
            for value in ('', '0'*len(self.row[key]), 'invalid'):
                row=copy.deepcopy(self.row);row[key]=value
                with self.subTest(key=key,value=value),self.assertRaises(ValueError):self.check(row)

    def test_copy_rounds_and_full_reference_required(self):
        for key,value in [('verified_bytes',4096),('verified_bytes',12287),('reference_rounds',2),('verified_bytes',True)]:
            row=copy.deepcopy(self.row);row[key]=value
            with self.subTest(key=key,value=value),self.assertRaises(ValueError):self.check(row)

    def test_source_review_or_name_match_does_not_prove_native_handles(self):
        self.row['native_handle_verified']=False
        with self.assertRaises(ValueError):self.check()

    def test_no_graphics_queue_rejected(self):
        self.row['queue_flags']=2
        with self.assertRaises(ValueError):self.check()

    def test_queue_index_not_assumed(self):
        self.row['queue_index']=1
        with self.assertRaises(ValueError):self.check()

    def test_validation_requires_actual_loaded_layer(self):
        self.row['validation']=True
        with self.assertRaises(ValueError):self.check(validation=True)
        self.row['loaded_validation_layer']='VkLayer_khronos_validation.dll'
        self.check(validation=True)

    def test_plain_probe_cannot_load_validation_layer(self):
        self.row['loaded_validation_layer']='VkLayer_khronos_validation.dll'
        with self.assertRaises(ValueError):self.check()

    def test_delivered_native_errors_rejected(self):
        self.row['delivered_errors']=1
        with self.assertRaises(ValueError):self.check()

    def test_minimal_reference_is_not_workload_or_performance_acceptance(self):
        for key,value in [('performance_samples',1),('gpu_ms',1.0),('workload_correctness_accepted',True),('performance_accepted',True),('instance_API_request_observed',True)]:
            row=copy.deepcopy(self.row);row[key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):self.check(row)

    def test_processes_must_match_exact_native_identity(self):
        identity=self.check()
        for key in v.IDENTITY:
            row=copy.deepcopy(self.row)
            row[key]=row[key]+1 if type(row[key]) is int else 'f'*len(row[key])
            with self.subTest(key=key),self.assertRaises(ValueError):self.check(row,reference=identity)

    def test_missing_runtime_snapshot_rejected(self):
        self.row['runtime_modules']=[]
        with self.assertRaises(ValueError):self.check()

    def test_invalid_native_field_types_rejected(self):
        for key in ('native_device_id','native_driver_version','native_api_version','queue_family','queue_flags','loader_api_version'):
            row=copy.deepcopy(self.row);row[key]=True
            with self.subTest(key=key),self.assertRaises(ValueError):self.check(row)

    def saved_verifier(self):
        path=v.ROOT/'benchmarks/reports/validation/2026-10-28-phase3-llgl-vulkan/verify.py'
        spec=importlib.util.spec_from_file_location('saved_llgl_probe',path)
        module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
        return module

    def test_saved_native_evidence_replays_without_GPU(self):
        self.assertEqual(self.saved_verifier().verify()['minimum_reference_bytes'],24576)

    def test_saved_native_output_tamper_rejected(self):
        module=self.saved_verifier()
        with tempfile.TemporaryDirectory() as directory:
            root=pathlib.Path(directory)/'evidence';shutil.copytree(module.HERE,root)
            path=root/'probe/plain-smoke.stdout';path.write_bytes(path.read_bytes()+b' ')
            with self.assertRaisesRegex(ValueError,'Archived input hash differs'):module.verify(root)

    def test_rebound_manifest_cannot_promote_receipt_to_performance(self):
        module=self.saved_verifier()
        with tempfile.TemporaryDirectory() as directory:
            root=pathlib.Path(directory)/'evidence';shutil.copytree(module.HERE,root)
            path=root/'probe/receipt.json';receipt=module.read(path);receipt['performance_accepted']=True
            path.write_bytes(json.dumps(receipt).encode())
            manifest=module.read(root/'manifest.json')
            item=next(r for r in manifest['files'] if r['path']=='probe/receipt.json')
            item.update(bytes=path.stat().st_size,sha256=module.sha(path.read_bytes()))
            (root/'manifest.json').write_text(json.dumps(manifest),encoding='utf-8')
            with self.assertRaisesRegex(ValueError,'Receipt scope drift'):module.verify(root)


if __name__=='__main__':unittest.main()
