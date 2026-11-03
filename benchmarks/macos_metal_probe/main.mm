#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <mach-o/dyld.h>
#include <algorithm>
#include <iostream>
#include <set>
#include <string>

static NSString* identity(id<MTLDevice> device){
    return device?[NSString stringWithFormat:@"%016llx",static_cast<unsigned long long>(device.registryID)]:nil;
}
int main(int argc,char** argv){@autoreleasepool{
    const bool select=argc==3&&std::string(argv[1])=="--require-device";
    if(argc!=1&&!select){std::cerr<<"FAIL: Invalid probe arguments\n";return 1;}
    std::string requested=select?argv[2]:"";
    if(select&&(requested.size()!=16||!std::all_of(requested.begin(),requested.end(),[](char c){return(c>='0'&&c<='9')||(c>='a'&&c<='f');}))){std::cerr<<"FAIL: Invalid registry ID\n";return 1;}
    NSArray<id<MTLDevice>>* devices=MTLCopyAllDevices();
    id<MTLDevice> primary=MTLCreateSystemDefaultDevice();
    NSMutableArray* rows=[NSMutableArray array];std::set<std::string> ids;
    for(id<MTLDevice> device in devices){
        NSString* registry=identity(device);
        if(!device.name.length||!device.registryID||!ids.insert(registry.UTF8String).second){std::cerr<<"FAIL: Invalid or duplicate Metal identity\n";return 1;}
        MTLSize threads=device.maxThreadsPerThreadgroup;
        [rows addObject:@{@"registry_id":registry,@"name":device.name,@"low_power":@(device.lowPower),
            @"removable":@(device.removable),@"headless":@(device.headless),@"unified_memory":@(device.hasUnifiedMemory),
            @"max_buffer_length":@(device.maxBufferLength),@"max_threads_per_threadgroup":@[@(threads.width),@(threads.height),@(threads.depth)]}];
    }
    [rows sortUsingDescriptors:@[[NSSortDescriptor sortDescriptorWithKey:@"registry_id" ascending:YES]]];
    if(primary&&!ids.contains(identity(primary).UTF8String)){std::cerr<<"FAIL: Default Metal device absent from inventory\n";return 1;}
    if(select&&!ids.contains(requested)){std::cerr<<"FAIL: Requested Metal registry ID absent or ambiguous\n";return 1;}
    NSMutableArray* modules=[NSMutableArray array];
    for(uint32_t i=0;i<_dyld_image_count();++i){
        NSString* path=[NSString stringWithUTF8String:_dyld_get_image_name(i)];
        if([path containsString:@"/System/Library/Frameworks/Metal.framework/"]||[path containsString:@"/System/Library/Frameworks/Foundation.framework/"])
            if(![modules containsObject:path])[modules addObject:path];
    }
    [modules sortUsingSelector:@selector(compare:)];
    NSDictionary* value=@{@"schema_version":@1,@"contract":@"native_macos_metal_inventory_v1",@"devices":rows,
        @"default_registry_id":identity(primary)?:[NSNull null],@"selected_registry_id":select?[NSString stringWithUTF8String:requested.c_str()]:[NSNull null],
        @"framework_modules":modules,@"GPU_work_submitted":@NO,@"SDK_backend_used":@NO};
    NSError* error=nil;NSData* json=[NSJSONSerialization dataWithJSONObject:value options:NSJSONWritingSortedKeys error:&error];
    if(!json){std::cerr<<"FAIL: Inventory serialization failed\n";return 1;}
    std::cout.write(static_cast<const char*>(json.bytes),json.length);std::cout<<'\n';return 0;
}}
