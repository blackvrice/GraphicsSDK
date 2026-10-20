#include <graphics/Graphics.hpp>
#include <iostream>

int main() {
    if (gfx::buildFingerprint().size() != 64) return 1;
    std::cout << "GraphicsSDK " << gfx::version << "\n";
    if (!gfx::backendCompiled(gfx::Backend::D3D12)) return 0;
    auto device = gfx::createDevice();
    if (!device && device.error().code == gfx::ErrorCode::Unsupported) {
        std::cout << "Hardware unavailable; consumer smoke explicitly requests WARP\n";
        device=gfx::createDevice({gfx::Backend::D3D12,false,true});
    }
    if (!device) { std::cerr << device.error().message; return 1; }
    auto buffer = (*device)->createBuffer({4096});
    if (!buffer) return 1;
    std::cout << (*device)->caps().adapterName << "\n";
    return 0;
}
