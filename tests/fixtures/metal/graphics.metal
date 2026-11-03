#include <metal_stdlib>
using namespace metal;
#ifndef GFX_INPUT
#define GFX_INPUT 0
#endif
#ifndef GFX_INSTANCE
#define GFX_INSTANCE 0
#endif
#ifndef GFX_UNIFORM
#define GFX_UNIFORM 0
#endif
#ifndef GFX_TEXTURES
#define GFX_TEXTURES 0
#endif
#ifndef GFX_CENTER
#define GFX_CENTER 1
#endif
#ifndef GFX_MRT
#define GFX_MRT 0
#endif
struct Params { float4 tint; float4 info; };
struct Varying { float4 position [[position]]; float4 color [[user(locn0)]]; };
#if GFX_INPUT
struct Input {
    float2 position [[attribute(0)]];
    float4 color [[attribute(1)]];
#if GFX_INSTANCE
    float4 tint [[attribute(2)]];
#endif
};
#endif
vertex Varying gfx_vertex(
#if GFX_INPUT
    Input input [[stage_in]],
#endif
    uint vertexId [[vertex_id]]
#if GFX_UNIFORM
    , constant Params& parameters [[buffer(0)]]
#endif
) {
    Varying result;
#if GFX_INPUT
    result.position=float4(input.position,.5,1);result.color=input.color;
#if GFX_INSTANCE
    result.color*=input.tint;
#endif
#else
    const float2 positions[3]={float2(-1,-1),float2(3,-1),float2(-1,3)};
    result.position=float4(positions[vertexId%3],.5,1);result.color=float4(1,0,0,1);
#endif
#if GFX_UNIFORM
    result.position.z=parameters.info.x;
    result.color=parameters.tint;
#endif
    return result;
}
#if GFX_MRT
struct Output { float4 first [[color(0)]]; float4 second [[color(1)]]; };
#define GFX_OUTPUT Output
#else
#define GFX_OUTPUT float4
#endif
fragment GFX_OUTPUT gfx_fragment(Varying input [[stage_in]]
#if GFX_UNIFORM
    , constant Params& parameters [[buffer(1)]]
#endif
#if GFX_TEXTURES
    , array<texture2d<float>,GFX_TEXTURES> images [[texture(0)]], sampler sampling [[sampler(0)]]
#endif
) {
    float4 color=input.color;
#if GFX_UNIFORM
    color*=parameters.tint;
#endif
#if GFX_TEXTURES
#if GFX_CENTER
    const float2 uv=float2(.5,.5);
#else
    const float2 uv=float2(1.25,-.25);
#endif
#if GFX_UNIFORM
    color*=images[uint(parameters.info.y)].sample(sampling,uv);
#else
    color=images[0].sample(sampling,uv);
#endif
#endif
#if GFX_MRT
    return Output{color,float4(color.b,color.r,color.g,color.a)};
#else
    return color;
#endif
}
