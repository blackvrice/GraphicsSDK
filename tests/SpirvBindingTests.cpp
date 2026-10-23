#include "Spirv.hpp"
#include "VulkanBindingSupport.hpp"
#include <iostream>
#include <stdexcept>

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
            auto changed = shader;
            std::vector<std::uint32_t> words(changed.bytes.size() / 4); std::memcpy(words.data(), changed.bytes.data(), changed.bytes.size());
            for (auto decoration : {34u, 33u}) {
                auto copy = words;
                for (std::size_t at = 5; at < copy.size(); at += copy[at] >> 16) if ((copy[at] & 65535) == 71 && copy[at + 2] == decoration) { copy[at + 3] = decoration == 34 ? 1 : 31; break; }
                auto bad = bindingTest::binary(copy, stage);
                check(!vulkan::inspectSpirv(bad, stage, stage == ShaderStage::Fragment ? 1 : 0, {}, &varyings, p.bindingLayout));
            }
        }
    }
    auto p = bindingTest::pipeline(); std::map<unsigned, unsigned> varyings;
    check(bool(vulkan::inspectSpirv(p.fragment, ShaderStage::Fragment, 1, {}, &varyings, BindingLayout{{{}}, {{}}})));
    std::cout << "PASS SPIRV_binding_assertions=" << assertions << " full_semantic_validator=false GPU_calls=0\n"; return 0;
} catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
