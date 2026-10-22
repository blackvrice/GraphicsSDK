"""Offline, fail-closed inventory of the reviewed Windows Release build inputs.

MSBuild compiler-read logs are conservative evidence, not proof that every header or
static-library member contributes code to a final executable. No legal approval is inferred.
"""
import argparse
import fnmatch
import hashlib
import json
import pathlib
import re
import subprocess
import sys
import xml.etree.ElementTree as ET
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
SDK_DIRS = {"llgl": ".deps/LLGL", "sdl": ".deps/SDL"}
TRACE_DIRS = {
    "core": "out/core/GraphicsSDKCore.dir/Release",
    "llgl": "out/llgl/LLGL.dir/Release",
    "llgl_d3d12": "out/llgl/sources/Renderer/Direct3D12/LLGL_Direct3D12.dir/Release",
    "llgl_dxcommon": "out/llgl/sources/Renderer/DXCommon/LLGL_DXCommon.dir/Release",
    "sdl": "out/sdl/SDL3-static.dir/Release",
    "benchmark": "out/bench/gfx_bench.dir/Release",
    "benchmark_core": "out/bench/sdk/GraphicsSDKCore.dir/Release",
}
ARTIFACTS = {
    "core": "out/core/Release/GraphicsSDKCore.lib",
    "llgl": "out/llgl/build/Release/LLGL.dll",
    "llgl_d3d12": "out/llgl/build/Release/LLGL_Direct3D12.dll",
    "llgl_dxcommon": "out/llgl/build/Release/LLGL_DXCommon.lib",
    "sdl": "out/sdl/Release/SDL3-static.lib",
    "benchmark": "out/bench/Release/gfx_bench.exe",
    "benchmark_core": "out/bench/sdk/Release/GraphicsSDKCore.lib",
    "llgl_import": "out/llgl/build/Release/LLGL.lib",
}
PROJECTS = {
    "core": "out/core/GraphicsSDKCore.vcxproj",
    "llgl_d3d12": "out/llgl/sources/Renderer/Direct3D12/LLGL_Direct3D12.vcxproj",
    "benchmark": "out/bench/gfx_bench.vcxproj",
}
GENERATED_INPUTS = {
    "out/sdl/cmakefiles/sdl3-static.dir/cmake_pch.c",
    "out/sdl/cmakefiles/sdl3-static.dir/cmake_pch.cxx",
    "out/sdl/cmakefiles/sdl3-static.dir/release/cmake_pch.h",
    "out/sdl/cmakefiles/sdl3-static.dir/release/cmake_pch.hxx",
    "out/sdl/include-config-release/build_config/sdl_build_config.h",
    "out/sdl/include-revision/sdl3/sdl_revision.h",
}

class AuditError(RuntimeError):
    pass

def sha256(data):
    return hashlib.sha256(data).hexdigest()

def text_file(path):
    return path.read_text(encoding="utf-8-sig")

def record_file(root, path):
    data = path.read_bytes()
    return {"path": path.relative_to(root).as_posix(), "sha256": sha256(data), "bytes": len(data)}

def parse_tlog(data):
    """CL logs use UTF-16 BOM, ^source records and | for batched source roots."""
    try:
        text = data.decode("utf-16")
    except UnicodeError as exc:
        raise AuditError("Invalid MSBuild compiler-read encoding") from exc
    return sorted({part.strip() for line in text.splitlines() for part in line.lstrip("^").split("|") if part.strip()}, key=str.casefold)

def read_trace(root, name, trace_dirs=None):
    trace_dirs = TRACE_DIRS if trace_dirs is None else trace_dirs
    logs = sorted((root / trace_dirs[name]).rglob("CL.read.1.tlog"))
    if not logs:
        raise AuditError(f"Missing Release compiler-read evidence: {trace_dirs[name]}")
    paths = set()
    evidence = []
    for log in logs:
        evidence.append(record_file(root, log))
        paths.update(parse_tlog(log.read_bytes()))
    return paths, evidence

