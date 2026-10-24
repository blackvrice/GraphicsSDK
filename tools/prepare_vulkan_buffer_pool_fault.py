"""Harden buffer allocation output ownership in a new, verified adaptive SDK copy."""
import argparse
import difflib
import json
import pathlib
import shutil
import audit_licenses as audit
ROOT=pathlib.Path(__file__).resolve().parents[1]
SOURCE=ROOT/'out/vulkan-adaptive-buffer-pool-source'
BASE='4f4e63639037f18613227e71cc5635956fa3a2b095807621aadb6fcba6aecb43'

def prepare(destination):
    dst=destination.resolve()
    if not dst.is_relative_to(ROOT/'out') or dst.exists():raise ValueError('New isolated out/ source root required')
    receipt=json.loads((SOURCE/'candidate-receipt.json').read_text(encoding='utf-8'))
    if audit.sdk_fingerprint(SOURCE)!=BASE:raise ValueError('Adaptive source identity changed')
    for r in receipt['files']:
        if audit.record_file(ROOT,ROOT/r['path'])!=r:raise ValueError('Changed adaptive snapshot file: '+r['path'])
    shutil.copytree(SOURCE,dst)
    (dst/'candidate-receipt.json').unlink()
    p=dst/'src/backends/vulkan/Vulkan.cpp';s=p.read_text(encoding='utf-8')
    old='if(auto ok=owner->accept(owner->native->AllocateMemory(owner->native->handle,&allocate,nullptr,&a->memory),"vkAllocateMemory");!ok)return std::unexpected(ok.error());'
    new='''// Failed Vulkan output handles have undefined contents. Acquire ownership only on success.
            VkDeviceMemory allocated{};
            if(auto ok=owner->accept(owner->native->AllocateMemory(owner->native->handle,&allocate,nullptr,&allocated),"vkAllocateMemory");!ok)return std::unexpected(ok.error());
            a->memory=allocated;'''
    if s.count(old)!=1:raise ValueError('Adaptive buffer allocation anchor changed')
    before=s;s=s.replace(old,new);p.write_bytes(s.replace('\n','\r\n').encode())
    patch=''.join(difflib.unified_diff(before.splitlines(True),s.splitlines(True),fromfile='a/src/backends/vulkan/Vulkan.cpp',tofile='b/src/backends/vulkan/Vulkan.cpp'))
    (ROOT/'experiments/vulkan_buffer_pool_fault/candidate.patch').write_bytes(patch.encode())
    files=[audit.record_file(ROOT,p) for p in sorted(dst.rglob('*')) if p.is_file()]
    r=dict(schema_version=1,product_SDK_source_fingerprint=audit.sdk_fingerprint(ROOT),prior_candidate_SDK_source_fingerprint=BASE,candidate_SDK_source_fingerprint=audit.sdk_fingerprint(dst),source_root=dst.relative_to(ROOT).as_posix(),files=files,patch_tool=audit.record_file(ROOT,pathlib.Path(__file__)),experimental=True,production_adopted=False)
    (dst/'candidate-receipt.json').write_text(json.dumps(r,indent=2)+'\n',encoding='utf-8');print('PASS hardened isolated candidate',r['candidate_SDK_source_fingerprint'])
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=pathlib.Path,required=True);prepare(p.parse_args().output)
