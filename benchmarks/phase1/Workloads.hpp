#pragma once
#include "common/Adapter.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <functional>
#include <stdexcept>
namespace phase1 {
using Bytes=std::vector<std::byte>;
inline void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
constexpr char quadSource[]=R"(
struct V { float4 p:SV_Position; nointerpolation uint id:COLOR; };
V VS(uint id:SV_VertexID,uint instance:SV_InstanceID) {
    float2 corners[6]={float2(0,0),float2(1,0),float2(0,1),float2(0,1),float2(1,0),float2(1,1)};
    V v; v.p=float4(corners[id]*float2(2,-2)+float2(-1,1),.2,1); v.id=instance; return v;
}
float4 Red(V v):SV_Target0 { return float4(1,0,0,1); }
float4 Green(V v):SV_Target0 { return float4(0,1,0,1); }
)";
struct Task { std::function<void()> run,verify; std::string contract; unsigned draws=0,dispatches=0; std::uint64_t payload=0; std::function<std::string()> stats=[] {return "null";}; };
inline std::string graphicsSource(){
    std::string source=quadSource;
    source+=R"(
Texture2D<float4> T:register(t0);
V Sprite(uint id:SV_VertexID,uint instance:SV_InstanceID) {
 float2 q[6]={float2(0,0),float2(1,0),float2(0,1),float2(0,1),float2(1,0),float2(1,1)};
 uint cell=instance%4096; float2 p=float2(cell%64,cell/64)+q[id];
 V v;v.p=float4(p*float2(2.0/64,-2.0/64)+float2(-1,1),.2,1);v.id=instance%4;return v;
}
float4 Atlas(V v):SV_Target0 {return T.Load(int3(v.id%2,v.id/2,0));}
V Mesh(float3 p:POSITION,uint instance:SV_InstanceID) {V v;v.p=float4(p.xy,p.z+(instance%2)*.3,1);v.id=instance%2;return v;}
struct Outputs {float4 a:SV_Target0;float4 b:SV_Target1;};
Outputs MRT(V v) {Outputs o;o.a=v.id==0?float4(1,0,0,1):float4(0,1,0,1);o.b=float4(0,1,0,1);return o;}
float4 Invert(V v):SV_Target0 {return float4(1-T.Load(int3(int2(v.p.xy),0)).rgb,1);}
)";
    return source;
}
struct Compute {std::vector<unsigned> input,expected;std::string source;};
inline Compute computeDefinition(const std::string& op,bool raw){
    constexpr unsigned count=256*256;
    std::vector<unsigned> input(count),expected(count,0); for(unsigned i=0;i<count;++i) input[i]=((i*13)%256)|(((i*7)%256)<<8)|(((i*3)%256)<<16);

    std::string body;
    if(op=="blur") { body="uint x=i%256,y=i/256,sum=0; for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) sum+=LoadA(clamp(int(y)+dy,0,255)*256+clamp(int(x)+dx,0,255)); StoreB(i,sum/9);";
        for(unsigned y=0;y<256;++y)for(unsigned x=0;x<256;++x){unsigned sum=0;for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)sum+=input[std::clamp(int(y)+dy,0,255)*256+std::clamp(int(x)+dx,0,255)];expected[y*256+x]=sum/9;}
    } else if(op=="color") { body="uint v=LoadA(i); StoreB(i,(77*(v&255)+150*((v>>8)&255)+29*((v>>16)&255)+128)>>8);"; for(unsigned i=0;i<count;++i) {auto v=input[i];expected[i]=(77*(v&255)+150*((v>>8)&255)+29*((v>>16)&255)+128)>>8;}
    } else if(op=="reduce") { body="P[lane]=LoadA(i); GroupMemoryBarrierWithGroupSync(); for(uint n=32;n>0;n>>=1){if(lane<n)P[lane]+=P[lane+n];GroupMemoryBarrierWithGroupSync();} if(lane==0)StoreB(group.x,P[0]);"; for(unsigned i=0;i<count;++i)expected[i/64]+=input[i];
    } else { require(op=="buffer"||op=="graphics","Unknown compute operation"); body="StoreB(i,LoadA(i)*3+7);";for(unsigned i=0;i<count;++i)expected[i]=input[i]*3+7; }
    std::string source=raw?
        "ByteAddressBuffer A:register(t0,space0); RWByteAddressBuffer B:register(u0,space1);uint LoadA(uint i){return A.Load(i*4);}void StoreB(uint i,uint v){B.Store(i*4,v);}":
        "StructuredBuffer<uint> A:register(t0); RWStructuredBuffer<uint> B:register(u1);uint LoadA(uint i){return A[i];}void StoreB(uint i,uint v){B[i]=v;}";
    source+="groupshared uint P[64]; [numthreads(64,1,1)] void CS(uint3 id:SV_DispatchThreadID,uint lane:SV_GroupIndex,uint3 group:SV_GroupID){uint i=id.x;"+body+"}";
    return {std::move(input),std::move(expected),std::move(source)};
}
inline std::string videoSource(bool ten){
    std::string source=std::string(quadSource)+R"(
Texture2D<uint> Y:register(t0);Texture2D<uint2> UV:register(t1);
float4 Video(V v):SV_Target0 {int2 p=int2(v.p.xy);uint y=Y.Load(int3(p,0));uint2 uv=UV.Load(int3(p>>1,0));
)";
    source+=ten?"y>>=6;uv>>=6;float yf=(float(y)-64)/876, uf=(float(uv.x)-512)/896,vf=(float(uv.y)-512)/896;":"float yf=(float(y)-16)/219,uf=(float(uv.x)-128)/224,vf=(float(uv.y)-128)/224;";
    source+="return float4(saturate(float3(yf+1.5748*vf,yf-.187324*uf-.468124*vf,yf+1.8556*uf)),1);}";
    source+=R"(
float4 OverlayVideo(V v):SV_Target0{float4 c=Video(v);c.a=.5;return c;}
Texture2D<float4> Image:register(t0);
float4 Filter(V v):SV_Target0 {int2 p=int2(v.p.xy);uint w,h;Image.GetDimensions(w,h);float3 sum=0;
for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)sum+=Image.Load(int3(clamp(p+int2(dx,dy),int2(0,0),int2(w-1,h-1)),0)).rgb;
return float4(1-sum/9,1);}
)";
    return source;
}
inline Bytes videoReference(unsigned w,unsigned h,bool ten,bool layers,bool filter){
    auto expected=std::make_shared<Bytes>(static_cast<size_t>(w)*h*4);auto quant=[](double f){return static_cast<std::byte>(std::clamp(int(std::lround(f*255)),0,255));};
    for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){auto at=(static_cast<size_t>(y)*w+x)*4;double yy=double((x+y)%(ten?877:220))/(ten?876:219),u=ten?0:double(90-128)/224,v=ten?0:double(170-128)/224;(*expected)[at]=quant(yy+1.5748*v);(*expected)[at+1]=quant(yy-.187324*u-.468124*v);(*expected)[at+2]=quant(yy+1.8556*u);(*expected)[at+3]=std::byte{255};}
    if(layers)for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){auto at=(static_cast<size_t>(y)*w+x)*4;double overlayValue=std::clamp((double(112+x%32)-16)/219,0.,1.);for(unsigned c=0;c<3;++c)(*expected)[at+c]=static_cast<std::byte>(std::lround((overlayValue*255+int((*expected)[at+c]))*.5));}
    if(filter){auto original=*expected;for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x)for(unsigned c=0;c<3;++c){unsigned sum=0;for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)sum+=int(original[(static_cast<size_t>(std::clamp(int(y)+dy,0,int(h)-1))*w+std::clamp(int(x)+dx,0,int(w)-1))*4+c]);(*expected)[(static_cast<size_t>(y)*w+x)*4+c]=static_cast<std::byte>(std::lround(255.-sum/9.));}}
    return std::move(*expected);
}
inline constexpr std::array<unsigned char,16> atlasTexels{255,0,0,128,0,255,0,128,0,0,255,128,255,255,255,128};
inline constexpr std::array<float,18> meshVertices{-1,1,.2f,1,1,.2f,-1,-1,.2f,-1,-1,.2f,1,1,.2f,1,-1,.2f};
inline Bytes graphicsReference(bool sprites,unsigned count){
    Bytes expected(64*64*4);for(unsigned i=0;i<4096;++i){expected[i*4+2]=expected[i*4+3]=std::byte{255};}
    if(sprites)for(unsigned i=0;i<count;++i){auto cell=i%4096;auto x=cell%64,y=cell/64;if(x<8||x>=56||y<8||y>=56)continue;for(unsigned c=0;c<3;++c){auto old=int(expected[cell*4+c]);expected[cell*4+c]=static_cast<std::byte>((unsigned(atlasTexels[(i%4)*4+c])*128+old*127+127)/255);}}
    else for(unsigned i=0;i<4096;++i){expected[i*4]=std::byte{255};expected[i*4+2]=std::byte{0};}
    return expected;
}
inline std::string graphicsContract(bool sprites,unsigned count){return sprites?"E_fixed_order_instanced_pixel_sprites_2x2_atlas_alpha128_scissor8_8_48_48_count_"+std::to_string(count):"F_2000_mesh_quads_vertex_buffer_depth_less_write_MRT2_invert_postprocess_fixed_order";}
inline std::string videoContract(unsigned w,unsigned h,bool ten,bool layers,bool filter){return std::string("H_public_planar_upload_BT709_limited_")+(ten?"P010_10bit":"NV12_8bit")+(layers?"_two_independent_NV12_videos_alpha":"")+(filter?"_blur3x3_then_invert":"")+"_"+std::to_string(w)+"x"+std::to_string(h)+"_no_decoder_no_external_import";}
inline constexpr std::array advancedScenarios{"compute_blur","compute_color","compute_reduce","compute_buffer","compute_graphics","sprites_10000","sprites_100000","mesh_draw","mesh_instanced","video_1080","video_4k","video_p010","video_layers","video_filter"};
} // namespace phase1
