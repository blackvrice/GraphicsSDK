"""Fixed-working-set GBV gate. It cannot accept the unbounded texture-churn gate."""
import argparse
import hashlib
import json
import pathlib
import re
import statistics

from analyze_memory_stress import OWNED, POLICY as MEMORY_POLICY, read_records, require, number

POLICY=pathlib.Path(__file__).with_name("bounded_gbv_policy.json")


def analyze(records,fingerprint):
    p=json.loads(POLICY.read_text());old=json.loads(MEMORY_POLICY.read_text())
    require(len(records)>=10 and all(isinstance(r,dict) for r in records),"Missing evidence records")
    require(re.fullmatch(r"[a-f0-9]{64}",fingerprint) is not None,"Invalid SDK fingerprint")
    h,f=records[0],records[-1]
    require(h.get("case")=="bounded-reuse" and h.get("schema")==1 and h.get("sdk_sha256")==fingerprint,"Wrong bounded profile/source")
    require(h.get("gpu_state_tracking") is True,"GBV state tracking must remain enabled")
    require(type(h.get("seconds")) is int and h["seconds"]>=p["minimum_seconds"] and type(h.get("warmup")) is int and h["warmup"]>=p["minimum_warmup"],"Short smoke is not bounded acceptance")
    require((h.get("ring"),h.get("side"),h.get("copy_bytes"))==(8,64,262144),"Workload mismatch")
    require(isinstance(h.get("adapter"),str) and h["adapter"] and type(h.get("luid_low")) is int and type(h.get("luid_high")) is int,"Missing adapter identity")
    samples=records[1:-1];phases=[r.get("phase") for r in samples]
    require(phases[:7]==["baseline","cpu_held","cpu_released","gpu_held","gpu_released","pool_ready","measurement_start"] and phases[-1]=="released" and all(n=="drained" for n in phases[7:-1]),"Missing/invalid control or sample phases")
    previous=None
    for r in samples:
        require(number(r.get("elapsed")) and r["elapsed"]>=0,"Invalid elapsed time")
        for k in (*OWNED,"frames","private_bytes","working_set_bytes","handles"):
            require(type(r.get(k)) is int and r[k]>=0,"Invalid/missing counter: "+k)
        for segment in ("local","nonlocal"):
            status=r.get("gpu_"+segment+"_hresult")
            require(type(status) is int and 0<=status<2**31,"Hardware observation unavailable")
            for suffix in ("bytes","budget"):
                value=r.get("gpu_"+segment+"_"+suffix)
                require(type(value) is int and value>=0,"Missing DXGI memory")
        require(r["frames"]%8==0,"Partial reference batch")
        if previous:require(r["elapsed"]>=previous["elapsed"] and r["frames"]>=previous["frames"],"Non-monotonic evidence")
        previous=r
    a,c,cr,g,gr,pool,start=samples[:7]
    for r in (a,c,cr,gr,samples[-1]):require(all(r[n]==0 for n in OWNED),"Control/final resources not released")
    delta=old["cpu_control_minimum_delta_bytes"]
    require(c["private_bytes"]-a["private_bytes"]>=delta and c["private_bytes"]-cr["private_bytes"]>=delta,"CPU retain/release control failed")
    require(g["objects"]==g["resources"]==1 and g["allocation_bytes"]>=old["gpu_control_minimum_allocation_bytes"] and g["pending"]==g["pending_refs"]==0,"GPU allocation control failed")
    delta=old["hardware_gpu_control_minimum_usage_delta_bytes"]
    require(g["gpu_local_bytes"]-cr["gpu_local_bytes"]>=delta and g["gpu_local_bytes"]-gr["gpu_local_bytes"]>=delta,"GPU usage control failed")
    require(start["elapsed"]-pool["elapsed"]>=h["warmup"],"Warmup timing mismatch")
    require(pool["objects"]==p["tracked_objects"] and pool["resources"]==p["unique_resources"] and 0<pool["allocation_bytes"]<=p["maximum_allocation_bytes"],"Unexpected working set")
    drained=samples[7:-1]
    require(len(drained)>=p["minimum_drained_samples"],"Missing checkpoints")
    for r in (pool,start,*drained):
        require(r["objects"]==pool["objects"] and r["resources"]==pool["resources"] and r["allocation_bytes"]==pool["allocation_bytes"] and r["pending"]==r["pending_refs"]==0,"Working set changed or queue did not drain")
    times=[start["elapsed"],*[r["elapsed"] for r in drained],samples[-1]["elapsed"]]
    require(all(b-a<=p["maximum_sample_gap_seconds"] for a,b in zip(times,times[1:])),"Checkpoint gap")
    require(f.get("functional_passed") is True and f.get("validation_counter")=="unfiltered_callback" and type(f.get("validation_errors")) is int and f["validation_errors"]==0,"Incomplete native error coverage")
    require(number(f.get("measured_seconds")) and f["measured_seconds"]>=h["seconds"] and samples[-1]["elapsed"]-start["elapsed"]>=h["seconds"] and abs(samples[-1]["elapsed"]-start["elapsed"]-f["measured_seconds"])<1,"Incomplete duration")
    require(f.get("frames")==samples[-1]["frames"] and f.get("full_images")==f["frames"]*3 and f.get("verified_buffer_bytes")==f["frames"]*h["copy_bytes"],"Incomplete references")
    require(f["frames"]-start["frames"]>=h["seconds"]*p["minimum_measured_frames_per_second"],"Insufficient measured work")
    tail=[r for r in drained if r["elapsed"]>=start["elapsed"]+h["seconds"]*.5];require(len(tail)>=8,"Insufficient plateau samples");metrics={}
    for name,limit in old["limits"].items():
        y=[r[name] for r in tail];x=[(r["elapsed"]-tail[0]["elapsed"])/60 for r in tail];xm,ym=statistics.mean(x),statistics.mean(y);denominator=sum((a-xm)**2 for a in x)
        require(denominator>0,"Insufficient plateau time span")
        slope=sum((a-xm)*(b-ym) for a,b in zip(x,y))/denominator;q=max(2,len(y)//4);drift=statistics.median(y[-q:])-statistics.median(y[:q]);spread=max(y)-min(y)
        require(drift<=limit["median_drift"] and slope<=limit["slope_per_minute"] and spread<=limit["range"],"Memory limit failed: "+name)
        metrics[name]={"median_drift":drift,"slope_per_minute":slope,"range":spread}
    return {"bounded_working_set_accepted":True,"unbounded_texture_churn_accepted":False,"SDK_source_fingerprint":fingerprint,"measured_seconds":f["measured_seconds"],"frames":f["frames"],"full_images":f["full_images"],"verified_buffer_bytes":f["verified_buffer_bytes"],"drained_samples":len(drained),"tail_samples":len(tail),"working_set_objects":pool["objects"],"working_set_resources":pool["resources"],"working_set_allocation_bytes":pool["allocation_bytes"],"final_owned_resources":0,"metrics":metrics}


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument("input",type=pathlib.Path);p.add_argument("--fingerprint",required=True);p.add_argument("--output",required=True,type=pathlib.Path);a=p.parse_args()
    result=analyze(read_records(a.input),a.fingerprint)
    result.update(bounded_policy_sha256=hashlib.sha256(POLICY.read_bytes()).hexdigest(),memory_limits_policy_sha256=hashlib.sha256(MEMORY_POLICY.read_bytes()).hexdigest())
    with a.output.open("x",encoding="utf-8") as out:out.write(json.dumps(result,indent=2)+"\n")
    print("PASS fixed-working-set GBV gate; unbounded texture churn remains unaccepted")


if __name__=="__main__":main()
