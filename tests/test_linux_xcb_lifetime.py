import copy
import pathlib
import sys
import unittest
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'tools'))
from run_linux_xcb_lifetime import CASES,validate_rows
FP='b'*64


def rows(validation=True):
    r=dict(event='lifetime_result',source=FP,validation=validation,physical_gpu_accepted=False,human_display_accepted=False,cases=6,rejections=16,references=3,verified_bytes=10752,gates=3,blocked=2,released=3,cleaned=3,faults=3,order_errors=0,validation_errors=0,chains=8,presents=11,fenced_presents=11,waited_presents=11,instances=4,devices=3,surfaces=7,views=24,semaphores=24,fences=40)
    for key in ('views','semaphores','fences','chains','instances','devices','surfaces'):r['destroyed_'+key]=r[key]
    return [*[dict(event='case',name=n,rejections=16,references=3) for n in CASES],r]


class LifetimeContracts(unittest.TestCase):
    def test_complete_campaign(self):
        for v in (False,True):validate_rows(rows(v),FP,v)

    def test_real_pending_gate_and_completion_required(self):
        for key in ('gates','blocked','released','cleaned','fenced_presents','waited_presents'):
            r=rows();r[-1][key]-=1
            with self.assertRaises(ValueError):validate_rows(r,FP,True)

    def test_all_native_objects_and_order_required(self):
        for key in ('views','semaphores','fences','chains','instances','devices','surfaces'):
            r=rows();r[-1]['destroyed_'+key]-=1
            with self.assertRaises(ValueError):validate_rows(r,FP,True)
        r=rows();r[-1]['order_errors']=1
        with self.assertRaises(ValueError):validate_rows(r,FP,True)

    def test_missing_or_reordered_cases_fail(self):
        for indices in ([0,1,2,3,4,6],[1,0,2,3,4,5,6]):
            r=rows();r=[r[i] for i in indices]
            with self.assertRaises(ValueError):validate_rows(r,FP,True)

    def test_references_rejections_faults_and_errors(self):
        for key in ('references','verified_bytes','rejections','faults','validation_errors'):
            r=rows();r[-1][key]+=1
            with self.assertRaises(ValueError):validate_rows(r,FP,True)

    def test_no_source_validation_or_scope_substitution(self):
        with self.assertRaises(ValueError):validate_rows(rows(),'a'*64,True)
        with self.assertRaises(ValueError):validate_rows(rows(),FP,False)
        for key in ('physical_gpu_accepted','human_display_accepted'):
            r=rows();r[-1][key]=True
            with self.assertRaises(ValueError):validate_rows(r,FP,True)
