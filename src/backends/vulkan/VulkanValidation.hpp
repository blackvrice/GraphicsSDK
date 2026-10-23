#pragma once
#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <vulkan/vulkan_core.h>
#include <atomic>
#include <cstdint>

namespace gfx::vulkan {
struct ValidationCounter { std::atomic<std::uint64_t> errors{}; };
inline VKAPI_ATTR VkBool32 VKAPI_CALL validationMessage(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT, const VkDebugUtilsMessengerCallbackDataEXT*, void* user) {
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        static_cast<ValidationCounter*>(user)->errors.fetch_add(1, std::memory_order_relaxed);
    return VK_FALSE;
}
// Self-referential pNext/value pointers remain valid through vkCreateInstance.
struct ValidationConfiguration {
    inline static constexpr const char* layer = "VK_LAYER_KHRONOS_validation";
    inline static constexpr const char* extensions[] = {VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
        VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME, VK_EXT_LAYER_SETTINGS_EXTENSION_NAME};
    VkDebugUtilsMessengerCreateInfoEXT messenger{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
    VkValidationFeatureEnableEXT synchronization{VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT};
    VkValidationFeaturesEXT features{VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT};
    std::uint32_t duplicateLimit{};
    const char* emptyFilter = "";
    VkBool32 submitValidation{VK_TRUE};
    VkLayerSettingEXT settings[3]{};
    VkLayerSettingsCreateInfoEXT layerSettings{VK_STRUCTURE_TYPE_LAYER_SETTINGS_CREATE_INFO_EXT};
    explicit ValidationConfiguration(ValidationCounter& counter) {
        messenger.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        messenger.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        messenger.pfnUserCallback = validationMessage; messenger.pUserData = &counter;
        features.pNext = &messenger; features.enabledValidationFeatureCount = 1; features.pEnabledValidationFeatures = &synchronization;
        settings[0] = {layer, "duplicate_message_limit", VK_LAYER_SETTING_TYPE_UINT32_EXT, 1, &duplicateLimit};
        settings[1] = {layer, "message_id_filter", VK_LAYER_SETTING_TYPE_STRING_EXT, 1, &emptyFilter};
        settings[2] = {layer, "syncval_submit_time_validation", VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1, &submitValidation};
        layerSettings.pNext = &features; layerSettings.settingCount = 3; layerSettings.pSettings = settings;
    }
    ValidationConfiguration(const ValidationConfiguration&) = delete;
    ValidationConfiguration& operator=(const ValidationConfiguration&) = delete;
};
}
