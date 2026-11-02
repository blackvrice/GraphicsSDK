"""Shared LLGL workload build/qualification policy; no native work or dispatch here."""
import linux_llgl_probe as p

SOURCE='tools/linux_llgl_workload_mode.py'
PROFILES=('upload','draw','pso','allocation')


def prefix(profile, mode='software-diagnostic'):
    p.require(profile in PROFILES,'Unknown LLGL workload')
    p.identity_prefix(mode)
    return 'linux-llgl-'+profile+('-physical' if mode=='physical' else '')


def mode(r):
    value=r.get('mode','software-diagnostic');p.identity_prefix(value);return value


def guard(r):
    value=r.get('physical_guard_only',False)
    p.require(type(value) is bool and (not value or mode(r)=='physical'),'Invalid workload guard profile')
    return value


def validate_probe(probe, selected_mode, guard_only=False):
    p.validate_qualification(probe['qualification'],selected_mode,guard_only)
    p.require(mode(probe)==selected_mode and guard(probe) is guard_only,'Probe/workload profile differs')
    p.exact(probe,dict(status='completed',inputs_unchanged=True,minimal_buffer_reference_accepted=not guard_only,
        physical_GPU_accepted=False,workload_correctness_accepted=False,performance_accepted=False,performance_samples=0))
    if selected_mode=='physical':
        p.exact(probe,dict(physical_probe_qualified=not guard_only,P3_BEN_08_completed=False))


def validate_receipt(r, profile):
    selected_mode=mode(r);guard_only=guard(r)
    validate_probe(r['probe'],selected_mode,guard_only)
    p.exact(r,{f'software_{profile}_correctness_verified':selected_mode=='software-diagnostic'})
    counters=('verified_images','verified_bytes')+(('verified_buffers',) if profile=='allocation' else ())
    p.require(all(type(r[k]) is int and r[k]>=0 for k in counters),'Invalid workload counters')
    if selected_mode=='physical':
        p.exact(r,dict(physical_guard_only=guard_only,P3_BEN_08_completed=False,
            **{f'physical_{profile}_correctness_verified':not guard_only}))
        p.exact(r['adaptation'],dict(mode='physical'))
    else:
        for key in (f'physical_{profile}_correctness_verified','P3_BEN_08_completed'):
            if key in r:p.exact(r,{key:False})


def finish(r, profile):
    r[f'software_{profile}_correctness_verified']=mode(r)=='software-diagnostic'
    if mode(r)=='physical':r[f'physical_{profile}_correctness_verified']=not guard(r)


def initialize(r, selected_mode, guard_only, profile):
    if selected_mode=='physical':
        r.update(mode=selected_mode,physical_guard_only=guard_only,P3_BEN_08_completed=False,
                 **{f'physical_{profile}_correctness_verified':False})


def jobs(r, normal):
    if not guard(r):return normal
    first=normal[0]['scenario']
    return [dict(j,scenario=first,name=j['name'].replace('smoke',first)) for j in p.guard_jobs()]


def native_kind(job,r):
    selected_mode=mode(r)
    return ('software-diagnostic' if selected_mode=='physical' else 'physical') if job['kind']=='wrong-kind' else selected_mode


def wrong_kind_message(selected_mode):
    p.identity_prefix(selected_mode)
    return b'Physical qualification only' if selected_mode=='physical' else b'Software diagnostics only'


def totals(r, values):
    return tuple(0 for _ in values) if guard(r) else values


def summary(r, value, profile):
    if guard(r):value.update(processes=4,normal_processes=0,core_sync_controls=0,rejection_controls=4)
    if mode(r)=='physical':
        value.update(mode='physical',physical_guard_only=guard(r),P3_BEN_08_completed=False,
                     **{f'physical_{profile}_correctness_verified':not guard(r)})
    return value


def adapt(platform, changes, profile, selected_mode):
    prefix(profile,selected_mode)
    if selected_mode=='software-diagnostic':return platform,changes
    variable='GFX_LLGL_'+profile.upper()+'_KIND'
    old=f'const char* kind=std::getenv("{variable}");require(!kind||std::string_view(kind)=="software-diagnostic","Software diagnostics only");'
    new=f'const char* kind=std::getenv("{variable}");require(kind&&std::string_view(kind)=="physical","Physical qualification only");'
    pairs=[(old,new),
           ('selected=linuxbench::select(query.enumerate(validation),expected,"software-diagnostic");',
            'selected=linuxbench::select(query.enumerate(validation),expected,"physical");'),
           ('\\"mode\\":\\"software-diagnostic\\"','\\"mode\\":\\"physical\\"')]
    for old,new in pairs:
        p.require(platform.count(old)==1,'Physical platform adaptation token changed')
        platform=platform.replace(old,new)
    return platform,changes+['physical-only native selection and identity']
