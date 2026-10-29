"""Check the PSO correctness extension of the approved LLGL transfer-src profile."""
import hashlib
import json
import pathlib
import check_llgl_draw_profile as draw

ROOT=pathlib.Path(__file__).resolve().parents[1]
PROFILE=ROOT/'benchmarks/phase3_comparison/competitors/llgl-transfer-src-pso.json'

def read(p):return json.loads(p.read_text(encoding='utf-8'))

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()

def require(ok,message):
    if not ok:raise ValueError(message)

def validate(plan,root=ROOT):
    draw.validate(draw.read(root/'benchmarks/phase3_comparison/competitors/llgl-transfer-src-draw.json'),root)
    require(type(plan['schema_version']) is int and plan['schema_version']==1 and plan['task']=='P3-CMP-05' and plan['profile_id']=='llgl-fixed-pin-transfer-src-v1' and plan['status']=='pso_correctness_verified','Wrong PSO extension')
    for key,name in [('base_contract','llgl-transfer-src-profile.json'),('prior_draw_contract','llgl-transfer-src-draw.json')]:
        parent=plan[key];require(parent['path']=='benchmarks/phase3_comparison/competitors/'+name and parent['sha256']==sha(root/parent['path']),'Historical profile contract changed')
    require(plan['verified_correctness']==['pso'] and plan['unmodified_upstream'] is plan['performance_accepted'] is False and plan['observer_free_performance_required'] is True,'Other scope/performance granted')
    require(plan['pending_tasks']==['P3-CMP-06','P3-CMP-07','P3-CMP-08','P3-CMP-09','P3-CMP-10','P3-BEN-09'],'Required coverage dropped')
    for key in ('GPU_memory_reclamation_verified','native_pipeline_count_verified','driver_cache_state_known'):require(plan[key] is False,'Unverified physical/cache claim')
    item=plan['evidence'];require(item['path']=='benchmarks/reports/validation/2026-10-28-phase3-llgl-pso/native/receipt.json' and item['sha256']==sha(root/item['path']),'PSO receipt changed')
    r=read(root/item['path'])
    expected=dict(task='P3-CMP-05',status='pso_correctness_passed',profile_id=plan['profile_id'],inputs_unchanged=True,verified_images=16,verified_bytes=153920,created_pipelines=72,released_pipeline_handles=72,used_pipeline_regions=72,created_shader_handles=144,released_shader_handles=144,native_draw_calls=72,native_instances=72,native_vertices=216,core_and_sync_controls_passed=True,performance_accepted=False,performance_samples=0,GPU_memory_reclamation_verified=False,native_pipeline_count_verified=False,driver_cache_state_known=False)
    require(all(k in r and type(r[k]) is type(v) and r[k]==v for k,v in expected.items()),'Native PSO lifecycle evidence incomplete')
    require(plan['source_fingerprint']==r['source_fingerprint'] and plan['shader_header_sha256']==r['shader_header_sha256'],'Stale source/shader binding')
    require(plan['clear_strategy']=='automatic_offscreen_pass_plus_full_attachment_clear' and plan['texture_bind_policy']=='color_attachment_sampled_copy_src' and plan['includes_shader_object_create_release'] is True,'Undeclared public API mapping')
    return plan

if __name__=='__main__':validate(read(PROFILE));print('PASS explicit LLGL PSO extension; allocation/performance gates remain pending')
