cmake_minimum_required(VERSION 3.25)
project(GraphicsSDKLLGLVulkanPSO LANGUAGES CXX)
if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "Audited Windows x64 MSVC PSO profile required")
endif()
file(REAL_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../.." root)
set(LLGL_BASE_BUILD "${root}/out/phase3-llgl-vulkan-20261028-2" CACHE PATH "Audited import libraries")
set(LLGL_PROFILE_BUILD "${root}/out/phase3-llgl-transfer-src-candidate-final" CACHE PATH "Approved profile DLL build")
set(LLGL_DRAW_BUILD "${root}/out/phase3-llgl-draw-20261028/build" CACHE PATH "Audited pass-through observer build")
set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION OFF)
file(GLOB own CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp")
list(APPEND own "${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt" "${root}/benchmarks/llgl_vulkan_upload/Context.hpp"
  "${root}/benchmarks/llgl_vulkan_draw/Observer.hpp" "${root}/benchmarks/llgl_vulkan_draw/Layer.cpp"
  "${root}/benchmarks/vulkan_pso/Frame.hpp" "${root}/benchmarks/phase3_comparison/pso.json" "${root}/benchmarks/competitors/sources.lock.json"
  "${root}/tests/fixtures/SpirvFixtures.hpp" "${root}/tests/fixtures/spirv/fullscreen.spvasm" "${root}/tests/fixtures/spirv/red.spvasm" "${root}/tests/fixtures/spirv/green.spvasm")
list(SORT own)
set(identity "profile|llgl-fixed-pin-transfer-src-v1\nworkload|pso\n")
foreach(p IN LISTS own)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${p}")
  file(SHA256 "${p}" digest)
  file(RELATIVE_PATH name "${root}" "${p}")
  string(APPEND identity "${name}|${digest}\n")
endforeach()
string(SHA256 fingerprint "${identity}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/source-identity.txt" "${identity}")
file(SHA256 "${root}/tests/fixtures/SpirvFixtures.hpp" shader_sha)
add_executable(llgl_vulkan_pso main.cpp)
target_compile_features(llgl_vulkan_pso PRIVATE cxx_std_23)
target_compile_options(llgl_vulkan_pso PRIVATE /W4 /permissive- /EHsc /utf-8)
target_compile_definitions(llgl_vulkan_pso PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN GFX_LLGL_PSO_FINGERPRINT="${fingerprint}" GFX_PSO_SHADER_SHA="${shader_sha}")
target_include_directories(llgl_vulkan_pso PRIVATE "${root}/.deps/LLGL/include" "${root}/.deps/Vulkan-Headers/include")
target_link_libraries(llgl_vulkan_pso PRIVATE "${LLGL_BASE_BUILD}/build/llgl/build/Release/LLGL.lib" "${LLGL_BASE_BUILD}/vulkan-1.lib" bcrypt)
add_custom_command(TARGET llgl_vulkan_pso POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LLGL_PROFILE_BUILD}/build/llgl/build/Release/LLGL.dll" "$<TARGET_FILE_DIR:llgl_vulkan_pso>"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LLGL_PROFILE_BUILD}/build/llgl/build/Release/LLGL_Vulkan.dll" "$<TARGET_FILE_DIR:llgl_vulkan_pso>"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LLGL_DRAW_BUILD}/Release/VkLayer_gfx_llgl_draw.dll" "$<TARGET_FILE_DIR:llgl_vulkan_pso>"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LLGL_DRAW_BUILD}/Release/VkLayer_gfx_llgl_draw.json" "$<TARGET_FILE_DIR:llgl_vulkan_pso>")
