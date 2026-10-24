"""Derive a new, independently reviewable study; preserve all fixed-pool evidence."""
import pathlib

root=pathlib.Path(__file__).resolve().parents[5]
old='vulkan_buffer_pool';new='vulkan_adaptive_buffer_pool'
old_path='vulkan-buffer-pool';new_path='vulkan-adaptive-buffer-pool'
v=pathlib.Path(__file__).resolve().parent

def read(p):return (root/p).read_text(encoding='utf-8')
def write(p,s):
    p=root/p;p.parent.mkdir(parents=True,exist_ok=True)
    p.write_bytes(s.replace('\r\n','\n').replace('\n','\r\n').encode())
def derive(p):
    s=read(p).replace(old,new).replace(old_path,new_path).replace(new_path+'-source-v2',new_path+'-source')
    return s
def replace(s,a,b):
    assert s.count(a)==1,a[:100]
    return s.replace(a,b)

for n in ('BufferPoolRange.hpp','BufferPool.hpp','PoolRangeTests.cpp','PoolGpuTests.cpp'):
    write('experiments/'+new+'/'+n,derive('experiments/'+old+'/'+n))

p='experiments/'+new+'/BufferPoolRange.hpp';s=read(p)
s=s.replace('Experimental size-class slab.','Experimental growing size-class slab.')
s=replace(s,'std::uint32_t stride_{};','std::uint32_t stride_{}, capacity_{};')
s=replace(s,'explicit BufferSlots(std::uint32_t s):stride_(std::has_single_bit(s)&&s>=64&&s<=4096?s:0){}',
'''static std::uint32_t nextCapacity(std::uint32_t stride,std::uint32_t largest){
        if(!std::has_single_bit(stride)||stride<64||stride>4096)return 0;
        const auto limit=blockBytes/stride;
        if(largest&&(!std::has_single_bit(largest)||largest>limit))return 0;
        return largest?std::min(largest*2,limit):1;
    }
    explicit BufferSlots(std::uint32_t s,std::uint32_t capacity):
        stride_(std::has_single_bit(s)&&s>=64&&s<=4096?s:0){
        if(stride_&&std::has_single_bit(capacity)&&capacity<=blockBytes/stride_)capacity_=capacity;
        else stride_=0;
    }''')
s=s.replace('#include <array>','#include <algorithm>\n#include <array>')
s=replace(s,'std::uint32_t capacity()const{return stride_?blockBytes/stride_:0;}','std::uint32_t capacity()const{return capacity_;}\n    std::uint32_t bytes()const{return stride_*capacity_;}')
write(p,s)
p='experiments/'+new+'/BufferPool.hpp';s=read(p)
s=replace(s,'std::uint32_t stride):native(std::move(n)),type(t),slots(stride){}','std::uint32_t stride,std::uint32_t capacity):native(std::move(n)),type(t),slots(stride,capacity){}')
write(p,s)

s=derive('tools/prepare_vulkan_buffer_pool.py')
s=s.replace('result.resourceAllocationBytes+=vulkan::BufferSlots::blockBytes','result.resourceAllocationBytes+=a->block->slots.bytes()')
s=replace(s,'            for(auto&w:owner->bufferBlocks)if(auto b=w.lock();b&&b->type==*type&&b->slots.stride()==stride)if(auto slot=b->take()){a->block=b;a->slot=*slot;break;}',
'''            std::uint32_t largest{};
            for(auto&w:owner->bufferBlocks)if(auto b=w.lock();b&&b->type==*type&&b->slots.stride()==stride){
                largest=std::max(largest,b->slots.capacity());
                if(auto slot=b->take()){a->block=b;a->slot=*slot;break;}
            }''')
s=replace(s,'std::make_shared<BufferBlock>(owner->native,*type,stride);','std::make_shared<BufferBlock>(owner->native,*type,stride,vulkan::BufferSlots::nextCapacity(stride,largest));')
s=replace(s,'pool.allocationSize=vulkan::BufferSlots::blockBytes','pool.allocationSize=block->slots.bytes()')
s=s.replace('On a larger block OOM','On a block OOM')
write('tools/prepare_'+new+'.py',s)

s=derive('tools/run_vulkan_buffer_pool.py')
s=s.replace("VARIANTS=('baseline','candidate','sdl')","VARIANTS=('baseline','fixed','candidate','sdl')")
s=replace(s,"def source_ids():return dict(baseline=audit.sdk_fingerprint(ROOT),candidate=audit.sdk_fingerprint(SOURCE))",'''def source_ids():return dict(baseline=audit.sdk_fingerprint(ROOT),fixed=audit.sdk_fingerprint(ROOT/'out/vulkan-buffer-pool-source-v2'),candidate=audit.sdk_fingerprint(SOURCE))

def reservation(n,variant):
    if variant=='baseline':return n,n*4096
    if variant=='fixed':return (n+15)//16,((n+15)//16)*65536
    b.require(variant=='candidate' and type(n) is int and n>0,'Invalid growing reservation')
    blocks=slots=largest=0
    while slots<n:
        largest=min(largest*2,16) if largest else 1;blocks+=1;slots+=largest
    return blocks,slots*4096''')
