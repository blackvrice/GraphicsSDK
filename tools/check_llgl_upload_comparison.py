"""Check narrowly scoped fresh LLGL/SDK/SDL upload performance evidence."""
import hashlib
import json
import pathlib
import check_llgl_allocation_profile as prior
import compare_llgl_upload as comparison

ROOT=pathlib.Path(__file__).resolve().parents[1]
PROFILE=ROOT/'benchmarks/phase3_comparison/competitors/llgl-transfer-src-upload-performance.json'

def read(p):return json.loads(p.read_text(encoding='utf-8'))

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()

def require(ok,message):
    if not ok:raise ValueError(message)

def validate(p,root=ROOT):
    prior.validate(prior.read(root/'benchmarks/phase3_comparison/competitors/llgl-transfer-src-allocation.json'),root)
    comparison.exact(p,dict(schema_version=1,task='P3-CMP-07',profile_id='llgl-fixed-pin-transfer-src-v1',status='upload_performance_measured',verified_performance=['upload'],sdks=['graphicssdk','sdl','llgl'],performance_accepted=True,unmodified_upstream=False,historical_timings_reused=False,overall_SDK_ranking=False,GPU_timestamp_measurement=False,SDL_native_handle_verified=False,native_allocation_count_verified=False,GPU_memory_reclamation_verified=False,Linux_hardware_accepted=False,pending_tasks=['P3-CMP-08','P3-CMP-09','P3-CMP-10','P3-BEN-09'],measurement_samples=1800,measurement_processes=60),'Upload performance extension scope changed')
    for key,name in [('base_contract','llgl-transfer-src-profile.json'),('prior_correctness_contract','llgl-transfer-src-allocation.json')]:
        item=p[key];require(item['path']=='benchmarks/phase3_comparison/competitors/'+name and item['sha256']==sha(root/item['path']),'Historical contract changed')
    item=p['evidence'];require(item['path']=='benchmarks/reports/validation/2026-10-29-phase3-three-sdk-upload/native/receipt.json' and item['sha256']==sha(root/item['path']),'Comparison receipt changed')
    result=comparison.check((root/item['path']).parent);require(result['performance_accepted'] is True,'Upload comparison not accepted')
    r=read(root/item['path']);require(p['LLGL_source_fingerprint']==r['LLGL_upload_fingerprint'] and p['SDK_source_fingerprint']==r['SDK_source_fingerprint'] and p['SDK_upload_fingerprint']==r['SDK_upload_fingerprint'] and p['identity']==r['identity'],'Source/current hardware binding differs')
    return p

if __name__=='__main__':validate(read(PROFILE));print('PASS fresh three-SDK upload performance only; other workloads, GPU timestamp and platform gates remain pending')
