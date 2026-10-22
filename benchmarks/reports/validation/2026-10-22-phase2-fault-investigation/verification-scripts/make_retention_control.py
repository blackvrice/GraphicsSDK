import pathlib
root=pathlib.Path.cwd();source=(root/'out/gbv-allocation-origin/main.cpp').read_text()
assert 'trim();sample("after_heap_trim")' in source
source=source.replace('trim();sample("after_heap_trim")','sample("without_heap_trim")')
source=source.replace('trim();sample("after_device_heap_trim")','sample("without_device_heap_trim")')
anchor='        ComPtr<ID3D12DebugDevice> report;'
assert anchor in source
source=source.replace(anchor,'        for(unsigned second=1;second<=20;++second){Sleep(1000);sample("alive_quiet");}\n'+anchor)
source=source.replace('mode=="full"','mode.starts_with("full")')
source=source.replace('mode=="cpu"||mode.starts_with("full")||mode=="no-tracking"','mode=="cpu"||mode=="full"||mode=="full-drain"||mode=="no-tracking"')
anchor='    static Counts counts;'
assert anchor in source
source=source.replace(anchor,'''    ComPtr<ID3D12CommandQueue> queue;ComPtr<ID3D12Fence> fence;ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;HANDLE done=nullptr;UINT64 timeline=0;
    if(mode=="full-drain"){
        D3D12_COMMAND_QUEUE_DESC desc{};hr(device->CreateCommandQueue(&desc,IID_PPV_ARGS(&queue)));hr(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));
        hr(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)));hr(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)));hr(list->Close());
        done=CreateEventW(nullptr,FALSE,FALSE,nullptr);check(done!=nullptr,"Queue event");
    }
'''+anchor)
anchor='            trim();'
assert anchor not in source
anchor='            sample("without_heap_trim");'
assert anchor in source
source=source.replace(anchor,'''            if(queue){hr(allocator->Reset());hr(list->Reset(allocator.Get(),nullptr));hr(list->Close());ID3D12CommandList* lists[]{list.Get()};queue->ExecuteCommandLists(1,lists);hr(queue->Signal(fence.Get(),++timeline));hr(fence->SetEventOnCompletion(timeline,done));check(WaitForSingleObject(done,30000)==WAIT_OBJECT_0,"Queue drain timeout");sample("after_queue_drain");}
'''+anchor)
source=source.replace('    device.Reset();sample("device_released");','    list.Reset();allocator.Reset();queue.Reset();fence.Reset();if(done)CloseHandle(done);device.Reset();sample("device_released");')
folder=root/'out/gbv-retention-control';folder.mkdir(exist_ok=False)
(folder/'main.cpp').write_text(source)
cmake=(root/'out/gbv-allocation-origin/CMakeLists.txt').read_text().replace('gbv_allocation_origin','gbv_retention_control').replace('GBVAllocationOrigin','GBVRetentionControl')
(folder/'CMakeLists.txt').write_text(cmake)
print('Generated no-trim quiet/drain causal intervention from frozen own native source')
