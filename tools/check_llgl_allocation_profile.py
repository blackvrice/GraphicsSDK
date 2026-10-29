"""Check the resource correctness extension without changing earlier LLGL contracts."""
import hashlib
import json
import pathlib
import check_llgl_pso_profile as prior

ROOT=pathlib.Path(__file__).resolve().parents[1]
PROFILE=ROOT/'benchmarks/phase3_comparison/competitors/llgl-transfer-src-allocation.json'

def read(p):return json.loads(p.read_text(encoding='utf-8'))

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()

def require(ok,message):
    if not ok:raise ValueError(message)

def validate(plan,root=ROOT):
    prior.validate(prior.read(root/'benchmarks/phase3_comparison/competitors/llgl-transfer-src-pso.json'),root)
    expected=dict(schema_version=1,task='P3-CMP-06',profile_id='llgl-fixed-pin-transfer-src-v1',status='allocation_correctness_verified',verified_correctness=['allocation'],unmodified_upstream=False,performance_accepted=False,observer_free_performance_required=True,
        pending_tasks=['P3-CMP-07','P3-CMP-08','P3-CMP-09','P3-CMP-10','P3-BEN-09'],GPU_memory_reclamation_verified=False,native_allocation_count_verified=False,allocator_cache_state_known=False,
        usage_policy=dict(buffer='llgl_vertex_buffer_copy_src_dst',texture='sampled_rgba8_2d'),copy_submission_policy='separate_upload_download_completed_fence_idle',staging_in_create_release_boundary=False)
    require(all(k in plan and type(plan[k]) is type(v) and plan[k]==v for k,v in expected.items()),'Resource extension scope/policy differs')
    for key,name in [('base_contract','llgl-transfer-src-profile.json'),('prior_pso_contract','llgl-transfer-src-pso.json')]:
        item=plan[key];require(item['path']=='benchmarks/phase3_comparison/competitors/'+name and item['sha256']==sha(root/item['path']),'Historical contract changed')
    item=plan['evidence'];require(item['path']=='benchmarks/reports/validation/2026-10-29-phase3-llgl-allocation/native/receipt.json' and item['sha256']==sha(root/item['path']),'Native resource receipt changed')
    r=read(root/item['path']);expected=dict(task='P3-CMP-06',status='allocation_correctness_passed',profile_id=plan['profile_id'],source_fingerprint=plan['source_fingerprint'],inputs_unchanged=True,
        created_resources=16016,released_resource_handles=16016,used_resources=16016,verified_resources=16016,unique_native_resources_checked=16016,verified_images=8008,verified_buffers=8008,verified_bytes=109837728,
        core_and_sync_controls_passed=True,GPU_memory_reclamation_verified=False,native_allocation_count_verified=False,allocator_cache_state_known=False,performance_samples=0,performance_accepted=False)
    require(all(k in r and type(r[k]) is type(v) and r[k]==v for k,v in expected.items()),'Resource lifecycle evidence incomplete')
    return plan

if __name__=='__main__':validate(read(PROFILE));print('PASS explicit LLGL resource correctness; all performance gates remain pending')
