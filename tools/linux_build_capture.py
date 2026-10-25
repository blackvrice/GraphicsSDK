"""CMake GNU compile/link launcher; preserve actual depfiles before Ninja removes them."""
import hashlib
import json
import os
import pathlib
import subprocess
import sys


def main():
    mode, directory, *args = sys.argv[1:]
    if mode not in ('compile', 'link') or not args:
        raise ValueError('Expected compile/link, evidence directory and actual compiler argv')
    directory = pathlib.Path(directory).resolve()
    directory.mkdir(parents=True, exist_ok=True)
    key = hashlib.sha256(json.dumps([mode, os.getcwd(), args]).encode()).hexdigest()[:24]
    stem = directory / (key + '-' + str(os.getpid()))
    output = pathlib.Path(args[args.index('-o') + 1]).resolve()
    if mode == 'compile':
        if '-MD' not in args or '-MMD' in args or '-MF' not in args:
            raise ValueError('Full GNU -MD dependency output required, including system headers')
    else:
        args += ['-Wl,-t,-t', '-Wl,-Map=' + str(stem.with_suffix('.map'))]
    child = subprocess.run(args, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    stem.with_suffix('.stdout').write_bytes(child.stdout)
    stem.with_suffix('.stderr').write_bytes(child.stderr)
    row = dict(mode=mode, cwd=os.getcwd(), argv=args, returncode=child.returncode, output=str(output))
    if child.returncode == 0:
        row['output_sha256'] = hashlib.sha256(output.read_bytes()).hexdigest()
        if mode == 'compile':
            dep = pathlib.Path(args[args.index('-MF') + 1])
            stem.with_suffix('.dep').write_bytes(dep.read_bytes())
            row['dependency_sha256'] = hashlib.sha256(dep.read_bytes()).hexdigest()
    stem.with_suffix('.json').write_text(json.dumps(row, indent=2) + '\n')
    sys.stdout.buffer.write(child.stdout)
    sys.stderr.buffer.write(child.stderr)
    return child.returncode


if __name__ == '__main__':
    sys.exit(main())
