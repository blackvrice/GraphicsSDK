import pathlib
root=pathlib.Path.cwd()
s=(root/'out/finish_fence_wait.py').read_text()
for a,b in [('phase2-fence-wait','phase2-swap-guards'),('phase2-budget-campaign','phase2-fence-wait'),
            ('windows-sdk-fence-wait','windows-sdk-swap-guards'),('windows-benchmarks-fence-wait','windows-benchmarks-swap-guards'),
            ('windows-x64-fence-wait.zip','windows-x64-swap-guards.zip'),('out/wait-timeout-repro/build','out/swap-guard-repro/build'),
            ('installed-timeout-repro','installed-swap-repro'),('sdk-waits','sdk-swaps'),
            ('07260dcfd152001342b801d0206bab594109b5d9046e58eff5694b4ba98a1fa6','165288f2cd9e3f820d5e08333ea0d129533a5fcab201490dee41b1df1c8be8fa'),
            ("('core',30)","('core',32)"),('41,1,7','42,1,8'),("len(old['SDK_packages'])==8","len(old['SDK_packages'])==9")]:
    assert a in s,a
    s=s.replace(a,b)
start=s.index("assert 'completed_negative_timeout_accepted=1")
end=s.index("old=load(v/'historical-before.json')",start)
s=s[:start]+'''old_repro=load(v/'before-repro.log');new_repro=load(v/'installed-swap-repro-1.log')
assert old_repro['wrong_buffer_recording_accepted']==old_repro['destroyed_chain_recording_accepted']==1
assert new_repro['wrong_buffer_recording_accepted']==new_repro['destroyed_chain_recording_accepted']==0 and new_repro['sdk']==fp
'''+s[end:]
start=s.index("assert all(r['returncode']==0 for r in load(v/'pilot2-commands.json'))")
end=s.index("assert all(r['returncode']==0 for r in load(v/'audit-commands.json'))",start)
s=s[:start]+'''assert all(r['returncode']==0 for r in load(v/'pilot-commands.json'))
events=[json.loads(line) for line in txt(v/'core-native.log').splitlines() if line.startswith('{"event":"swap_guard_')]
controls=[e for e in events if e['event']=='swap_guard_ring'];results=[e for e in events if e['event']=='swap_guard_result']
assert len(controls)==18 and len(results)==2 and {e['validation'] for e in results}=={False,True}
assert all(e['adapters']==3 and e['ring_cases']==9 and e['full_images']==45 and e['verified_pixels']==21135 and e['rejections']==129 and e['recoveries']==3 and e['passed'] and e['sdk_sha256']==fp for e in results)
assert all(e['buffers'] in (2,3,4) and e['full_images']==4 and e['passed'] for e in controls)
'''+s[end:]
old="('preserve_wait_repro.py','pilot_fence_wait.py','pilot2_fence_wait.py','verify_fence_wait.py','package_fence_wait.py','finish_fence_wait.py','finish_fence_wait-initial.py')"
assert old in s
s=s.replace(old,"('preserve_swap_repro.py','pilot_swap_guards.py','verify_swap_guards.py','package_swap_guards.py','make_swap_finalizer.py','finish_swap_guards.py')")
start=s.index('summary=dict(')
end=s.index("write(report/'summary.json',summary)",start)
s=s[:start]+'''summary=dict(SDK_source_fingerprint=fp,production_SDK_changed=True,public_virtual_ABI_changed=False,phase2_complete=False,memory_stress_accepted=False,Intel_AV_cause_resolved=False,
    before_repro_exit=2,after_repro_exit=0,noncurrent_recording_rejected=True,destroyed_chain_recording_rejected=True,present_version_invalidation=True,
    adapters=adapters,ring_cases=controls,reference_images=90,reference_pixels=42270,rejections=258,controlled_recoveries=6,
    core_ctest=32,no_backend_ctest=6,common_correctness_ctest=72,presentation_ctest=7,python_tests=159,compiler_retries=0,
    window_campaign_full_images=images,window_campaign_verified_pixels=pixels,audits=audits,SDK_package=pkg,previous_SDK_packages=old['SDK_packages'],
    old_live_out_artifacts_rebuilt=True,historical_evidence_preserved=True,original_policy_unchanged=True,original_failed_memory_gate_preserved=True,
    new_long_soak_claim=False,continuous_peak_claim=False,performance_claim=False,git_precommit_fsck_exit=0)
'''+s[end:]
s=s.replace("f'benchmarks/reports/sources/fence-wait-", "f'benchmarks/reports/sources/swap-guards-")
s=s.replace("24,576 zero polls; 384 timed retries; 18 native registrations; nine SDK ZIPs", "90 full images; 42,270 pixels; 258 rejections; ten SDK ZIPs")
(root/'out/finish_swap_guards.py').write_text(s)
print('Created adapted finalizer; exact profile/count changes asserted')
