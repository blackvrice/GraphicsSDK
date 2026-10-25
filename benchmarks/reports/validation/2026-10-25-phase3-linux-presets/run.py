"""Windows CMake preset definition checks, not Linux/SDK build acceptance.

The temporary fixture uses project(... NONE) and changes only the copied Linux
host condition to true. It exercises CMake inheritance/macros and a zero-test
failure control without compiling the SDK or pretending this host is Linux.
"""
import copy
import hashlib
import json
import pathlib
import shutil
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT/'tools'))
import audit_licenses as audit

HERE = pathlib.Path(__file__).resolve().parent
OUTPUT = HERE/'run'
WORK = ROOT/'out/linux-vulkan-preset-contracts'
SUMMARY = ROOT/'benchmarks/reports/2026-10-25-phase3-linux-presets/summary.json'
SDK = '2e511b8e53dbef4bbb2bf3d86ab4b19cf03e8765111de2b3dad4fe45fa551401'
NAMES = ('linux-vulkan', 'linux-vulkan-validation')

def record(path):
    return dict(path=path.relative_to(ROOT).as_posix(), bytes=path.stat().st_size,
                sha256=hashlib.sha256(path.read_bytes()).hexdigest())

def main():
    assert sys.platform == 'win32' and audit.sdk_fingerprint(ROOT) == SDK
    assert not SUMMARY.exists() and not WORK.exists()
    cmake = shutil.which('cmake')
    assert cmake
    ctest = pathlib.Path(cmake).with_name('ctest.exe')
    candidates = [pathlib.Path.home()/'AppData/Local/Programs/CLion/bin/ninja/win/x64/ninja.exe',
                  pathlib.Path('C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe')]
    ninja = next(p for p in candidates if p.is_file())
    OUTPUT.mkdir()
    WORK.mkdir()
    commands = []
    def run(name, argv, cwd=ROOT, success=True):
        start = time.monotonic()
        child = subprocess.run(argv,cwd=cwd,stdin=subprocess.DEVNULL,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=60)
        (OUTPUT/(name+'.stdout')).write_bytes(child.stdout)
        (OUTPUT/(name+'.stderr')).write_bytes(child.stderr)
        commands.append(dict(name=name,argv=[str(x) for x in argv],cwd=str(cwd),returncode=child.returncode,expected_success=success,seconds=time.monotonic()-start))
        (OUTPUT/'commands.json').write_text(json.dumps(commands,indent=2)+'\n',encoding='utf-8')
        assert (child.returncode == 0) == success, name
        print(name+' PASS',flush=True)
        return (child.stdout+child.stderr).decode('utf-8',errors='replace')
    version = run('cmake-version',[cmake,'--version'])
    presets = json.loads((ROOT/'CMakePresets.json').read_text(encoding='utf-8'))
    prior = json.loads(subprocess.check_output(['git','--no-optional-locks','-c','gc.auto=0','-c','maintenance.auto=false','-c','core.preloadIndex=false','show','8172b4f:CMakePresets.json'],cwd=ROOT))
    for group in ('configurePresets','buildPresets','testPresets'):
        current = {p['name']:p for p in presets[group]}
        assert all(current[p['name']]==p for p in prior[group])
        assert set(current)-{p['name'] for p in prior[group]}==set(NAMES)
    listed = run('repository-list',[cmake,'--list-presets=all'])
    assert all(name not in listed for name in NAMES)
    for name in NAMES:
        assert not (ROOT/'out'/name).exists()
        assert not (ROOT/'out'/(name+'-install')).exists()
        for kind,argv in (('configure',[cmake,'--preset',name]),('build',[cmake,'--build','--preset',name]),('test',[str(ctest),'--preset',name])):
            text = run('disabled-'+kind+'-'+name,argv,success=False)
            assert 'disabled' in text.lower() and 'preset' in text.lower(),text
        assert not (ROOT/'out'/name).exists()
    fixture = WORK/'fixture'
    fixture.mkdir()
    fixture_presets = copy.deepcopy(presets)
    for group in ('configurePresets','buildPresets','testPresets'):
        next(p for p in fixture_presets[group] if p['name']=='linux-vulkan')['condition']=True
    fixture_file = fixture/'CMakePresets.json'
    fixture_file.write_text(json.dumps(fixture_presets,indent=2)+'\n',encoding='utf-8')
    (fixture/'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.25)\nproject(PresetContractFixture NONE)\nenable_testing()\n',encoding='utf-8')
    listed = run('fixture-list',[cmake,'--list-presets=all'],fixture)
    assert all(name in listed for name in NAMES)
    caches=[]
    for name in NAMES:
        run('fixture-configure-'+name,[cmake,'--preset',name,'-DCMAKE_MAKE_PROGRAM='+ninja.as_posix()],fixture)
        cache = fixture/'out'/name/'CMakeCache.txt'
        values={k:v for k,v in (line.split('=',1) for line in cache.read_text(encoding='utf-8').splitlines() if re_cache(line))}
        expected={'CMAKE_BUILD_TYPE':'Release','GFX_ENABLE_D3D12':'OFF','GFX_ENABLE_VULKAN':'ON','GFX_BUILD_TESTS':'ON','GFX_BUILD_EXAMPLES':'OFF','GFX_VULKAN_VALIDATION_TESTS':'ON' if name.endswith('-validation') else 'OFF',
                  'CMAKE_INSTALL_PREFIX':(fixture/'out'/(name+'-install')).as_posix(),'GFX_VULKAN_HEADERS':(fixture/'.deps/Vulkan-Headers').as_posix()}
        for key,value in expected.items():
            assert next(v for k,v in values.items() if k.split(':')[0]==key)==value,(name,key)
        empty = run('fixture-zero-tests-'+name,[str(ctest),'--preset',name],fixture,success=False)
        assert 'no tests were found' in empty.lower(),empty
        snapshot=OUTPUT/('fixture-cache-'+name+'.txt')
        snapshot.write_bytes(cache.read_bytes())
        caches.append(dict(preset=name,values=expected,cache=record(snapshot)))
    override = (ROOT/'.deps/Vulkan-Headers').as_posix()
    run('fixture-external-headers',[cmake,'--preset',NAMES[0],'-DCMAKE_MAKE_PROGRAM='+ninja.as_posix(),'-DGFX_VULKAN_HEADERS='+override],fixture)
    assert 'GFX_VULKAN_HEADERS:UNINITIALIZED='+override in (fixture/'out/linux-vulkan/CMakeCache.txt').read_text(encoding='utf-8')
    (OUTPUT/'fixture-cache-external-headers.txt').write_bytes((fixture/'out/linux-vulkan/CMakeCache.txt').read_bytes())
    assert audit.sdk_fingerprint(ROOT)==SDK
    receipt=dict(schema_version=1,task='P3-LNX-02',observed_at='2026-10-25',baseline_commit='8172b4fea35ac74e6a95877fc29dd0d8d66fff37',
                 SDK_source_fingerprint=SDK,SDK_modified=False,Linux_configure_executed=False,Linux_build_executed=False,Linux_runtime_executed=False,
                 verification_host='Windows',cmake_version=version.strip(),cmake_executable=cmake,fixture_Ninja_executable=str(ninja),
                 fixture_scope='CMake project NONE; copied Linux conditions set true only in fixture; no SDK/compilation/GPU tests',
                 fixture_caches=caches,fixture_inputs=[record(fixture_file),record(fixture/'CMakeLists.txt')],
                 runner=record(pathlib.Path(__file__).resolve()),preset_file=record(ROOT/'CMakePresets.json'),
                 fixture_output_caches_not_native_acceptance=True,
                 raw_files=[record(p) for p in sorted(OUTPUT.iterdir()) if p.is_file()],
                 controls=['Original presets unchanged','All six Linux configure/build/test invocations disabled on actual Windows host','CMake macro/inheritance yields separate Release/install roots and required cache flags','Zero-test CTest fixture fails for both test presets','External headers CLI override accepted in configuration-only fixture'])
    SUMMARY.parent.mkdir(parents=True)
    SUMMARY.write_text(json.dumps(receipt,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')

def re_cache(line):
    return '=' in line and not line.startswith(('#','//'))

if __name__=='__main__':
    main()
