"""Explicit additive configuration for the reviewed Diligent benchmark build."""
import hashlib
import json
import re


def configure(root, policy, traces, artifacts, projects, directories, generated):
    # Kept separate so the original SDK/LLGL/SDL inventory policy stays intact.
    review_path = root / "licenses/diligent-policy.json"
    review = json.loads(review_path.read_text())
    lock_path = root / "benchmarks/competitors/diligent-build.lock.json"
    dependencies = json.loads(lock_path.read_text())["dependencies"]
    for name, dep in dependencies.items():
        if review["reviewed_commits"].get(name) != dep["commit"]:
            raise ValueError(f"Diligent dependency needs a new license review: {name}")
        directories[name] = dep["directory"]
    policy["reviewed_commits"].update(review["reviewed_commits"])
    policy["components"] += review["components"]
    for name in ("benchmark", "benchmark_core"):
        traces[name] = traces[name].replace("out/bench/", "out/bench-diligent/")
        artifacts[name] = artifacts[name].replace("out/bench/", "out/bench-diligent/")
    projects["benchmark"] = projects["benchmark"].replace("out/bench/", "out/bench-diligent/")
    # Enumerate reviewed targets rather than accepting any newly found library.
    targets = {
        "diligent_common": ("Common", "Diligent-Common"),
        "diligent_primitives": ("Primitives", "Diligent-Primitives"),
        "diligent_platform": ("Platforms/Basic", "Diligent-BasicPlatform"),
        "diligent_win32": ("Platforms/Win32", "Diligent-Win32Platform"),
        "diligent_accessories": ("Graphics/GraphicsAccessories", "Diligent-GraphicsAccessories"),
        "diligent_engine": ("Graphics/GraphicsEngine", "Diligent-GraphicsEngine"),
        "diligent_d3dbase": ("Graphics/GraphicsEngineD3DBase", "Diligent-GraphicsEngineD3DBase"),
        "diligent_nextgen": ("Graphics/GraphicsEngineNextGenBase", "Diligent-GraphicsEngineNextGenBase"),
        "diligent_shader": ("Graphics/ShaderTools", "Diligent-ShaderTools"),
        "diligent_static": ("Graphics/GraphicsEngineD3D12", "Diligent-GraphicsEngineD3D12-static"),
        "diligent_shared": ("Graphics/GraphicsEngineD3D12", "Diligent-GraphicsEngineD3D12-shared"),
    }
    for name, (directory, target) in targets.items():
        base = f"out/bench-diligent/diligent/{directory}"
        traces[name] = f"{base}/{target}.dir/Release"
        artifacts[name] = f"{base}/Release/{target}.lib" if name != "diligent_shared" else f"{base}/Release/GraphicsEngineD3D12_64r.dll"
    projects["diligent_shared"] = "out/bench-diligent/diligent/Graphics/GraphicsEngineD3D12/Diligent-GraphicsEngineD3D12-shared.vcxproj"
    artifacts["diligent_import"] = "out/bench-diligent/diligent/Graphics/GraphicsEngineD3D12/Release/GraphicsEngineD3D12_64r.lib"
    artifacts["diligent_nvapi"] = dependencies["diligent_nvapi"]["directory"] + "/amd64/nvapi64.lib"
    runtime = root / artifacts["diligent_shared"]
    if runtime.read_bytes() != (root / "out/bench-diligent/Release" / runtime.name).read_bytes():
        raise ValueError("Diligent copied runtime differs from the built DLL")
    from run_benchmarks import diligent_fingerprint
    if diligent_fingerprint(root).encode() not in (root / artifacts["benchmark"]).read_bytes():
        raise ValueError("Diligent benchmark executable is stale for the current sources/pins")
    commands = []
    for name in ["benchmark", "benchmark_core", *targets]:
        logs = sorted((root / traces[name]).rglob("CL.command.1.tlog"))
        if not logs:
            raise ValueError(f"Missing Diligent compiler command evidence: {name}")
        for log in logs:
            command = log.read_bytes().decode("utf-16")
            if not all(re.search(flag, command) for flag in (r"/O2(?:\s|$)", r"/MD(?:\s|$)")) or re.search(r"/(?:GL|MTd?)(?:\s|$)", command):
                raise ValueError(f"Diligent compiler options differ from /O2 /MD no-LTO: {name}")
            commands.append({"path":log.relative_to(root).as_posix(),"sha256":hashlib.sha256(log.read_bytes()).hexdigest()})
    shader_names = [f"GenerateMips{mode}{odd}CS" for mode in ("Gamma", "Linear") for odd in ("", "Odd", "OddX", "OddY")]
    shader_sources = [root / ".deps/DiligentCore/Graphics/GraphicsEngineD3D12/shaders/GenerateMips" / (name + ".hlsl") for name in shader_names]
    shader_sources += [root / ".deps/DiligentCore/Graphics/GraphicsEngineD3D12/shaders/GenerateMips/GenerateMipsCS.hlsli",
                       root / ".deps/DiligentCore/Graphics/ShaderTools/include/HLSLDefinitions.fxh"]
    for name in shader_names:
        generated.add(f"out/bench-diligent/diligent/graphics/graphicsengined3d12/compiledshaders/generatemips/{name.lower()}.h")
    generated.add("out/bench-diligent/diligent/graphics/shadertools/generatedshaders/hlsldefinitions_inc.fxh")
    def record(path):
        return {"path": path.relative_to(root).as_posix(), "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
    return {"dependencies": dependencies, "shader_sources": shader_sources,
            "evidence": {"policy": record(review_path), "lock": record(lock_path),
                         "shader_source_inputs": [record(path) for path in shader_sources],
                         "configured_dependencies": dependencies,
                         "compiler_command_evidence": commands,
                         "note": "Configured Abseil/xxHash checkouts are verified; only compiler-read files appear in components. Generated mip shaders are included even though the measured workload does not dispatch them."}}
