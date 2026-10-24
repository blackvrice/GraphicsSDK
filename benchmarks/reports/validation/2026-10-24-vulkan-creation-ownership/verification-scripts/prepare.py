"""Freeze the unmodified product and make bounded ownership edits, preserving all old profiles."""
import difflib
import json
import pathlib
import re
import shutil
import sys
root=pathlib.Path(__file__).resolve().parents[5];sys.path.insert(0,str(root/'tools'))
import audit_licenses as audit
snapshot=root/'out/vulkan-creation-before-source'
assert not snapshot.exists()
snapshot.mkdir()
for folder in ('include','src','cmake'):shutil.copytree(root/folder,snapshot/folder,ignore=shutil.ignore_patterns('Creation.hpp'))
shutil.copyfile(root/'CMakeLists.txt',snapshot/'CMakeLists.txt')
assert audit.sdk_fingerprint(snapshot)=='44b92cdf3a4ae01c97af10fe25d57b6dea487bf6ed7826a7f322e338a1d39e25'
receipt=dict(SDK_source_fingerprint=audit.sdk_fingerprint(snapshot),files=[audit.record_file(root,p) for p in sorted(snapshot.rglob('*')) if p.is_file()])
(snapshot/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
records=[]
for name in ('src/backends/vulkan/Vulkan.cpp','src/backends/vulkan/Presentation.hpp'):
    p=root/name;s=p.read_text(encoding='utf-8');before=s
    if name.endswith('Vulkan.cpp'):s=s.replace('#include "Timestamp.hpp"','#include "Timestamp.hpp"\n#include "Creation.hpp"')
    pattern=r'(?:owner->native->|\(\*instance\)->|n\.|in\.)(?:Create\w+|AllocateMemory|AllocateCommandBuffers|AllocateDescriptorSets)\(|(?<![\w:])(?:create|make)\('
    matches=list(re.finditer(pattern,s))
    for m in reversed(matches):
        function=m.group()[:-1]
        if function.endswith('CreateGraphicsPipelines'):continue # Defined null/partial-success outputs; preserve cleanup.
        start=m.end();depth=1;end=start
        while depth:
            if s[end]=='(':depth+=1
            elif s[end]==')':depth-=1
            end+=1
        args=s[start:end-1];last=args.rsplit(',',1)
        if len(last)!=2 or not last[1].strip().startswith('&'):continue
        destination=last[1].strip()[1:]
        assert re.fullmatch(r'[\w>\.\-\[\]]+',destination),destination
        s=s[:m.start()]+f'vulkan::acquireOutput({destination}, {function}, {last[0]})'+s[end:]
        records.append(dict(file=name,entry=function,output=destination))
    p.write_bytes(s.replace('\n','\r\n').encode())
    assert s!=before
v=pathlib.Path(__file__).resolve().parent.parent
(v/'guarded-call-sites.json').write_text(json.dumps(records,indent=2)+'\n')
assert len(records)==29,len(records)
print('PASS frozen original product and guarded',len(records),'creation sites')
