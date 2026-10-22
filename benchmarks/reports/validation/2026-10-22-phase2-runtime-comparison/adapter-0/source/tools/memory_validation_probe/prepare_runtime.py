"""Prepare a pinned, local-only Microsoft D3D12 runtime; no SDK/package changes."""
import argparse
import hashlib
import io
import json
import pathlib
import urllib.request
import zipfile

LOCK = pathlib.Path(__file__).with_name("agility-runtime.lock.json")


def digest(data):
    return hashlib.sha256(data).hexdigest()


def verify_package(data, lock):
    if len(data) > lock["maximum_package_bytes"] or digest(data) != lock["package_sha256"]:
        raise ValueError("Runtime package hash/size mismatch")
    result = {}
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        names = archive.namelist()
        if len(names) != len({n.casefold() for n in names}):
            raise ValueError("Duplicate runtime package member")
        for name in [*lock["runtime_members"], *lock["metadata_members"]]:
            # Select exact members and flatten to their fixed leaf names. Never extract arbitrary paths.
            if archive.getinfo(name).file_size > lock["maximum_package_bytes"]:
                raise ValueError("Oversized runtime package member")
            payload = archive.read(name)
            if name in lock["runtime_members"] and digest(payload) != lock["runtime_members"][name]:
                raise ValueError("Runtime DLL hash mismatch: "+name)
            result[pathlib.PurePosixPath(name).name] = payload
        if digest(result["LICENSE.txt"]) != lock["runtime_license_sha256"]:
            raise ValueError("Runtime license changed")
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--output", required=True, type=pathlib.Path)
    p.add_argument("--package", type=pathlib.Path, help="Use a local copy; it must match the same official pin")
    a = p.parse_args()
    if a.output.exists():
        p.error("Choose a fresh runtime directory")
    lock = json.loads(LOCK.read_text())
    if a.package:
        if a.package.stat().st_size > lock["maximum_package_bytes"]:
            raise ValueError("Oversized runtime package")
        data = a.package.read_bytes()
    else:
        with urllib.request.urlopen(lock["url"], timeout=60) as response:
            data = response.read(lock["maximum_package_bytes"]+1)
    members = verify_package(data, lock)
    a.output.mkdir(parents=True, exist_ok=False)
    (a.output/"runtime.nupkg").write_bytes(data)
    for name, content in members.items():
        (a.output/name).write_bytes(content)
    provenance = {"lock_sha256": digest(LOCK.read_bytes()), "package": lock["package"], "version": lock["version"],
                  "source_url": lock["url"], "package_sha256": digest(data), "runtime_license": lock["runtime_license"],
                  "redistributed_with_SDK": False, "files": {n: {"sha256": digest(b), "bytes": len(b)} for n, b in members.items()}}
    (a.output/"provenance.json").write_text(json.dumps(provenance, indent=2)+"\n", encoding="utf-8")
    print("PASS pinned local diagnostic runtime; package and DLLs remain outside the SDK distribution")


if __name__ == "__main__":
    main()
