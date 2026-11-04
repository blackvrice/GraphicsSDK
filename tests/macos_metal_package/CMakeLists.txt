cmake_minimum_required(VERSION 3.25)
project(RelocatedMetalConsumer LANGUAGES CXX)

find_package(GraphicsSDK CONFIG REQUIRED
  PATHS "${PACKAGE_ROOT}/lib/cmake/GraphicsSDK" NO_DEFAULT_PATH)
add_executable(package_consumer Consumer.cpp)
set_target_properties(package_consumer PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(package_consumer PRIVATE GraphicsSDK::Core)
target_link_options(package_consumer PRIVATE "LINKER:-map,${CMAKE_CURRENT_BINARY_DIR}/consumer.map")
