cmake_minimum_required(VERSION 3.25)
project(GraphicsSDKFourSlotRegression LANGUAGES CXX)
if(NOT WIN32 OR NOT MSVC)
  message(FATAL_ERROR "The existing native fault observer requires Windows x64 MSVC")
endif()

get_filename_component(root "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
set(GFX_INSTALLED_SDK "" CACHE PATH "Four-slot SDK install")
set(GFX_OVERLAY "" CACHE PATH "Generated churn sources")
find_package(GraphicsSDK CONFIG REQUIRED PATHS "${GFX_INSTALLED_SDK}" NO_DEFAULT_PATH)

set(churn "${root}/experiments/vulkan_pool_churn")
set(fault "${root}/experiments/vulkan_buffer_pool_fault")
set(env "${root}/benchmarks/common/Environment.cpp")
set(sources "${GFX_OVERLAY}/main.cpp" "${GFX_OVERLAY}/Model.hpp" "${GFX_OVERLAY}/ModelTests.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/CapFault.hpp" "${CMAKE_CURRENT_LIST_FILE}"
    "${churn}/Snapshot.hpp" "${churn}/Layer.cpp" "${fault}/Layer.cpp"
    "${fault}/Accounting.hpp" "${fault}/FaultPlan.hpp" "${fault}/Client.hpp"
    "${root}/benchmarks/common/Adapter.hpp" "${root}/benchmarks/vulkan_upload/Identity.hpp" "${env}")
set(identity "")
foreach(path IN LISTS sources)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${path}")
  file(SHA256 "${path}" sha)
  string(APPEND identity "${sha}\n")
endforeach()
string(SHA256 fingerprint "${identity}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/harness-fingerprint.txt" "${fingerprint}\n")

add_executable(vulkan_four_slot_churn "${GFX_OVERLAY}/main.cpp" "${env}")
target_link_libraries(vulkan_four_slot_churn PRIVATE GraphicsSDK::Core dxgi advapi32)
add_library(VkLayer_gfx_buffer_fault SHARED "${churn}/Layer.cpp")
add_executable(vulkan_four_slot_model "${GFX_OVERLAY}/ModelTests.cpp")
foreach(target vulkan_four_slot_churn VkLayer_gfx_buffer_fault vulkan_four_slot_model)
  target_compile_features(${target} PRIVATE cxx_std_23)
  target_include_directories(${target} PRIVATE "${churn}" "${fault}" "${CMAKE_CURRENT_SOURCE_DIR}"
      "${root}/benchmarks/common" "${root}/benchmarks/vulkan_upload" "${root}/.deps/Vulkan-Headers/include")
  target_compile_definitions(${target} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN GFX_TRACE_FINGERPRINT="${fingerprint}")
  target_compile_options(${target} PRIVATE /W4 /permissive- /EHsc /utf-8)
endforeach()
set(manifest "{\"file_format_version\":\"1.2.0\",\"layer\":{\"name\":\"VK_LAYER_GFX_buffer_fault\",\"type\":\"GLOBAL\",\"library_path\":\"./VkLayer_gfx_buffer_fault.dll\",\"api_version\":\"1.1.0\",\"implementation_version\":\"1\",\"functions\":{\"vkNegotiateLoaderLayerInterfaceVersion\":\"gfxNegotiateLayer\"},\"description\":\"GraphicsSDK read-only binding and guarded regression tests\"}}")
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/VkLayer_gfx_buffer_fault.json" CONTENT "${manifest}\n")
