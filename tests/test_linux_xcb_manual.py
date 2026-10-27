import copy
import json
import pathlib
import sys
import tempfile
import unittest
from unittest.mock import patch
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'tools'))
import linux_xcb_manual as m
FP='f'*64
SESSION='a'*32


def selected(software=True):
    uuid='1'*32;value=14695981039346656037
    for b in bytes.fromhex(uuid):value=((value^b)*1099511628211)&((1<<64)-1)
    return dict(id=str(value),name='Synthetic fixture',device_uuid=uuid,driver_uuid='2'*32,device_type=4 if software else 2,vendor_id=1,device_id=0 if software else 2,api_version_raw=3,driver_version_raw=4)


def identity(software=True):return dict(schema_version=1,case='selection',SDK_source_fingerprint=FP,adapters=[selected(software)])
def rows(smoke=True,validation=False):
    a=selected(smoke);first=dict(event='session',source=FP,adapter_id=a['id'],software=smoke,validation=validation,automated=smoke,target_fps=60,session_id=SESSION,human_display_accepted=False,physical_gpu_accepted=False)
    frames=[dict(event='frame',ordinal=i+1,width=129,height=73,x=i*7,y=24,rect_width=32,rect_height=24,captured=smoke) for i in range(12)]
    states=[dict(event='window_state',frames=0,closing=False),dict(event='window_state',frames=12,closing=not smoke)]
    last=dict(event='manual_result',session_id=SESSION,frames=12,captures=12 if smoke else 0,recoveries=0,presents=12,waited_presents=12,validation_errors=0,window_alive=True,close_reason='frame_limit' if smoke else 'wm_delete',chains=1,destroyed_chains=1,surfaces=1,destroyed_surfaces=1,instances=2,destroyed_instances=2,devices=1,destroyed_devices=1,human_display_accepted=False,physical_gpu_accepted=False)
    return [first,states[0],*frames,states[1],last]


def fixture(folder,smoke=True,validation=False):
    data=rows(smoke,validation);r=dict(mode='smoke' if smoke else 'run',status='smoke-passed' if smoke else 'ready-for-human-review',native_exit=0,inputs_unchanged=True,SDK_source_fingerprint=FP,selected_adapter=selected(smoke),session_id=SESSION,validation=validation,layer_path='/pinned/libVkLayer_khronos_validation.so',result=data[-1],human_display_accepted=False,physical_gpu_accepted=False,task_completed=False)
    (folder/'native.stdout').write_text(''.join(json.dumps(x)+'\n' for x in data),encoding='utf-8');(folder/'native.stderr').write_text(' 123: calling init: /pinned/./libVkLayer_khronos_validation.so\n' if validation else '',encoding='utf-8');m.save(folder/'identity.stdout',identity(smoke));r['logs']={n:m.digest((folder/n).read_bytes()) for n in ('native.stdout','native.stderr','identity.stdout')};m.save(folder/'receipt.json',r)
    m.save(folder/'human-observation.json',dict(session_id=SESSION,native_sha256=r['logs']['native.stdout'],SDK_source_fingerprint=FP,device_uuid=selected(smoke)['device_uuid'],observer=None,checks={k:None for k in m.CHECKS}));return r,data


