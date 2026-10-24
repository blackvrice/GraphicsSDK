import difflib
import pathlib
import sys
root=pathlib.Path(__file__).resolve().parents[5];sys.path.insert(0,str(root/'tools'))
import prepare_vulkan_adaptive_buffer_pool as candidate
paths=list((root/'experiments/vulkan_adaptive_buffer_pool').glob('*.hpp'))+list((root/'experiments/vulkan_adaptive_buffer_pool').glob('*.cpp'))+[root/'tools/prepare_vulkan_adaptive_buffer_pool.py']
for p in paths:p.write_bytes(p.read_text(encoding='utf-8').replace('\n','\r\n').encode())
destination=root/'out/vulkan-adaptive-buffer-pool-source';candidate.prepare(destination)
names=['CMakeLists.txt','src/backends/vulkan/Vulkan.cpp','src/backends/vulkan/BufferPool.hpp','src/backends/vulkan/BufferPoolRange.hpp','tests/PoolGpuTests.cpp','tests/PoolRangeTests.cpp']
patch=''
for n in names:
    before=(root/n).read_text(encoding='utf-8').splitlines(True) if (root/n).exists() else []
    after=(destination/n).read_text(encoding='utf-8').splitlines(True)
    patch+=''.join(difflib.unified_diff(before,after,fromfile='a/'+n,tofile='b/'+n))
(root/'experiments/vulkan_adaptive_buffer_pool/candidate.patch').write_bytes(patch.encode())
