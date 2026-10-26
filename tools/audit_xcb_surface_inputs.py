"""Record actual Ninja inputs, private XCB dependencies and installed notices."""
import hashlib
import json
import pathlib
import subprocess
from audit_licenses import ROOT
import audit_linux_package as audit


def record(p):
    return dict(path=str(p),bytes=p.stat().st_size,sha256=hashlib.sha256(p.read_bytes()).hexdigest())


def main():
    output=ROOT/'out/xcb-surface-evidence/setup'
    output.mkdir(parents=True,exist_ok=True)
    manifest=json.loads((ROOT/'licenses/linux-vulkan-policy.json').read_text())
    own={(ROOT/p).resolve() for p in manifest['own_compiler_inputs']}
    headers=ROOT/'.deps/Vulkan-Headers'
    base_reviewed=manifest['dependency']['reviewed_headers']
    reviewed=base_reviewed+manifest['xcb_reviewed_headers']
    vendor={(headers/r['path']).resolve() for r in reviewed}
    for item in reviewed:
        audit.require(hashlib.sha256((headers/item['path']).read_bytes()).hexdigest()==item['sha256'],'Reviewed external header changed')
    rows=[]
    for policy in ('on','off'):
        deps=(output/f'deps-{policy}.txt').read_text()
        paths={pathlib.Path(line.strip()).resolve() for line in deps.splitlines() if line.startswith('    ')}
        audit.require(paths,'Actual Ninja compiler dependencies missing')
        audited=audit.audit_dependencies(sorted(paths),'sdk',own,set(),set(),vendor)
        actual_vendor={p for p in paths if p in vendor}
        audit.require(bool(actual_vendor),'Reviewed Vulkan inputs missing')
        expected={ (headers/r['path']).resolve() for r in (reviewed if policy=='on' else base_reviewed) }
        audit.require(actual_vendor==expected,'Actual reviewed header set differs')
        xcb_header=any(p.as_posix().endswith('/xcb/xcb.h') for p in paths)
        audit.require(xcb_header is (policy=='on'),'XCB compiler input differs from build flag')
        audit.require(((headers/'include/vulkan/vulkan_xcb.h').resolve() in paths) is (policy=='on'),'Vulkan XCB header differs from build flag')
        exported=(output/f'export-{policy}.txt').read_text()
        audit.require('xcb' not in exported.lower(),'Private XCB dependency leaked to installed target')
        prefix=ROOT/f'out/xcb-install-{policy}'
        for p in (prefix/'include/graphics').glob('*.hpp'):
            audit.require('#include <xcb/' not in p.read_text(),'Platform header leaked to public API')
        notice=audit.notice(ROOT,actual_vendor,headers/'LICENSES/Apache-2.0.txt')
        (prefix/'share/GraphicsSDK/THIRD_PARTY_NOTICES.txt').write_text(notice,encoding='utf-8')
        (output/f'THIRD_PARTY_NOTICES-{policy}.txt').write_text(notice,encoding='utf-8')
        library=prefix/'lib/libGraphicsSDKCore.a'
        inventory=audit.payload(prefix,list((ROOT/'include/graphics').glob('*.hpp')),notice,hashlib.sha256(library.read_bytes()).hexdigest())
        rows.append(dict(policy=policy,compiler_inputs=audited,installed_payload=inventory,notice=record(output/f'THIRD_PARTY_NOTICES-{policy}.txt')))
    runtime=[]
    for name in ('libxcb.so.1','libvulkan.so.1','libvulkan_lvp.so'):
        p=(pathlib.Path('/usr/lib/x86_64-linux-gnu')/name).resolve()
        runtime.append({**record(p),'redistributed':False})
    (output/'input-audit.json').write_text(json.dumps(dict(builds=rows,runtime_files=runtime,Core_hard_XCB_link=False,public_platform_headers=False),indent=2)+'\n',encoding='utf-8')
    print('PASS actual ON/OFF compiler inputs, private dependency, installed payload and notices')


if __name__=='__main__':main()
