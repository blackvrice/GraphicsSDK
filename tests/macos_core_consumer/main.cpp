#include <graphics/Graphics.hpp>
#include <iostream>
#include <stdexcept>
#include <string_view>

int main(int argc,char** argv)try{
    auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    check(argc==2,"Expected source fingerprint required");
    const auto fingerprint=gfx::buildFingerprint();
    check(fingerprint.size()==64&&fingerprint==argv[1],"Installed/source fingerprint differs");
    static_assert(sizeof(void*)==8);
    for(auto backend:{gfx::Backend::D3D12,gfx::Backend::Vulkan,gfx::Backend::Metal}){
        check(!gfx::backendCompiled(backend),"Unexpected GPU backend compiled");
        auto adapters=gfx::enumerateAdapters(backend);
        check(!adapters&&adapters.error().code==gfx::ErrorCode::Unsupported,"Unavailable adapter query must reject");
        gfx::DeviceDesc description{};description.backend=backend;
        auto device=gfx::createDevice(description);
        check(!device&&device.error().code==gfx::ErrorCode::Unsupported,"Unavailable device must reject");
    }
    gfx::VertexLayout vertices{{{0,12}},{{"POSITION",0,gfx::VertexFormat::Float3,0,0}}};
    check(bool(gfx::validateVertexLayout(vertices)),"Installed vertex layout failed");
    vertices.buffers[0].stride=1;
    check(!gfx::validateVertexLayout(vertices),"Invalid vertex stride accepted");
    gfx::BindingLayout bindings{{{gfx::BindingType::ConstantBuffer,0,1,2,gfx::ShaderVisibility::All}}, {}};
    check(bool(gfx::validateBindingLayout(bindings)),"Installed binding layout failed");
    gfx::RenderTargetLayout targets{{gfx::Format::RGBA8UNorm,gfx::Format::RGBA16Float},gfx::Format::D32Float};
    check(bool(gfx::validateRenderTargets(targets,{true,true,gfx::CompareOp::Less})),"Installed render target layout failed");
    std::cout<<"{\"schema_version\":1,\"contract\":\"installed_core_cpu_only_v1\",\"SDK_source_fingerprint\":\""<<fingerprint
        <<"\",\"pointer_bits\":64,\"D3D12_compiled\":false,\"Vulkan_compiled\":false,\"Metal_compiled\":false,\"GPU_executed\":false}\n";
    return 0;
}catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
