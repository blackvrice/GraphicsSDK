"""Historical replay cannot substitute current source identity or mask corrupt inputs."""
import copy
import json
import pathlib
import sys
import tempfile
import unittest
from unittest import mock

ROOT=pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import recorded_sources as recorded
import run_vulkan_loader_fault as loader
import run_vulkan_pool_churn as churn
import run_vulkan_pool_rebase_fault as rebase


class RecordedSourceContracts(unittest.TestCase):
    def entry(self,key):
        return json.loads(recorded.LOCK.read_text(encoding='utf-8'))['sources'][key]

    def rejected(self,mutate):
        entry=self.entry('loader');mutate(entry)
        with tempfile.TemporaryDirectory() as tmp, self.assertRaises((ValueError,KeyError,FileNotFoundError)):
            recorded.restore(ROOT,entry,pathlib.Path(tmp).resolve())

    def test_archive_digest_is_required(self):
        self.rejected(lambda e:e['archive'].update(sha256='0'*64))

    def test_member_digest_is_required(self):
        self.rejected(lambda e:e['files'][0].update(sha256='0'*64))

    def test_aggregate_fingerprint_is_required(self):
        self.rejected(lambda e:e.update(source_fingerprint='0'*64))

    def test_duplicate_source_path_rejected(self):
        self.rejected(lambda e:e['files'].append(copy.deepcopy(e['files'][0])))

    def test_empty_source_set_rejected(self):
        self.rejected(lambda e:e.update(files=[]))

    def test_source_path_escape_rejected(self):
        self.rejected(lambda e:e['files'][0].update(path='../outside.cpp'))

    def test_missing_archive_fails_without_skip(self):
        self.rejected(lambda e:e['archive'].update(path='absent-source.zip'))

    def replay(self,key,module,folder):
        entry=self.entry(key)
        rows=json.loads((ROOT/'benchmarks/results'/folder/'results.json').read_text(encoding='utf-8'))['runs']
        row=next(x for x in rows if key!='rebase' or x['kind']=='mixed');r=row['result']
        def validate():
            if key=='loader':return module.validate(r,r['kind'],r['validation'],r['result'],r['poison'],module.OLD if row['variant']=='before' else module.SDK,row['variant']=='before')
            if key=='churn':return module.validate(r,r['profile'],r['validation'],r['candidate'])
            return module.validate_mixed(r,r['case'],r['validation'])
        original=module.ROOT
        with tempfile.TemporaryDirectory() as tmp:
            root=pathlib.Path(tmp).resolve();recorded.restore(ROOT,entry,root)
            with mock.patch.object(module,'ROOT',root):
                self.assertEqual(module.fingerprint(),entry['source_fingerprint'])
                validate()
                # A real byte change in the selected source, including LF/CRLF,
                # must still fail the unmodified production validator.
                path=next(root/row['path'] for row in entry['files'] if b'\r\n' in (root/row['path']).read_bytes());before=path.read_bytes()
                path.write_bytes(before.replace(b'\r\n',b'\n'))
                self.assertNotEqual(module.fingerprint(),entry['source_fingerprint'])
                with self.assertRaises(ValueError):validate()
        self.assertEqual(module.ROOT,original)

    def test_loader_replay_and_current_byte_change_rejection(self):
        self.replay('loader',loader,'2026-10-24-vulkan-loader-fault-v2')

    def test_churn_replay_and_current_byte_change_rejection(self):
        self.replay('churn',churn,'2026-10-24-vulkan-pool-churn')

    def test_rebase_replay_and_current_byte_change_rejection(self):
        self.replay('rebase',rebase,'2026-10-24-vulkan-pool-rebase-fault')


if __name__=='__main__':unittest.main()
