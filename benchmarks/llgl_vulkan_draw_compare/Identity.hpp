#pragma once
#include "../llgl_vulkan_pso/Support.hpp"
#include <charconv>
namespace llglcompare {
using namespace llglupload;
unsigned number(std::string_view s,unsigned lo,unsigned hi){unsigned value{};auto[p,e]=std::from_chars(s.data(),s.data()+s.size(),value);require(e==std::errc{}&&p==s.data()+s.size()&&value>=lo&&value<=hi,"Invalid bounded sample count");return value;}
std::string nativeIdentity(Context& c,bool observed){auto& p=c.properties.properties;std::ostringstream o;
    o<<"\"device_name\":"<<quote(p.deviceName)<<",\"device_uuid\":"<<quote(hex(c.id.deviceUUID))<<",\"driver_uuid\":"<<quote(hex(c.id.driverUUID))<<",\"device_luid\":"<<quote(hex(c.id.deviceLUID))
     <<",\"native_vendor_id\":"<<p.vendorID<<",\"native_device_id\":"<<p.deviceID<<",\"native_driver_version\":"<<p.driverVersion<<",\"native_api_version\":"<<p.apiVersion<<",\"native_handle_verified\":true"
     <<",\"queue_family\":"<<c.native.queueFamily<<",\"queue_index\":0,\"queue_flags\":"<<c.flags<<",\"loader_api_version\":"<<c.loaderVersion<<",\"instance_API_request_observed\":"<<(observed?"true":"false")
     <<",\"loader_path\":"<<quote(c.loader)<<",\"LLGL_path\":"<<quote(c.core)<<",\"backend_path\":"<<quote(c.backend)<<",\"loaded_validation_layer\":"<<quote(c.loadedLayer);return o.str();}
}
