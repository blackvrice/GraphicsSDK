"""A private Openbox for the surrounding xvfb-run; never a user's desktop."""
import os
import pathlib
import shlex
import subprocess
import sys
import time


def wait_ready(manager,marker,timeout=8):
    deadline=time.monotonic()+timeout
    while True:
        code=manager.poll()
        if code is not None:raise RuntimeError('Openbox exited before native client: '+str(code))
        if marker.is_file():return
        if time.monotonic()>=deadline:raise RuntimeError('Openbox startup handshake deadline')
        time.sleep(.02)


def main():
    log,config,*command=sys.argv[1:]
    marker=pathlib.Path(log+'.ready').resolve()
    if marker.exists():raise ValueError('Fresh WM startup marker required')
    env={k:v for k,v in os.environ.items() if not k.upper().startswith(('VK_','VVL_','LD_','GFX_'))}
    startup=shlex.join([sys.executable,'-c','import pathlib,sys;pathlib.Path(sys.argv[1]).write_text("ready\\n")',str(marker)])
    with pathlib.Path(log).open('wb') as output:
        manager=subprocess.Popen(['openbox','--sm-disable','--debug','--config-file',config,'--startup',startup],env=env,stdout=output,stderr=subprocess.STDOUT)
        try:
            wait_ready(manager,marker)
            output.write(b'WM startup acknowledged before native client\n');output.flush()
            result=subprocess.run(command,timeout=160)
            code=manager.poll();output.write(('WM status after native client: '+str(code)+'\n').encode());output.flush()
            if code is not None:raise RuntimeError('Openbox exited during native client: '+str(code))
            return result.returncode
        finally:
            if manager.poll() is None:manager.terminate()
            try:manager.wait(timeout=8)
            except subprocess.TimeoutExpired:manager.kill();manager.wait(timeout=8)


if __name__=='__main__':sys.exit(main())
