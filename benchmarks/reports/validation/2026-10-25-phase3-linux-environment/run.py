"""Read-only Windows-side Linux environment inventory. Never install/start a distro.

Only the report files are written. Nonzero WSL status is an observation, not a
successful Linux execution. This collector does not probe unspecified SSH hosts.
"""
import hashlib
import json
import pathlib
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / 'tools'))
import audit_licenses as audit

HERE = pathlib.Path(__file__).resolve().parent
OUTPUT = HERE / 'run'
SUMMARY = ROOT / 'benchmarks/reports/2026-10-25-phase3-linux-environment/summary.json'
SDK = '2e511b8e53dbef4bbb2bf3d86ab4b19cf03e8765111de2b3dad4fe45fa551401'

def record(path):
    return dict(path=path.relative_to(ROOT).as_posix(), bytes=path.stat().st_size,
                sha256=hashlib.sha256(path.read_bytes()).hexdigest())

def decode(raw):
    return raw.decode('utf-16-le' if b'\x00' in raw else 'utf-8-sig', errors='replace').lstrip('\ufeff')

def main():
    assert sys.platform == 'win32', 'This receipt inventories this Windows host; native Linux needs its own receipt'
    assert audit.sdk_fingerprint(ROOT) == SDK
    assert not SUMMARY.exists()
    OUTPUT.mkdir()
    commands = []
    def run(name, argv):
        started = time.monotonic()
        timed_out = False
        try:
            child = subprocess.run(argv, cwd=ROOT, stdin=subprocess.DEVNULL,
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=25)
            rc, stdout, stderr = child.returncode, child.stdout, child.stderr
        except subprocess.TimeoutExpired as exc:
            timed_out = True
            rc, stdout, stderr = None, exc.stdout or b'', exc.stderr or b''
        (OUTPUT / (name + '.stdout')).write_bytes(stdout)
        (OUTPUT / (name + '.stderr')).write_bytes(stderr)
        entry = dict(name=name, argv=argv, returncode=rc, timed_out=timed_out,
                     seconds=time.monotonic()-started, stdout=decode(stdout), stderr=decode(stderr))
        commands.append(entry)
        (OUTPUT / 'commands.json').write_text(json.dumps(commands, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
        print(f'{name}: returncode={rc}, timed_out={timed_out}', flush=True)
        return entry
    ps = r'''
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
$linuxProbeOs = Get-CimInstance Win32_OperatingSystem
$linuxProbeNames = 'wsl.exe','docker.exe','podman.exe','ssh.exe','VBoxManage.exe','vmrun.exe','multipass.exe','qemu-system-x86_64.exe','cmake.exe','ninja.exe','clang++.exe','g++.exe','vulkaninfo.exe'
$linuxProbeTools = @($linuxProbeNames | ForEach-Object {
    $linuxProbeToolName = $_
    $linuxProbePaths = @(Get-Command $linuxProbeToolName -CommandType Application -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source)
    [pscustomobject]@{name=$linuxProbeToolName;paths=$linuxProbePaths}
})
$linuxProbeSshConfig = Join-Path $env:USERPROFILE '.ssh\config'
$linuxProbeServices = @(Get-Service -Name WslService,LxssManager,vmms,com.docker.service -ErrorAction SilentlyContinue | ForEach-Object {[pscustomobject]@{name=$_.Name;status=$_.Status.ToString()}})
[pscustomobject]@{
    host_os=[pscustomobject]@{version=$linuxProbeOs.Version;build=$linuxProbeOs.BuildNumber;architecture=$linuxProbeOs.OSArchitecture}
    tools=$linuxProbeTools;services=$linuxProbeServices
    ssh_config_exists=(Test-Path -LiteralPath $linuxProbeSshConfig)
    candidate_environment_keys=@(Get-ChildItem Env: | Where-Object Name -match '^WSL|^DOCKER_HOST$|^CONTAINER_HOST$' | Select-Object -ExpandProperty Name)
} | ConvertTo-Json -Depth 5
'''
    host = run('host-inventory', ['powershell', '-NoProfile', '-NonInteractive', '-Command', ps])
    assert host['returncode'] == 0 and not host['timed_out']
    inventory = json.loads(host['stdout'])
    tools = {row['name']: row['paths'] for row in inventory['tools']}
    wsl = []
    if tools['wsl.exe']:
        for name, args in (('wsl-list', ['--list','--verbose']), ('wsl-status', ['--status'])):
            wsl.append(run(name, [tools['wsl.exe'][0], *args]))
    # Preserve observations without claiming access to any uninspected environment.
    receipt = dict(schema_version=1, task='P3-LNX-01', observed_at='2026-10-25',
        baseline_commit='b18505dd9fd4052feef33e446eee6c887b4ae628',
        SDK_source_fingerprint=SDK, SDK_modified=False,
        inventory_complete=True, native_Linux_observed=False,
        Linux_build_attempted=False, Linux_runtime_executed=False,
        Linux_GPU_accepted=False, Linux_WSI_accepted=False,
        host_inventory=inventory,
        wsl_queries=[dict(name=r['name'],returncode=r['returncode'],timed_out=r['timed_out']) for r in wsl],
        discovery_scope='Windows host command PATH, selected service names, SSH config existence only, WSL list/status; no network/SSH scan or credential reads',
        Linux_capabilities={k:'not observed: no accessible Linux execution environment established' for k in
            ('OS','CXX_compiler','CMake','Ninja','Vulkan_loader','ICD','GPU','validation_layer','display_server')},
        actions_not_performed=['WSL/container/VM install or start','system feature or driver changes','remote login','native SDK build','GPU work','deferred memory/Intel investigation'],
        runner=record(pathlib.Path(__file__).resolve()),
        raw_files=[record(p) for p in sorted(OUTPUT.iterdir()) if p.is_file()],
        repository_inputs=[record(ROOT/p) for p in ('CMakeLists.txt','CMakePresets.json','.github/workflows/build.yml','tools/memory_stress_policy.json')])
    assert audit.sdk_fingerprint(ROOT) == SDK
    SUMMARY.parent.mkdir(parents=True)
    SUMMARY.write_text(json.dumps(receipt,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')

if __name__ == '__main__':
    main()
