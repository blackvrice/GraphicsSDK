#include <metal_stdlib>
using namespace metal;

vertex float4 gfx_vertex(uint index [[vertex_id]]) {
    const float2 corners[3] = {float2(-1, -1), float2(3, -1), float2(-1, 3)};
    return float4(corners[index], 0, 1);
}

fragment float4 gfx_fragment() {
    return float4(1, 0, 0, 1);
}
