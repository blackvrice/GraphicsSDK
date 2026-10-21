#include <graphics/Pipeline.hpp>
#include <algorithm>
#include <array>
#include <set>

namespace gfx {
Result<void> validateVertexLayout(const VertexLayout& layout) {
    auto bad=[] { return std::unexpected(Error{ErrorCode::InvalidArgument,"Invalid vertex layout: slots/attributes, semantics, format, stride or alignment"}); };
    if(layout.buffers.size()>32 || layout.attributes.size()>32) return bad();
    std::array<const VertexBufferLayout*,32> slots{};
    std::array<bool,32> used{};
    for(const auto& binding:layout.buffers) {
        if(binding.slot>=32 || slots[binding.slot] || !binding.stride || binding.stride>2048 ||
           (binding.rate!=VertexInputRate::Vertex && binding.rate!=VertexInputRate::Instance)) return bad();
        slots[binding.slot]=&binding;
    }
    std::set<std::pair<std::string,std::uint32_t>> semantics;
    for(const auto& attribute:layout.attributes) {
        if(attribute.slot>=32 || !slots[attribute.slot] || attribute.semantic.empty()) return bad();
        std::string semantic;
        for(unsigned char c:attribute.semantic) {
            if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='_'||(!semantic.empty()&&c>='0'&&c<='9'))) return bad();
            semantic.push_back(c>='a'&&c<='z'?static_cast<char>(c-'a'+'A'):static_cast<char>(c));
        }
        if(semantic.starts_with("SV_") || !semantics.emplace(semantic,attribute.semanticIndex).second) return bad();
        unsigned bytes=0,alignment=4;
        switch(attribute.format) {
        case VertexFormat::Float:bytes=4;break;
        case VertexFormat::Float2:bytes=8;break;
        case VertexFormat::Float3:bytes=12;break;
        case VertexFormat::Float4:bytes=16;break;
        case VertexFormat::UNorm8x4:bytes=4;alignment=1;break;
        default:return bad();
        }
        const auto stride=slots[attribute.slot]->stride;
        if(attribute.offset>stride || bytes>stride-attribute.offset || attribute.offset%alignment || stride%alignment) return bad();
        used[attribute.slot]=true;
    }
    for(const auto& binding:layout.buffers) if(!used[binding.slot]) return bad();
    return {};
}
}
