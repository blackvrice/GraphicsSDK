"""Validate product ownership using real implementation PFN tests and separate hardware errors."""
import argparse
import json
import os
import pathlib
import subprocess
import audit_licenses as audit
import run_vulkan_buffer_pool_fault as native
ROOT=native.ROOT
OLD='44b92cdf3a4ae01c97af10fe25d57b6dea487bf6ed7826a7f322e338a1d39e25'
CASES=('control','dedicated-oom','upload-oom','readback-oom','bind-fresh')
CPU_CASES=('buffer','buffer-memory','upload-memory','readback-memory','image','image-memory','image-view','command-pool','command-array','query-pool','submit-fence','sampler','sampler-second','set-layout','shader','shader-second','pipeline-layout','pipeline-pass','pipeline-array','pass','framebuffer','descriptor-pool','descriptor-array','swapchain','swap-view','swap-semaphore','swap-semaphore-second')
SAFE_BEFORE={'command-array','submit-fence','sampler','sampler-second','pipeline-array','descriptor-array'}
CPU_APIS=dict(zip(CPU_CASES,('CreateBuffer','AllocateMemory','AllocateMemory','AllocateMemory','CreateImage','AllocateMemory','CreateImageView','CreateCommandPool','AllocateCommandBuffers','CreateQueryPool','CreateFence','CreateSampler','CreateSampler','CreateDescriptorSetLayout','CreateShaderModule','CreateShaderModule','CreatePipelineLayout','CreateRenderPass','CreateGraphicsPipelines','CreateRenderPass','CreateFramebuffer','CreateDescriptorPool','AllocateDescriptorSets','CreateSwapchainKHR','CreateImageView','CreateSemaphore','CreateSemaphore')))
require=native.require
def record(p):return audit.record_file(ROOT,p)
def validate_cpu(r,sdk,before):
    require(r['SDK_source_fingerprint']==sdk and type(r['GPU_calls']) is int and r['GPU_calls']==0,'CPU implementation identity differs')
    require([c['case'] for c in r['cases']]==list(CPU_CASES),'CPU failure/retry matrix differs')
    for c in r['cases']:
        expected=int(before and c['case'] not in SAFE_BEFORE)
        require(c['api']==CPU_APIS[c['case']] and type(c['ordinal']) is int and c['ordinal']==(2 if c['case'] in ('sampler-second','shader-second','swap-semaphore-second') else 1),'CPU injected call differs')
        require(all(type(c[k]) is int and c[k]>=0 for k in ('invalid_cleanup','created','destroyed','live','assertions')) and c['invalid_cleanup']==expected and c['created']==c['destroyed'] and c['created']>0 and c['live']==0 and c['assertions']>=10,'CPU output ownership/cleanup differs')
    require(r['invalid_cleanup']==sum(c['invalid_cleanup'] for c in r['cases']) and r['assertions']==sum(c['assertions'] for c in r['cases']) and r['created']==sum(c['created'] for c in r['cases']),'CPU aggregate differs')
    return r
