#include "Spirv.hpp"
#include "VulkanBindingSupport.hpp"
#include <iostream>
#include <stdexcept>

namespace {
std::vector<std::uint32_t> words(const gfx::ShaderBinary& shader) {
    std::vector<std::uint32_t> result(shader.bytes.size() / 4);
    std::memcpy(result.data(), shader.bytes.data(), shader.bytes.size());
    return result;
}
}

int main() try {
    unsigned assertions{};
    auto check = [&](bool value) { ++assertions; if (!value) throw std::runtime_error("SPIR-V resource interface contract"); };
    using namespace gfx;
    for (bool large : {false, true}) {
        auto p = bindingTest::pipeline(true, large);
        check(bool(validateBindingLayout(p.bindingLayout)));
        for (auto stage : {ShaderStage::Vertex, ShaderStage::Fragment}) {
            const auto& shader = stage == ShaderStage::Vertex ? p.vertex : p.fragment;
            std::map<unsigned, unsigned> varyings; std::vector<unsigned> minimum;
            auto inspect = [&](const BindingLayout& layout) { return vulkan::inspectSpirv(shader, stage, stage == ShaderStage::Fragment ? 1 : 0, {}, &varyings, layout, &minimum); };
            check(bool(inspect(p.bindingLayout)));
            const auto reflected = reflectSpirvInterface(shader);
            check(reflected && reflected->stage == stage && reflected->entryPoint == "main");
            if (stage == ShaderStage::Vertex) {
                check(reflected->inputs.empty() && reflected->outputs == std::vector<ShaderLocation>{{0, 2}});
                check(reflected->bindings.size() == 1 && reflected->bindings[0].slot == 2 &&
                      reflected->bindings[0].type == ShaderResourceType::ConstantBuffer &&
                      reflected->bindings[0].count == 1 && reflected->bindings[0].minimumBytes == 32);
            } else {
                check(reflected->inputs == std::vector<ShaderLocation>{{0, 2}} &&
                      reflected->outputs == std::vector<ShaderLocation>{{0, 4}});
                const auto& b = reflected->bindings;
                check(b.size() == 4 && b[0].slot == 0 && b[0].type == ShaderResourceType::Texture2D && b[0].count == 2 &&
                      b[1].slot == 1 && b[1].type == ShaderResourceType::ConstantBuffer && b[1].count == 2 &&
                      b[1].minimumBytes == (large ? 272u : 32u) &&
                      b[2].slot == 32 && b[3].slot == 33 && b[2].type == ShaderResourceType::Sampler &&
                      b[3].type == ShaderResourceType::Sampler && b[2].count == 1 && b[3].count == 1);
            }
            const auto bindings = reflectShader(shader);
            check(bindings && bindings->size() == reflected->bindings.size());
            check(minimum == (stage == ShaderStage::Vertex ? std::vector<unsigned>{0, 0, 32} : std::vector<unsigned>{0, large ? 272u : 32u, 0}));
            check(varyings == std::map<unsigned, unsigned>{{0, 2}});
            check(!inspect({}));
            auto wrong = p.bindingLayout;
            wrong.entries[stage == ShaderStage::Vertex ? 2 : 0].visibility = stage == ShaderStage::Vertex ? ShaderVisibility::Fragment : ShaderVisibility::Vertex; check(!inspect(wrong));
            wrong = p.bindingLayout; wrong.entries[stage == ShaderStage::Vertex ? 2 : 0].count = 3; check(!inspect(wrong));
            wrong = p.bindingLayout; wrong.entries[stage == ShaderStage::Vertex ? 2 : 0].type = stage == ShaderStage::Vertex ? BindingType::Texture2D : BindingType::ConstantBuffer; check(!inspect(wrong));
            if (stage == ShaderStage::Fragment) {
                wrong = p.bindingLayout; wrong.samplers.pop_back(); check(!inspect(wrong));
                wrong = p.bindingLayout; wrong.samplers[0].visibility = ShaderVisibility::Vertex; check(!inspect(wrong));
            }
            for (auto decoration : {34u, 33u}) {
                auto copy = words(shader);
                for (std::size_t at = 5; at < copy.size(); at += copy[at] >> 16) if ((copy[at] & 65535) == 71 && copy[at + 2] == decoration) { copy[at + 3] = decoration == 34 ? 1 : 31; break; }
                auto bad = bindingTest::binary(copy, stage);
                check(!vulkan::inspectSpirv(bad, stage, stage == ShaderStage::Fragment ? 1 : 0, {}, &varyings, p.bindingLayout));
                auto metadata = reflectSpirvInterface(bad);
                check(metadata && std::ranges::any_of(metadata->bindings, [&](const auto& binding) {
                    return decoration == 34 ? binding.space == 1 : binding.slot == 31;
                }));
            }
        }
    }
    auto p = bindingTest::pipeline(); std::map<unsigned, unsigned> varyings;
    check(bool(vulkan::inspectSpirv(p.fragment, ShaderStage::Fragment, 1, {}, &varyings, BindingLayout{{{}}, {{}}})));
    auto named = words(p.fragment);
    std::size_t annotations{};
    std::uint32_t texture{};
    for (std::size_t at = 5; at < named.size(); at += named[at] >> 16) {
        const auto op = named[at] & 65535;
        if (op == 15) named[at + 3] = 0x77617264; // "draw"
        if (op == 71) {
            if (!annotations) annotations = at;
            if (named[at + 2] == 33 && named[at + 3] == 0) texture = named[at + 1];
        }
    }
    check(annotations && texture);
    const auto debug = named.begin() + annotations;
    named.insert(debug, {(4u << 16) | 5u, texture, 0x6e69616d, 0x786554}); // OpName "mainTex"
    auto renamed = bindingTest::binary(named, ShaderStage::Fragment);
    const auto metadata = reflectSpirvInterface(renamed);
    check(metadata && metadata->entryPoint == "draw" && metadata->bindings.size() == 2 &&
          metadata->bindings[0].name == "mainTex" && metadata->bindings[1].name.empty());
    const auto invalidEntry = vulkan::inspectSpirv(renamed, ShaderStage::Fragment, 1, {}, &varyings, BindingLayout{{{}}, {{}}});
    check(!invalidEntry && invalidEntry.error().code == ErrorCode::Unsupported);
    renamed.stage = ShaderStage::Vertex;
    const auto invalidStage = reflectSpirvInterface(renamed);
    check(!invalidStage && invalidStage.error().code == ErrorCode::InvalidArgument);
    named[annotations + 3] = 0x7f7f7f7f;
    const auto invalidName = reflectSpirvInterface(bindingTest::binary(named, ShaderStage::Fragment));
    check(!invalidName && invalidName.error().code == ErrorCode::InvalidArgument);
    auto duplicated = words(p.fragment);
    for (std::size_t at = 5; at < duplicated.size(); at += duplicated[at] >> 16) {
        if ((duplicated[at] & 65535) == 71 && duplicated[at + 2] == 33 && duplicated[at + 3] == 32)
            duplicated[at + 3] = 0;
    }
    const auto duplicateBinding = reflectSpirvInterface(bindingTest::binary(duplicated, ShaderStage::Fragment));
    check(!duplicateBinding && duplicateBinding.error().code == ErrorCode::InvalidArgument);
    std::cout << "PASS SPIRV_binding_assertions=" << assertions << " full_semantic_validator=false GPU_calls=0\n"; return 0;
} catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
