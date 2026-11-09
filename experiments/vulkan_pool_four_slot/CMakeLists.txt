cmake_minimum_required(VERSION 3.25)
project(VulkanPoolFourSlotContracts LANGUAGES CXX)

set(POOL_SOURCE "" CACHE PATH "Generated four-slot source tree")
file(READ "${POOL_SOURCE}/candidate-receipt.json" receipt)
string(JSON policy GET "${receipt}" policy)
string(JSON fingerprint GET "${receipt}" candidate_SDK_source_fingerprint)
if(NOT policy STREQUAL "four-slot")
  message(FATAL_ERROR "Expected an isolated four-slot source tree")
endif()

find_package(GraphicsSDK CONFIG REQUIRED)
add_executable(four_slot_contracts RangeTests.cpp)
target_include_directories(four_slot_contracts PRIVATE "${POOL_SOURCE}/src/backends/vulkan")
target_link_libraries(four_slot_contracts PRIVATE GraphicsSDK::Core)

enable_testing()
add_test(NAME four_slot_contracts COMMAND four_slot_contracts "${fingerprint}")