s=s.replace("ids['candidate' if variant=='candidate' else 'baseline']","ids[variant if variant in ('fixed','candidate') else 'baseline']")
s=replace(s,"native=n if variant=='baseline' else (n+15)//16","native,reserved=reservation(n,variant)")
s=s.replace("(native*65536 if variant=='candidate' else n*4096)","reserved")
s=replace(s,"exe['sdl']=exe['baseline']","exe['fixed']=ROOT/'out/vulkan-buffer-pool-benchmark/Release/vulkan_allocation.exe';exe['sdl']=exe['baseline']")
s=replace(s,"    dlls={k:p.parent/'VkLayer_gfx_allocation.dll' for k,p in trace.items()}","    trace['fixed']=ROOT/'out/vulkan-buffer-pool-trace/Release/vulkan_allocation_trace.exe'\n    dlls={k:p.parent/'VkLayer_gfx_allocation.dll' for k,p in trace.items()}")
s=s.replace("for variant in ('baseline','candidate'):","for variant in ('baseline','fixed','candidate'):")
s=s.replace('20261029','20261030').replace('vulkan_small_buffer_slab_candidate_trial_v1','vulkan_growing_buffer_slab_candidate_trial_v1')
s=s.replace('PASS 48 benchmark processes + 8 separate trace processes','PASS 64 benchmark processes + 12 separate trace processes')
write('tools/run_'+new+'.py',s)

s=derive('tools/report_vulkan_buffer_pool.py')
s=s.replace('vulkan_small_buffer_slab_candidate_trial_v1','vulkan_growing_buffer_slab_candidate_trial_v1').replace('20261029','20261030')
s=s.replace('len(commands)==70 and len(by_name)==70','len(commands)==92 and len(by_name)==92')
s=s.replace("('baseline','candidate')","('baseline','fixed','candidate')")
s=s.replace("len(x['runs'])==48 and len(x['traces'])==8","len(x['runs'])==64 and len(x['traces'])==12")
s=s.replace('Python_tests=444,measurement_processes=36,correctness_processes=12,native_trace_processes=8,measurement_iterations=1080,CPU_interval_observations=2160','Python_tests=478,measurement_processes=48,correctness_processes=16,native_trace_processes=12,measurement_iterations=1440,CPU_interval_observations=2880')
s=s.replace('native_allocation_calls_1000_candidate=63','native_allocation_calls_1000_candidate=66,native_allocation_calls_1000_fixed=63')
s=s.replace('requested_bytes_1000_candidate=4128768','requested_bytes_1000_candidate=4124672,requested_bytes_1000_fixed=4128768')
s=s.replace('requested_bytes_1_candidate=65536','requested_bytes_1_candidate=4096,requested_bytes_1_fixed=65536')
s=s.replace('63-block native proof','1/66-block adaptive native proof')
write('tools/report_'+new+'.py',s)

s=derive('tools/audit_vulkan_buffer_pool.py')
s=s.replace('licenses/generated/windows-benchmarks-vulkan-allocation-trace-release-final/inventory.json','licenses/generated/windows-vulkan-buffer-pool-candidate-release/inventory.json')
s=s.replace('licenses/generated/windows-benchmarks-vulkan-allocation-trace-release-final/THIRD_PARTY_NOTICES.txt','licenses/generated/windows-vulkan-buffer-pool-candidate-release/THIRD_PARTY_NOTICES.txt')
write('tools/audit_'+new+'.py',s)

for n in ('build.py','report-controls.py','finalize.py','verify.py'):
    s=derive('benchmarks/reports/validation/2026-10-24-phase3-vulkan-buffer-pool/verification-scripts/'+n)
    write(v.relative_to(root)/n,s)

s=derive('tests/test_vulkan_buffer_pool.py')
s=s.replace('self.ids=dict(baseline=\'b\'*64,candidate=\'d\'*64)',"self.ids=dict(baseline='b'*64,fixed='e'*64,candidate='d'*64)")
s=s.replace('def candidate(self,n=1):',"def candidate(self,n=1,variant='candidate'):")
s=s.replace("SDK_source_fingerprint='d'*64","SDK_source_fingerprint=('e' if variant=='fixed' else 'd')*64")
s=s.replace('blocks=(n+15)//16;reserved=blocks*65536','blocks,reserved=v.reservation(n,variant)')
s=s.replace('test_1000_buffers_63_blocks','test_1000_buffers_66_blocks')
s=s.replace('allocated_bytes=4096','allocated_bytes=65536')
s=s.replace("    def test_candidate_fingerprint",'''    def test_fixed_single_control(self):self.candidate(1,'fixed');self.validate('fixed')
    def test_fixed_1000_control(self):self.candidate(1000,'fixed');self.validate('fixed')
    def test_fixed_candidate_identity(self):self.candidate(1,'fixed');self.reject(lambda r:r.update(SDK_source_fingerprint='d'*64),'fixed')
    def test_policy_boundaries(self):
        for n,blocks,slots in ((1,1,1),(2,2,3),(3,2,3),(4,3,7),(7,3,7),(8,4,15),(15,4,15),(16,5,31),(31,5,31),(32,6,47),(1000,66,1007)):
            self.assertEqual(v.reservation(n,'candidate'),(blocks,slots*4096))
    def test_invalid_policy_variant(self):
        with self.assertRaises(ValueError):v.reservation(1,'other')
    def test_invalid_zero_policy(self):
        with self.assertRaises(ValueError):v.reservation(0,'candidate')
    def test_invalid_boolean_policy(self):
        with self.assertRaises(ValueError):v.reservation(True,'candidate')
    def test_candidate_fingerprint''')
write('tests/test_'+new+'.py',s)
print('PASS generated independent adaptive study tools')
