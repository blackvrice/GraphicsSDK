"""Fetch public, immutable research snapshots; never write to source repositories."""
import json
import pathlib
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parents[1]
REPOS = {
    "llgl": ("LukasBanana/LLGL", ["LICENSE.txt", "README.md", "include/LLGL/RenderSystem.h", "include/LLGL/CommandBuffer.h", "include/LLGL/ResourceHeap.h"]),
    "bgfx": ("bkaradzic/bgfx", ["LICENSE", "README.md", "include/bgfx/bgfx.h", "src/renderer_d3d12.cpp"]),
    "diligent": ("DiligentGraphics/DiligentEngine", ["License.txt", "README.md", ".gitmodules"]),
    "diligent_core": ("DiligentGraphics/DiligentCore", ["License.txt", "README.md", "Graphics/GraphicsEngine/interface/DeviceContext.h", "Graphics/GraphicsEngine/interface/ShaderResourceBinding.h"]),
    "the_forge": ("ConfettiFX/The-Forge", ["LICENSE", "README.md", "Common_3/Graphics/Interfaces/IGraphics.h"]),
    "dawn": ("google/dawn", ["LICENSE", "README.md", "src/dawn/native/Device.h"]),
    "nvrhi": ("NVIDIA-RTX/NVRHI", ["LICENSE.txt", "README.md", "include/nvrhi/nvrhi.h"]),
    "sdl": ("libsdl-org/SDL", ["LICENSE.txt", "README.md", "include/SDL3/SDL_gpu.h"]),
    "sandbox": ("blackvrice/Sandbox", ["README.md", "CMakeLists.txt"]),
}

def get(url):
    req = urllib.request.Request(url, headers={"User-Agent": "GraphicsSDK-source-audit"})
    return urllib.request.urlopen(req, timeout=60).read()

if __name__ == "__main__":
    output = ROOT / ".deps/references"
    output.mkdir(parents=True, exist_ok=True)
    lock = {}
    for name, (repo, paths) in REPOS.items():
        try:
            info = json.loads(get(f"https://api.github.com/repos/{repo}/commits/HEAD"))
            sha = info["sha"]
            entry = {"repository": repo, "commit": sha, "commit_date": info["commit"]["committer"]["date"], "files": [], "unavailable": []}
            for path in paths:
                try:
                    data = get(f"https://raw.githubusercontent.com/{repo}/{sha}/{path}")
                    dest = output / name / path
                    dest.parent.mkdir(parents=True, exist_ok=True)
                    dest.write_bytes(data)
                    entry["files"].append(path)
                except Exception as exc:
                    entry["unavailable"].append({"path": path, "error": str(exc)})
            if name == "sandbox":
                tree = json.loads(get(f"https://api.github.com/repos/{repo}/git/trees/{sha}?recursive=1"))
                (output / name / "tree.json").write_text(json.dumps(tree, indent=2), encoding="utf-8")
            lock[name] = entry
            print(name, sha, len(entry["files"]), "files", flush=True)
        except Exception as exc:
            lock[name] = {"repository": repo, "error": str(exc)}
            print(name, exc, flush=True)
    (ROOT / "benchmarks/competitors/sources.lock.json").write_text(json.dumps(lock, indent=2) + "\n", encoding="utf-8")
