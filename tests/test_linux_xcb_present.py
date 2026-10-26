import pathlib
import sys
import unittest
import subprocess
import tempfile
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'tools'))
from run_linux_xcb_present import validate_rows,DIAGNOSTIC_WRAPPER
FP='a'*64


def rows(control=0,validation=False):
    configurations=[(n,s) for n in (2,3,4) for s in (False,True)] if control==0 else [] if control==6 else [(2,control==7)]
    chains=[dict(event='chain',requested_buffers=n,vsync=s,width=17 if s else 65,height=9 if s else 7,native_format=44,native_mode=2 if s else 1 if control==8 else 0,native_min_images=max(n,3),native_images=max(n,3)) for n,s in configurations]
    count=len(chains)
    return [dict(event='adapter',id=123,software=True),*chains,dict(event='present_result',source=FP,control=control,validation=validation,human_display_accepted=False,adapters=1,cases=count,rejected=int(control in (6,7)),chains=count,destroyed_chains=count,acquires=3*count,presents=3*count,fenced_presents=3*count,waited_presents=3*count,instances=2,destroyed_instances=2,devices=1,destroyed_devices=1,surfaces=3*count,destroyed_surfaces=3*count,images=6+3*count,pixel_bytes=10920+sum(c['width']*c['height']*12 for c in chains),transfers=2,transfer_bytes=3640,padding_rows=42+sum(c['height']*3 for c in chains),validation_errors=0)]


class PresentContracts(unittest.TestCase):
    def test_native_stderr_survives_outer_stderr_stdout_merge(self):
        with tempfile.TemporaryDirectory() as folder:
            path=pathlib.Path(folder)/'native.stderr'
            result=subprocess.run([sys.executable,'-c',DIAGNOSTIC_WRAPPER,str(path),sys.executable,'-c',"import sys;print('native-out');print('native-err',file=sys.stderr)"],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,check=True)
            self.assertEqual(result.stdout.decode().strip(),'native-out')
            self.assertIn('native-err',path.read_text())

    def test_all_declared_controls(self):
        for mode in (0,6,7,8):
            for validation in (False,True):validate_rows(rows(mode,validation),FP,mode,validation)

    def test_stale_source_or_wrong_validation(self):
        with self.assertRaises(ValueError):validate_rows(rows(),'b'*64,0,False)
        with self.assertRaises(ValueError):validate_rows(rows(),FP,0,True)

    def test_missing_presents_or_completion(self):
        for k in ('acquires','presents','fenced_presents','waited_presents','destroyed_chains'):
            r=rows();r[-1][k]-=1
            with self.assertRaises(ValueError):validate_rows(r,FP,0,False)

    def test_format_mode_extent_are_native(self):
        for key,value in [('native_format',50),('native_mode',2),('width',64),('native_images',1)]:
            r=rows();r[1][key]=value
            with self.assertRaises(ValueError):validate_rows(r,FP,0,False)

    def test_full_images_transfers_and_padding_required(self):
        for key in ('images','pixel_bytes','transfers','transfer_bytes','padding_rows'):
            r=rows();r[-1][key]-=1
            with self.assertRaises(ValueError):validate_rows(r,FP,0,False)

    def test_no_fake_coverage_or_human_acceptance(self):
        r=rows();r[-1]['human_display_accepted']=True
        with self.assertRaises(ValueError):validate_rows(r,FP,0,False)
        r=rows();r[1]['requested_buffers']=4
        with self.assertRaises(ValueError):validate_rows(r,FP,0,False)

    def test_no_native_leak_or_validation_errors(self):
        for key in ('destroyed_instances','destroyed_surfaces','destroyed_devices','validation_errors'):
            r=rows();r[-1][key]+=1
            with self.assertRaises(ValueError):validate_rows(r,FP,0,False)


if __name__=='__main__':unittest.main()
