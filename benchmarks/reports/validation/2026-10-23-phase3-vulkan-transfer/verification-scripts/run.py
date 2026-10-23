"""Bounded serial Windows validation; no installation or driver/system changes."""
import json
import pathlib
import shutil
import subprocess
import time

root = pathlib.Path(__file__).resolve().parents[5]
output = root / 'benchmarks/reports/validation/2026-10-23-phase3-vulkan-transfer'
commands = []

def run(name, args, expected=0):
    start = time.monotonic()
    result = subprocess.run(args, cwd=root, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=600)
    (output / (name + '.log')).write_bytes(result.stdout)
    commands.append(dict(name=name, argv=args, returncode=result.returncode, expected=expected,
                         elapsed_seconds=time.monotonic() - start))
    (output / 'commands.json').write_bytes((json.dumps(commands, indent=2) + '\n').encode())
    print(f'{name}: {result.returncode} ({time.monotonic()-start:.1f}s)', flush=True)
    if result.returncode != expected:
        raise RuntimeError(f'{name} failed; inspect saved log')
    return result.stdout

run('configure', ['cmake', '-S', '.', '-B', 'out/vulkan-transfer', '-G', 'Visual Studio 17 2022', '-A', 'x64',
    '-DGFX_ENABLE_VULKAN=ON', '-DGFX_ENABLE_D3D12=ON', '-DGFX_BUILD_EXAMPLES=ON'])
run('build', ['cmake', '--build', 'out/vulkan-transfer', '--config', 'Release', '--parallel', '1'])
run('ctest', ['ctest', '--test-dir', 'out/vulkan-transfer', '-C', 'Release', '--output-on-failure', '-j', '1',
    '--output-junit', str(output / 'ctest.xml')])
shutil.copyfile(root / 'out/vulkan-transfer/Testing/Temporary/LastTest.log', output / 'native-tests.log')
missing = run('validation-unavailable', ['out/vulkan-transfer/Release/gfx_vulkan_tests.exe', '--validation'], expected=1)
assert b'VK_LAYER_KHRONOS_validation is not installed' in missing
run('install', ['cmake', '--install', 'out/vulkan-transfer', '--config', 'Release', '--prefix', 'out/vulkan-install'])
run('consumer-configure', ['cmake', '-S', 'tests/vulkan_consumer', '-B', 'out/vulkan-consumer', '-G',
    'Visual Studio 17 2022', '-A', 'x64', '-DCMAKE_PREFIX_PATH=' + str(root / 'out/vulkan-install')])
run('consumer-build', ['cmake', '--build', 'out/vulkan-consumer', '--config', 'Release', '--parallel', '1'])
run('consumer-native', ['out/vulkan-consumer/Release/vulkan_consumer.exe'])
run('only-configure', ['cmake', '-S', '.', '-B', 'out/vulkan-only', '-G', 'Visual Studio 17 2022', '-A', 'x64',
    '-DGFX_ENABLE_VULKAN=ON', '-DGFX_ENABLE_D3D12=OFF', '-DGFX_BUILD_EXAMPLES=OFF'])
run('only-build', ['cmake', '--build', 'out/vulkan-only', '--config', 'Release', '--parallel', '1'])
run('only-ctest', ['ctest', '--test-dir', 'out/vulkan-only', '-C', 'Release', '--output-on-failure', '-j', '1',
    '--output-junit', str(output / 'only-ctest.xml')])
shutil.copyfile(root / 'out/vulkan-only/Testing/Temporary/LastTest.log', output / 'only-native-tests.log')
run('portable-configure', ['cmake', '-S', '.', '-B', 'out/vulkan-portable', '-G', 'Visual Studio 17 2022', '-A', 'x64',
    '-DGFX_ENABLE_VULKAN=OFF', '-DGFX_ENABLE_D3D12=OFF', '-DGFX_BUILD_EXAMPLES=OFF'])
run('portable-build', ['cmake', '--build', 'out/vulkan-portable', '--config', 'Release', '--parallel', '1'])
run('portable-ctest', ['ctest', '--test-dir', 'out/vulkan-portable', '-C', 'Release', '--output-on-failure', '-j', '1',
    '--output-junit', str(output / 'portable-ctest.xml')])
run('missing-headers', ['cmake', '-S', '.', '-B', 'out/vulkan-missing-headers', '-G', 'Visual Studio 17 2022', '-A', 'x64',
    '-DGFX_ENABLE_VULKAN=ON', '-DGFX_VULKAN_HEADERS=' + str(root / 'out/absent-vulkan-headers')], expected=1)
print('PASS: Windows native, installed consumer, Vulkan-only, no-backend and missing-prerequisite controls', flush=True)
