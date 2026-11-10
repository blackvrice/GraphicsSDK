#include <graphics/Graphics.hpp>

#include <iostream>
#include <string_view>

int main(int argc, char** argv) {
    if (argc != 4)
        return 1;

    const std::string_view profile = argv[1];
    if ((profile != "d3d12" && profile != "vulkan") ||
        gfx::version != argv[2] || gfx::buildFingerprint() != argv[3] ||
        gfx::backendCompiled(gfx::Backend::D3D12) != (profile == "d3d12") ||
        gfx::backendCompiled(gfx::Backend::Vulkan) != (profile == "vulkan") ||
        gfx::backendCompiled(gfx::Backend::Metal))
        return 1;

    std::cout << "PASS " << profile << ' ' << gfx::version << ' ' << gfx::buildFingerprint() << '\n';
}
