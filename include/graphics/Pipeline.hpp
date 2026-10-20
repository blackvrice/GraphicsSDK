#pragma once
#include "Shader.hpp"
#include <memory>

namespace gfx {
struct GraphicsPipelineDesc { ShaderBinary vertex, fragment; Format colorFormat{Format::RGBA8UNorm}; bool sampledTexture{}; bool alphaBlend{}; };
class Pipeline { public: virtual ~Pipeline() = default; };
using PipelinePtr = std::shared_ptr<Pipeline>;
}
