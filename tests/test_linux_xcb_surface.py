import pathlib
import sys
import unittest
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'tools'))
from run_linux_xcb_surface import validate_rows
FP='a'*64


def rows(mode=0,disabled=False,headless=False):
    created=not disabled and not headless and mode not in (1,4)
    return [dict(event='adapter',id=123,software=True),dict(event='result',source=FP,mode=mode,disabled=disabled,headless=headless,adapters=1,
        instances=2,destroyed_instances=2,xcb_enabled_instances=0 if disabled or mode==1 else 2,surfaces=2 if created else 0,destroyed_surfaces=2 if created else 0,
        surface_queries=2 if created else 0,visual_queries=2 if created else 0,supported_surfaces=1 if mode==0 and not disabled and not headless else 0,
        native_surface_true=2 if created and mode!=5 else 0,native_visual_true=2 if created else 0,buffer_bytes=514,assertions=1,family=0,swapchain_implemented=not disabled,presented=mode==0 and not disabled and not headless)]


class XcbContracts(unittest.TestCase):
    def test_all_control_modes_and_builds(self):
        for mode in range(6):validate_rows(rows(mode),FP,mode)
        validate_rows(rows(disabled=True),FP,0,True)
        validate_rows(rows(headless=True),FP,0,False,True)

    def test_source_and_mode_are_bound(self):
        with self.assertRaises(ValueError):validate_rows(rows(),'b'*64,0)
        with self.assertRaises(ValueError):validate_rows(rows(),FP,1)
        with self.assertRaises(ValueError):validate_rows(rows(),FP,0,True)

    def test_native_closure_and_both_queries(self):
        for k in ('destroyed_instances','surfaces','destroyed_surfaces','surface_queries','visual_queries','native_surface_true','native_visual_true'):
            r=rows();r[-1][k]-=1
            with self.assertRaises(ValueError):validate_rows(r,FP,0)

    def test_missing_and_disabled_paths_never_synthesize_support(self):
        for mode in (1,2,3,4,5):
            r=rows(mode);r[-1]['supported_surfaces']=1
            with self.assertRaises(ValueError):validate_rows(r,FP,mode)

    def test_GPU_reference_and_origin(self):
        for k in ('buffer_bytes','xcb_enabled_instances','family'):
            r=rows();r[-1][k]+=1
            with self.assertRaises(ValueError):validate_rows(r,FP,0)
        r=rows();r[0]['software']=False
        with self.assertRaises(ValueError):validate_rows(r,FP,0)

    def test_no_presentation_or_missing_result(self):
        for k in ('presented','swapchain_implemented'):
            r=rows();r[-1][k]=not r[-1][k]
            with self.assertRaises(ValueError):validate_rows(r,FP,0)
        with self.assertRaises(ValueError):validate_rows(rows()[:-1],FP,0)


if __name__=='__main__':unittest.main()
