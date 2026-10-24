// Included after NativeDevice inside the isolated candidate Vulkan implementation.
struct BufferBlock {
    std::shared_ptr<NativeDevice> native;
    VkDeviceMemory memory{};
    std::uint32_t type{};
    vulkan::BufferSlots slots;
    std::mutex mutex;
    BufferBlock(std::shared_ptr<NativeDevice> n,std::uint32_t t,std::uint32_t stride):native(std::move(n)),type(t),slots(stride){}
    std::optional<std::uint32_t> take(){std::lock_guard lock(mutex);return slots.take();}
    void give(std::uint32_t slot){std::lock_guard lock(mutex);if(!slots.release(slot))std::abort();}
    ~BufferBlock(){if(memory)native->FreeMemory(native->handle,memory,nullptr);}
};
