#include <graphics/Bindings.hpp>
#include <array>

namespace gfx {
namespace {
bool valid(ShaderVisibility v) {
    return v == ShaderVisibility::Vertex || v == ShaderVisibility::Fragment ||
           v == ShaderVisibility::All || v == ShaderVisibility::Compute;
}
bool overlaps(ShaderVisibility a, ShaderVisibility b) {
    return a == b || a == ShaderVisibility::All || b == ShaderVisibility::All;
}
bool storageBinding(BindingType type) {
    return type == BindingType::StorageBuffer || type == BindingType::StorageTexture2D;
}
bool sameRegisterBank(BindingType a, BindingType b) {
    if (storageBinding(a) && storageBinding(b)) return true;
    return a == b;
}
bool visibleOnStage(ShaderVisibility v, unsigned stage) {
    if (v == ShaderVisibility::All) return true;
    if (stage == 0) return v == ShaderVisibility::Vertex;
    if (stage == 1) return v == ShaderVisibility::Fragment;
    return v == ShaderVisibility::Compute;
}
}
Result<void> validateBindingLayout(const BindingLayout& layout) {
    auto bad=[] {return std::unexpected(Error{ErrorCode::InvalidArgument,"Invalid binding layout: type, visibility, register range, overlap or limits"});};
    if(layout.entries.size()>32 || layout.samplers.size()>16)return bad();
    std::array<unsigned,3> textures{},constants{},uavs{};
    unsigned total=0;
    for(size_t i=0;i<layout.entries.size();++i) {
        const auto& e=layout.entries[i];
        const bool validType = e.type==BindingType::Texture2D || e.type==BindingType::ConstantBuffer ||
                               e.type==BindingType::StorageBuffer || e.type==BindingType::StorageTexture2D;
        if(!validType || !valid(e.visibility) || !e.count || e.space>=0xfffffff0u)return bad();
        const auto limit = e.type==BindingType::Texture2D ? 128u : e.type==BindingType::ConstantBuffer ? 14u : 64u;
        if(e.slot>=limit || e.count>limit-e.slot || e.count>256-total)return bad();
        total+=e.count;
        for(unsigned stage=0;stage<3;++stage)if(visibleOnStage(e.visibility, stage)) {
            auto& count = e.type==BindingType::Texture2D ? textures[stage] :
                          e.type==BindingType::ConstantBuffer ? constants[stage] : uavs[stage];
            count+=e.count;if(count>limit)return bad();
        }
        for(size_t j=0;j<i;++j) {
            const auto& a=layout.entries[j];
            if(sameRegisterBank(e.type,a.type) && e.space==a.space && overlaps(e.visibility,a.visibility) && e.slot<a.slot+a.count && a.slot<e.slot+e.count)return bad();
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
