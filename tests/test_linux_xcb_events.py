import pathlib
import sys
import unittest
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'tools'))
from run_linux_xcb_events import CASES,validate_rows
FP='d'*64


def rows(validation=True):
    r=dict(event='events_result',source=FP,validation=validation,physical_gpu_accepted=False,human_display_accepted=False,cases=11,references=19,rejections=17,pixel_bytes=724476,padding_rows=1393,work_readback_bytes=56064,hidden=1,iconic=1,restored=2,idle_polls=2,recovered=3,bounded_failures=1,acquire_lost=4,present_lost=1,query_lost=1,after_close=0,gates=1,blocked=1,released=1,cleaned=1,order_errors=0,validation_errors=0,chains=17,surfaces=17,instances=2,devices=1,presents=20,fenced_presents=20,waited_presents=20,views=51,semaphores=51,fences=67)
    for k in ('views','semaphores','fences','chains','instances','devices','surfaces'):r['destroyed_'+k]=r[k]
    images=[dict(event='image',kind='red-pending' if i==0 else 'clear',width=129,height=73) for i in range(18)]+[dict(event='image',kind='clear',width=147,height=79)]
    return [*[dict(event='case',name=n,references=19,rejections=17) for n in CASES],*images,r]


class LifecycleContracts(unittest.TestCase):
    def test_complete_campaign(self):
        for v in (False,True):validate_rows(rows(v),FP,v)
    def test_real_hide_iconify_restore_idle_and_close(self):
        for k,v in [('hidden',0),('iconic',0),('restored',1),('idle_polls',0),('after_close',1)]:
            r=rows();r[-1][k]=v
            with self.assertRaises(ValueError):validate_rows(r,FP,True)
    def test_loss_recovery_is_bounded_and_covers_queries(self):
        for k in ('acquire_lost','present_lost','query_lost','recovered','bounded_failures'):
            r=rows();r[-1][k]=0
            with self.assertRaises(ValueError):validate_rows(r,FP,True)
    def test_pending_close_is_actual_and_completed(self):
        for k in ('gates','blocked','released','cleaned','fenced_presents','waited_presents'):
            r=rows();r[-1][k]-=1
            with self.assertRaises(ValueError):validate_rows(r,FP,True)
    def test_native_closure(self):
        for k in ('views','semaphores','fences','chains','instances','devices','surfaces'):
            r=rows();r[-1]['destroyed_'+k]-=1
            with self.assertRaises(ValueError):validate_rows(r,FP,True)
    def test_full_images_and_padding(self):
        r=rows();r.pop(11)
        with self.assertRaises(ValueError):validate_rows(r,FP,True)
        for k in ('references','pixel_bytes','padding_rows','work_readback_bytes'):
            r=rows();r[-1][k]-=1
            with self.assertRaises(ValueError):validate_rows(r,FP,True)
    def test_wrong_case_order_source_validation_or_scope(self):
        r=rows();r[0],r[1]=r[1],r[0]
        with self.assertRaises(ValueError):validate_rows(r,FP,True)
        with self.assertRaises(ValueError):validate_rows(rows(),'e'*64,True)
        with self.assertRaises(ValueError):validate_rows(rows(),FP,False)
        r=rows();r[-1]['human_display_accepted']=True
        with self.assertRaises(ValueError):validate_rows(r,FP,True)
    def test_error_or_boolean_counters_fail(self):
        for k,v in [('order_errors',1),('validation_errors',1),('gates',True)]:
            r=rows();r[-1][k]=v
            with self.assertRaises(ValueError):validate_rows(r,FP,True)
