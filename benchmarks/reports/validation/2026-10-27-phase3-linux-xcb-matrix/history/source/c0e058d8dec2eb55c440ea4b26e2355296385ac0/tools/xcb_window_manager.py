"""A private Openbox for the surrounding xvfb-run; never a user's desktop."""
import os
import pathlib
import subprocess
import sys


def main():
    log,config,*command=sys.argv[1:]
    env={k:v for k,v in os.environ.items() if not k.upper().startswith(('VK_','VVL_','LD_','GFX_'))}
    with pathlib.Path(log).open('wb') as output:
        manager=subprocess.Popen(['openbox','--sm-disable','--config-file',config],env=env,stdout=output,stderr=subprocess.STDOUT)
        try:
            result=subprocess.run(command,timeout=160)
            return result.returncode
        finally:
            manager.terminate()
            try:manager.wait(timeout=8)
            except subprocess.TimeoutExpired:manager.kill();manager.wait(timeout=8)


if __name__=='__main__':sys.exit(main())
