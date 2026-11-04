cmake_minimum_required(VERSION 3.25)
project(MetalPresentationConsumer LANGUAGES CXX OBJCXX)

find_package(GraphicsSDK CONFIG REQUIRED)
find_library(APPKIT_FRAMEWORK AppKit REQUIRED)
add_executable(metal_presentation Presentation.mm)
target_link_libraries(metal_presentation PRIVATE GraphicsSDK::Core ${APPKIT_FRAMEWORK})
target_compile_options(metal_presentation PRIVATE -fobjc-arc)
set_target_properties(metal_presentation PROPERTIES
    OBJCXX_STANDARD 23 OBJCXX_STANDARD_REQUIRED YES OBJCXX_EXTENSIONS OFF)
