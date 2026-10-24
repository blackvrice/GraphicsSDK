#pragma once

namespace gfx::vulkan {
// Single-output creation only. Vulkan errors leave most outputs undefined.
// Batch pipeline creation has defined partial-success outputs and must keep its
// own cleanup. The caller must pass an empty destination for a new object.
template<class Handle, class Function, class... Args>
auto acquireOutput(Handle& destination, Function function, Args... args) {
    Handle value{};
    const auto result = function(args..., &value);
    if (result == decltype(result){}) destination = value;
    return result;
}
}
