import pathlib
import subprocess
import sys
root=pathlib.Path(__file__).resolve().parents[5]
subprocess.run([sys.executable,str(pathlib.Path(__file__).with_name('make-layer.py'))],cwd=root,check=True)
for p in list((root/'experiments/vulkan_buffer_pool_fault').glob('*.cpp'))+list((root/'experiments/vulkan_buffer_pool_fault').glob('*.hpp'))+[root/'experiments/vulkan_buffer_pool_fault/CMakeLists.txt',root/'tools/prepare_vulkan_buffer_pool_fault.py']:
    p.write_bytes(p.read_text(encoding='utf-8').replace('\n','\r\n').encode())
subprocess.run([sys.executable,'tools/prepare_vulkan_buffer_pool_fault.py','--output','out/vulkan-buffer-pool-fault-source'],cwd=root,check=True)
