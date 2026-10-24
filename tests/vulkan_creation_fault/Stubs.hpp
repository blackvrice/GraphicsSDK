// Own CPU-only PFN stubs. Signatures generated from reviewed vk.xml.
[[maybe_unused]] VkResult VKAPI_CALL fAllocateCommandBuffers(VkDevice device,const VkCommandBufferAllocateInfo* pAllocateInfo,VkCommandBuffer* pCommandBuffers){return create("AllocateCommandBuffers",pAllocateInfo,pCommandBuffers,true);}
[[maybe_unused]] VkResult VKAPI_CALL fAllocateDescriptorSets(VkDevice device,const VkDescriptorSetAllocateInfo* pAllocateInfo,VkDescriptorSet* pDescriptorSets){return create("AllocateDescriptorSets",pAllocateInfo,pDescriptorSets,true);}
[[maybe_unused]] VkResult VKAPI_CALL fAllocateMemory(VkDevice device,const VkMemoryAllocateInfo* pAllocateInfo,const VkAllocationCallbacks* pAllocator,VkDeviceMemory* pMemory){return create("AllocateMemory",pAllocateInfo,pMemory);}
[[maybe_unused]] VkResult VKAPI_CALL fBeginCommandBuffer(VkCommandBuffer commandBuffer,const VkCommandBufferBeginInfo* pBeginInfo){return VK_SUCCESS;}
[[maybe_unused]] VkResult VKAPI_CALL fBindBufferMemory(VkDevice device,VkBuffer buffer,VkDeviceMemory memory,VkDeviceSize memoryOffset){return VK_SUCCESS;}
[[maybe_unused]] VkResult VKAPI_CALL fBindImageMemory(VkDevice device,VkImage image,VkDeviceMemory memory,VkDeviceSize memoryOffset){return VK_SUCCESS;}
[[maybe_unused]] void VKAPI_CALL fCmdBeginRenderPass(VkCommandBuffer commandBuffer,const VkRenderPassBeginInfo* pRenderPassBegin,VkSubpassContents contents){}
[[maybe_unused]] void VKAPI_CALL fCmdBindDescriptorSets(VkCommandBuffer commandBuffer,VkPipelineBindPoint pipelineBindPoint,VkPipelineLayout layout,uint32_t firstSet,uint32_t descriptorSetCount,const VkDescriptorSet* pDescriptorSets,uint32_t dynamicOffsetCount,const uint32_t* pDynamicOffsets){}
[[maybe_unused]] void VKAPI_CALL fCmdBindIndexBuffer(VkCommandBuffer commandBuffer,VkBuffer buffer,VkDeviceSize offset,VkIndexType indexType){}
[[maybe_unused]] void VKAPI_CALL fCmdBindPipeline(VkCommandBuffer commandBuffer,VkPipelineBindPoint pipelineBindPoint,VkPipeline pipeline){}
[[maybe_unused]] void VKAPI_CALL fCmdBindVertexBuffers(VkCommandBuffer commandBuffer,uint32_t firstBinding,uint32_t bindingCount,const VkBuffer* pBuffers,const VkDeviceSize* pOffsets){}
[[maybe_unused]] void VKAPI_CALL fCmdCopyBuffer(VkCommandBuffer commandBuffer,VkBuffer srcBuffer,VkBuffer dstBuffer,uint32_t regionCount,const VkBufferCopy* pRegions){}
[[maybe_unused]] void VKAPI_CALL fCmdCopyBufferToImage(VkCommandBuffer commandBuffer,VkBuffer srcBuffer,VkImage dstImage,VkImageLayout dstImageLayout,uint32_t regionCount,const VkBufferImageCopy* pRegions){}
[[maybe_unused]] void VKAPI_CALL fCmdCopyImageToBuffer(VkCommandBuffer commandBuffer,VkImage srcImage,VkImageLayout srcImageLayout,VkBuffer dstBuffer,uint32_t regionCount,const VkBufferImageCopy* pRegions){}
[[maybe_unused]] void VKAPI_CALL fCmdDraw(VkCommandBuffer commandBuffer,uint32_t vertexCount,uint32_t instanceCount,uint32_t firstVertex,uint32_t firstInstance){}
[[maybe_unused]] void VKAPI_CALL fCmdDrawIndexed(VkCommandBuffer commandBuffer,uint32_t indexCount,uint32_t instanceCount,uint32_t firstIndex,int32_t vertexOffset,uint32_t firstInstance){}
[[maybe_unused]] void VKAPI_CALL fCmdEndRenderPass(VkCommandBuffer commandBuffer){}
[[maybe_unused]] void VKAPI_CALL fCmdPipelineBarrier(VkCommandBuffer commandBuffer,VkPipelineStageFlags srcStageMask,VkPipelineStageFlags dstStageMask,VkDependencyFlags dependencyFlags,uint32_t memoryBarrierCount,const VkMemoryBarrier* pMemoryBarriers,uint32_t bufferMemoryBarrierCount,const VkBufferMemoryBarrier* pBufferMemoryBarriers,uint32_t imageMemoryBarrierCount,const VkImageMemoryBarrier* pImageMemoryBarriers){}
[[maybe_unused]] void VKAPI_CALL fCmdResetQueryPool(VkCommandBuffer commandBuffer,VkQueryPool queryPool,uint32_t firstQuery,uint32_t queryCount){}
[[maybe_unused]] void VKAPI_CALL fCmdSetScissor(VkCommandBuffer commandBuffer,uint32_t firstScissor,uint32_t scissorCount,const VkRect2D* pScissors){}
[[maybe_unused]] void VKAPI_CALL fCmdSetViewport(VkCommandBuffer commandBuffer,uint32_t firstViewport,uint32_t viewportCount,const VkViewport* pViewports){}
[[maybe_unused]] void VKAPI_CALL fCmdWriteTimestamp(VkCommandBuffer commandBuffer,VkPipelineStageFlagBits pipelineStage,VkQueryPool queryPool,uint32_t query){}
[[maybe_unused]] VkResult VKAPI_CALL fCreateBuffer(VkDevice device,const VkBufferCreateInfo* pCreateInfo,const VkAllocationCallbacks* pAllocator,VkBuffer* pBuffer){return create("CreateBuffer",pCreateInfo,pBuffer);}
[[maybe_unused]] VkResult VKAPI_CALL fCreateCommandPool(VkDevice device,const VkCommandPoolCreateInfo* pCreateInfo,const VkAllocationCallbacks* pAllocator,VkCommandPool* pCommandPool){return create("CreateCommandPool",pCreateInfo,pCommandPool);}
[[maybe_unused]] VkResult VKAPI_CALL fCreateDescriptorPool(VkDevice device,const VkDescriptorPoolCreateInfo* pCreateInfo,const VkAllocationCallbacks* pAllocator,VkDescriptorPool* pDescriptorPool){return create("CreateDescriptorPool",pCreateInfo,pDescriptorPool);}
[[maybe_unused]] VkResult VKAPI_CALL fCreateDescriptorSetLayout(VkDevice device,const VkDescriptorSetLayoutCreateInfo* pCreateInfo,const VkAllocationCallbacks* pAllocator,VkDescriptorSetLayout* pSetLayout){return create("CreateDescriptorSetLayout",pCreateInfo,pSetLayout);}
[[maybe_unused]] VkResult VKAPI_CALL fCreateDevice(VkPhysicalDevice physicalDevice,const VkDeviceCreateInfo* pCreateInfo,const VkAllocationCallbacks* pAllocator,VkDevice* pDevice){return VK_ERROR_INITIALIZATION_FAILED;}
[[maybe_unused]] VkResult VKAPI_CALL fCreateFence(VkDevice device,const VkFenceCreateInfo* pCreateInfo,const VkAllocationCallbacks* pAllocator,VkFence* pFence){return create("CreateFence",pCreateInfo,pFence);}
[[maybe_unused]] VkResult VKAPI_CALL fCreateFramebuffer(VkDevice device,const VkFramebufferCreateInfo* pCreateInfo,const VkAllocationCallbacks* pAllocator,VkFramebuffer* pFramebuffer){return create("CreateFramebuffer",pCreateInfo,pFramebuffer);}
[[maybe_unused]] VkResult VKAPI_CALL fCreateGraphicsPipelines(VkDevice device,VkPipelineCache pipelineCache,uint32_t createInfoCount,const VkGraphicsPipelineCreateInfo* pCreateInfos,const VkAllocationCallbacks* pAllocator,VkPipeline* pPipelines){return create("CreateGraphicsPipelines",pCreateInfos,pPipelines,true);}
[[maybe_unused]] VkResult VKAPI_CALL fCreateImage(VkDevice device,const VkImageCreateInfo* pCreateInfo,const VkAllocationCallbacks* pAllocator,VkImage* pImage){return create("CreateImage",pCreateInfo,pImage);}
[[maybe_unused]] VkResult VKAPI_CALL fCreateImageView(VkDevice device,const VkImageViewCreateInfo* pCreateInfo,const VkAllocationCallbacks* pAllocator,VkImageView* pView){return create("CreateImageView",pCreateInfo,pView);}
[[maybe_unused]] VkResult VKAPI_CALL fCreatePipelineLayout(VkDevice device,const VkPipelineLayoutCreateInfo* pCreateInfo,const VkAllocationCallbacks* pAllocator,VkPipelineLayout* pPipelineLayout){return create("CreatePipelineLayout",pCreateInfo,pPipelineLayout);}
[[maybe_unused]] VkResult VKAPI_CALL fCreateQueryPool(VkDevice device,const VkQueryPoolCreateInfo* pCreateInfo,const VkAllocationCallbacks* pAllocator,VkQueryPool* pQueryPool){return create("CreateQueryPool",pCreateInfo,pQueryPool);}
[[maybe_unused]] VkResult VKAPI_CALL fCreateRenderPass(VkDevice device,const VkRenderPassCreateInfo* pCreateInfo,const VkAllocationCallbacks* pAllocator,VkRenderPass* pRenderPass){return create("CreateRenderPass",pCreateInfo,pRenderPass);}
[[maybe_unused]] VkResult VKAPI_CALL fCreateSampler(VkDevice device,const VkSamplerCreateInfo* pCreateInfo,const VkAllocationCallbacks* pAllocator,VkSampler* pSampler){return create("CreateSampler",pCreateInfo,pSampler);}
[[maybe_unused]] VkResult VKAPI_CALL fCreateSemaphore(VkDevice device,const VkSemaphoreCreateInfo* pCreateInfo,const VkAllocationCallbacks* pAllocator,VkSemaphore* pSemaphore){return create("CreateSemaphore",pCreateInfo,pSemaphore);}
[[maybe_unused]] VkResult VKAPI_CALL fCreateShaderModule(VkDevice device,const VkShaderModuleCreateInfo* pCreateInfo,const VkAllocationCallbacks* pAllocator,VkShaderModule* pShaderModule){return create("CreateShaderModule",pCreateInfo,pShaderModule);}
[[maybe_unused]] VkResult VKAPI_CALL fCreateSwapchainKHR(VkDevice device,const VkSwapchainCreateInfoKHR* pCreateInfo,const VkAllocationCallbacks* pAllocator,VkSwapchainKHR* pSwapchain){return create("CreateSwapchainKHR",pCreateInfo,pSwapchain);}
[[maybe_unused]] void VKAPI_CALL fDestroyBuffer(VkDevice device,VkBuffer buffer,const VkAllocationCallbacks* pAllocator){destroy(buffer);}
[[maybe_unused]] void VKAPI_CALL fDestroyCommandPool(VkDevice device,VkCommandPool commandPool,const VkAllocationCallbacks* pAllocator){destroy(commandPool);}
[[maybe_unused]] void VKAPI_CALL fDestroyDescriptorPool(VkDevice device,VkDescriptorPool descriptorPool,const VkAllocationCallbacks* pAllocator){destroy(descriptorPool);}
[[maybe_unused]] void VKAPI_CALL fDestroyDescriptorSetLayout(VkDevice device,VkDescriptorSetLayout descriptorSetLayout,const VkAllocationCallbacks* pAllocator){destroy(descriptorSetLayout);}
[[maybe_unused]] void VKAPI_CALL fDestroyDevice(VkDevice device,const VkAllocationCallbacks* pAllocator){}
[[maybe_unused]] void VKAPI_CALL fDestroyFence(VkDevice device,VkFence fence,const VkAllocationCallbacks* pAllocator){destroy(fence);}
[[maybe_unused]] void VKAPI_CALL fDestroyFramebuffer(VkDevice device,VkFramebuffer framebuffer,const VkAllocationCallbacks* pAllocator){destroy(framebuffer);}
[[maybe_unused]] void VKAPI_CALL fDestroyImage(VkDevice device,VkImage image,const VkAllocationCallbacks* pAllocator){destroy(image);}
[[maybe_unused]] void VKAPI_CALL fDestroyImageView(VkDevice device,VkImageView imageView,const VkAllocationCallbacks* pAllocator){destroy(imageView);}
[[maybe_unused]] void VKAPI_CALL fDestroyInstance(VkInstance instance,const VkAllocationCallbacks* pAllocator){}
[[maybe_unused]] void VKAPI_CALL fDestroyPipeline(VkDevice device,VkPipeline pipeline,const VkAllocationCallbacks* pAllocator){destroy(pipeline);}
[[maybe_unused]] void VKAPI_CALL fDestroyPipelineLayout(VkDevice device,VkPipelineLayout pipelineLayout,const VkAllocationCallbacks* pAllocator){destroy(pipelineLayout);}
[[maybe_unused]] void VKAPI_CALL fDestroyQueryPool(VkDevice device,VkQueryPool queryPool,const VkAllocationCallbacks* pAllocator){destroy(queryPool);}
[[maybe_unused]] void VKAPI_CALL fDestroyRenderPass(VkDevice device,VkRenderPass renderPass,const VkAllocationCallbacks* pAllocator){destroy(renderPass);}
[[maybe_unused]] void VKAPI_CALL fDestroySampler(VkDevice device,VkSampler sampler,const VkAllocationCallbacks* pAllocator){destroy(sampler);}
[[maybe_unused]] void VKAPI_CALL fDestroySemaphore(VkDevice device,VkSemaphore semaphore,const VkAllocationCallbacks* pAllocator){destroy(semaphore);}
[[maybe_unused]] void VKAPI_CALL fDestroyShaderModule(VkDevice device,VkShaderModule shaderModule,const VkAllocationCallbacks* pAllocator){destroy(shaderModule);}
[[maybe_unused]] void VKAPI_CALL fDestroySwapchainKHR(VkDevice device,VkSwapchainKHR swapchain,const VkAllocationCallbacks* pAllocator){destroy(swapchain);}
[[maybe_unused]] VkResult VKAPI_CALL fDeviceWaitIdle(VkDevice device){return VK_SUCCESS;}
[[maybe_unused]] VkResult VKAPI_CALL fEndCommandBuffer(VkCommandBuffer commandBuffer){return VK_SUCCESS;}
[[maybe_unused]] VkResult VKAPI_CALL fEnumerateDeviceExtensionProperties(VkPhysicalDevice physicalDevice,const char* pLayerName,uint32_t* pPropertyCount,VkExtensionProperties* pProperties){return VK_SUCCESS;}
[[maybe_unused]] VkResult VKAPI_CALL fEnumeratePhysicalDevices(VkInstance instance,uint32_t* pPhysicalDeviceCount,VkPhysicalDevice* pPhysicalDevices){return VK_SUCCESS;}
[[maybe_unused]] VkResult VKAPI_CALL fFlushMappedMemoryRanges(VkDevice device,uint32_t memoryRangeCount,const VkMappedMemoryRange* pMemoryRanges){return VK_SUCCESS;}
[[maybe_unused]] void VKAPI_CALL fFreeMemory(VkDevice device,VkDeviceMemory memory,const VkAllocationCallbacks* pAllocator){destroy(memory);}
[[maybe_unused]] void VKAPI_CALL fGetBufferMemoryRequirements(VkDevice device,VkBuffer buffer,VkMemoryRequirements* pMemoryRequirements){*pMemoryRequirements={4096,64,1};}
[[maybe_unused]] PFN_vkVoidFunction VKAPI_CALL fGetDeviceProcAddr(VkDevice device,const char* pName){return {};}
[[maybe_unused]] void VKAPI_CALL fGetDeviceQueue(VkDevice device,uint32_t queueFamilyIndex,uint32_t queueIndex,VkQueue* pQueue){}
[[maybe_unused]] VkResult VKAPI_CALL fGetFenceStatus(VkDevice device,VkFence fence){return VK_SUCCESS;}
[[maybe_unused]] void VKAPI_CALL fGetImageMemoryRequirements(VkDevice device,VkImage image,VkMemoryRequirements* pMemoryRequirements){*pMemoryRequirements={4096,64,1};}
[[maybe_unused]] void VKAPI_CALL fGetPhysicalDeviceFeatures(VkPhysicalDevice physicalDevice,VkPhysicalDeviceFeatures* pFeatures){}
[[maybe_unused]] void VKAPI_CALL fGetPhysicalDeviceFeatures2(VkPhysicalDevice physicalDevice,VkPhysicalDeviceFeatures2* pFeatures){}
[[maybe_unused]] void VKAPI_CALL fGetPhysicalDeviceFormatProperties(VkPhysicalDevice physicalDevice,VkFormat format,VkFormatProperties* pFormatProperties){*pFormatProperties={UINT32_MAX,UINT32_MAX,UINT32_MAX};}
[[maybe_unused]] VkResult VKAPI_CALL fGetPhysicalDeviceImageFormatProperties(VkPhysicalDevice physicalDevice,VkFormat format,VkImageType type,VkImageTiling tiling,VkImageUsageFlags usage,VkImageCreateFlags flags,VkImageFormatProperties* pImageFormatProperties){*pImageFormatProperties={{65536,65536,1},1,1,VK_SAMPLE_COUNT_1_BIT,UINT64_MAX};return VK_SUCCESS;}
[[maybe_unused]] void VKAPI_CALL fGetPhysicalDeviceMemoryProperties(VkPhysicalDevice physicalDevice,VkPhysicalDeviceMemoryProperties* pMemoryProperties){}
[[maybe_unused]] void VKAPI_CALL fGetPhysicalDeviceProperties2(VkPhysicalDevice physicalDevice,VkPhysicalDeviceProperties2* pProperties){}
[[maybe_unused]] void VKAPI_CALL fGetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice physicalDevice,uint32_t* pQueueFamilyPropertyCount,VkQueueFamilyProperties* pQueueFamilyProperties){}
[[maybe_unused]] VkResult VKAPI_CALL fGetQueryPoolResults(VkDevice device,VkQueryPool queryPool,uint32_t firstQuery,uint32_t queryCount,size_t dataSize,void* pData,VkDeviceSize stride,VkQueryResultFlags flags){return VK_SUCCESS;}
[[maybe_unused]] VkResult VKAPI_CALL fGetSwapchainImagesKHR(VkDevice device,VkSwapchainKHR swapchain,uint32_t* pSwapchainImageCount,VkImage* pSwapchainImages){if(!pSwapchainImages)*pSwapchainImageCount=2;else for(unsigned i=0;i<*pSwapchainImageCount;++i)pSwapchainImages[i]=reinterpret_cast<VkImage>(std::uintptr_t(0x80000+i));return VK_SUCCESS;}
[[maybe_unused]] VkResult VKAPI_CALL fInvalidateMappedMemoryRanges(VkDevice device,uint32_t memoryRangeCount,const VkMappedMemoryRange* pMemoryRanges){return VK_SUCCESS;}
[[maybe_unused]] VkResult VKAPI_CALL fMapMemory(VkDevice device,VkDeviceMemory memory,VkDeviceSize offset,VkDeviceSize size,VkMemoryMapFlags flags,void** ppData){return VK_SUCCESS;}
[[maybe_unused]] VkResult VKAPI_CALL fQueueSubmit(VkQueue queue,uint32_t submitCount,const VkSubmitInfo* pSubmits,VkFence fence){return VK_SUCCESS;}
[[maybe_unused]] VkResult VKAPI_CALL fQueueWaitIdle(VkQueue queue){return VK_SUCCESS;}
[[maybe_unused]] void VKAPI_CALL fUnmapMemory(VkDevice device,VkDeviceMemory memory){}
[[maybe_unused]] void VKAPI_CALL fUpdateDescriptorSets(VkDevice device,uint32_t descriptorWriteCount,const VkWriteDescriptorSet* pDescriptorWrites,uint32_t descriptorCopyCount,const VkCopyDescriptorSet* pDescriptorCopies){}
[[maybe_unused]] VkResult VKAPI_CALL fWaitForFences(VkDevice device,uint32_t fenceCount,const VkFence* pFences,VkBool32 waitAll,uint64_t timeout){return VK_SUCCESS;}
