import copy
import pathlib
import sys
import unittest
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'tools'))
from run_linux_xcb_matrix import validate_rows
from run_linux_xcb_campaign import SUITES,validate_campaign,validate_controls
FP='e'*64


def rows(validation=True):
    rows=[]
    for pair,(buffers,vsync) in enumerate((b,v) for b in (2,3,4) for v in (False,True)):
        for sync in (vsync,not vsync,vsync):rows.append(dict(event='configuration',pair=pair,buffers=buffers,vsync=sync,mode=2 if sync else 0,format=44,images=5,minimum=4,mode_minimum=3,mode_maximum=0))
        seq=[]
        for a,b in [((17,9),(65,7)),((83,11),(129,13))]:
            for draw in range(3):seq.extend([(0,*a,draw),(1,*b,2-draw)])
            if a==(17,9):seq.extend([(1,65,7,1),(1,129,13,2)])
        seq.extend([(1,129,13,0),(0,83,11,2),(1,129,13,1)])
        rows.extend(dict(event='image',pair=pair,window=w,width=x,height=y,draw=d) for w,x,y,d in seq)
        rows.append(dict(event='pair',pair=pair,buffers=buffers,first_vsync=vsync,images=17,rejections=4))
    r=dict(event='matrix_result',source=FP,validation=validation,physical_gpu_accepted=False,human_display_accepted=False,pairs=6,images=102,pixel_bytes=383832,padding_rows=1062,rejections=24,acquires=114,presents=102,fenced_presents=102,waited_presents=102,chains=30,surfaces=18,instances=2,devices=1,order_errors=0,validation_errors=0,views=90,semaphores=90,fences=222)
    for k in ('chains','surfaces','instances','devices','views','semaphores','fences'):r['destroyed_'+k]=r[k]
    return [*rows,r]


def controls():return dict(source=FP,adapters=1,native_core_errors=32,native_sync_hazards=1,sync_disabled_controls=1,synthetic_errors=1025,invalid_GPU_submissions=0)
def receipts(validation=True):
    native=dict(source=FP,validation=validation,validation_errors=0,human_display_accepted=False)
    ordinary=dict(status='passed',inputs_unchanged=True,SDK_source_fingerprint=FP,returncode=0,result=native)
    result={n:copy.deepcopy(ordinary) for n in SUITES}
    result['present']['cases']=[dict(control=c,returncode=0,result=copy.deepcopy(native)) for c in (0,6,7,8)]
    return result


class MultiWindowContracts(unittest.TestCase):
    def test_complete_modes_and_references(self):
        for v in (False,True):validate_rows(rows(v),FP,v)
    def test_peer_output_sequence_not_interchangeable(self):
        r=rows();image=next(x for x in r if x['event']=='image' and x['window']==1);image['draw']=0
        with self.assertRaises(ValueError):validate_rows(r,FP,True)
    def test_missing_pair_or_image(self):
        for event in ('pair','image','configuration'):
            r=rows();r.remove(next(x for x in r if x['event']==event))
            with self.assertRaises(ValueError):validate_rows(r,FP,True)
    def test_native_mode_format_and_image_bounds(self):
        for key,value in [('mode',1),('format',50),('images',2),('minimum',2),('mode_maximum',2)]:
            r=rows();r[0][key]=value
            with self.assertRaises(ValueError):validate_rows(r,FP,True)
    def test_present_completion_and_resource_closure(self):
        for key in ('fenced_presents','waited_presents','destroyed_chains','destroyed_surfaces','destroyed_views','destroyed_semaphores','destroyed_fences','destroyed_instances','destroyed_devices'):
            r=rows();r[-1][key]-=1
            with self.assertRaises(ValueError):validate_rows(r,FP,True)
    def test_pixel_padding_rejections_and_boolean_counters(self):
        for key,value in [('pixel_bytes',1),('padding_rows',0),('rejections',23),('pairs',True),('acquires',102),('order_errors',1)]:
            r=rows();r[-1][key]=value
            with self.assertRaises(ValueError):validate_rows(r,FP,True)
    def test_scope_and_source_identity(self):
        for key,value in [('source','f'*64),('validation',False),('human_display_accepted',True),('physical_gpu_accepted',True)]:
            r=rows();r[-1][key]=value
            with self.assertRaises(ValueError):validate_rows(r,FP,True)


class IntegratedContracts(unittest.TestCase):
    def test_complete_campaigns(self):
        for v in (False,True):validate_campaign(receipts(v),FP,v,controls() if v else None)
    def test_missing_suite_or_changed_install(self):
        r=receipts();del r['lifetime']
        with self.assertRaises(ValueError):validate_campaign(r,FP,True,controls())
        for key,value in [('status','failed'),('inputs_unchanged',False),('SDK_source_fingerprint','f'*64)]:
            r=receipts();r['events'][key]=value
            with self.assertRaises(ValueError):validate_campaign(r,FP,True,controls())
    def test_unsupported_controls_must_run_and_match_scope(self):
        r=receipts();r['present']['cases'].pop()
        with self.assertRaises(ValueError):validate_campaign(r,FP,True,controls())
        for key,value in [('validation',False),('validation_errors',1),('human_display_accepted',True)]:
            r=receipts();r['resize']['result'][key]=value
            with self.assertRaises(ValueError):validate_campaign(r,FP,True,controls())
    def test_validation_must_detect_core_and_sync_without_gpu_submission(self):
        for key,value in [('native_core_errors',0),('native_sync_hazards',0),('sync_disabled_controls',0),('synthetic_errors',0),('invalid_GPU_submissions',1)]:
            c=controls();c[key]=value
            with self.assertRaises(ValueError):validate_campaign(receipts(),FP,True,c)
        with self.assertRaises(ValueError):validate_campaign(receipts(),FP,True,None)
    def test_all_three_control_instances_use_pinned_layer(self):
        stdout='source='+FP+' isolated_expected_errors=true\n'+('loaded_layer=/pinned/layer.so\n'*3)+'PASS assertions=500 adapters=1 native_core_errors=32 native_sync_hazards=1 sync_disabled_controls=1 synthetic_errors=1025 invalid_GPU_submissions=0\n'
        validate_controls(stdout,FP,'/pinned/layer.so')
        for bad in (stdout.replace('loaded_layer=/pinned/layer.so','loaded_layer=/wrong/layer.so',1),stdout.replace(FP,'f'*64),stdout.replace('PASS assertions=','FAIL assertions=')):
            with self.assertRaises(ValueError):validate_controls(bad,FP,'/pinned/layer.so')