def run(out):
    out=out.resolve();require(out.is_relative_to(ROOT) and not out.exists(),'Exclusive output root required');out.mkdir(parents=True)
    sdk=audit.sdk_fingerprint(ROOT);require(sdk!=OLD and audit.sdk_fingerprint(ROOT/'out/vulkan-creation-before-source')==OLD,'Product transition identity differs')
    v=ROOT/'benchmarks/reports/validation/2026-10-24-vulkan-creation-ownership'
    cpu={variant:validate_cpu(json.loads((v/('cpu-'+variant+'-run.log')).read_bytes()),OLD if variant=='before' else sdk,variant=='before') for variant in ('before','after')}
    builds={variant:ROOT/('out/vulkan-creation-native-'+variant)/'Release' for variant in ('before','after')}
    artifacts=[record(p) for d in builds.values() for p in sorted(d.iterdir()) if p.suffix.lower() in ('.exe','.dll','.json')]
    receipt=json.loads((ROOT/'out/vulkan-validation-runtime/receipt.json').read_text(encoding='utf-8'));vvl=next(ROOT/r['path'] for r in receipt['artifacts'] if r['path'].endswith('.dll'))
    clean={k:value for k,value in os.environ.items() if not k.upper().startswith(('VK_','VVL_','SDL_','GFX_EXPECT_','GFX_TRACE_'))};clean['VK_LOADER_LAYERS_DISABLE']='~implicit~'
    settings=out/'settings';settings.mkdir();(settings/'vk_layer_settings.txt').write_text('# SDK pNext controls synchronization validation.\n')
    commands=[];runs=[];fp=native.fingerprint()
    def call(name,variant,args,validation=False,expected=0,overrides=None):
        require(audit.sdk_fingerprint(ROOT)==sdk and native.fingerprint()==fp,'Source changed during validation')
        for r in artifacts+receipt['artifacts']:require(record(ROOT/r['path'])==r,'Native binary/runtime changed')
        d=builds[variant];env={**clean,'VK_LAYER_PATH':str(d)+(os.pathsep+str(vvl.parent) if validation else ''),'VK_INSTANCE_LAYERS':'VK_LAYER_GFX_buffer_fault','GFX_EXPECT_FAULT_LAYER':str(d/'VkLayer_gfx_buffer_fault.dll'),'VK_LAYER_SETTINGS_PATH':str(settings)}
        if validation:env['GFX_EXPECT_VULKAN_LAYER']=str(vvl)
        p=subprocess.run([str(d/'vulkan_buffer_fault.exe'),*args],cwd=ROOT,env={**env,**(overrides or {})},stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=120)
        for suffix,data in (('stdout',p.stdout),('stderr',p.stderr)):(out/(name+'.'+suffix)).write_bytes(data)
        commands.append(dict(name=name,variant=variant,argv=[str(d/'vulkan_buffer_fault.exe'),*args],validation=validation,expected=expected,returncode=p.returncode,overrides=overrides or {}));(out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
        require(p.returncode==expected,name+' unexpected exit')
        if expected==1:require(not p.stdout and b'FAIL:' in p.stderr,'Control emitted results');return
        require(not p.stderr,name+' unexpected diagnostics');print(name,'PASS',flush=True);return json.loads(p.stdout)
    probe=call('probe','after',['--probe']);require(probe['SDK_source_fingerprint']==sdk and len(set(probe['adapters']))==2,'Two GPUs required')
    for index,gpu in enumerate(probe['adapters']):
        for validation in (False,True):
            for case in CASES:
                name=f'after-gpu{index}-validation{int(validation)}-{case}';r=call(name,'after',[case,gpu,str(int(validation))],validation);native.validate(r,case,validation,sdk);runs.append(dict(name=name,variant='after',result=r))
            name=f'before-gpu{index}-validation{int(validation)}-dedicated-oom';r=call(name,'before',['dedicated-oom',gpu,str(int(validation))],validation,2);native.validate(r,'dedicated-oom',validation,OLD,True);runs.append(dict(name=name,variant='before',result=r))
    missing=out/'missing-layer';missing.mkdir()
    for i,gpu in enumerate(probe['adapters']):
        call('missing-layer-'+str(i),'after',['control',gpu,'0'],False,1,{'VK_LAYER_PATH':str(missing)})
        call('wrong-gpu-'+str(i),'after',['control','absent-GPU','0'],False,1)
    for i,args in enumerate(([],['other',probe['adapters'][0],'0'],['control',probe['adapters'][0],'2'],['control',probe['adapters'][0],'0','extra'])):call('invalid-cli-'+str(i),'after',args,False,1)
    x=dict(schema_version=1,contract='vulkan_product_creation_ownership_v1',SDK_source_fingerprint=sdk,prior_SDK_source_fingerprint=OLD,native_source_fingerprint=fp,product_fix_adopted=True,buffer_pool_adopted=False,performance_remeasured=False,physical_memory_reclamation_verified=False,probe=probe,artifacts=artifacts,cpu=cpu,runs=runs)
    (out/'results.json').write_text(json.dumps(x,indent=2)+'\n');(out/'runtime-receipt.json').write_text(json.dumps(receipt,indent=2)+'\n');print('PASS product CPU 27 before/after cases + 20 clean hardware / 4 guarded regression / 8 fail-closed processes')
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=pathlib.Path,required=True);run(p.parse_args().output)
