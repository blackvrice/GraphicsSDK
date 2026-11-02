"""Controlled GNU Release cache/compile-command evidence; preserves legacy defaults."""
import hashlib
import json
import pathlib
import shlex
import shutil

NAME='gnu-release-o2-no-ipo-unity-v1'
SOURCE='tools/linux_build_policy.py'
SETTINGS={'CMAKE_BUILD_TYPE':'Release','CMAKE_CXX_FLAGS_RELEASE':'-O2 -DNDEBUG',
          'CMAKE_INTERPROCEDURAL_OPTIMIZATION':'OFF','CMAKE_UNITY_BUILD':'OFF'}
ARGS=['-D'+key+'='+value for key,value in SETTINGS.items()]+['-DCMAKE_C_FLAGS_RELEASE=-O2 -DNDEBUG']


def require(ok,message):
    if not ok:raise ValueError(message)


def cache(path):
    result={}
    for line in path.read_text(encoding='utf-8-sig').splitlines():
        if not line.startswith(('#','//')) and ':' in line and '=' in line:
            key,value=line.split('=',1);result[key.split(':',1)[0]]=value
    return result


def scope(value,projects):
    require(type(value) is dict and value.get('name')==NAME and type(value.get('projects')) is dict
            and set(value['projects'])==set(projects),'Controlled GNU build policy required')


def flags(commands):
    require(type(commands) is list and commands,'Empty controlled compile graph')
    for row in commands:
        argv=shlex.split(row['command'])
        require('-O2' in argv and '-DNDEBUG' in argv and not any(
            (x.startswith('-O') and x!='-O2') or x.startswith(('-flto','-march=','-mtune=')) for x in argv),
            'Uncontrolled optimization/LTO/CPU tuning in compile graph')


def check(folder,value,projects,frozen):
    scope(value,projects)
    for project,row in value['projects'].items():
        for key in ('cache','commands'):
            path=folder/'controlled'/(project+('-cache.txt' if key=='cache' else '-commands.json'))
            item=row[key]
            require(any(x==item for x in frozen) and path.is_file() and path.stat().st_size==item['bytes']
                    and hashlib.sha256(path.read_bytes()).hexdigest()==item['sha256'],'Controlled build snapshot/hash drift')
        settings=cache(folder/'controlled'/(project+'-cache.txt'))
        require(all(settings.get(k)==v for k,v in SETTINGS.items()),'Controlled CMake settings drift')
        flags(json.loads((folder/'controlled'/(project+'-commands.json')).read_text(encoding='utf-8')))


def capture(folder,builds,record):
    (folder/'controlled').mkdir();value=dict(name=NAME,projects={})
    for project,build in builds.items():
        row={}
        for key,name,suffix in (('cache','CMakeCache.txt','-cache.txt'),('commands','compile_commands.json','-commands.json')):
            source=build/name;shutil.copyfile(source,folder/'controlled'/(project+suffix));row[key]=record(source)
        value['projects'][project]=row
    check(folder,value,builds,[item for row in value['projects'].values() for item in row.values()])
    return value
