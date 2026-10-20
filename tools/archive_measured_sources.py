"""Preserve exact fingerprinted SDK sources used for OPT-001 (no competitor code)."""
import hashlib
import json
import pathlib
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
AFTER_BLOCK = '''        // Device removal is checked at begin/submit/wait, rather than polling the
        // driver for every recorded draw. Recording itself does not execute GPU work.
        if (owner->thread != std::this_thread::get_id()) return invalid("Command recording must run on the creating thread");'''
BEFORE_BLOCK = '''        auto status = owner->check(); if (!status) return status;'''

def fingerprint(files):
    digest=hashlib.sha256()
    for name,data in sorted(files.items()):
        digest.update((name+"|"+hashlib.sha256(data).hexdigest()+"\n").encode())
    return digest.hexdigest()

def main():
    files={p.relative_to(ROOT).as_posix():p.read_bytes() for p in list((ROOT/"include").rglob("*.hpp"))+list((ROOT/"src").rglob("*.cpp"))+[ROOT/"CMakeLists.txt"]}
    after=fingerprint(files)
    source=files["src/backends/d3d12/D3D12.cpp"].decode()
    if AFTER_BLOCK not in source:
        raise RuntimeError("Current source differs from the measured OPT-001 revision; do not guess")
    before_files={**files,"src/backends/d3d12/D3D12.cpp":source.replace(AFTER_BLOCK,BEFORE_BLOCK,1).encode()}
    snapshots={"before":before_files,"after":files}
    destination=ROOT/"benchmarks/reports/sources"
    destination.mkdir(exist_ok=True)
    for stage, snapshot in snapshots.items():
        digest=fingerprint(snapshot)
        result=json.loads((ROOT/f"benchmarks/results/2026-10-20-draw-{stage}/graphicssdk-draw-0.json").read_text())
        if digest != result["sdk_build_source_sha256"]:
            raise RuntimeError(f"{stage} reconstruction does not match the actual build fingerprint")
        path=destination/f"{digest}.zip"
        if path.exists(): raise RuntimeError("Source snapshot already exists")
        with zipfile.ZipFile(path,"w",compression=zipfile.ZIP_DEFLATED) as archive:
            for name,data in sorted(snapshot.items()): archive.writestr(name,data)
            for extra in ("cmake/GraphicsSDKConfig.cmake.in","LICENSE"):
                archive.write(ROOT/extra,extra)
        print(stage,digest)
    (destination/"README.md").write_text("# Measured SDK source snapshots\n\nArchive names are verified SDK build fingerprints. These contain own SDK code only.\nBuild the archive with `-DGFX_BUILD_TESTS=OFF -DGFX_BUILD_EXAMPLES=OFF`; tests/examples and competitor source are separate.\nPins and raw observations remain in the parent project. OPT-001 before was reconstructed by reverting exactly its ready() change and verified against the recorded fingerprint.\n",encoding="utf-8")

if __name__ == "__main__": main()
