#include "Upload.hpp"
#include "../common/Adapter.hpp"
#include <charconv>
#include <iomanip>

#ifndef NDEBUG
#error Release required
#endif
namespace llglcompare {
using namespace llglupload;
unsigned number(std::string_view s,unsigned lo,unsigned hi){unsigned value{};auto[p,e]=std::from_chars(s.data(),s.data()+s.size(),value);require(e==std::errc{}&&p==s.data()+s.size()&&value>=lo&&value<=hi,"Invalid bounded sample count");return value;}
std::string nativeIdentity(Context& c,bool observed){auto& p=c.properties.properties;std::ostringstream o;
    o<<"\"device_name\":"<<quote(p.deviceName)<<",\"device_uuid\":"<<quote(hex(c.id.deviceUUID))<<",\"driver_uuid\":"<<quote(hex(c.id.driverUUID))<<",\"device_luid\":"<<quote(hex(c.id.deviceLUID))
     <<",\"native_vendor_id\":"<<p.vendorID<<",\"native_device_id\":"<<p.deviceID<<",\"native_driver_version\":"<<p.driverVersion<<",\"native_api_version\":"<<p.apiVersion<<",\"native_handle_verified\":true"
     <<",\"queue_family\":"<<c.native.queueFamily<<",\"queue_index\":0,\"queue_flags\":"<<c.flags<<",\"loader_api_version\":"<<c.loaderVersion<<",\"instance_API_request_observed\":"<<(observed?"true":"false")
     <<",\"loader_path\":"<<quote(c.loader)<<",\"LLGL_path\":"<<quote(c.core)<<",\"backend_path\":"<<quote(c.backend)<<",\"loaded_validation_layer\":"<<quote(c.loadedLayer);return o.str();}
}
int main(int argc,char** argv)try{
    using namespace llglcompare;require(argc==6,"Usage: llgl_vulkan_upload_compare EXPECTED_GPU plain|validation|observe SCENARIO|probe|CONTROL WARMUP SAMPLES");std::string gpu=argv[1],mode=argv[2],name=argv[3];require(!gpu.empty()&&(mode=="plain"||mode=="validation"||mode=="observe"),"Invalid arguments");bool validation=mode=="validation",observe=mode=="observe",probe=name=="probe",control=name=="core-control"||name=="sync-control";require(!observe||probe,"Observation restricted to untimed probe");
    unsigned warmup=number(argv[4],probe||control?0:1,probe||control?0:64),samples=number(argv[5],probe||control?0:3,probe||control?0:256);auto s=probe||control?Shape{}:shape(name=="corrupt-readback"?"odd":name);
    Context c(validation,gpu);std::unique_ptr<llglpso::Observer> observer;if(observe)observer=std::make_unique<llglpso::Observer>();else require(GetModuleHandleW(L"VkLayer_gfx_llgl_draw.dll")==nullptr,"Observer active in upload process");auto id=nativeIdentity(c,observe);auto env=bench::environment(gpu);std::string diagnostic;std::vector<double> measured;std::vector<std::string> hashes;std::uint64_t verifiedBytes{};
    if(control)diagnostic=llglpso::validationControl(c,name);
    else if(!probe){Upload upload(c,s);std::vector<std::byte> expected(std::size_t(s.w)*s.h*s.bpp);
        for(unsigned i=0;i<warmup+samples;++i){payload(expected,s,i+1);auto start=bench::Clock::now();upload.write(expected);auto end=bench::Clock::now();auto actual=upload.read(expected.size());if(name=="corrupt-readback")actual.back()^=std::byte{1};require(actual==expected,"Full changing upload readback mismatch");verifiedBytes+=actual.size();if(validation)hashes.push_back(llglpso::sha256(actual));if(i>=warmup)measured.push_back(bench::ms(start,end));}upload.close();}
    auto runtime=modules();require(observe||GetModuleHandleW(L"VkLayer_gfx_llgl_draw.dll")==nullptr,"Observer active in upload process");auto observed=observer?observer->snapshot():drawobserver::Snapshot{};c.close();auto retired=observer?observer->snapshot():drawobserver::Snapshot{};if(observe)require(retired.liveInstances==0&&retired.liveDevices==0,"Observed native objects not retired");if(!control)require(errors==0,"Native validation error delivered");
    std::cout<<std::setprecision(17)<<"{\"schema_version\":1,\"task\":\"P3-CMP-07\",\"sdk\":\"llgl\",\"api\":\"vulkan\",\"configuration\":\"Release\",\"profile_id\":\"llgl-fixed-pin-transfer-src-v1\",\"source_fingerprint\":"<<quote(GFX_LLGL_UPLOAD_COMPARE_FINGERPRINT)<<','<<id
      <<",\"environment\":"<<env<<",\"identity_attribution\":\"actual_LLGL_native_handles_and_unique_DXGI\",\"contract\":\"vulkan_upload_cpu_payload_to_texture_fence_complete_each_sample_readback_v1\",\"category\":\"end_to_end\",\"scenario\":"<<quote(name)<<",\"validation\":"<<(validation?"true":"false")<<",\"probe\":"<<(probe?"true":"false")<<",\"control\":"<<(control?"true":"false")<<",\"control_diagnostic\":"<<quote(diagnostic)<<",\"delivered_errors\":"<<errors<<",\"submitted_control_work\":0,\"warmup\":"<<warmup<<",\"sample_count\":"<<samples
      <<",\"width\":"<<s.w<<",\"height\":"<<s.h<<",\"texel_bytes\":"<<s.bpp<<",\"verified_images\":"<<(probe||control?0:warmup+samples)<<",\"verified_bytes\":"<<verifiedBytes<<",\"readback_verified\":"<<(probe||control?"false":"true")<<",\"row_stride\":0,\"layer_stride\":0,\"gpu_ms\":null,\"samples_ms\":[";
    for(unsigned i=0;i<measured.size();++i){if(i)std::cout<<',';std::cout<<measured[i];}std::cout<<"],\"payload_sha256\":[";for(unsigned i=0;i<hashes.size();++i){if(i)std::cout<<',';std::cout<<quote(hashes[i]);}
    std::cout<<"],\"runtime_modules\":"<<runtime<<",\"observer_enabled\":"<<(observe?"true":"false")<<",\"observed_instance_api_version\":"<<observed.instanceApi<<",\"observer_live_instances_after_close\":"<<retired.liveInstances<<",\"observer_live_devices_after_close\":"<<retired.liveDevices<<",\"native_workload_shortcuts\":false}\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