def component_for(policy, sdk, relative):
    # Policy order is significant: narrowly reviewed exceptions precede root licenses.
    for component in policy["components"]:
        if component["sdk"] == sdk and any(fnmatch.fnmatchcase(relative.casefold(), pattern.casefold()) for pattern in component["patterns"]):
            return component
    raise AuditError(f"Unclassified compiler input: {sdk}/{relative}")

def source_notices(text):
    # Important: SDL_egl.h includes Apache notices deep inside an amalgamated header;
    # stb_image's complete license is at the end, not in its first comment.
    comments = re.findall(r"/\*.*?\*/|(?:(?://[^\n]*)(?:\n|$))+", text, re.DOTALL)
    return sorted({c.strip() for c in comments if re.search(r"copyright|spdx-license|public[ -]domain|licensed under|permission.{0,30}granted|software may be licensed", c, re.I)}, key=lambda value: (sha256(value.encode()), value))

def verify_checkout(root, sdk, commit, directories=None):
    directory = root / (SDK_DIRS if directories is None else directories)[sdk]
    def git(*args):
        process = subprocess.run(["git", "-C", str(directory), *args], capture_output=True, text=True, encoding="utf-8", errors="strict", timeout=30)
        if process.returncode:
            raise AuditError(f"git failed for {sdk}: {process.stderr.strip()}")
        return process.stdout
    if git("rev-parse", "HEAD").strip() != commit:
        raise AuditError(f"{sdk} checkout does not match the reviewed commit")
    if git("status", "--porcelain", "--untracked-files=no").strip():
        raise AuditError(f"{sdk} contains tracked modifications; review them separately")
    files = git("ls-files", "-z").split("\0")
    return {file.casefold(): file for file in files if file}

def verify_pins(policy, pins):
    for sdk, reviewed in policy["reviewed_commits"].items():
        if pins[sdk]["commit"] != reviewed:
            raise AuditError(f"License policy has not reviewed the new {sdk} commit")

def verify_d3dx12(root):
    origin = json.loads(text_file(root / "licenses/provenance/d3dx12-origin.json"))
    data = (root / ".deps/LLGL/sources/Renderer/Direct3D12/D3DX12/d3dx12.h").read_bytes().replace(b"\r\n", b"\n").strip()
    if sha256(data) != origin["sha256_lf_stripped"]:
        raise AuditError("D3DX12 no longer matches the verified Microsoft source snapshot")
    return origin

def link_evidence(root, relative):
    document = ET.fromstring((root / relative).read_bytes())
    ns = {"m": "http://schemas.microsoft.com/developer/msbuild/2003"}
    libraries = set()
    for group in document.findall("m:ItemDefinitionGroup", ns):
        if "'Release|x64'" not in group.get("Condition", ""):
            continue
        for node in group.findall(".//m:AdditionalDependencies", ns):
            libraries.update(value for value in (node.text or "").split(";") if value and not value.startswith("%(") )
    return {**record_file(root, root / relative), "release_x64_link_inputs": sorted(libraries, key=str.casefold)}

def reviewed_library(root, path, artifacts=None):
    artifacts = ARTIFACTS if artifacts is None else artifacts
    local = {str((root / relative).resolve()).casefold() for relative in artifacts.values() if relative.casefold().endswith(".lib")}
    if str(path.resolve()).casefold() in local:
        return "reviewed_project_artifact"
    normalized = path.as_posix().casefold()
    if normalized.startswith(("c:/program files/microsoft visual studio/", "c:/program files (x86)/windows kits/", "c:/program files/windows kits/")):
        return "system_prerequisite"
    raise AuditError(f"Unreviewed linked library: {path}")