class ManualPreparation(unittest.TestCase):
    def test_smoke_and_real_close_records_never_accept_display(self):
        for smoke in (False,True):
            with tempfile.TemporaryDirectory() as tmp:
                folder=pathlib.Path(tmp);fixture(folder,smoke)
                result=m.check(folder);self.assertTrue(result['preparation_verified']);self.assertFalse(result['human_display_accepted']);self.assertFalse(result['physical_gpu_accepted']);self.assertFalse(result['task_completed'])
    def test_exact_selection_no_fallback_and_uuid_binding(self):
        a=selected();self.assertEqual(m.select(identity(),FP,a['id'],True,False),a)
        for adapter,software in [('0',True),(str(2**64),True),(str(int(a['id'])+1),True),(a['id'],False)]:
            with self.assertRaises(ValueError):m.select(identity(),FP,adapter,software,False)
        i=identity();i['adapters'][0]['device_uuid']='3'*32
        with self.assertRaises(ValueError):m.select(i,FP,a['id'],True,False)
    def test_smoke_cannot_choose_physical_or_multiple_candidates(self):
        with self.assertRaises(ValueError):m.select(identity(False),FP,None,False,True)
        i=identity();i['adapters'].append(copy.deepcopy(i['adapters'][0]))
        with self.assertRaises(ValueError):m.select(i,FP,None,True,True)
    def test_wrong_platform_is_rejected_without_creating_output(self):
        with tempfile.TemporaryDirectory() as tmp:
            out=pathlib.Path(tmp)/'unused'
            with patch.object(sys,'argv',['tool','smoke','--output',str(out)]),patch('linux_xcb_manual.platform.system',return_value='Windows'),self.assertRaisesRegex(ValueError,'Linux host'):m.main()
            self.assertFalse(out.exists())
    def test_changed_native_log_is_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder=pathlib.Path(tmp);fixture(folder);(folder/'native.stdout').write_text('{}\n')
            with self.assertRaisesRegex(ValueError,'Changed session log'):m.check(folder)
    def test_human_form_bound_to_exact_session_and_device(self):
        for key,value in [('session_id','b'*32),('native_sha256','0'*64),('SDK_source_fingerprint','0'*64),('device_uuid','3'*32)]:
            with tempfile.TemporaryDirectory() as tmp:
                folder=pathlib.Path(tmp);fixture(folder);form=m.read(folder/'human-observation.json');form[key]=value;m.save(folder/'human-observation.json',form)
                with self.assertRaises(ValueError):m.check(folder)
    def test_filled_human_claims_are_still_not_acceptance(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder=pathlib.Path(tmp);fixture(folder,False);form=m.read(folder/'human-observation.json');form.update(observer='A typed name',checks={k:True for k in m.CHECKS},direct_user_statements=['A claim in a file is not authenticated observation']);m.save(folder/'human-observation.json',form)
            self.assertFalse(m.check(folder)['task_completed'])
    def test_failed_exit_changed_inputs_and_acceptance_flag_fail(self):
        for key,value in [('native_exit',1),('inputs_unchanged',False),('status','failed'),('human_display_accepted',True),('physical_gpu_accepted',True),('task_completed',True)]:
            with tempfile.TemporaryDirectory() as tmp:
                folder=pathlib.Path(tmp);r,_=fixture(folder);r[key]=value;m.save(folder/'receipt.json',r)
                with self.assertRaises(ValueError):m.check(folder)
    def test_bounded_smoke_and_actual_close_are_distinct(self):
        with tempfile.TemporaryDirectory() as tmp:
            r,data=fixture(pathlib.Path(tmp));data[-1]['close_reason']='wm_delete'
            with self.assertRaises(ValueError):m.validate_rows(data,r)
        with tempfile.TemporaryDirectory() as tmp:
            r,data=fixture(pathlib.Path(tmp),False);data[-1]['close_reason']='frame_limit'
            with self.assertRaises(ValueError):m.validate_rows(data,r)
    def test_motion_scissor_and_frame_order(self):
        for key,value in [('x',0),('ordinal',1),('rect_width',999),('width',0)]:
            with tempfile.TemporaryDirectory() as tmp:
                r,data=fixture(pathlib.Path(tmp));data[3][key]=value
                with self.assertRaises(ValueError):m.validate_rows(data,r)
    def test_captured_reference_required_for_every_smoke_frame(self):
        for key,value in [('captures',0),('frames',11)]:
            with tempfile.TemporaryDirectory() as tmp:
                r,data=fixture(pathlib.Path(tmp));data[-1][key]=value
                with self.assertRaises(ValueError):m.validate_rows(data,r)
        with tempfile.TemporaryDirectory() as tmp:
            r,data=fixture(pathlib.Path(tmp));data[2]['captured']=False
            with self.assertRaises(ValueError):m.validate_rows(data,r)
    def test_completion_closure_errors_and_recovery_fail(self):
        for key,value in [('waited_presents',11),('destroyed_chains',0),('destroyed_surfaces',0),('destroyed_instances',0),('destroyed_devices',0),('validation_errors',1),('recoveries',1),('window_alive',False)]:
            with tempfile.TemporaryDirectory() as tmp:
                r,data=fixture(pathlib.Path(tmp));data[-1][key]=value
                with self.assertRaises(ValueError):m.validate_rows(data,r)
    def test_source_validation_and_automated_scope_match(self):
        for key,value in [('source','0'*64),('validation',True),('automated',False),('human_display_accepted',True)]:
            with tempfile.TemporaryDirectory() as tmp:
                r,data=fixture(pathlib.Path(tmp));data[0][key]=value
                with self.assertRaises(ValueError):m.validate_rows(data,r)
    def test_layer_dot_path_and_teardown_errors(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder=pathlib.Path(tmp);r,_=fixture(folder,True,True);self.assertTrue(m.check(folder)['preparation_verified'])
            for text in (' 123: calling init: /wrong/libVkLayer_khronos_validation.so\n',' 123: calling fini: /pinned/libVkLayer_khronos_validation.so\n','validation: unexpected teardown error\n'):
                (folder/'native.stderr').write_text(text);r['logs']['native.stderr']=m.digest((folder/'native.stderr').read_bytes());m.save(folder/'receipt.json',r)
                with self.assertRaises(ValueError):m.check(folder)
