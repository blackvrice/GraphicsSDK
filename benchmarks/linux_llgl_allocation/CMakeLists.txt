cmake_minimum_required(VERSION 3.25)
project(GraphicsSDKLinuxLLGLAllocation LANGUAGES CXX)
set(GFX_LLGL_WORKLOAD_MODE "software-diagnostic" CACHE STRING "Qualified workload mode")
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "Native Linux x86_64 required")
endif()
file(REAL_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../.." root)
if(NOT EXISTS "${LLGL_PROBE_BUILD}/lib/libLLGL.so" OR NOT EXISTS "${LLGL_PROBE_BUILD}/lib/libLLGL_Vulkan.so")
  message(FATAL_ERROR "Qualified approved LLGL shared-library build required")
endif()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(OPENSSL_USE_STATIC_LIBS OFF)
find_package(OpenSSL REQUIRED COMPONENTS Crypto)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION OFF)
set(CMAKE_UNITY_BUILD OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")
execute_process(COMMAND "${Python3_EXECUTABLE}" "${root}/tools/port_linux_llgl_allocation.py"
  --output "${CMAKE_CURRENT_BINARY_DIR}" --mode "${GFX_LLGL_WORKLOAD_MODE}" COMMAND_ERROR_IS_FATAL ANY)
file(READ "${CMAKE_CURRENT_BINARY_DIR}/adaptation.json" manifest)
string(JSON fingerprint GET "${manifest}" source_fingerprint)
file(READ "${root}/benchmarks/competitors/sources.lock.json" lock)
string(JSON pin GET "${lock}" llgl commit)
foreach(p main.cpp Context.hpp)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${root}/benchmarks/llgl_vulkan_upload/${p}")
endforeach()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${root}/tools/port_linux_llgl_allocation.py"
  "${root}/benchmarks/common/LinuxEnvironment.hpp" "${root}/benchmarks/competitors/sources.lock.json"
  "${root}/benchmarks/phase3_comparison/upload.json" "${root}/benchmarks/phase3_comparison/draw.json")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${root}/benchmarks/llgl_vulkan_draw/main.cpp" "${root}/benchmarks/llgl_vulkan_draw/Layer.cpp"
  "${root}/benchmarks/llgl_vulkan_draw/Observer.hpp" "${root}/benchmarks/vulkan_draw/Frame.hpp"
  "${root}/tests/fixtures/SpirvFixtures.hpp" "${root}/tools/port_linux_llgl_upload.py"
  "${root}/tools/port_linux_llgl_draw.py" "${root}/benchmarks/llgl_vulkan_pso/main.cpp"
  "${root}/benchmarks/llgl_vulkan_pso/Support.hpp" "${root}/benchmarks/vulkan_pso/Frame.hpp"
  "${root}/benchmarks/phase3_comparison/pso.json"
  "${root}/tools/port_linux_llgl_pso.py" "${root}/benchmarks/llgl_vulkan_allocation/main.cpp"
  "${root}/benchmarks/vulkan_allocation/Payload.hpp" "${root}/benchmarks/phase3_comparison/allocation.json")
add_library(llgl_draw_observer SHARED "${CMAKE_CURRENT_BINARY_DIR}/Layer.cpp")
set_target_properties(llgl_draw_observer PROPERTIES OUTPUT_NAME VkLayer_gfx_llgl_draw)
target_compile_features(llgl_draw_observer PRIVATE cxx_std_23)
target_include_directories(llgl_draw_observer PRIVATE "${root}/benchmarks/llgl_vulkan_draw" "${root}/.deps/Vulkan-Headers/include")
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/VkLayer_gfx_llgl_draw.json" CONTENT "{\n\"file_format_version\":\"1.2.0\",\"layer\":{\"name\":\"VK_LAYER_GFX_llgl_draw\",\"type\":\"GLOBAL\",\"library_path\":\"./libVkLayer_gfx_llgl_draw.so\",\"api_version\":\"1.3.290\",\"implementation_version\":\"1\",\"description\":\"Own Linux pass-through draw observer\",\"functions\":{\"vkNegotiateLoaderLayerInterfaceVersion\":\"gfxDrawNegotiate\"}}}\n")
add_executable(linux_llgl_allocation "${CMAKE_CURRENT_BINARY_DIR}/main.cpp")
add_dependencies(linux_llgl_allocation llgl_draw_observer)
target_compile_features(linux_llgl_allocation PRIVATE cxx_std_23)
target_compile_options(linux_llgl_allocation PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(linux_llgl_allocation PRIVATE GFX_LLGL_PIN="${pin}"
  GFX_LLGL_ALLOCATION_FINGERPRINT="${fingerprint}" GFX_LLGL_PROFILE="llgl-fixed-pin-transfer-src-v1")
target_include_directories(linux_llgl_allocation PRIVATE "${root}/benchmarks/common" "${root}/benchmarks/llgl_vulkan_draw" "${root}/benchmarks/vulkan_allocation")
target_include_directories(linux_llgl_allocation SYSTEM PRIVATE "${root}/.deps/LLGL/include" "${root}/.deps/Vulkan-Headers/include")
target_link_libraries(linux_llgl_allocation PRIVATE "${LLGL_PROBE_BUILD}/lib/libLLGL.so" "${Vulkan_LIBRARY}" OpenSSL::Crypto ${CMAKE_DL_LIBS})

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${root}/tools/linux_llgl_workload_mode.py")
