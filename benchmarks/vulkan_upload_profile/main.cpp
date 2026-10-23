#include "../common/Adapter.hpp"
#include "../vulkan_upload/Identity.hpp"
#include <graphics/Graphics.hpp>
#include <algorithm>
#include <charconv>
#include <cstring>
#include <iomanip>
#include <iostream>

#ifndef NDEBUG
#error Release required
#endif
namespace {
template<class T>T take(gfx::Result<T> r){if(!r)throw std::runtime_error(r.error().message);return std::move(*r);}
void take(gfx::Result<void> r){if(!r)throw std::runtime_error(r.error().message);}
unsigned number(std::string_view s,unsigned lo,unsigned hi){unsigned n{};auto [p,e]=std::from_chars(s.data(),s.data()+s.size(),n);if(e!=std::errc{}||p!=s.data()+s.size()||n<lo||n>hi)throw std::runtime_error("Invalid profile bound");return n;}
struct Shape{unsigned w,h,bpp;};
Shape shape(std::string_view s){if(s=="upload1080")return{1920,1080,4};if(s=="upload4k")return{3840,2160,4};if(s=="upload4k16f")return{3840,2160,8};throw std::runtime_error("Unknown profile scenario");}
void payload(std::vector<std::byte>& data,Shape s,unsigned seed){
    for(std::size_t i=0;i<std::size_t(s.w)*s.h;++i){auto v=std::uint32_t(i)*1315423911u+seed*2654435761u;v^=v>>16;
        if(s.bpp==4)std::memcpy(data.data()+i*4,&v,4);else{constexpr std::uint16_t pool[]{0,0x3400,0x3800,0x3c00,0xb400,0x4000,0x4200,0x0400};for(unsigned k=0;k<4;++k){auto h=pool[(v>>(k*3))&7];std::memcpy(data.data()+i*8+k*2,&h,2);}}
    }
}
}
int main(int argc,char**argv)try{
    if(argc!=6)throw std::runtime_error("Usage: vulkan_upload_profile <GPU name> <upload1080|upload4k|upload4k16f> <warmup 1..64> <samples 3..256> <validation 0|1>");
    const auto s=shape(argv[2]);auto warmup=number(argv[3],1,64),samples=number(argv[4],3,256);const bool validation=number(argv[5],0,1)!=0;
    auto adapters=take(gfx::enumerateAdapters(gfx::Backend::Vulkan));
    if(std::count_if(adapters.begin(),adapters.end(),[&](auto&a){return a.name==argv[1];})!=1)throw std::runtime_error("Unique exact adapter name required");
    auto a=std::find_if(adapters.begin(),adapters.end(),[&](auto&a){return a.name==argv[1];});gfx::DeviceDesc desc;desc.backend=gfx::Backend::Vulkan;desc.adapter=a->id;desc.validation=validation;desc.softwareAdapter=a->softwareAdapter;
    auto d=take(gfx::createDevice(desc));auto id=upload::identify(a->name);auto env=bench::environment(a->name);
    if(!validation&&GetModuleHandleW(L"VkLayer_khronos_validation.dll"))throw std::runtime_error("Unvalidated CPU intervals require no validation layer");
    auto texture=take(d->createTexture({s.w,s.h,s.bpp==8?gfx::Format::RGBA16Float:gfx::Format::RGBA8UNorm}));const auto fp=texture->footprint();
    if(fp.rowPitch!=fp.rowBytes)throw std::runtime_error("Timed profile requires aligned tight rows");
    auto staging=take(d->createBuffer({fp.size,gfx::Memory::Upload}));auto cmd=take(d->createCommandList());std::vector<std::byte> pixels(std::size_t(s.w)*s.h*s.bpp);std::vector<std::array<double,6>> times;
    std::uint64_t verified{};
    for(unsigned i=0;i<warmup+samples;++i){
        payload(pixels,s,i+1);auto t0=bench::Clock::now();take(staging->write(0,pixels));auto t1=bench::Clock::now();take(cmd->begin());auto t2=bench::Clock::now();
        take(cmd->transition(texture,gfx::ResourceState::CopyDestination));take(cmd->copyBufferToTexture(staging,texture));take(cmd->end());auto t3=bench::Clock::now();auto fence=take(d->queue().submit(*cmd));auto t4=bench::Clock::now();take(d->queue().wait(fence));auto t5=bench::Clock::now();
        auto r=take(d->createBuffer({fp.size,gfx::Memory::Readback}));auto read=take(d->createCommandList());take(read->begin());take(read->transition(texture,gfx::ResourceState::CopySource));take(read->copyTextureToBuffer(texture,r));take(read->end());take(d->queue().wait(take(d->queue().submit(*read))));
        if(take(r->read(0,fp.size))!=pixels)throw std::runtime_error("Profile full-image reference mismatch");verified+=pixels.size();
        if(i>=warmup)times.push_back({bench::ms(t0,t1),bench::ms(t1,t2),bench::ms(t2,t3),bench::ms(t3,t4),bench::ms(t4,t5),bench::ms(t0,t5)});
    }
    take(d->queue().waitIdle());cmd.reset();texture.reset();staging.reset();if(validation&&take(d->validationErrorCount()))throw std::runtime_error("Profile validation errors");d.reset();
    using bench::quote;std::cout<<std::setprecision(17)<<"{\"category\":\"diagnostic_CPU_API_intervals\",\"contract\":\"vulkan_upload_cpu_stage_partition_v1\",\"api\":\"vulkan\",\"SDK_source_fingerprint\":"<<quote(gfx::buildFingerprint())<<",\"profile_source_fingerprint\":"<<quote(GFX_PROFILE_SHA)<<",\"device_name\":"<<quote(a->name)<<",\"device_uuid\":"<<quote(id.uuid)<<",\"driver_uuid\":"<<quote(id.driverUuid)<<",\"device_luid\":"<<quote(id.luid)<<",\"environment\":"<<env<<",\"scenario\":"<<quote(argv[2])<<",\"validation\":"<<(validation?"true":"false")<<",\"warmup\":"<<warmup<<",\"sample_count\":"<<samples<<",\"verified_images\":"<<warmup+samples<<",\"verified_bytes\":"<<verified<<",\"gpu_ms\":null,\"columns\":[\"write_ms\",\"begin_ms\",\"record_ms\",\"submit_ms\",\"wait_ms\",\"total_ms\"],\"samples_ms\":[";
    for(std::size_t i=0;i<times.size();++i){if(i)std::cout<<',';std::cout<<'[';for(unsigned j=0;j<6;++j){if(j)std::cout<<',';std::cout<<times[i][j];}std::cout<<']';}std::cout<<"]}\n";return 0;
}catch(const std::exception&e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
