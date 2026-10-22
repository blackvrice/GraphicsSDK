"""Run short GBV factor-isolation experiments; these are not memory acceptance."""
import argparse
import hashlib
import json
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/"tools"))
from audit_licenses import sdk_fingerprint


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--output",required=True,type=pathlib.Path)
    p.add_argument("--seconds",default=30,type=int)
    args = p.parse_args()
    args.output.mkdir(parents=True,exist_ok=False)
    exe = ROOT/"out/memory-validation-probe/Release/memory_validation_probe.exe"
    fingerprint = sdk_fingerprint(ROOT)
    assert fingerprint.encode() in exe.read_bytes()
    paths = [exe,ROOT/"tools/MemoryStress.cpp",ROOT/"tools/memory_validation_probe/main.cpp",pathlib.Path(__file__),ROOT/"tools/memory_validation_probe/CMakeLists.txt",ROOT/"out/core/Release/GraphicsSDKCore.lib"]
    hashes = {str(path):hashlib.sha256(path.read_bytes()).hexdigest() for path in paths}
    cases = []
    for mode in ("reuse-pipeline","reuse-commands","churn-pipeline","counter-contract"):
        output = (args.output/(mode+".jsonl")).resolve()
        command = [str(exe),mode,str(args.seconds),str(output)]
        with (args.output/(mode+".log")).open("xb") as log:
            process = subprocess.Popen(command,stdout=log,stderr=subprocess.STDOUT)
            print("Started diagnostic "+mode+" pid="+str(process.pid),flush=True)
            code = process.wait(timeout=args.seconds+120)
        assert code == 0,mode
        records = [json.loads(line) for line in output.read_text().splitlines()]
        if mode=="counter-contract":
            assert records[0]["counter_contract_passed"] and records[0]["expected_injected_errors"]==6
            cases.append({"case":mode,"pid":process.pid,"command":command,"returncode":code,**records[0]})
            print("PASS callback counter overflow/filter/clear/thread/owner contracts",flush=True)
            continue
        assert records[-1]["functional_passed"] and records[-1]["validation_errors"] == 0
        first = next(r for r in records if r.get("phase")=="baseline")
        last = next(r for r in records if r.get("phase")=="released")
        cleared = next(r for r in records if r.get("phase")=="after_message_clear")
        assert all(last[n]==0 for n in ("objects","resources","allocation_bytes","pending","pending_refs"))
        cases.append({"case":mode,"pid":process.pid,"command":command,"returncode":code,"frames":records[-1]["frames"],
                      "full_images":records[-1]["full_images"],"private_growth_bytes":last["private_bytes"]-first["private_bytes"],
                      "private_before_message_clear":last["private_bytes"],"private_after_message_clear":cleared["private_bytes"],
                      "stored_messages_before_live_report":last["stored_messages"],"discarded_messages":last["discarded_messages"],
                      "SDK_owned_after_release":0,"diagnostic_only":True})
        print("PASS references "+mode+"; private growth MiB="+str(cases[-1]["private_growth_bytes"]/1048576),flush=True)
    assert sdk_fingerprint(ROOT)==fingerprint
    assert all(hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()==sha for path,sha in hashes.items())
    (args.output/"summary.json").write_text(json.dumps({"SDK_source_fingerprint":fingerprint,"frozen_inputs":hashes,"diagnostic_only":True,"memory_acceptance_claim":False,"cases":cases},indent=2)+"\n",encoding="utf-8")


if __name__=="__main__":
    main()
