#include <metal_stdlib>
using namespace metal;
kernel void graphics_sdk_probe(device uint* result [[buffer(0)]],uint index [[thread_position_in_grid]]){
    result[index]=index^0xa5c31927u;
}
