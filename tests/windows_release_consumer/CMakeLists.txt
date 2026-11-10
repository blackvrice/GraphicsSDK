cmake_minimum_required(VERSION 3.25)
project(GraphicsSDKReleaseConsumer LANGUAGES CXX)

find_package(GraphicsSDK ${PACKAGE_VERSION} EXACT CONFIG REQUIRED
  PATHS "${PACKAGE_ROOT}" NO_DEFAULT_PATH)
get_target_property(archive GraphicsSDK::Core IMPORTED_LOCATION_RELEASE)
if(NOT archive STREQUAL "${PACKAGE_ROOT}/lib/GraphicsSDKCore.lib" OR NOT EXISTS "${archive}")
  message(FATAL_ERROR "Expected the relocated Release SDK archive")
endif()
if(NOT MSVC OR NOT MSVC_CXX_ARCHITECTURE_ID STREQUAL "x64")
  message(FATAL_ERROR "Expected an MSVC x64 consumer")
endif()

add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE GraphicsSDK::Core)
