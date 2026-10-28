"""Check the human-approved, explicitly modified LLGL profile without GPU execution."""
import json
import pathlib
import hashlib

ROOT=pathlib.Path(__file__).resolve().parents[1]
PROFILE=ROOT/'benchmarks/phase3_comparison/competitors/llgl-transfer-src-profile.json'
PIN='649c0cc8fa608312c5e4b94dae409a5859fc7553'

def read(p):return json.loads(p.read_text(encoding='utf-8'))
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def require(ok,message):
    if not ok:raise ValueError(message)

def validate(profile,root=ROOT):
    require(profile['schema_version']==1 and profile['profile_id']=='llgl-fixed-pin-transfer-src-v1' and profile['status']=='approved_for_explicit_comparisons','Wrong profile/status')
    require(profile['upstream_commit']==PIN==read(root/'benchmarks/competitors/sources.lock.json')['llgl']['commit'],'Upstream pin drift')
    require(profile['display_name']=='LLGL 649c0cc + transfer-src patch v1' and profile['runtime_evidence_profile']=='transfer-src-candidate' and
            profile['unmodified_upstream'] is profile['performance_accepted'] is profile['unmodified_upstream_correctness_accepted'] is False,'Modified profile must not be relabeled or granted performance acceptance')
    require(profile['human_decision']=='명시적 패치 프로필을 허용해 계속 진행' and profile['decision_date']=='2026-10-28','Human scope decision absent')
    require(profile['changed_upstream_files']==['sources/Renderer/Vulkan/Command/VKCommandContext.cpp'] and profile['added_lines']==10,'Patch scope drift')
    require(profile['correctness_tasks']==dict(upload='P3-CMP-03',draw='P3-CMP-04',pso='P3-CMP-05',allocation='P3-CMP-06'),'Correctness tasks drift')
    require(profile['verified_correctness']==['upload'] and profile['pending_performance_tasks']==['P3-CMP-07','P3-CMP-08','P3-CMP-09','P3-CMP-10'] and profile['GPU_timestamp_pending_task']=='P3-BEN-09','Required coverage/gates reduced')
    expected={'scope_decision':'docs/PHASE3_LLGL_UPLOAD_SCOPE.md',
        'patch':'benchmarks/reports/validation/2026-10-28-phase3-llgl-upload/patch/transfer-src.patch',
        'original_source':'benchmarks/reports/validation/2026-10-28-phase3-llgl-upload/patch/VKCommandContext.original.cpp',
        'modified_source':'benchmarks/reports/validation/2026-10-28-phase3-llgl-upload/patch/VKCommandContext.cpp',
        'upstream_failure':'benchmarks/reports/validation/2026-10-28-phase3-llgl-upload/upstream/receipt.json',
        'candidate_correctness':'benchmarks/reports/validation/2026-10-28-phase3-llgl-upload/candidate/receipt.json'}
    require(set(profile['evidence'])==set(expected),'Missing profile evidence')
    for key,name in expected.items():
        item=profile['evidence'][key]
        require(item['path']==name and item['sha256']==sha(root/name),'Profile evidence changed: '+key)
    require(profile['human_decision'] in (root/expected['scope_decision']).read_text(encoding='utf-8'),'Decision text differs')
    failure=read(root/expected['upstream_failure']);passed=read(root/expected['candidate_correctness'])
    require(failure['status']=='fixed_pin_validation_defect_reproduced' and failure['correctness_accepted'] is False and
            passed['status']=='candidate_correctness_passed' and passed['verified_images']==32 and passed['verified_bytes']==862694560 and passed['core_and_sync_controls_passed'] is True,'Native evidence does not support scoped correctness')
    return profile

if __name__=='__main__':
    validate(read(PROFILE));print('PASS explicit LLGL transfer-src v1 profile; upstream failure retained; performance and other correctness gates pending')
