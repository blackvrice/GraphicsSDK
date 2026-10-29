"""Check narrowly scoped fresh LLGL/SDK/SDL resource performance evidence."""
import hashlib
import json
import pathlib
import check_llgl_pso_comparison as prior
import compare_llgl_allocation as comparison

ROOT=pathlib.Path(__file__).resolve().parents[1]
PROFILE=ROOT/'benchmarks/phase3_comparison/competitors/llgl-transfer-src-allocation-performance.json'

def read(p):return json.loads(p.read_text(encoding='utf-8'))

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()

def require(ok,message):
    if not ok:raise ValueError(message)

def validate(p,root=ROOT):
    prior.validate(prior.read(root/'benchmarks/phase3_comparison/competitors/llgl-transfer-src-pso-performance.json'),root)
    comparison.exact(p,dict(schema_version=1,task='P3-CMP-10',profile_id='llgl-fixed-pin-transfer-src-v1',status='allocation_performance_measured',verified_performance=['allocation'],sdks=['graphicssdk','sdl','llgl'],performance_accepted=True,unmodified_upstream=False,historical_timings_reused=False,overall_SDK_ranking=False,GPU_timestamp_measurement=False,SDL_native_handle_verified=False,native_allocation_count_verified=False,GPU_memory_reclamation_verified=False,allocator_cache_state_known=False,allocation_policy='public_device_resource_default_policy',Linux_hardware_accepted=False,pending_tasks=['P3-BEN-07', 'P3-BEN-08', 'P3-BEN-09', 'P3-LBEN-01', 'P3-LBEN-02', 'P3-LBEN-03', 'P3-LBEN-04', 'P3-GATE-02'],measurement_samples=1800,measurement_values=3600,measurement_processes=60),'resource performance extension scope changed')
    for key,name in [('base_contract','llgl-transfer-src-profile.json'),('prior_performance_contract','llgl-transfer-src-pso-performance.json')]:
        item=p[key];require(item['path']=='benchmarks/phase3_comparison/competitors/'+name and item['sha256']==sha(root/item['path']),'Historical contract changed')
    item=p['evidence'];require(item['path']=='benchmarks/reports/validation/2026-10-29-phase3-three-sdk-allocation/native/receipt.json' and item['sha256']==sha(root/item['path']),'Comparison receipt changed')
    result=comparison.check((root/item['path']).parent);require(result['performance_accepted'] is True,'resource comparison not accepted')
    r=read(root/item['path']);require(p['LLGL_source_fingerprint']==r['LLGL_allocation_fingerprint'] and p['SDK_source_fingerprint']==r['SDK_source_fingerprint'] and p['SDK_allocation_fingerprint']==r['SDK_allocation_fingerprint'] and p['identity']==r['identity'],'Source/current hardware binding differs')
    return p

if __name__=='__main__':validate(read(PROFILE));print('PASS fresh three-SDK resource performance only; other workloads, GPU timestamp and platform gates remain pending')
