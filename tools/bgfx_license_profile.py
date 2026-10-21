"""Additive inventory configuration for the pinned single-thread bgfx D3D12 profile."""
import hashlib
import json
import re
from run_benchmarks import bgfx_fingerprint


def configure(root, policy, traces, artifacts, projects, directories, generated):
    lock_path=root/"benchmarks/competitors/bgfx-build.lock.json"
    review_path=root/"licenses/bgfx-policy.json"
    deps=json.loads(lock_path.read_text(encoding="utf-8"))["dependencies"]
    review=json.loads(review_path.read_text(encoding="utf-8"))
    for name, dep in deps.items():
        if review["reviewed_commits"].get(name)!=dep["commit"]:
            raise ValueError(f"bgfx dependency needs a new license review: {name}")
        directories[name]=dep["directory"]
    policy["reviewed_commits"].update(review["reviewed_commits"])
    policy["components"]+=review["components"]
    for name in ("benchmark", "benchmark_core"):
        traces[name]=traces[name].replace("out/bench/","out/bench-bgfx/")
        artifacts[name]=artifacts[name].replace("out/bench/","out/bench-bgfx/")
    projects["benchmark"]=projects["benchmark"].replace("out/bench/","out/bench-bgfx/")
    for name in ("bgfx","bx","bimg"):
        traces[name]=f"out/bench-bgfx/bench_{name}.dir/Release"
        artifacts[name]=f"out/bench-bgfx/Release/bench_{name}.lib"
    fingerprint=bgfx_fingerprint(root)
    for name in ("bgfx","benchmark"):
        if fingerprint.encode() not in (root/artifacts[name]).read_bytes():
            raise ValueError(f"Stale bgfx compiled identity in {name}")
    commands=[]
    for name in ("bgfx","bx","bimg","benchmark","benchmark_core"):
        logs=sorted((root/traces[name]).rglob("CL.command.1.tlog"))
        if not logs: raise ValueError(f"Missing bgfx compiler command evidence: {name}")
        for log in logs:
            command=log.read_bytes().decode("utf-16")
            if not all(re.search(flag,command) for flag in (r"/O2(?:\s|$)",r"/MD(?:\s|$)")) or re.search(r"/(?:GL|MTd?)(?:\s|$)",command):
                raise ValueError(f"Compiler options differ from /O2 /MD no-LTO: {name}")
            if name=="bgfx" and not all(flag in command for flag in ("BGFX_CONFIG_MULTITHREADED=0","BGFX_CONFIG_RENDERER_DIRECT3D12=1","BGFX_CONFIG_VIDEO=0")):
                raise ValueError("bgfx backend/thread/video configuration differs from the reviewed profile")
            commands.append({"path":log.relative_to(root).as_posix(),"sha256":hashlib.sha256(log.read_bytes()).hexdigest()})
    def record(path):
        return {"path":path.relative_to(root).as_posix(),"sha256":hashlib.sha256(path.read_bytes()).hexdigest()}
    return {"dependencies":deps,"review_key":"bgfx_build_review","link_names":["benchmark","llgl","llgl_d3d12"],
            "evidence":{"lock":record(lock_path),"policy":record(review_path),"compiler_command_evidence":commands,
                        "build_fingerprint":fingerprint,"dirent_terms_provenance":record(root/"licenses/provenance/dirent-license-source.json"),
                        "note":"Compiler-read closure includes amalgamated backend files with preprocessed-out bodies. No competitor/profiler/driver binaries are added to the own SDK package."}}
