import copy
import gzip
import hashlib
import json
import pathlib
import random
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]/'tools'))
from analyze_virtual_ranges import (ORIGINS, Ranges, analyze_manifest, reconstruct,
                                    read_events, symbol_index)


def event(start, end, time=1, kind='VirtualAlloc', flags='COMMIT', stack=None):
    return dict(event=kind, flags=flags, time_us=time, pid=17, base=start*4096,
                end=end*4096, bytes=(end-start)*4096, stack=[] if stack is None else stack)


def symbols():
    identities=[dict(module=name,sym_type=3,pdb_age=1,pdb_guid='12345678-1234-1234-1234-123456789abc',
                     pdb_sha256='a'*64,image_sha256='b'*64,pdb_redistributed=False)
                for name in ('d3d12core.dll','d3d12sdklayers.dll')]
    frames=[dict(module='D3D12SDKLayers.dll',offset=1,resolved=True,symbol='NDebug::CGBVDataController::AddResourceInfo'),
            dict(module='D3D12Core.dll',offset=2,resolved=True,symbol='NDXGI::CDevice::AllocateCB_0022')]
    return dict(public_PDB_identity_matches=identities, unique_frames=frames)


class VirtualRangeContracts(unittest.TestCase):
    def test_recommit_does_not_double_count_or_reassign_origin(self):
        stack=[dict(module='D3D12SDKLayers.dll',offset=1),dict(module='D3D12Core.dll',offset=2)]
        rows=[event(1,5,1,stack=stack),event(3,7,2)]
        result,_=reconstruct(rows,17,symbol_index(symbols()))
        self.assertEqual(result['peak_observed_live_bytes'],6*4096)
        self.assertEqual(result['commit_call_bytes'],{ORIGINS[0]:4*4096,ORIGINS[2]:4*4096})
        self.assertEqual(result['overlapping_recommit_bytes'][ORIGINS[2]],2*4096)
        self.assertEqual(result['final_origins'],{ORIGINS[0]:4*4096,ORIGINS[1]:0,ORIGINS[2]:2*4096})
        self.assertFalse(result['memory_acceptance'])

    def test_split_partial_free_release_and_reused_hole(self):
        rows=[event(1,9),event(3,7,2,'VirtualFree','DECOMMIT'),event(4,6,3),
              event(2,8,4,'VirtualFree','RELEASE'),event(20,22,5,'VirtualFree','DECOMMIT')]
        result,_=reconstruct(rows,17,{})
        self.assertEqual(result['final_observed_live_bytes'],2*4096)
        self.assertEqual([(r['base']//4096,r['end']//4096) for r in result['surviving_ranges']],[(1,2),(8,9)])
        self.assertEqual(result['introduced_observed_bytes'][ORIGINS[2]],10*4096)
        self.assertEqual(result['retired_observed_bytes'][ORIGINS[2]],8*4096)
        self.assertEqual(result['free_bytes_outside_observed_live_ranges'],4*4096)

    def test_reservation_is_not_commit_and_combined_reserve_commit_is(self):
        result,_=reconstruct([event(1,10,flags='RESERVE'),event(2,4,2,flags='RESERVE COMMIT')],17,{})
        self.assertEqual(result['final_observed_live_bytes'],2*4096)
        self.assertEqual(sum(result['commit_call_bytes'].values()),2*4096)

    def test_sparse_64bit_ranges_do_not_require_page_sized_storage(self):
        r=Ranges();start=2**48
        self.assertEqual(r.commit(start,start+2**40,ORIGINS[2],0),2**40)
        r.free(start+4096,start+2**40-4096)
        self.assertEqual(sum(r.totals().values()),8192)
        self.assertEqual(len(r.live),2)

    def test_interval_results_match_independent_page_oracle(self):
        rng=random.Random(517);ledger=Ranges();pages={}
        for i in range(700):
            left=rng.randrange(1,40);right=rng.randrange(left+1,50);tag=rng.choice(ORIGINS)
            if rng.choice((True,False)):
                expected=sum(p not in pages for p in range(left,right))*4096
                self.assertEqual(ledger.commit(left*4096,right*4096,tag,i),expected)
                for p in range(left,right):pages.setdefault(p,tag)
            else:
                removed=ledger.free(left*4096,right*4096)
                for name in ORIGINS:
                    self.assertEqual(removed[name],sum(pages.get(p)==name for p in range(left,right))*4096)
                pages={p:t for p,t in pages.items() if not left<=p<right}
            self.assertEqual(ledger.totals(),{name:sum(t==name for t in pages.values())*4096 for name in ORIGINS})
            self.assertTrue(all(a[1]<=b[0] for a,b in zip(ledger.live,ledger.live[1:])))

    def test_disjoint_timestamp_ties_allowed_overlapping_ties_rejected(self):
        result,_=reconstruct([event(1,3),event(5,7)],17,{})
        self.assertEqual(result['disjoint_same_time_events'],1)
        for kind,flags in (('VirtualAlloc','COMMIT'),('VirtualFree','DECOMMIT'),('VirtualAlloc','RESERVE')):
            with self.subTest(kind=kind,flags=flags),self.assertRaisesRegex(ValueError,'ambiguous'):
                reconstruct([event(1,3),event(2,4,kind=kind,flags=flags)],17,{})

    def test_disjoint_tied_free_and_commit_report_peak_bounds_not_export_order(self):
        before=event(1,3);free=event(1,3,2,'VirtualFree','RELEASE');commit=event(5,8,2)
        for tied in ([free,commit],[commit,free]):
            result,timeline=reconstruct([before,*tied],17,{})
            self.assertEqual(result['peak_observed_live_bytes'],3*4096)
            self.assertEqual(result['peak_upper_bound_bytes'],5*4096)
            self.assertEqual(len(timeline),2)
            self.assertEqual(timeline[-1]['observed_live_bytes'],3*4096)

    def test_invalid_target_time_range_flags_and_stack_rejected(self):
        mutations=[dict(pid=True),dict(pid=18),dict(time_us=True),dict(time_us=-1),dict(base=0),
                   dict(end=4096),dict(end=8193),dict(bytes=True),dict(bytes=1),dict(flags='RESET'),
                   dict(event='VirtualFree',flags='COMMIT'),dict(stack=None),
                   dict(stack=[dict(module='D3D12Core.dll',offset=99)])]
        for fields in mutations:
            with self.subTest(fields=fields),self.assertRaises(ValueError):
                reconstruct([{**event(1,2),**fields}],17,symbol_index(symbols()))
        with self.assertRaises(ValueError):reconstruct([event(1,2,2),event(4,5,1)],17,{})
        with self.assertRaises(ValueError):reconstruct([],17,{})

    def test_symbol_identity_conflict_missing_or_duplicate_frame_rejected(self):
        for mutate in ('age','guid','hash','type','duplicate_identity','duplicate_frame','empty'):
            data=symbols()
            if mutate=='age':data['public_PDB_identity_matches'][0]['pdb_age']=True
            if mutate=='guid':data['public_PDB_identity_matches'][0]['pdb_guid']='wrong'
            if mutate=='hash':data['public_PDB_identity_matches'][0]['image_sha256']='wrong'
            if mutate=='type':data['public_PDB_identity_matches'][0]['sym_type']=2
            if mutate=='duplicate_identity':data['public_PDB_identity_matches'][1]=data['public_PDB_identity_matches'][0]
            if mutate=='duplicate_frame':data['unique_frames'].append(data['unique_frames'][0])
            if mutate=='empty':data['unique_frames']=[]
            with self.subTest(mutate=mutate),self.assertRaises(ValueError):symbol_index(data)

    def test_frees_need_no_allocation_origin_symbol(self):
        free=event(1,3,2,'VirtualFree','RELEASE',stack=[dict(module='D3D12Core.dll',offset=99)])
        result,_=reconstruct([event(1,3),free],17,{})
        self.assertEqual(result['final_observed_live_bytes'],0)

    def test_malformed_document_containers_rejected(self):
        for value in (None,[],True,dict(public_PDB_identity_matches=[None,None])):
            with self.subTest(value=value),self.assertRaises(ValueError):symbol_index(value)
        with tempfile.TemporaryDirectory() as tmp:
            root=pathlib.Path(tmp);manifest,sym,item=self.fixture(root)
            for value in ([],{},[None],[True],[{**item,'controller':[]}],[{**item,'filtered_events':[]}],[{**item,'counters':[]}],
                          [{**item,'counters':{**item['counters'],'VirtualFree_events':False}}]):
                manifest.write_text(json.dumps(value))
                with self.subTest(value=value),self.assertRaises(ValueError):analyze_manifest(manifest,sym,root)

    def fixture(self, root):
        path=root/'events.jsonl.gz';data=(json.dumps(event(1,3))+'\n').encode();path.write_bytes(gzip.compress(data))
        symbol_path=root/'symbols.json';symbol_path.write_text(json.dumps(symbols()))
        item=dict(case='test',pid=17,memory_acceptance=False,controller=dict.fromkeys(('started','stack_status','flags_status','stop_status','events_lost'),0),
                  trace_header='OS Version: fixture,Events Lost: 0,Buffers lost: 0',
                  filtered_events=dict(path=path.name,sha256=hashlib.sha256(path.read_bytes()).hexdigest(),bytes=path.stat().st_size),
                  counters=dict(traced_commit_call_bytes=8192,VirtualAlloc_events=1,VirtualFree_events=0))
        manifest=root/'manifest.json';manifest.write_text(json.dumps([item]));return manifest,symbol_path,item

    def test_manifest_requires_lossless_frozen_target_and_exact_counters(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=pathlib.Path(tmp);manifest,sym,item=self.fixture(root)
            result,frozen=analyze_manifest(manifest,sym,root);self.assertEqual(len(frozen),3)
            self.assertEqual(result[0]['summary']['final_observed_live_bytes'],8192)
            for mutation in ('loss','missing_header','partial_zero','counter','hash','path','duplicate_case','pid','unsafe_name'):
                value=copy.deepcopy(item)
                if mutation=='loss':value['controller']['events_lost']=1
                if mutation=='missing_header':value['trace_header']=''
                if mutation=='partial_zero':value['trace_header']='Events Lost: 01,Buffers lost: 0'
                if mutation=='counter':value['counters']['traced_commit_call_bytes']=4096
                if mutation=='hash':value['filtered_events']['sha256']='a'*64
                if mutation=='path':value['filtered_events']['path']='../outside.gz'
                if mutation=='pid':value['pid']=18
                if mutation=='unsafe_name':value['case']='../bad'
                manifest.write_text(json.dumps([value,value] if mutation=='duplicate_case' else [value]))
                with self.subTest(mutation=mutation),self.assertRaises(ValueError):analyze_manifest(manifest,sym,root)

    def test_json_duplicate_truncation_and_existing_output_are_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=pathlib.Path(tmp);manifest,sym,_=self.fixture(root);path=root/'bad.gz'
            for data in (b'{"event":1,"event":2}\n',b'{"event":1}'):
                path.write_bytes(gzip.compress(data))
                with self.assertRaises(ValueError):read_events(path)
            output=root/'output';output.mkdir();sentinel=output/'keep';sentinel.write_bytes(b'original')
            run=subprocess.run([sys.executable,str(pathlib.Path(__file__).resolve().parents[1]/'tools/analyze_virtual_ranges.py'),
                '--trace-manifest',str(manifest),'--symbols',str(sym),'--root',str(root),'--output',str(output)],capture_output=True)
            self.assertEqual(run.returncode,1);self.assertEqual(sentinel.read_bytes(),b'original')
            self.assertIn(b'Output already exists',run.stdout)


if __name__=='__main__':unittest.main()