def actual_link_evidence(root, trace_dirs=None, artifacts=None, names=("benchmark", "llgl", "llgl_d3d12")):
    trace_dirs = TRACE_DIRS if trace_dirs is None else trace_dirs
    records = []
    for name in names:
        logs = [path for path in (root / trace_dirs[name]).rglob("*.tlog") if path.name.casefold() == "link.read.1.tlog"]
        if not logs:
            raise AuditError(f"Missing linker-read evidence: {name}")
        for log in sorted(logs):
            libraries = []
            for raw in parse_tlog(log.read_bytes()):
                if raw.casefold().endswith(".lib"):
                    path = pathlib.Path(raw)
                    libraries.append({"path": path.as_posix(), "sha256": sha256(path.read_bytes()), "kind": reviewed_library(root, path, artifacts)})
            records.append({**record_file(root, log), "libraries": libraries})
    return records

def sdk_fingerprint(root):
    files = list((root / "include").rglob("*.hpp")) + list((root / "src").rglob("*.cpp")) + list((root / "src").rglob("*.hpp")) + [root / "CMakeLists.txt"]
    digest = hashlib.sha256()
    for path in sorted(files):
        digest.update((path.relative_to(root).as_posix() + "|" + sha256(path.read_bytes()) + "\n").encode())
    return digest.hexdigest()


def installed_presentation_header(root, path):
    directory = root / "out/install/include/graphics"
    headers = {header.name.casefold(): header for header in (root / "include/graphics").glob("*.hpp")}
    relative = path.relative_to(directory)
    source = headers.get(relative.as_posix().casefold())
    if source is None or not path.is_file() or path.read_bytes() != source.read_bytes():
        raise AuditError(f"Unreviewed or stale installed presentation header: {path}")
    return {**record_file(root, path), "component": "graphicssdk", "verified_source": record_file(root, source)}


def verify_presentation_library(root):
    installed = root / "out/install/lib/GraphicsSDKCore.lib"
    if not installed.is_file() or installed.read_bytes() != (root / ARTIFACTS["core"]).read_bytes():
        raise AuditError("Presentation consumer links a missing or stale installed SDK library")

def validate_package_entries(entries, required):
    """Reject unsafe names, collisions, missing notices and unexpected payloads."""
    names = set()
    for name, data in entries:
        path = pathlib.PurePosixPath(name)
        if "\\" in name or path.is_absolute() or ".." in path.parts or ":" in name or name.endswith("/"):
            raise AuditError(f"Unsafe or non-file package member: {name}")
        key = name.casefold()
        if key in names:
            raise AuditError(f"Duplicate package member: {name}")
        names.add(key)
        allowed = key in {member.casefold() for member in required} or key in {
            "share/graphicssdk/readme.md", "share/graphicssdk/build.md"}
        if not allowed:
            raise AuditError(f"Unaudited package member: {name}")
    by_name = {name: data for name, data in entries}
    for name, expected in required.items():
        if name not in by_name or by_name[name] != expected:
            raise AuditError(f"Missing or changed required package payload: {name}")

def audit_package(root, package):
    required = {"share/GraphicsSDK/LICENSE": (root / "LICENSE").read_bytes(), "lib/GraphicsSDKCore.lib": (root / ARTIFACTS["core"]).read_bytes()}
    for header in sorted((root / "include/graphics").glob("*.hpp")):
        required["include/graphics/" + header.name] = header.read_bytes()
    for name in ("GraphicsSDKConfig.cmake", "GraphicsSDKConfigVersion.cmake", "GraphicsSDKTargets.cmake", "GraphicsSDKTargets-release.cmake"):
        source = root / "out/install/lib/cmake/GraphicsSDK" / name
        required["lib/cmake/GraphicsSDK/" + name] = source.read_bytes()
    with zipfile.ZipFile(package) as archive:
        for info in archive.infolist():
            path = pathlib.PurePosixPath(info.filename)
            if "\\" in info.filename or ":" in info.filename or path.is_absolute() or ".." in path.parts:
                raise AuditError(f"Unsafe archive member: {info.filename}")
        entries = [(info.filename, archive.read(info)) for info in archive.infolist() if not info.is_dir()]
    validate_package_entries(entries, required)
    return {**record_file(root, package), "status": "inventory_verified", "competitor_payload": False,
            "files": [{"path": name, "sha256": sha256(data), "bytes": len(data)} for name, data in sorted(entries)]}

