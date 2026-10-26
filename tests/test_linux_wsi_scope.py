import copy
import pathlib
import sys
import unittest
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'tools'))
from run_linux_wsi_scope import validate_rows

FP='a'*64
UUID='b'*32


def rows():
    return [dict(event='session',system='xcb',native_surface=True,width=64,height=48,wayland_session_tested=False,surface_maintenance1=True,surface_capabilities2=True),
            dict(event='queue',uuid=UUID,family=0,graphics=True,surface_present=True,visual_present=True),
            dict(event='adapter',uuid=UUID,device_type=4,selected_family=0,selected_present=True,swapchain_extension=True,maintenance_extension=True,maintenance_feature=True,rgba8=True,fifo=True,usage=19,policy_ready=True,native_device_created=True,current_width=64,current_height=48,min_images=2,max_images=0),
            dict(event='result',source=FP,adapters=1,software_adapters=1,policy_ready_adapters=1,sdk_linux_wsi=False,swapchain_created=False,presented=False,human_display_accepted=False)]


class ScopeContracts(unittest.TestCase):
    def test_scope_is_not_SDK_presentation(self):
        self.assertEqual(validate_rows(rows(),FP)['policy_ready_adapters'],1)
        for flag in ('sdk_linux_wsi','swapchain_created','presented','human_display_accepted'):
            r=rows();r[-1][flag]=True
            with self.assertRaises(ValueError):validate_rows(r,FP)

    def test_stale_or_incomplete_source(self):
        with self.assertRaises(ValueError):validate_rows(rows(),'c'*64)
        with self.assertRaises(ValueError):validate_rows(rows()[:-1],FP)

    def test_wrong_selected_queue_or_surface_support(self):
        for mutate in (lambda r:r[2].update(selected_family=1),lambda r:r[1].update(surface_present=False),lambda r:r[1].update(visual_present=False)):
            r=rows();mutate(r)
            with self.assertRaises(ValueError):validate_rows(r,FP)

    def test_missing_completion_or_format_or_usage(self):
        for flag in ('maintenance_extension','maintenance_feature','rgba8','fifo','swapchain_extension'):
            r=rows();r[2][flag]=False
            with self.assertRaises(ValueError):validate_rows(r,FP)
        r=rows();r[2]['usage']=16
        with self.assertRaises(ValueError):validate_rows(r,FP)

    def test_honest_unsupported_prerequisite(self):
        r=rows();r[2].update(maintenance_feature=False,policy_ready=False,native_device_created=False);r[-1]['policy_ready_adapters']=0
        self.assertEqual(validate_rows(r,FP)['policy_ready_adapters'],0)

    def test_no_duplicate_or_unknown_native_identity(self):
        r=rows();r.insert(2,copy.deepcopy(r[1]))
        with self.assertRaises(ValueError):validate_rows(r,FP)
        r=rows();r[1]['uuid']='c'*32
        with self.assertRaises(ValueError):validate_rows(r,FP)
        r=rows();r[2]['uuid']='0'*32
        with self.assertRaises(ValueError):validate_rows(r,FP)

    def test_limits_and_device_creation_are_required(self):
        for changes in (dict(current_width=65),dict(min_images=3,max_images=2),dict(native_device_created=False)):
            r=rows();r[2].update(changes)
            with self.assertRaises(ValueError):validate_rows(r,FP)

    def test_Wayland_and_software_are_not_inferred(self):
        r=rows();r[0]['wayland_session_tested']=True
        with self.assertRaises(ValueError):validate_rows(r,FP)
        r=rows();r[2]['device_type']=2
        with self.assertRaises(ValueError):validate_rows(r,FP)


if __name__=='__main__':unittest.main()
