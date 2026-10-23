#pragma once
#include <graphics/Pipeline.hpp>
#include <algorithm>
#include <cstring>
#include <map>
#include <set>

namespace gfx::vulkan {
// Bounded framing and interface inspection for externally validated modules.
// This deliberately does not implement a SPIR-V semantic validator.
inline Result<std::vector<std::uint32_t>> inspectSpirv(const ShaderBinary& shader, ShaderStage stage, std::uint32_t colors,
    const VertexLayout& vertexLayout = {}, std::map<unsigned, unsigned>* varyings = nullptr,
    const BindingLayout& bindings = {}, std::vector<unsigned>* minimumBytes = nullptr) {
    auto bad = [](const char* message) { return std::unexpected(Error{ErrorCode::InvalidArgument, message}); };
    auto unsupported = [](const char* message) { return std::unexpected(Error{ErrorCode::Unsupported, message}); };
    if (shader.format != ShaderFormat::SPIRV) return unsupported("Vulkan requires explicit SPIRV shader format");
    if (shader.stage != stage || shader.bytes.size() < 20 || shader.bytes.size() % 4 || shader.bytes.size() > 4 * 1024 * 1024)
        return bad("Invalid SPIR-V stage/byte framing");
    std::vector<std::uint32_t> words(shader.bytes.size() / 4);
    std::memcpy(words.data(), shader.bytes.data(), shader.bytes.size());
    if (words[0] != 0x07230203 || words[4] || !words[3] || words[3] > 1048576) return bad("Invalid SPIR-V header");
    if (words[1] != 0x00010300) return unsupported("Initial Vulkan 1.1 shader path requires SPIR-V 1.3");
    struct Type { unsigned op{}, a{}, b{}; };
    struct Variable { unsigned pointer{}, storage{}; };
    std::map<unsigned, Type> types;
    std::map<unsigned, Variable> variables;
    std::map<unsigned, unsigned> locations, builtins;
    std::map<unsigned, unsigned> sets, slots, constants;
    std::map<unsigned, std::vector<unsigned>> structures;
    std::map<std::pair<unsigned, unsigned>, unsigned> offsets;
    std::set<unsigned> blocks;
    std::set<unsigned> resourceSlots;
    std::vector<unsigned> minimum(bindings.entries.size());
    std::set<unsigned> interfaceIds, outputs;
    std::map<unsigned, unsigned> vertexInputs, stageVaryings;
    unsigned entries{}, memoryModels{}, capabilities{}, origins{}, entryId{}, executionId{};
    auto id = [&](unsigned value) { return value && value < words[3]; };
    for (std::size_t at = 5; at < words.size();) {
        const unsigned n = words[at] >> 16, op = words[at] & 65535;
        if (!n || n > words.size() - at) return bad("Truncated SPIR-V instruction");
        const auto p = words.data() + at;
        if (op == 17) { // OpCapability
            if (n != 2) return bad("Invalid capability framing");
            if (p[1] != 1) return unsupported("Only Shader capability is supported");
            ++capabilities;
        } else if (op == 10 || op == 11) {
            return unsupported("SPIR-V extensions/imports unsupported");
        } else if (op == 72) { // Uniform block member Offset; stage interface members remain unsupported.
            if (n != 5 || !id(p[1]) || p[3] != 35) return unsupported("Only uniform block member Offset supported");
            if (!offsets.emplace(std::pair{p[1], p[2]}, p[4]).second) return bad("Duplicate member offset");
        } else if (op == 14) { // OpMemoryModel
            if (n != 3 || p[1] != 0 || p[2] != 1) return unsupported("Logical GLSL450 memory model required");
            ++memoryModels;
        } else if (op == 15) { // OpEntryPoint
            if (n < 5 || !id(p[2])) return bad("Invalid entry point framing");
            if (p[1] != (stage == ShaderStage::Vertex ? 0u : 4u)) return bad("SPIR-V execution model/stage mismatch");
            entryId = p[2];
            const auto name = reinterpret_cast<const char*>(p + 3);
            const auto end = static_cast<const char*>(std::memchr(name, 0, (n - 3) * 4));
            if (!end) return bad("Unterminated SPIR-V entry name");
            if (std::string_view(name, end - name) != "main") return unsupported("SPIR-V entry point must be main");
            const auto first = 3 + (std::size_t(end - name) + 1 + 3) / 4;
            for (auto i = first; i < n; ++i) if (!id(p[i]) || !interfaceIds.insert(p[i]).second) return bad("Invalid entry interface ID");
            ++entries;
        } else if (op == 16) { // OpExecutionMode
            if (n != 3 || !id(p[1]) || p[2] != 7 || stage != ShaderStage::Fragment)
                return unsupported("Only fragment OriginUpperLeft execution mode supported");
            ++origins;
            executionId = p[1];
        } else if (op == 71) { // OpDecorate
            if (n < 3 || !id(p[1])) return bad("Invalid decoration framing");
            if (n == 3 && p[2] == 2) { if (!blocks.insert(p[1]).second) return bad("Duplicate Block decoration"); }
            else {
                if (n != 4 || (p[2] != 11 && p[2] != 30 && p[2] != 33 && p[2] != 34)) return unsupported("Unsupported SPIR-V decoration");
                auto& map = p[2] == 11 ? builtins : p[2] == 30 ? locations : p[2] == 33 ? slots : sets;
                if (!map.emplace(p[1], p[3]).second) return bad("Duplicate interface/resource decoration");
            }
        } else if (op == 25) { // Float32 sampled non-arrayed, non-MS 2D image with unknown format.
            if (n != 9 || p[3] != 1 || p[4] || p[5] || p[6] || p[7] != 1 || p[8]) return unsupported("Only separate sampled float Texture2D supported");
            if (!id(p[1]) || !types.emplace(p[1], Type{op, p[2], 0}).second) return bad("Invalid image type");
        } else if (op == 26) {
            if (n != 2 || !id(p[1]) || !types.emplace(p[1], Type{op}).second) return bad("Invalid sampler type");
        } else if (op == 28) {
            if (n != 4 || !id(p[1]) || !types.emplace(p[1], Type{op, p[2], p[3]}).second) return bad("Invalid descriptor array type");
        } else if (op == 30) {
            if (n < 3 || !id(p[1]) || !structures.emplace(p[1], std::vector<unsigned>(p + 2, p + n)).second) return bad("Invalid uniform structure");
        } else if (op == 43 && n == 4) {
            if (!constants.emplace(p[2], p[3]).second) return bad("Duplicate scalar constant ID");
        } else if (op == 21 || op == 22 || op == 23 || op == 32) { // Int, Float, Vector, Pointer
            if (n != (op == 22 ? 3u : 4u) || !id(p[1]) || !types.emplace(p[1], Type{op, p[2], n == 4 ? p[3] : 0}).second)
                return bad("Invalid interface type framing/ID");
        } else if (op == 59) { // OpVariable
            if (n < 4 || n > 5 || !id(p[1]) || !id(p[2])) return bad("Invalid variable framing");
            if (p[3] != 0 && p[3] != 1 && p[3] != 2 && p[3] != 3 && p[3] != 6 && p[3] != 7) return unsupported("Push/workgroup/storage variables unsupported");
            if (!variables.emplace(p[2], Variable{p[1], p[3]}).second) return bad("Duplicate variable ID");
        }
        at += n;
    }
    if (entries != 1 || memoryModels != 1 || capabilities != 1 || origins != (stage == ShaderStage::Fragment ? 1u : 0u))
        return bad("SPIR-V requires one Shader capability, memory model and main entry");
    if (origins && executionId != entryId) return bad("Execution mode does not name main entry");
    auto components = [&](unsigned type) -> unsigned {
        const auto t = types.find(type);
        if (t == types.end()) return 0;
        if (t->second.op == 22) return t->second.a == 32 ? 1u : 0u;
        if (t->second.op != 23 || t->second.b < 2 || t->second.b > 4) return 0;
        const auto scalar = types.find(t->second.a);
        return scalar != types.end() && scalar->second.op == 22 && scalar->second.a == 32 ? t->second.b : 0;
    };
    bool position{};
    for (const auto& [variable, v] : variables) {
        if (v.storage == 0 || v.storage == 2) {
            if (!sets.contains(variable) || sets.at(variable) != 0 || !slots.contains(variable) || locations.contains(variable) || builtins.contains(variable))
                return bad("Resource requires DescriptorSet 0 and Binding");
            const auto slot = slots.at(variable);
            if (!resourceSlots.insert(slot).second) return bad("Duplicate shader descriptor binding");
            const auto pointer = types.find(v.pointer);
            if (pointer == types.end() || pointer->second.op != 32 || pointer->second.a != v.storage) return bad("Invalid resource pointer");
            unsigned type = pointer->second.b, count = 1;
            if (types.contains(type) && types.at(type).op == 28) {
                const auto a = types.at(type);
                if (!constants.contains(a.b) || !constants.at(a.b) || constants.at(a.b) > 256) return bad("Invalid fixed descriptor array count");
                count = constants.at(a.b); type = a.a;
            }
            auto visible = [&](ShaderVisibility visibility) { return visibility == ShaderVisibility::All || static_cast<unsigned>(visibility) == static_cast<unsigned>(stage); };
            if (types.contains(type) && types.at(type).op == 26 && v.storage == 0) {
                if (count != 1 || slot < 32 || slot - 32 >= bindings.samplers.size() || !visible(bindings.samplers[slot - 32].visibility)) return bad("Sampler binding/visibility mismatch");
            } else {
                if (slot >= bindings.entries.size()) return bad("Missing resource layout entry");
                const auto& entry = bindings.entries[slot];
                if (count != entry.count || !visible(entry.visibility)) return bad("Resource count/visibility mismatch");
                if (entry.type == BindingType::Texture2D) {
                    if (v.storage != 0 || !types.contains(type) || types.at(type).op != 25) return bad("Expected separate sampled image");
                    const auto scalar = types.find(types.at(type).a);
                    if (scalar == types.end() || scalar->second.op != 22 || scalar->second.a != 32) return unsupported("Sampled image requires float32");
                } else {
                    if (entry.type != BindingType::ConstantBuffer || v.storage != 2 || !blocks.contains(type) || !structures.contains(type)) return bad("Expected Uniform Block");
                    const auto& members = structures.at(type);
                    if (members.size() > 4096) return unsupported("Uniform block exceeds 64KiB");
                    for (unsigned i = 0; i < members.size(); ++i) {
                        const auto t = types.find(members[i]);
                        if (t == types.end() || t->second.op != 23 || t->second.b != 4 || !types.contains(t->second.a) ||
                            types.at(t->second.a).op != 22 || types.at(t->second.a).a != 32 || !offsets.contains({type, i}) || offsets.at({type, i}) != i * 16)
                            return unsupported("Uniform block requires consecutive float4 members at 16-byte offsets");
                    }
                    minimum[slot] = static_cast<unsigned>(members.size()) * 16;
                }
            }
            continue;
        }
        if (v.storage != 1 && v.storage != 3) continue;
        if (!interfaceIds.contains(variable)) return bad("Entry point omits declared stage interface");
        const auto pointer = types.find(v.pointer);
        if (pointer == types.end() || pointer->second.op != 32 || pointer->second.a != v.storage) return bad("Invalid interface pointer type");
        if (builtins.contains(variable) && locations.contains(variable)) return bad("BuiltIn cannot also have Location");
        if (builtins.contains(variable)) {
            const auto builtin = builtins.at(variable);
            if (stage == ShaderStage::Vertex && v.storage == 3 && builtin == 0 && components(pointer->second.b) == 4) {
                if (position) return bad("Duplicate Position output"); position = true;
            } else if (stage == ShaderStage::Vertex && v.storage == 1 && (builtin == 42 || builtin == 43)) {
                const auto t = types.find(pointer->second.b);
                if (t == types.end() || t->second.op != 21 || t->second.a != 32) return bad("Vertex/instance index must be int32");
            } else return unsupported("Unsupported builtin interface");
        } else if (locations.contains(variable) && components(pointer->second.b)) {
            const auto location = locations.at(variable), count = components(pointer->second.b);
            if (stage == ShaderStage::Fragment && v.storage == 3) {
                if (count != 4 || !outputs.insert(location).second) return bad("Fragment output requires unique float4 location");
            } else if (stage == ShaderStage::Vertex && v.storage == 1) {
                if (!vertexInputs.emplace(location, count).second) return bad("Duplicate vertex input location");
            } else if (!stageVaryings.emplace(location, count).second) return bad("Duplicate stage varying location");
        } else return unsupported("Only standalone float32 scalar/vector interfaces supported");
    }
    for (auto variable : interfaceIds) if (!variables.contains(variable)) return bad("Entry interface ID is not a variable");
    if (stage == ShaderStage::Vertex && !position) return bad("Vertex requires standalone float4 Position");
    if (stage == ShaderStage::Vertex) {
        if (vertexInputs.size() != vertexLayout.attributes.size()) return bad("Vertex input count does not match layout");
        for (unsigned i = 0; i < vertexLayout.attributes.size(); ++i) {
            unsigned expected{};
            switch (vertexLayout.attributes[i].format) {
            case VertexFormat::Float: expected = 1; break;
            case VertexFormat::Float2: expected = 2; break;
            case VertexFormat::Float3: expected = 3; break;
            case VertexFormat::Float4: case VertexFormat::UNorm8x4: expected = 4; break;
            default: return bad("Invalid vertex input format");
            }
            if (!vertexInputs.contains(i) || vertexInputs.at(i) != expected) return bad("Vertex location/type does not match attribute order");
        }
    }
    if (stage == ShaderStage::Fragment) {
        if (outputs.size() != colors) return bad("Fragment outputs do not match render targets");
        for (unsigned i = 0; i < colors; ++i) if (!outputs.contains(i)) return bad("Missing fragment output location");
    }
    if (varyings) *varyings = std::move(stageVaryings);
    else if (!stageVaryings.empty()) return unsupported("Varying interface requires cross-stage inspection");
    if (minimumBytes) *minimumBytes = std::move(minimum);
    return words;
}
}
