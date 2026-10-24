"""Resolve historical product inputs through the verified pre-fix snapshot only."""
import pathlib
import audit_licenses as audit
ROOT=pathlib.Path(__file__).resolve().parents[1]
SNAPSHOT=pathlib.Path('out/vulkan-creation-before-source')
OLD='44b92cdf3a4ae01c97af10fe25d57b6dea487bf6ed7826a7f322e338a1d39e25'
CHANGED={'CMakeLists.txt','src/backends/vulkan/Vulkan.cpp','src/backends/vulkan/Presentation.hpp'}
def resolve(record,root=ROOT):
    path=pathlib.Path(record['path'])
    if path.is_absolute() or '..' in path.parts:raise audit.AuditError('Historical path must stay inside workspace')
    expected={k:record[k] for k in ('path','sha256','bytes')};file=root/path
    if file.is_file() and audit.record_file(root,file)==expected:return expected
    if record['path'] not in CHANGED:raise audit.AuditError('Changed historical input: '+record['path'])
    snapshot=root/SNAPSHOT
    if audit.sdk_fingerprint(snapshot)!=OLD:raise audit.AuditError('Pre-fix SDK snapshot identity differs')
    copy=snapshot/record['path'];actual=audit.record_file(root,copy)
    if (actual['sha256'],actual['bytes'])!=(record['sha256'],record['bytes']):raise audit.AuditError('Historical product snapshot bytes differ: '+record['path'])
    return actual
