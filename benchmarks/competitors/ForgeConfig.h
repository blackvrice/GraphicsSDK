#pragma once
// Use upstream's normal Release platform configuration, with application systems
// outside this headless graphics workload disabled for every Forge translation unit.
#include "../../.deps/TheForge/Common_3/Application/Config.h"
#undef ENABLE_FORGE_SCRIPTING
#undef ENABLE_FORGE_RELOAD_SHADER
#undef ENABLE_FORGE_UI
#undef ENABLE_FORGE_FONTS
#undef ENABLE_FORGE_INPUT
#undef ENABLE_FORGE_ANIMATION_DEBUG
#undef ENABLE_FORGE_MATERIALS
#undef ENABLE_ZIP_FILESYSTEM
#undef ENABLE_SCREENSHOT
#undef ENABLE_PROFILER
#undef ENABLE_THREAD_PERFORMANCE_STATS
#undef ENABLE_MESHOPTIMIZER

#ifdef GFX_FORGE_NO_NVAPI
// Select the upstream helper's existing no-NVAPI branch before its pragma-once
// include in Direct3D12.c. Only the helper sees _WINDOWS unset; graphics/platform
// configuration is already initialized and _WINDOWS is restored immediately.
// The legacy bundled NVAPI is excluded from compilation and linking entirely.
#include "../../.deps/TheForge/Common_3/Graphics/GraphicsConfig.h"
#pragma push_macro("_WINDOWS")
#undef _WINDOWS
#include "../../.deps/TheForge/Common_3/Graphics/ThirdParty/OpenSource/nvapi/NvApiHelper.h"
#pragma pop_macro("_WINDOWS")
#endif
