"""Check the dedicated native GPU comparison without promoting historical CPU results."""
import pathlib
import run_draw_gpu_timestamps as v

E=v.ROOT/'benchmarks/reports/validation/2026-10-29-phase3-draw-gpu'

def check(folder=E):
    result=v.check(folder/'native');s=v.read(folder/'summary.json');r=v.read(folder/'native/receipt.json')
    v.exact(s,dict(task='P3-BEN-09',status='native_GPU_timestamp_comparison_completed',processes=111,measurement_GPU_samples=900,measurement_CPU_samples=2700,verified_images=3206,verified_bytes=30841720,native_render_device_and_queue_verified=True,actual_counter_availability_and_fence_verified=True,measurement_samples_removed=0,CPU_draw_campaigns_unchanged=True,historical_timings_reused=False,overall_SDK_ranking=False,uninstrumented_GPU_time_claimed=False,Linux_hardware_accepted=False),'GPU acceptance scope changed')
    v.require(s['identity']==r['identity'] and s['groups']==r['groups'] and s['layer_fingerprint']==r['layer_fingerprint'] and s['SDK_source_fingerprint']==r['SDK_source_fingerprint'] and s['draw_fingerprint']==r['draw_fingerprint'],'GPU summary binding differs')
    preserved=v.read(folder/'cpu-campaign-preservation.json');v.exact(preserved,dict(task='P3-BEN-09',CPU_draw_campaigns_unchanged=True,historical_CPU_timings_reused=False),'CPU campaign preservation scope changed')
    for item in preserved['files']:
        path=v.ROOT/item['path'];v.require(v.sha(path)==item['sha256']==item['base_blob_sha256'],'Historical CPU comparison changed')
    return result

if __name__=='__main__':print(check())
