#include <graphics/Bindings.hpp>
#include <array>

namespace gfx {
namespace {
bool valid(ShaderVisibility v) { return v==ShaderVisibility::Vertex || v==ShaderVisibility::Fragment || v==ShaderVisibility::All; }
bool overlaps(ShaderVisibility a,ShaderVisibility b) { return a==b || a==ShaderVisibility::All || b==ShaderVisibility::All; }
}
Result<void> validateBindingLayout(const BindingLayout& layout) {
    auto bad=[] {return std::unexpected(Error{ErrorCode::InvalidArgument,"Invalid binding layout: type, visibility, register range, overlap or limits"});};
    if(layout.entries.size()>32 || layout.samplers.size()>16)return bad();
    std::array<unsigned,2> textures{},constants{};
    unsigned total=0;
    for(size_t i=0;i<layout.entries.size();++i) {
        const auto& e=layout.entries[i];
        if((e.type!=BindingType::Texture2D && e.type!=BindingType::ConstantBuffer) || !valid(e.visibility) || !e.count || e.space>=0xfffffff0u)return bad();
        const auto limit=e.type==BindingType::Texture2D?128u:14u;
        if(e.slot>=limit || e.count>limit-e.slot || e.count>256-total)return bad();
        total+=e.count;
        for(unsigned stage=0;stage<2;++stage)if(e.visibility==ShaderVisibility::All || static_cast<unsigned>(e.visibility)==stage) {
            auto& count=e.type==BindingType::Texture2D?textures[stage]:constants[stage];
            count+=e.count;if(count>limit)return bad();
        }
        for(size_t j=0;j<i;++j) {
            const auto& a=layout.entries[j];
            if(e.type==a.type && e.space==a.space && overlaps(e.visibility,a.visibility) && e.slot<a.slot+a.count && a.slot<e.slot+e.count)return bad();
        }
    }
    for(size_t i=0;i<layout.samplers.size();++i) {
        const auto& s=layout.samplers[i];
        if(s.slot>=16 || s.space>=0xfffffff0u || !valid(s.visibility) ||
           (s.filter!=SamplerFilter::Point && s.filter!=SamplerFilter::Linear) || (s.address!=SamplerAddress::Clamp && s.address!=SamplerAddress::Repeat))return bad();
        for(size_t j=0;j<i;++j) {const auto& a=layout.samplers[j];if(a.slot==s.slot && a.space==s.space && overlaps(a.visibility,s.visibility))return bad();}
    }
    return {};
}
}
