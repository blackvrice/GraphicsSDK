"""Generate exact PFN signatures from reviewed Vulkan-Headers; only own stubs are written."""
import pathlib
import re
import xml.etree.ElementTree as ET
root=pathlib.Path(__file__).resolve().parents[5]
xml=ET.parse(root/'.deps/Vulkan-Headers/registry/vk.xml');commands={c.findtext('proto/name'):c for c in xml.findall('./commands/command') if c.find('proto') is not None}
source=(root/'src/backends/vulkan/Vulkan.cpp').read_text(encoding='utf-8');names=set(re.findall(r'X\((\w+)\)',source))
names|={'CreateSwapchainKHR','DestroySwapchainKHR','GetSwapchainImagesKHR'}
out='// Own CPU-only PFN stubs. Signatures generated from reviewed vk.xml.\n'
for name in sorted(names):
    c=commands['vk'+name];ret=c.findtext('proto/type');parameters=[p for p in c.findall('param') if 'vulkan' in p.attrib.get('api','vulkan').split(',')];params=[re.sub(r'\s+',' ',''.join(p.itertext())).strip() for p in parameters];pnames=[p.findtext('name') for p in parameters]
    body='return VK_SUCCESS;' if ret=='VkResult' else 'return {};' if ret!='void' else ''
    if name.startswith('Create') or name=='AllocateMemory':
        if name=='CreateDevice':body='return VK_ERROR_INITIALIZATION_FAILED;'
        elif name=='CreateGraphicsPipelines':body=f'return create("{name}",pCreateInfos,pPipelines,true);'
        else:body=f'return create("{name}",{pnames[1]},{pnames[-1]});'
    if name.startswith('Destroy') or name=='FreeMemory':body='' if name in ('DestroyDevice','DestroyInstance') else f'destroy({pnames[1]});'
    if name in ('AllocateCommandBuffers','AllocateDescriptorSets'):body=f'return create("{name}",{pnames[1]},{pnames[-1]},true);'
    if name in ('GetBufferMemoryRequirements','GetImageMemoryRequirements'):body=f'*{pnames[-1]}={{4096,64,1}};'
    if name=='GetPhysicalDeviceImageFormatProperties':body='*pImageFormatProperties={{65536,65536,1},1,1,VK_SAMPLE_COUNT_1_BIT,UINT64_MAX};return VK_SUCCESS;'
    if name=='GetPhysicalDeviceFormatProperties':body='*pFormatProperties={UINT32_MAX,UINT32_MAX,UINT32_MAX};'
    if name=='GetSwapchainImagesKHR':body='if(!pSwapchainImages)*pSwapchainImageCount=2;else for(unsigned i=0;i<*pSwapchainImageCount;++i)pSwapchainImages[i]=reinterpret_cast<VkImage>(std::uintptr_t(0x80000+i));return VK_SUCCESS;'
    out+=f'[[maybe_unused]] {ret} VKAPI_CALL f{name}('+','.join(params)+'){'+body+'}\n'
(root/'tests/vulkan_creation_fault/Stubs.hpp').write_bytes(out.replace('\n','\r\n').encode());print('PASS',len(names),'exact-signature own CPU PFN stubs')
