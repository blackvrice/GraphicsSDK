import pathlib
import sys
import unittest
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'tools'))
from run_linux_xcb_resize import CASES,validate_rows
FP='c'*64


def rows(validation=True):
    r=dict(event='resize_result',source=FP,validation=validation,physical_gpu_accepted=False,human_display_accepted=False,cases=7,resizes=13,rejections=24,images=43,references=3,pixel_bytes=127792,work_readback_bytes=10752,padding_rows=409,creates=23,old_attempts=13,fresh_attempts=10,native_failures=1,allocation_callbacks=1,enum_failures=1,view_failures=1,acquire_out_of_date=1,acquire_suboptimal=1,present_out_of_date=1,present_suboptimal=1,gates=1,blocked=1,released=1,cleaned=1,order_errors=0,validation_errors=0,chains=22,presents=45,fenced_presents=45,waited_presents=45,instances=2,devices=1,surfaces=7,views=61,semaphores=61,fences=139)
    for k in ('views','semaphores','fences','chains','instances','devices','surfaces'):r['destroyed_'+k]=r[k]
    return [*[dict(event='case',name=n,resizes=13,rejections=24,images=43,references=3) for n in CASES],r]


class ResizeContracts(unittest.TestCase):
    def test_complete_campaign(self):
        for v in (False,True):validate_rows(rows(v),FP,v)

    def test_missing_cases_and_wrong_order(self):
        r=rows();r.pop(2)
        with self.assertRaises(ValueError):validate_rows(r,FP,True)
        r=rows();r[0],r[1]=r[1],r[0]
        with self.assertRaises(ValueError):validate_rows(r,FP,True)

    def test_pending_work_really_blocked_and_released(self):
        for k in ('gates','blocked','released','cleaned'):
            r=rows();r[-1][k]=0
            with self.assertRaises(ValueError):validate_rows(r,FP,True)

    def test_retirement_failure_retry_and_status_coverage(self):
        for k in ('creates','old_attempts','fresh_attempts','native_failures','allocation_callbacks','enum_failures','view_failures','acquire_out_of_date','acquire_suboptimal','present_out_of_date','present_suboptimal'):
            r=rows();r[-1][k]=0
            with self.assertRaises(ValueError):validate_rows(r,FP,True)

    def test_full_pixels_and_padding(self):
        for k in ('images','references','pixel_bytes','work_readback_bytes','padding_rows'):
            r=rows();r[-1][k]-=1
            with self.assertRaises(ValueError):validate_rows(r,FP,True)

    def test_native_closure_and_completion(self):
        for k in ('destroyed_views','destroyed_semaphores','destroyed_fences','destroyed_chains','destroyed_instances','destroyed_devices','destroyed_surfaces','fenced_presents','waited_presents'):
            r=rows();r[-1][k]-=1
            with self.assertRaises(ValueError):validate_rows(r,FP,True)

    def test_no_wrong_source_validation_or_scope(self):
        with self.assertRaises(ValueError):validate_rows(rows(),'d'*64,True)
        with self.assertRaises(ValueError):validate_rows(rows(),FP,False)
        for k in ('physical_gpu_accepted','human_display_accepted'):
            r=rows();r[-1][k]=True
            with self.assertRaises(ValueError):validate_rows(r,FP,True)

    def test_errors_and_boolean_counters_rejected(self):
        for k,v in [('order_errors',1),('validation_errors',1),('gates',True)]:
            r=rows();r[-1][k]=v
            with self.assertRaises(ValueError):validate_rows(r,FP,True)
