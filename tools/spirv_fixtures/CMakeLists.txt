cmake_minimum_required(VERSION 3.25)
project(GraphicsSDKSpirvFixtureTool LANGUAGES CXX)
# Local-only assembler/validator. No dependency from the SDK or installed package.
set(GFX_SPIRV_PREREQUISITES "${CMAKE_CURRENT_SOURCE_DIR}/../../out/vulkan-validation-runtime/prerequisites" CACHE PATH "Pinned local SPIRV-Tools install")
add_executable(spirv_fixture_tool main.cpp)
target_compile_features(spirv_fixture_tool PRIVATE cxx_std_23)
target_include_directories(spirv_fixture_tool SYSTEM PRIVATE "${GFX_SPIRV_PREREQUISITES}/include")
target_link_libraries(spirv_fixture_tool PRIVATE "${GFX_SPIRV_PREREQUISITES}/lib/SPIRV-Tools.lib")