def collect(root, profile, package=None):
    policy = json.loads(text_file(root / "licenses/policy.json"))
    trace_dirs, artifacts_map, projects, sdk_dirs = dict(TRACE_DIRS), dict(ARTIFACTS), dict(PROJECTS), dict(SDK_DIRS)
    generated = set(GENERATED_INPUTS)
    extra = None
    if profile in ("benchmarks-phase1", "benchmarks-gpu-memory"):
        trace_dirs["phase1"]="out/bench/phase1_llgl.dir/Release"
        artifacts_map["phase1"]="out/bench/Release/phase1_llgl.exe"
        projects["phase1"]="out/bench/phase1_llgl.vcxproj"
        trace_dirs["phase1_sdl"]="out/bench/phase1_sdl.dir/Release"
        artifacts_map["phase1_sdl"]="out/bench/Release/phase1_sdl.exe"
        projects["phase1_sdl"]="out/bench/phase1_sdl.vcxproj"
    if profile == "benchmarks-gpu-memory":
        for name in ("gpu_etl_export", "gpu_memory_control"):
            trace_dirs[name]=f"out/gpu-trace/{name}.dir/Release"
            artifacts_map[name]=f"out/gpu-trace/Release/{name}.exe"
            projects[name]=f"out/gpu-trace/{name}.vcxproj"
    if profile == "benchmarks-diligent":
        from diligent_license_profile import configure
        extra = configure(root, policy, trace_dirs, artifacts_map, projects, sdk_dirs, generated)
    elif profile == "benchmarks-bgfx":
        from bgfx_license_profile import configure
        extra = configure(root, policy, trace_dirs, artifacts_map, projects, sdk_dirs, generated)
    elif profile == "benchmarks-forge":
        from forge_license_profile import configure
        extra = configure(root, policy, trace_dirs, artifacts_map, projects, sdk_dirs, generated)
    stress = profile in ("sdk-stress", "sdk-crash")
    crash = profile == "sdk-crash"
    copies = profile == "sdk-copy"
    swap = profile in ("sdk-swaps", "sdk-copy")
    fence = profile in ("sdk-waits", "sdk-swaps", "sdk-copy")
    budget = profile in ("sdk-budget", "sdk-waits", "sdk-swaps", "sdk-copy")
    adapters = profile in ("sdk-adapters", "sdk-budget", "sdk-waits", "sdk-swaps", "sdk-copy")
    presentation = profile in ("sdk-presentation", "sdk-adapters", "sdk-budget", "sdk-waits", "sdk-swaps", "sdk-copy")
    if stress:
        trace_dirs = {"core": TRACE_DIRS["core"], "memory_stress": "out/core/gfx_memory_stress.dir/Release", "memory_validation_probe": "out/memory-validation-probe/memory_validation_probe.dir/Release",
                      "native_lifetime": "out/memory-validation-probe/gbv_lifetime_probe.dir/Release", "sdk_lifetime": "out/memory-validation-probe/sdk_lifetime_probe.dir/Release", "runtime_probe": "out/memory-validation-probe/gbv_runtime_probe.dir/Release"}
        artifacts_map = {"core": ARTIFACTS["core"], "memory_stress": "out/core/Release/gfx_memory_stress.exe", "memory_validation_probe": "out/memory-validation-probe/Release/memory_validation_probe.exe",
                         "native_lifetime": "out/memory-validation-probe/Release/gbv_lifetime_probe.exe", "sdk_lifetime": "out/memory-validation-probe/Release/sdk_lifetime_probe.exe", "runtime_probe": "out/memory-validation-probe/Release/gbv_runtime_probe.exe"}
        projects = {"core": PROJECTS["core"], "memory_stress": "out/core/gfx_memory_stress.vcxproj", "memory_validation_probe": "out/memory-validation-probe/memory_validation_probe.vcxproj",
                    "native_lifetime": "out/memory-validation-probe/gbv_lifetime_probe.vcxproj", "sdk_lifetime": "out/memory-validation-probe/sdk_lifetime_probe.vcxproj", "runtime_probe": "out/memory-validation-probe/gbv_runtime_probe.vcxproj"}
    if presentation:
        verify_presentation_library(root)
        trace_dirs = {"core": TRACE_DIRS["core"], "presentation": "out/presentation/gfx_presentation.dir/Release",
                      "presentation_policy": "out/presentation/gfx_presentation_policy_tests.dir/Release"}
        artifacts_map = {"core": ARTIFACTS["core"], "presentation": "out/presentation/Release/gfx_presentation.exe",
                         "installed_core": "out/install/lib/GraphicsSDKCore.lib", "presentation_policy": "out/presentation/Release/gfx_presentation_policy_tests.exe"}
        projects = {"core": PROJECTS["core"], "presentation": "out/presentation/gfx_presentation.vcxproj",
                    "presentation_policy": "out/presentation/gfx_presentation_policy_tests.vcxproj"}
    if adapters:
        for name,folder,target in (("adapter_tests","out/core","gfx_adapter_tests"),("adapter_consumer","out/consumer","adapter_consumer")):
            trace_dirs[name]=f"{folder}/{target}.dir/Release"
            artifacts_map[name]=f"{folder}/Release/{target}.exe"
            projects[name]=f"{folder}/{target}.vcxproj"
    if budget:
        for name,folder,target in (("budget_tests","out/core","gfx_budget_tests"),("budget_consumer","out/consumer","budget_consumer")):
            trace_dirs[name]=f"{folder}/{target}.dir/Release"
            artifacts_map[name]=f"{folder}/Release/{target}.exe"
            projects[name]=f"{folder}/{target}.vcxproj"
    if fence:
        trace_dirs["fence_tests"]="out/core/gfx_fence_wait_tests.dir/Release"
        artifacts_map["fence_tests"]="out/core/Release/gfx_fence_wait_tests.exe"
        projects["fence_tests"]="out/core/gfx_fence_wait_tests.vcxproj"
    if swap:
        trace_dirs["swap_tests"]="out/core/gfx_swap_tests.dir/Release"
        artifacts_map["swap_tests"]="out/core/Release/gfx_swap_tests.exe"
        projects["swap_tests"]="out/core/gfx_swap_tests.vcxproj"
    if copies:
        trace_dirs["copy_tests"]="out/core/gfx_copy_tests.dir/Release"
        artifacts_map["copy_tests"]="out/core/Release/gfx_copy_tests.exe"
        projects["copy_tests"]="out/core/gfx_copy_tests.vcxproj"
    if crash:
        for target in ("gfx_crash_capture", "gfx_crash_fixture", "gfx_memory_stress_symbols"):
            trace_dirs[target]=f"out/crash-capture/{target}.dir/Release"
            artifacts_map[target]=f"out/crash-capture/Release/{target}.exe"
            artifacts_map[target+"_symbols"]=f"out/crash-capture/Release/{target}.pdb"
            projects[target]=f"out/crash-capture/{target}.vcxproj"
    benchmark = profile not in ("sdk", "sdk-stress", "sdk-crash", "sdk-presentation", "sdk-adapters", "sdk-budget", "sdk-waits", "sdk-swaps", "sdk-copy")
    extended_sdk = stress or presentation
    target_names = list(trace_dirs) if benchmark or extended_sdk else ["core"]
    evidence = []
    paths = set()
    for name in target_names:
        inputs, trace = read_trace(root, name, trace_dirs)
        paths.update(inputs); evidence.extend(trace)
    if extra:
        paths.update(str(path) for path in extra.get("shader_sources", []))
    pins = json.loads(text_file(root / "benchmarks/competitors/sources.lock.json"))
    if extra:
        pins.update(extra["dependencies"])
    files = {}; components = {}; notice_blocks = {}; system_inputs = set(); generated_inputs = []
    canonical = {}
    if benchmark:
        verify_pins(policy, pins)
        canonical = {sdk: verify_checkout(root, sdk, commit, sdk_dirs) for sdk, commit in policy["reviewed_commits"].items()}
        origin = verify_d3dx12(root)
    else:
        origin = None
    for raw in sorted(paths, key=str.casefold):
        path = pathlib.Path(raw)
        if not path.is_absolute():
            raise AuditError(f"Unrecognized compiler-read path: {raw}")
        if path.suffix.casefold() in {".obj", ".pch", ".pdb", ".tlog"}:
            continue
        if path.is_relative_to(root):
            relative = path.relative_to(root).as_posix()
            sdk = next((sdk for sdk, directory in sorted(sdk_dirs.items(), key=lambda item: -len(item[1])) if path.is_relative_to(root / directory)), None)
            if sdk:
                if not benchmark:
                    raise AuditError(f"Competitor source leaked into SDK compilation: {relative}")
                name = path.relative_to(root / sdk_dirs[sdk]).as_posix()
                if name.casefold() not in canonical[sdk]:
                    raise AuditError(f"Untracked or missing {sdk} compiler input: {name}")
                name = canonical[sdk][name.casefold()]
                path = root / sdk_dirs[sdk] / name
                component = component_for(policy, sdk, name)
                identifier = component["id"]
                components[identifier] = {k: v for k, v in component.items() if k not in ("patterns", "root_notices", "extra_notices")}
                components[identifier]["repository"] = pins[sdk]["repository"]
                components[identifier]["commit"] = pins[sdk]["commit"]
                blocks = notice_blocks.setdefault(identifier, set())
                for notice in component.get("root_notices", []):
                    blocks.add(text_file(root / sdk_dirs[sdk] / notice).strip())
                for notice in component.get("extra_notices", []):
                    blocks.add(text_file(root / notice).strip())
                if path.suffix.casefold() not in {".lib", ".dll"}:
                    blocks.update(source_notices(text_file(path)))
                files[relative.casefold()] = {**record_file(root, path), "component": identifier,
                    "source_url": pins[sdk].get("source_url", f"https://github.com/{pins[sdk]['repository']}/blob/{pins[sdk]['commit']}/") + name}
            elif presentation and path.is_relative_to(root / "out/install/include/graphics"):
                files[relative.casefold()] = installed_presentation_header(root, path)
            elif relative.casefold().startswith(("include/graphics/", "src/", "benchmarks/")) or (copies and relative.casefold()=="tests/copyatomicitytests.cpp") or (swap and relative.casefold()=="tests/swapchaintests.cpp") or (fence and relative.casefold()=="tests/fencewaittests.cpp") or (adapters and relative.casefold() in ("tests/adaptertests.cpp", "tests/consumer/adapter.cpp")) or (budget and relative.casefold() in ("tests/memorybudgettests.cpp", "tests/consumer/budget.cpp")) or (presentation and relative.casefold() in ("examples/presentation/windowed.cpp", "examples/presentation/presentationpolicy.hpp", "examples/presentation/policytests.cpp")) or (stress and relative.casefold() in ("tools/memorystress.cpp", "tools/memory_validation_probe/main.cpp", "tools/memory_validation_probe/reusableframe.hpp", "tools/memory_validation_probe/lifetime.cpp", "tools/memory_validation_probe/sdklifetime.cpp", "tools/memory_validation_probe/runtime.cpp")) or (crash and relative.casefold() in ("tools/crash_capture/capture.cpp", "tools/crash_capture/fixture.cpp")) or (profile=="benchmarks-gpu-memory" and relative.casefold() in ("tools/gpu_trace/export.cpp", "tools/gpu_trace/control.cpp")):
                files[relative.casefold()] = {**record_file(root, path), "component": "graphicssdk"}
            elif benchmark and relative.casefold() in generated:
                generated_inputs.append(record_file(root, path))
            else:
                raise AuditError(f"Unreviewed generated/local compiler input: {relative}")
        else:
            normalized = path.as_posix().casefold()
            os_data = normalized in {"c:/windows/globalization/sorting/sortdefault.nls", "c:/windows/system32/tzres.dll"}
            if not os_data and not normalized.startswith(("c:/program files/microsoft visual studio/", "c:/program files (x86)/windows kits/", "c:/program files/windows kits/")):
                raise AuditError(f"Unreviewed external compiler input: {raw}")
            system_inputs.add(normalized)
    components["graphicssdk"] = {"id": "graphicssdk", "license": "MIT", "selected": "MIT", "note": "Own SDK sources only; external system prerequisites are not relicensed."}
    notice_blocks["graphicssdk"] = {text_file(root / "LICENSE").strip()}
    # Vulkan/OpenXR SPDX-only headers also need the full selected MIT terms.
    if "sdl-khronos-dual" in notice_blocks:
        text = text_file(root / ".deps/SDL/include/SDL3/SDL_opengles2_khrplatform.h")
        notice_blocks["sdl-khronos-dual"].update(source_notices(text))
    artifact_names = list(artifacts_map) if benchmark or extended_sdk else ["core"]
    artifacts = [record_file(root, root / artifacts_map[name]) for name in artifact_names]
    fingerprint = sdk_fingerprint(root)
    if fingerprint.encode() not in (root / ARTIFACTS["core"]).read_bytes():
        raise AuditError("Core static library is stale; rebuild before auditing current SDK sources")
    if benchmark:
        if fingerprint.encode() not in (root / artifacts_map["benchmark_core"]).read_bytes():
            raise AuditError("Benchmark links a stale GraphicsSDK static library")
        for name in ("LLGL.dll", "LLGL_Direct3D12.dll"):
            if (root / artifacts_map["benchmark"]).parent.joinpath(name).read_bytes() != (root / "out/llgl/build/Release" / name).read_bytes():
                raise AuditError(f"Benchmark runtime copy is stale: {name}")
    inventory = {
        "schema_version": 1, "profile": profile, "platform": "windows-x64", "configuration": "Release", "status": "inventory_verified",
        "scope": "Conservative compiler-read closure; not linker-symbol reachability or a legal redistribution approval.",
        "sdk_build_source_sha256": fingerprint, "policy": record_file(root, root / "licenses/policy.json"),
        "components": sorted(components.values(), key=lambda value: value["id"]), "files": sorted(files.values(), key=lambda value: value["path"].casefold()),
        "compiler_read_evidence": evidence, "system_header_count": len(system_inputs),
        "system_input_paths_sha256": sha256("\n".join(sorted(system_inputs)).encode()),
        "system_prerequisites": policy["system_prerequisites"], "artifacts": artifacts,
        "links": {key: link_evidence(root, projects[key]) for key in (projects if benchmark or extended_sdk else ["core"])},
        "d3dx12_origin": origin,
        "exclusions": ["Other platforms/build configurations", "Unbuilt competitor SDKs", "Benchmark assets/examples not in compiler-read logs", "Report-generation Python packages (tools only, not bundled)"]
    }
    if package:
        inventory["package"] = audit_package(root, package)
    if benchmark:
        inventory["linker_read_evidence"] = actual_link_evidence(root, trace_dirs, artifacts_map,
            extra.get("link_names", ("benchmark", "llgl", "llgl_d3d12", "diligent_shared")) if extra else ("benchmark", "llgl", "llgl_d3d12", "phase1", "phase1_sdl", "gpu_etl_export", "gpu_memory_control") if profile=="benchmarks-gpu-memory" else ("benchmark", "llgl", "llgl_d3d12", "phase1", "phase1_sdl") if profile=="benchmarks-phase1" else ("benchmark", "llgl", "llgl_d3d12"))
    if stress:
        names=("memory_stress", "memory_validation_probe", "native_lifetime", "sdk_lifetime", "runtime_probe")
        if crash:
            names += ("gfx_crash_capture", "gfx_crash_fixture", "gfx_memory_stress_symbols")
        inventory["linker_read_evidence"] = actual_link_evidence(root, trace_dirs, artifacts_map, names)
    if crash:
        inventory["crash_capture_build_inputs"] = [record_file(root, root / "tools/crash_capture/CMakeLists.txt")]
        inventory["exclusions"].extend(["Local diagnostic minidumps (not redistributed)", "Debugger-instrumented timing and memory acceptance"])
    if presentation:
        names=("presentation", "presentation_policy", "adapter_tests", "adapter_consumer") if adapters else ("presentation", "presentation_policy")
        if budget:
            names += ("budget_tests", "budget_consumer")
        if fence:
            names += ("fence_tests",)
        if swap:
            names += ("swap_tests",)
        if copies:
            names += ("copy_tests",)
        inventory["linker_read_evidence"] = actual_link_evidence(root, trace_dirs, artifacts_map, names)
        inventory["presentation_build_inputs"] = [record_file(root, root / "examples/presentation" / name) for name in ("CMakeLists.txt", "app.manifest")]
        inventory["exclusions"].extend(["Interactive visual acceptance", "Actual cross-monitor DPI change", "Unexpected driver TDR/hot-unplug"])
    if extra:
        inventory[extra.get("review_key", "diligent_build_review")] = extra["evidence"]
    if generated_inputs:
        inventory["generated_build_inputs"] = generated_inputs
    sections = ["THIRD-PARTY AND SDK NOTICES", "", "Profile: " + profile,
                "Compiler-read inventory is conservative; unused/preprocessed code may be included.",
                "Windows/SDK/CRT/driver binaries are prerequisites and are not bundled or relicensed.", ""]
    for identifier in sorted(notice_blocks):
        sections += ["=" * 78, identifier, "Selected terms: " + components[identifier]["selected"], "=" * 78, ""]
        sections += [block + "\n" for block in sorted(notice_blocks[identifier])]
    return inventory, "\n".join(sections) + "\n"

