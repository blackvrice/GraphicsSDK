"""Check the draw correctness extension without rewriting the frozen upload contract."""
import json
import pathlib
import hashlib
import check_llgl_transfer_profile as base

ROOT=pathlib.Path(__file__).resolve().parents[1]
PROFILE=ROOT/'benchmarks/phase3_comparison/competitors/llgl-transfer-src-draw.json'
def read(p):return json.loads(p.read_text(encoding='utf-8'))
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def require(ok,message):
    if not ok:raise ValueError(message)

def validate(plan,root=ROOT):
    base.validate(base.read(root/'benchmarks/phase3_comparison/competitors/llgl-transfer-src-profile.json'),root)
    require(type(plan['schema_version']) is int and plan['schema_version']==1 and plan['task']=='P3-CMP-04' and
            plan['profile_id']=='llgl-fixed-pin-transfer-src-v1' and plan['status']=='draw_correctness_verified','Wrong draw extension')
    parent=plan['base_contract'];require(parent['path']=='benchmarks/phase3_comparison/competitors/llgl-transfer-src-profile.json' and parent['sha256']==sha(root/parent['path']),'Base explicit profile changed')
    require(plan['verified_correctness']==['draw'] and plan['unmodified_upstream'] is plan['performance_accepted'] is False and plan['observer_free_performance_required'] is True,'Other scope/performance granted')
    require(plan['pending_tasks']==['P3-CMP-05','P3-CMP-06','P3-CMP-07','P3-CMP-08','P3-CMP-09','P3-CMP-10','P3-BEN-09'],'Required coverage dropped')
    item=plan['evidence'];require(item['path']=='benchmarks/reports/validation/2026-10-28-phase3-llgl-draw/native/receipt.json' and item['sha256']==sha(root/item['path']),'Draw receipt changed')
    r=read(root/item['path']);require(r['status']=='draw_correctness_passed' and r['profile_id']==plan['profile_id'] and r['inputs_unchanged'] is True and
        r['verified_images']==24 and r['verified_bytes']==230880 and r['native_draw_calls']==8016 and r['native_instances']==16008 and r['native_vertices']==48024 and
        r['core_and_sync_controls_passed'] is True and r['performance_accepted'] is False and r['performance_samples']==0,'Native evidence incomplete')
    require(plan['source_fingerprint']==r['source_fingerprint'] and plan['shader_header_sha256']==r['shader_header_sha256'],'Stale source/shader binding')
    require(plan['clear_strategy']=='automatic_offscreen_pass_plus_full_attachment_clear' and plan['texture_bind_policy']=='color_attachment_sampled_copy_src','Undeclared API differences')
    return plan

if __name__=='__main__':validate(read(PROFILE));print('PASS explicit LLGL draw extension; other correctness/performance gates remain pending')
