#include "Spirv.hpp"
#include "VulkanVertexSupport.hpp"
#include <iostream>
#include <stdexcept>

int main() try {
    unsigned assertions{};
    auto check = [&](bool value) { ++assertions; if (!value) throw std::runtime_error("SPIR-V vertex/varying interface contract"); };
    using namespace gfx;
    for (unsigned n = 1; n <= 4; ++n) for (bool normalized : {false, true}) {
        const auto p = vertexTest::pipeline(n, normalized, Format::RGBA8UNorm);
        check(bool(validateVertexLayout(p.vertexLayout)));
        std::map<unsigned, unsigned> vertex, fragment;
        check(bool(vulkan::inspectSpirv(p.vertex, ShaderStage::Vertex, 0, p.vertexLayout, &vertex)));
        check(bool(vulkan::inspectSpirv(p.fragment, ShaderStage::Fragment, 1, {}, &fragment)));
        check(vertex == fragment && vertex == std::map<unsigned, unsigned>{{0, 4}});
        auto wrong = p.vertexLayout; wrong.attributes.pop_back();
        auto r = vulkan::inspectSpirv(p.vertex, ShaderStage::Vertex, 0, wrong, &vertex); check(!r && r.error().code == ErrorCode::InvalidArgument);
        wrong = p.vertexLayout; wrong.attributes.back().format = VertexFormat::Float3;
        r = vulkan::inspectSpirv(p.vertex, ShaderStage::Vertex, 0, wrong, &vertex); check(!r && r.error().code == ErrorCode::InvalidArgument);
        wrong = p.vertexLayout; wrong.attributes.front().format = VertexFormat::Float4;
        r = vulkan::inspectSpirv(p.vertex, ShaderStage::Vertex, 0, wrong, &vertex); check(n == 4 ? bool(r) : !r);
    }
    std::map<unsigned, unsigned> mismatch;
    check(bool(vulkan::inspectSpirv(vertexTest::binary(vertexFixtures::mismatch3, ShaderStage::Fragment), ShaderStage::Fragment, 1, {}, &mismatch)));
    check(mismatch == std::map<unsigned, unsigned>{{0, 3}});
    auto p = vertexTest::pipeline(2, true, Format::RGBA8UNorm);
    auto wrong = p.vertexLayout; wrong.attributes[0].offset = 1; check(!validateVertexLayout(wrong));
    wrong = p.vertexLayout; wrong.buffers[0].slot = 32; check(!validateVertexLayout(wrong));
    wrong = p.vertexLayout; wrong.buffers[0].stride = 2049; check(!validateVertexLayout(wrong));
    std::cout << "PASS SPIRV_vertex_assertions=" << assertions << " full_semantic_validator=false GPU_calls=0\n"; return 0;
} catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
