#pragma once
#include <graphics/Shader.hpp>
#include <algorithm>
#include <cstring>
#include <map>
#include <set>

namespace gfx::vulkan {
// Bounded framing and interface inspection for externally validated modules.
// This deliberately does not implement a SPIR-V semantic validator.
inline Result<std::vector<std::uint32_t>> inspectSpirv(const ShaderBinary& shader, ShaderStage stage, std::uint32_t colors) {
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
    std::set<unsigned> interfaceIds, outputs;
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
        } else if (op == 10 || op == 11 || op == 72) {
            return unsupported("SPIR-V extensions/imports/member-decorated interfaces unsupported");
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
            if (n != 4 || (p[2] != 11 && p[2] != 30)) return unsupported("Only standalone BuiltIn/Location interfaces supported");
            auto& map = p[2] == 11 ? builtins : locations;
            if (!map.emplace(p[1], p[3]).second) return bad("Duplicate interface decoration");
        } else if (op == 21 || op == 22 || op == 23 || op == 32) { // Int, Float, Vector, Pointer
            if (n != (op == 22 ? 3u : 4u) || !id(p[1]) || !types.emplace(p[1], Type{op, p[2], n == 4 ? p[3] : 0}).second)
                return bad("Invalid interface type framing/ID");
        } else if (op == 59) { // OpVariable
            if (n < 4 || n > 5 || !id(p[1]) || !id(p[2])) return bad("Invalid variable framing");
            if (p[3] != 1 && p[3] != 3 && p[3] != 6 && p[3] != 7) return unsupported("Descriptor/push/workgroup/storage variables unsupported");
            if (!variables.emplace(p[2], Variable{p[1], p[3]}).second) return bad("Duplicate variable ID");
        }
        at += n;
    }
    if (entries != 1 || memoryModels != 1 || capabilities != 1 || origins != (stage == ShaderStage::Fragment ? 1u : 0u))
        return bad("SPIR-V requires one Shader capability, memory model and main entry");
    if (origins && executionId != entryId) return bad("Execution mode does not name main entry");
    auto vec4 = [&](unsigned type) {
        const auto t = types.find(type);
        if (t == types.end() || t->second.op != 23 || t->second.b != 4) return false;
        const auto scalar = types.find(t->second.a);
        return scalar != types.end() && scalar->second.op == 22 && scalar->second.a == 32;
    };
    bool position{};
    for (const auto& [variable, v] : variables) {
        if (v.storage != 1 && v.storage != 3) continue;
        if (!interfaceIds.contains(variable)) return bad("Entry point omits declared stage interface");
        const auto pointer = types.find(v.pointer);
        if (pointer == types.end() || pointer->second.op != 32 || pointer->second.a != v.storage) return bad("Invalid interface pointer type");
        if (builtins.contains(variable) && locations.contains(variable)) return bad("BuiltIn cannot also have Location");
        if (builtins.contains(variable)) {
            const auto builtin = builtins.at(variable);
            if (stage == ShaderStage::Vertex && v.storage == 3 && builtin == 0 && vec4(pointer->second.b)) {
                if (position) return bad("Duplicate Position output"); position = true;
            } else if (stage == ShaderStage::Vertex && v.storage == 1 && (builtin == 42 || builtin == 43)) {
                const auto t = types.find(pointer->second.b);
                if (t == types.end() || t->second.op != 21 || t->second.a != 32) return bad("Vertex/instance index must be int32");
            } else return unsupported("Unsupported builtin interface");
        } else if (stage == ShaderStage::Fragment && v.storage == 3 && locations.contains(variable) && vec4(pointer->second.b)) {
            if (!outputs.insert(locations.at(variable)).second) return bad("Duplicate fragment output location");
        } else return unsupported("Only vertex builtins and float4 fragment outputs supported");
    }
    for (auto variable : interfaceIds) if (!variables.contains(variable)) return bad("Entry interface ID is not a variable");
    if (stage == ShaderStage::Vertex && !position) return bad("Vertex requires standalone float4 Position");
    if (stage == ShaderStage::Fragment) {
        if (outputs.size() != colors) return bad("Fragment outputs do not match render targets");
        for (unsigned i = 0; i < colors; ++i) if (!outputs.contains(i)) return bad("Missing fragment output location");
    }
    return words;
}
}
