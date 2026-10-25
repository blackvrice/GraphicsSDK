"""Short existing WSI query/presentation suites; no stress or driver changes.

Requires the preserved local wsi-probe, current product build and validation runtime.
Run once per evidence directory; raw output is never overwritten.
"""
import hashlib
import json
import os
import pathlib
import re
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / 'tools'))
import audit_licenses as audit

SDK = '2e511b8e53dbef4bbb2bf3d86ab4b19cf03e8765111de2b3dad4fe45fa551401'
HERE = pathlib.Path(__file__).resolve().parent
OUTPUT = HERE / 'run'
SUMMARY = ROOT / 'benchmarks/reports/2026-10-25-phase3-windows-support/summary.json'
PRODUCT = ROOT / 'out/vulkan-creation-product/Release/gfx_vulkan_presentation_tests.exe'
PROBE = ROOT / 'out/wsi-probe/build/Release/probe.exe'

def record(path):
    return dict(path=path.relative_to(ROOT).as_posix(), sha256=hashlib.sha256(path.read_bytes()).hexdigest(), bytes=path.stat().st_size)

def main():
    assert audit.sdk_fingerprint(ROOT) == SDK
    assert not SUMMARY.exists()
    runtime = json.loads((ROOT / 'out/vulkan-validation-runtime/receipt.json').read_text(encoding='utf-8'))
    for item in runtime['artifacts']:
        assert record(ROOT / item['path'])['sha256'] == item['sha256']
    # Verify the existing native query executable against its archived receipt.
    old = json.loads((ROOT / 'benchmarks/reports/2026-10-23-phase3-vulkan-upload-memory/evidence.json').read_text(encoding='utf-8'))
    def records(value):
        if isinstance(value, dict):
            if 'path' in value and 'sha256' in value:
                yield value
            for nested in value.values():
                yield from records(nested)
        elif isinstance(value, list):
            for nested in value:
                yield from records(nested)
    old_probe = [r for r in records(old) if r['path'] == PROBE.relative_to(ROOT).as_posix()]
    assert old_probe and all(r['sha256'] == record(PROBE)['sha256'] for r in old_probe)
    frozen = [record(p) for p in (PROBE, PRODUCT)] + runtime['artifacts']
    OUTPUT.mkdir()
    settings = OUTPUT / 'controlled-settings'
    settings.mkdir()
    (settings / 'vk_layer_settings.txt').write_bytes(b'# Empty settings: use SDK core/sync validation configuration.\n')
    dll = next(ROOT / r['path'] for r in runtime['artifacts'] if r['path'].endswith('.dll'))
    env = {k: v for k, v in os.environ.items() if not k.upper().startswith(('VK_', 'VVL_', 'GFX_EXPECT_VULKAN_'))}
    overrides = dict(VK_LAYER_PATH=str(dll.parent), VK_LAYER_SETTINGS_PATH=str(settings),
                     VK_LOADER_LAYERS_DISABLE='~implicit~', GFX_EXPECT_VULKAN_LAYER=str(dll))
    env.update(overrides)
    commands = []
    def run(name, argv):
        for item in frozen:
            assert record(ROOT / item['path'])['sha256'] == item['sha256']
        start = time.monotonic()
        result = subprocess.run(argv, cwd=ROOT, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=120)
        (OUTPUT / (name + '.stdout')).write_bytes(result.stdout)
        (OUTPUT / (name + '.stderr')).write_bytes(result.stderr)
        commands.append(dict(name=name, argv=argv, returncode=result.returncode, seconds=time.monotonic()-start))
        (OUTPUT / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n', encoding='utf-8')
        assert result.returncode == 0, name
        print(name + ' PASS', flush=True)
        return result.stdout.decode('utf-8-sig').replace('\r\n', '\n')
    ps = r'''
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
$supportOs = Get-CimInstance Win32_OperatingSystem
$supportGpus = @(Get-CimInstance Win32_VideoController | ForEach-Object {
    $supportVendor = [regex]::Match($_.PNPDeviceID, 'VEN_([0-9A-Fa-f]{4})').Groups[1].Value
    $supportDevice = [regex]::Match($_.PNPDeviceID, 'DEV_([0-9A-Fa-f]{4})').Groups[1].Value
    [pscustomobject]@{name=$_.Name;driver_version=$_.DriverVersion;vendor_id=$supportVendor;device_id=$supportDevice}
})
[pscustomobject]@{os_version=$supportOs.Version;os_build=$supportOs.BuildNumber;architecture=$supportOs.OSArchitecture;adapters=$supportGpus} | ConvertTo-Json -Depth 4
'''
    metadata = json.loads(run('windows-drivers', ['powershell', '-NoProfile', '-NonInteractive', '-Command', ps]))
    query = run('native-wsi-query', [str(PROBE)])
    names = ('NVIDIA GeForce GTX 1660 SUPER', 'Intel(R) UHD Graphics 770')
    assert query.index(names[0]) < query.index(names[1])
    nv, intel = query.split(names[1], 1)
    assert 'extension VK_EXT_swapchain_maintenance1\n' in nv and 'swapchainMaintenance1 1' in nv
    assert 'extension VK_EXT_swapchain_maintenance1\n' not in intel and 'swapchainMaintenance1 0' in intel
    assert all(sum(row['name'] == name for row in metadata['adapters']) == 1 for name in names)
    suites = []
    for validation in (False, True):
        name = 'presentation-validation' if validation else 'presentation-driver'
        output = run(name, [str(PRODUCT)] + (['--validation'] if validation else []))
        assert f'source={SDK} validation={int(validation)}' in output
        assert 'presentation=unsupported (completion fence feature unavailable)' in output
        tail = 'presentation_assertions=' + output.split('PASS presentation_assertions=', 1)[1]
        stats = dict((k, int(v)) for k, v in re.findall(r'(\w+)=(\d+)', tail))
        assert stats == dict(presentation_assertions=721, presents=145, images=132, draws=60, resizes=7, shown_windows=1, verified_texel_bytes=35918112, supported_adapters=1, unsupported_adapters=1, adapters=2)
        suites.append(dict(validation=validation, **stats))
    for item in frozen:
        assert record(ROOT / item['path'])['sha256'] == item['sha256']
    assert audit.sdk_fingerprint(ROOT) == SDK
    result = dict(schema_version=1, task='P3-SCP-02', observed_at='2026-10-25', baseline_commit='5d48c0d07284773d47add0751aab6ec06249d3f2',
                  SDK_source_fingerprint=SDK, SDK_modified=False, SDK_rebuilt=False,
                  windows=metadata, suites=suites, native_wsi_query=record(OUTPUT/'native-wsi-query.stdout'),
                  environment_overrides=overrides, removed_inherited_keys=sorted(k for k in os.environ if k not in env),
                  frozen_artifacts=frozen, runner=record(pathlib.Path(__file__).resolve()),
                  raw_files=[record(p) for p in sorted(OUTPUT.rglob('*')) if p.is_file()],
                  query_source='benchmarks/reports/validation/2026-10-23-phase3-vulkan-upload-memory/native-memory-probe/probe.cpp',
                  native_query_limitations='Reused diagnostic prints selected extensions/features and surface facts; SDK suites verify actual creation/presentation. No general device certification.',
                  legacy_fallback_adopted=False, adapter_fallback_adopted=False,
                  interactive_display_accepted=False, performance_measured=False, Phase3_complete=False,
                  memory_acceptance=False, Intel_AV_cause_resolved=False)
    SUMMARY.parent.mkdir(parents=True)
    SUMMARY.write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')

if __name__ == '__main__':
    main()
