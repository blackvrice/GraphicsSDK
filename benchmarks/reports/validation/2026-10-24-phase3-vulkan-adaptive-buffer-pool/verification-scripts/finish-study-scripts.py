"""Specialize copied evidence checks for the adaptive/fixed/baseline/SDL matrix."""
import pathlib
root=pathlib.Path(__file__).resolve().parents[5];v=pathlib.Path(__file__).resolve().parent
def update(n,changes):
    p=v/n;s=p.read_text(encoding='utf-8')
    for a,b in changes:
        assert a in s,(n,a)
        s=s.replace(a,b)
    p.write_bytes(s.replace('\n','\r\n').encode())
update('finalize.py',[
    ('==730','==805'),('compiler_inputs=730','compiler_inputs=805'),('raw_commands=70','raw_commands=92'),
    ('2026-10-24-phase3-vulkan-allocation-trace/evidence.json','2026-10-24-phase3-vulkan-buffer-pool/evidence.json'),
    ("'experiments/vulkan_adaptive_buffer_pool'","'experiments/vulkan_buffer_pool','experiments/vulkan_adaptive_buffer_pool'"),
])
update('verify.py',[
    ("len(x['runs'])==48 and len(x['traces'])==8 and x['verified_buffers']==654654 and x['verified_bytes']==2681462784","len(x['runs'])==64 and len(x['traces'])==12 and x['verified_buffers']==872872 and x['verified_bytes']==3575283712"),
    ('==16016','==24024'),('==65601536','==98402304'),('==70','==92'),('Ran 444 tests','Ran 478 tests'),
    ('pool_range_assertions=34810','pool_range_assertions=41246'),('pool_gpu_assertions=57','pool_gpu_assertions=103'),('pool_gpu_assertions=59','pool_gpu_assertions=105'),
    ("==1080 and summary['CPU_interval_observations']==2160","==1440 and summary['CPU_interval_observations']==2880"),
    ("summary['requested_bytes_1_candidate']==16*summary['requested_bytes_1_baseline'] and summary['requested_bytes_1000_candidate']-summary['requested_bytes_1000_baseline']==32768","summary['requested_bytes_1_candidate']==summary['requested_bytes_1_baseline']==4096 and summary['requested_bytes_1000_candidate']-summary['requested_bytes_1000_baseline']==28672 and summary['requested_bytes_1_fixed']==65536"),
    ('444 Python tests / 48 performance-correctness + 8 trace processes','478 Python tests / 64 performance-correctness + 12 trace processes'),
])
print('PASS specialized new evidence scripts')