def output_files(inventory, notices):
    return {"inventory.json": json.dumps(inventory, indent=2, ensure_ascii=False) + "\n", "THIRD_PARTY_NOTICES.txt": notices}

def write_or_check(output, files, check):
    if check:
        if not output.is_dir() or {p.name for p in output.iterdir()} != set(files):
            raise AuditError("Audit output members changed or missing")
        for name, content in files.items():
            if (output / name).read_bytes() != content.encode("utf-8"):
                raise AuditError(f"Stale audit output: {name}")
    else:
        if output.exists():
            raise AuditError("Output exists; use --check or select a new output directory")
        output.mkdir(parents=True)
        for name, content in files.items():
            (output / name).write_bytes(content.encode("utf-8"))

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile", choices=["sdk", "sdk-stress", "sdk-crash", "sdk-presentation", "sdk-adapters", "sdk-budget", "sdk-waits", "sdk-swaps", "sdk-copy", "benchmarks", "benchmarks-diligent", "benchmarks-bgfx", "benchmarks-forge", "benchmarks-phase1", "benchmarks-gpu-memory"], required=True)
    parser.add_argument("--package", type=pathlib.Path)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    try:
        package = args.package.resolve() if args.package else None
        inventory, notices = collect(ROOT, args.profile, package)
        write_or_check(args.output, output_files(inventory, notices), args.check)
        print(f"PASS: {args.profile}: {len(inventory['files'])} compiler-read project files, {len(inventory['components'])} components; " + ("snapshot unchanged" if args.check else str(args.output)))
    except (AuditError, OSError, ValueError, zipfile.BadZipFile, KeyError) as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        return 1
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
