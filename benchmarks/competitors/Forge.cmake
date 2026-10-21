set(forge_source "${CMAKE_CURRENT_SOURCE_DIR}/../.deps/TheForge")
set(forge_common "${forge_source}/Common_3")
if(GFX_BENCH_WITH_BGFX OR GFX_BENCH_WITH_DILIGENT)
  message(FATAL_ERROR "The Forge is a separately reviewed optional benchmark profile")
endif()
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/competitors/forge-build.lock.json" forge_lock)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/competitors/forge-build.lock.json")
string(JSON forge_sha GET "${forge_lock}" dependencies the_forge commit)
execute_process(COMMAND git -C "${forge_source}" rev-parse HEAD OUTPUT_VARIABLE actual_sha
  OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND git -C "${forge_source}" status --porcelain --untracked-files=no OUTPUT_VARIABLE dirty
  OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
if(NOT actual_sha STREQUAL forge_sha OR dirty)
  message(FATAL_ERROR "Unpinned or modified The Forge checkout")
endif()
set(forge_identity "the_forge|${forge_sha}\n")
file(GLOB_RECURSE forge_bench_sources CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/*.h" "${CMAKE_CURRENT_SOURCE_DIR}/*.cmake")
list(APPEND forge_bench_sources "${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt")
list(SORT forge_bench_sources)
foreach(source IN LISTS forge_bench_sources)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${source}")
  file(SHA256 "${source}" digest)
  file(RELATIVE_PATH relative "${CMAKE_CURRENT_SOURCE_DIR}/.." "${source}")
  string(APPEND forge_identity "${relative}|${digest}\n")
endforeach()
string(SHA256 forge_fingerprint "${forge_identity}")
# Supported external application-config hook. Keep the full upstream defaults,
# expanding only the compiler whitelist to the exact installed MSVC version.
file(READ "${forge_common}/Application/Config.h" forge_config)
string(FIND "${forge_config}" "#pragma once" notice_end)
string(SUBSTRING "${forge_config}" 0 ${notice_end} forge_notice)
string(FIND "${forge_config}" "#include <stdint.h>" config_start)
string(SUBSTRING "${forge_config}" ${config_start} -1 forge_config)
string(REPLACE "#endif\n\n#define VR_API_OPENXR" "\n#define VR_API_OPENXR" forge_config "${forge_config}")
string(REPLACE "#if (_MSC_VER == 1929)" "#if (_MSC_VER == 1929) || (_MSC_FULL_VER == 194435228)" forge_config "${forge_config}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/ForgeApplicationConfig.h" "${forge_notice}// Derived external application config; MSVC whitelist extended by GraphicsSDK.\n#pragma once\n${forge_config}")
add_library(bench_forge STATIC
  competitors/ForgeBuild.cpp
  "${forge_common}/Graphics/Direct3D12/Direct3D12.c"
  "${forge_common}/Graphics/Direct3D12/Direct3D12_cxx.cpp"
  "${forge_common}/Graphics/Direct3D12/Direct3D12Hooks.c"
  "${forge_common}/Graphics/Direct3D12/Direct3D12Raytracing.c"
  "${forge_common}/Graphics/GraphicsConfig.cpp"
  "${forge_common}/Resources/ResourceLoader/ResourceLoader.cpp"
  "${forge_common}/Utilities/MemoryTracking/MemoryTracking.c"
  "${forge_common}/Utilities/Math/Algorithms.c"
  "${forge_common}/Utilities/Math/StbDs.c"
  "${forge_common}/Utilities/ThirdParty/OpenSource/bstrlib/bstrlib.c"
  "${forge_common}/Utilities/ThirdParty/OpenSource/lz4/lz4.c"
  "${forge_common}/Utilities/ThirdParty/OpenSource/zstd/common/debug.c"
  "${forge_common}/Utilities/ThirdParty/OpenSource/zstd/common/entropy_common.c"
  "${forge_common}/Utilities/ThirdParty/OpenSource/zstd/common/error_private.c"
  "${forge_common}/Utilities/ThirdParty/OpenSource/zstd/common/fse_decompress.c"
  "${forge_common}/Utilities/ThirdParty/OpenSource/zstd/common/pool.c"
  "${forge_common}/Utilities/ThirdParty/OpenSource/zstd/common/threading.c"
  "${forge_common}/Utilities/ThirdParty/OpenSource/zstd/common/xxhash.c"
  "${forge_common}/Utilities/ThirdParty/OpenSource/zstd/common/zstd_common.c"
  "${forge_common}/Utilities/ThirdParty/OpenSource/zstd/decompress/huf_decompress.c"
  "${forge_common}/Utilities/ThirdParty/OpenSource/zstd/decompress/zstd_ddict.c"
  "${forge_common}/Utilities/ThirdParty/OpenSource/zstd/decompress/zstd_decompress.c"
  "${forge_common}/Utilities/ThirdParty/OpenSource/zstd/decompress/zstd_decompress_block.c"
  "${forge_common}/Utilities/FileSystem/FileSystem.c"
  "${forge_common}/Utilities/Log/Log.c"
  "${forge_common}/Utilities/Threading/ThreadSystem.c"
  "${forge_common}/Utilities/Timer.c"
  "${forge_common}/OS/Windows/WindowsFileSystem.cpp"
  "${forge_common}/OS/Windows/WindowsToolsFileSystem.cpp"
  "${forge_common}/OS/Windows/WindowsLog.c"
  "${forge_common}/OS/Windows/WindowsThread.c"
  "${forge_common}/OS/Windows/WindowsTime.c")
# Upstream supplies GUID variables missing from its older Windows SDK. Modern
# dxguid.lib also defines three of them. Rename only those local variables;
# their IID values and every call remain the upstream ones (no /FORCE:MULTIPLE).
set_source_files_properties("${forge_common}/Graphics/Direct3D12/Direct3D12.c" PROPERTIES COMPILE_DEFINITIONS
  "GFX_FORGE_NO_NVAPI=1;IID_ID3D12StateObjectProperties1=IID_ForgeStateObjectProperties1;IID_ID3D12WorkGraphProperties=IID_ForgeWorkGraphProperties;IID_ID3D12GraphicsCommandList10=IID_ForgeGraphicsCommandList10")
target_include_directories(bench_forge PUBLIC "${forge_source}")
target_include_directories(bench_forge PUBLIC "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_definitions(bench_forge PUBLIC _WINDOWS EXTERNAL_CONFIG_FILEPATH="ForgeApplicationConfig.h" PRIVATE _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_DEPRECATE)
target_compile_definitions(bench_forge PRIVATE GFX_FORGE_BUILD_SHA256="${forge_fingerprint}")
target_compile_options(bench_forge PUBLIC "/FI${CMAKE_CURRENT_SOURCE_DIR}/competitors/ForgeConfig.h")
target_compile_options(bench_forge PRIVATE /W3 /utf-8 /bigobj)
set_property(TARGET bench_forge PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
set_property(TARGET bench_forge PROPERTY INTERPROCEDURAL_OPTIMIZATION FALSE)
set(forge_third "${forge_common}/Graphics/ThirdParty/OpenSource")
target_link_libraries(bench_forge PUBLIC
  "${forge_third}/ags/ags_lib/lib/amd_ags_x64.lib"
  "${forge_third}/DirectXShaderCompiler/lib/x64/dxcompiler.lib"
  dxgi dxguid d3d12 shell32 shlwapi dbghelp)
target_link_libraries(gfx_bench PRIVATE bench_forge)
target_sources(gfx_bench PRIVATE competitors/Forge.cpp)
target_compile_definitions(gfx_bench PRIVATE GFX_BENCH_HAS_FORGE=1)
foreach(runtime
  "${forge_third}/ags/ags_lib/lib/amd_ags_x64.dll"
  "${forge_third}/DirectXShaderCompiler/bin/x64/dxcompiler.dll")
  add_custom_command(TARGET gfx_bench POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different "${runtime}" "$<TARGET_FILE_DIR:gfx_bench>")
endforeach()
add_custom_command(TARGET gfx_bench POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:gfx_bench>/forge-config"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${forge_source}/Examples_3/Unit_Tests/src/01_Transformations/GPUCfg/gpu.cfg" "$<TARGET_FILE_DIR:gfx_bench>/forge-config/gpu.cfg"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${forge_common}/OS/Windows/pc_gpu.data" "$<TARGET_FILE_DIR:gfx_bench>/forge-config/gpu.data")
enable_testing()
foreach(scenario buffers textures draw instanced upload1080 upload4k upload4k16f pipeline)
  add_test(NAME forge_${scenario} COMMAND gfx_bench the_forge ${scenario} 1 1 2 1)
  set_tests_properties(forge_${scenario} PROPERTIES RUN_SERIAL TRUE TIMEOUT 120)
endforeach()
foreach(scenario draw instanced)
  add_test(NAME forge_${scenario}_10000 COMMAND gfx_bench the_forge ${scenario} 10000 1 2 1)
  set_tests_properties(forge_${scenario}_10000 PROPERTIES RUN_SERIAL TRUE TIMEOUT 120)
endforeach()
