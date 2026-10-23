#include <spirv-tools/libspirv.h>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

int main(int argc, char** argv) try {
    if (argc != 3 && argc != 4) throw std::runtime_error("Usage: spirv_fixture_tool input.spvasm output.spv [--vulkan1.0]");
    const bool vulkan10 = argc == 4 && std::string(argv[3]) == "--vulkan1.0";
    if (argc == 4 && !vulkan10) throw std::runtime_error("Unknown target environment");
    std::ifstream input(argv[1], std::ios::binary); if (!input) throw std::runtime_error("Cannot open assembly");
    const std::string source{std::istreambuf_iterator<char>(input), {}};
    const auto context = spvContextCreate(vulkan10 ? SPV_ENV_VULKAN_1_0 : SPV_ENV_VULKAN_1_1);
    spv_binary binary{}; spv_diagnostic diagnostic{};
    auto result = spvTextToBinary(context, source.data(), source.size(), &binary, &diagnostic);
    if (result == SPV_SUCCESS) result = spvValidateBinary(context, binary->code, binary->wordCount, &diagnostic);
    if (result != SPV_SUCCESS) {
        std::cerr << (diagnostic ? diagnostic->error : "Assembler/validator failed") << '\n';
        spvDiagnosticDestroy(diagnostic); spvBinaryDestroy(binary); spvContextDestroy(context); return 1;
    }
    std::ofstream output(argv[2], std::ios::binary); output.write(reinterpret_cast<const char*>(binary->code), binary->wordCount * 4);
    const bool saved = bool(output);
    std::cout << "PASS " << (vulkan10 ? "Vulkan1.0" : "Vulkan1.1") << " assembly and validation words=" << binary->wordCount << '\n';
    spvBinaryDestroy(binary); spvContextDestroy(context); if (!saved) throw std::runtime_error("Cannot save binary"); return 0;
} catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
