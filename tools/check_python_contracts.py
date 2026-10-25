"""Run complete Python discovery and preserve fresh-checkout source identities."""
import argparse
import hashlib
import json
import os
import pathlib
import platform
import subprocess
import sys
import time
import unittest

from audit_licenses import sdk_fingerprint

ROOT=pathlib.Path(__file__).resolve().parents[1]


def record(path):
    data=path.read_bytes()
    return dict(path=path.relative_to(ROOT).as_posix(),bytes=len(data),sha256=hashlib.sha256(data).hexdigest())


class Tee:
    def __init__(self,log):self.log=log
    def write(self,text):
        self.log.write(text)
        sys.stderr.write(text)
    def flush(self):
        self.log.flush()
        sys.stderr.flush()


class Result(unittest.TextTestResult):
    def __init__(self,*args,**kwargs):
        super().__init__(*args,**kwargs)
        self.test_ids=[]
        self.subtests=0
    def startTest(self,test):
        self.test_ids.append(test.id())
        super().startTest(test)
    def addSubTest(self,test,subtest,error):
        self.subtests+=1
        super().addSubTest(test,subtest,error)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=pathlib.Path,required=True)
    args=parser.parse_args()
    output=args.output.resolve()
    if not output.is_relative_to(ROOT/'out') or output.exists():
        raise ValueError('Fresh output under repository out/ required')
    output.mkdir(parents=True)
    git=['git','--no-optional-locks','-c','gc.auto=0','-c','maintenance.auto=false','-c','core.preloadIndex=false']
    def git_text(*args):return subprocess.check_output(git+list(args),cwd=ROOT,text=True).strip()
    source=git_text('rev-parse','HEAD')
    tracked=git_text('ls-files').splitlines()
    selected=[p for p in tracked if p.startswith(('tests/','tools/')) and p.endswith(('.py','.json'))]
    selected += [p for p in tracked if p=='CMakeLists.txt' or p.startswith('include/') and p.endswith('.hpp') or p.startswith('src/') and p.endswith(('.cpp','.hpp'))]
    selected += ['.gitattributes','.github/workflows/build.yml']
    before=[record(ROOT/p) for p in sorted(set(selected))]
    fingerprint=sdk_fingerprint(ROOT)
    status=git_text('status','--porcelain','--untracked-files=no')
    start=time.monotonic()
    suite=unittest.defaultTestLoader.discover(str(ROOT/'tests'),pattern='test_*.py')
    with (output/'unittest.log').open('x',encoding='utf-8',newline='\n') as log:
        result=unittest.TextTestRunner(stream=Tee(log),verbosity=2,resultclass=Result).run(suite)
    after=[record(ROOT/p) for p in sorted(set(selected))]
    status_after=git_text('status','--porcelain','--untracked-files=no')
    unchanged=before==after and fingerprint==sdk_fingerprint(ROOT) and status==status_after
    receipt=dict(schema_version=1,task='P3-LNX-14',source_commit=source,github_run_id=os.environ.get('GITHUB_RUN_ID'),
        platform=dict(system=platform.system(),release=platform.release(),machine=platform.machine(),python=sys.version),
        execution='complete unittest discovery, no test exclusion or source/fixture rewriting',
        SDK_source_fingerprint=fingerprint,inputs=before,inputs_unchanged=unchanged,tracked_status_before=status,tracked_status_after=status_after,
        tests=result.testsRun,test_ids=result.test_ids,subtests=result.subtests,failures=[t.id() for t,_ in result.failures],errors=[t.id() for t,_ in result.errors],
        skipped=[dict(test=t.id(),reason=reason) for t,reason in result.skipped],expected_failures=[t.id() for t,_ in result.expectedFailures],unexpected_successes=[t.id() for t in result.unexpectedSuccesses],
        successful=result.wasSuccessful() and unchanged and result.testsRun>0,seconds=time.monotonic()-start,log=record(output/'unittest.log'),
        historical_replay='three pinned source ZIPs restored byte-for-byte by test fixtures; runtime validators still check their selected current source',
        native_GPU_executed_by_this_Python_suite=False,memory_or_Intel_investigation_resumed=False)
    (output/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf-8')
    print('GFX_PYTHON_CONTRACTS_RECEIPT='+json.dumps(receipt,separators=(',',':')),flush=True)
    return 0 if receipt['successful'] else 1


if __name__=='__main__':sys.exit(main())
