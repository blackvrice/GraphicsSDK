import pathlib
root=pathlib.Path.cwd();s=(root/'out/finish_swap_guards.py').read_text()
for a,b in [('phase2-swap-guards','phase2-copy-atomicity'),('phase2-fence-wait','phase2-swap-guards'),
            ('windows-sdk-swap-guards','windows-sdk-copy-atomicity'),('windows-benchmarks-swap-guards','windows-benchmarks-copy-atomicity'),
            ('windows-x64-swap-guards.zip','windows-x64-copy-atomicity.zip'),('out/swap-guard-repro/build','out/copy-rejection-repro/build'),
            ('installed-swap-repro','installed-copy-repro'),('sdk-swaps','sdk-copy'),
            ('165288f2cd9e3f820d5e08333ea0d129533a5fcab201490dee41b1df1c8be8fa','50fe75fea32a5444531868da9f1a225a05573d78ba12c52bb622a1ea80d35edf'),
            ("('core',32)","('core',34)"),('42,1,8','43,1,9'),("len(old['SDK_packages'])==9","len(old['SDK_packages'])==10")]:
    assert a in s,a;s=s.replace(a,b)
s=s.replace("assert old_repro['wrong_buffer_recording_accepted']==old_repro['destroyed_chain_recording_accepted']==1", "assert old_repro['rejected_copy_poisoned_submit']==1 and old_repro['retained_resources_after_drop']==3")
s=s.replace("assert new_repro['wrong_buffer_recording_accepted']==new_repro['destroyed_chain_recording_accepted']==0 and new_repro['sdk']==fp", "assert new_repro['rejected_copy_poisoned_submit']==0 and new_repro['retained_resources_after_drop']==1 and new_repro['sdk']==fp")
start=s.index('events=[json.loads(line)')
end=s.index("assert all(r['returncode']==0 for r in load(v/'audit-commands.json'))",start)
s=s[:start]+'''events=[json.loads(line) for line in txt(v/'core-native.log').splitlines() if line.startswith('{"event":"copy_')]
controls=[e for e in events if e['event']=='copy_rejection_case'];results=[e for e in events if e['event']=='copy_atomicity_result']
assert len(controls)==48 and len(results)==2 and {e['validation'] for e in results}=={False,True}
assert all(e['adapters']==3 and e['cases']==24 and e['full_images']==6 and e['verified_pixels']==2646 and e['verified_bytes']==28164 and e['rejections']==1557 and e['passed'] and e['sdk_sha256']==fp for e in results)
assert all(e['case'] in range(8) and e['repetitions']==64 and e['resources_after_drop']==1 and e['submit_passed'] for e in controls)
'''+s[end:]
old="('preserve_swap_repro.py','pilot_swap_guards.py','verify_swap_guards.py','package_swap_guards.py','make_swap_finalizer.py','finish_swap_guards.py')"
assert old in s;s=s.replace(old,"('preserve_copy_repro.py','pilot_copy_atomicity.py','verify_copy_atomicity.py','package_copy_atomicity.py','make_copy_finalizer.py','finish_copy_atomicity.py')")
start=s.index('summary=dict(');end=s.index("write(report/'summary.json',summary)",start)
s=s[:start]+'''summary=dict(SDK_source_fingerprint=fp,production_SDK_changed=True,public_virtual_ABI_changed=False,phase2_complete=False,memory_stress_accepted=False,Intel_AV_cause_resolved=False,
    before_repro_exit=2,after_repro_exit=0,before_repro_rejected_copy_resources=3,after_repro_rejected_copy_resources=1,
    validation_rejection_atomicity=True,accepted_shadow_state_and_lifetimes_preserved=True,accepted_use_stale_rejection_preserved=True,
    adapters=adapters,rejection_cases=controls,repeated_validation_rejections=3072,total_rejections=3114,reference_images=12,reference_pixels=5292,verified_payload_bytes=56328,
    core_ctest=34,no_backend_ctest=6,common_correctness_ctest=72,presentation_ctest=7,python_tests=159,compiler_retries=0,
    window_campaign_full_images=images,window_campaign_verified_pixels=pixels,audits=audits,SDK_package=pkg,previous_SDK_packages=old['SDK_packages'],
    old_live_out_artifacts_rebuilt=True,historical_evidence_preserved=True,original_policy_unchanged=True,original_failed_memory_gate_preserved=True,
    new_long_soak_claim=False,continuous_peak_claim=False,performance_claim=False,git_precommit_fsck_exit=0)
'''+s[end:]
s=s.replace("f'benchmarks/reports/sources/swap-guards-", "f'benchmarks/reports/sources/copy-atomicity-")
s=s.replace('90 full images; 42,270 pixels; 258 rejections; ten SDK ZIPs','12 full images; 56,328 payload bytes; 3,114 rejections; eleven SDK ZIPs')
(root/'out/finish_copy_atomicity.py').write_text(s)
print('Created copy atomicity finalizer with asserted profile and result changes')
