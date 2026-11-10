cmake_minimum_required(VERSION 3.25)
project(GraphicsSDKLinuxReleaseConsumer LANGUAGES CXX)

find_package(GraphicsSDK ${PACKAGE_VERSION} EXACT CONFIG REQUIRED PATHS "${PACKAGE_ROOT}" NO_DEFAULT_PATH)
get_target_property(archive GraphicsSDK::Core IMPORTED_LOCATION_RELEASE)
cmake_path(IS_PREFIX PACKAGE_ROOT "${archive}" NORMALIZE relocated)
if(NOT relocated OR NOT EXISTS "${archive}")
  message(FATAL_ERROR "GraphicsSDK archive must come from the relocated package")
endif()

add_executable(cpu_consumer cpu.cpp)
add_executable(transfer_consumer transfer.cpp)
target_link_libraries(cpu_consumer PRIVATE GraphicsSDK::Core)
target_link_libraries(transfer_consumer PRIVATE GraphicsSDK::Core)
