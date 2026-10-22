// Public-SDK-only frame slot. Wait for its fence before overwriting Upload data.
// This is a stress/example helper, not an SDK allocator or a hidden resource cache.
struct ReusableFrame {
    gfx::TexturePtr a,b,z,image;
    gfx::BufferPtr imageUpload,constants,input,device;
    Frame result;
    std::unique_ptr<gfx::CommandList> commands;
    explicit ReusableFrame(gfx::Device& d){
        a=take(d.createTexture({side,side,gfx::Format::RGBA8UNorm,true}));b=take(d.createTexture({side,side,gfx::Format::RGBA8UNorm,true}));z=take(d.createTexture({side,side,gfx::Format::D32Float,false,true}));image=take(d.createTexture({side,side}));const auto f=a->footprint();
        imageUpload=take(d.createBuffer({f.size,gfx::Memory::Upload}));std::vector<std::byte> white(f.size,std::byte{255});take(imageUpload->write(0,white));constants=take(d.createBuffer({512,gfx::Memory::Upload}));input=take(d.createBuffer({copyBytes,gfx::Memory::Upload}));device=take(d.createBuffer({copyBytes}));
        result={take(d.createBuffer({f.size,gfx::Memory::Readback})),take(d.createBuffer({f.size,gfx::Memory::Readback})),take(d.createBuffer({f.size,gfx::Memory::Readback})),take(d.createBuffer({copyBytes,gfx::Memory::Readback})),f,{},0};commands=take(d.createCommandList());
    }
    void submit(gfx::Device& d,const gfx::PipelinePtr& pipeline,unsigned index){
        const std::array<float,4> color{index%2?0.f:1.f,index%2?1.f:0.f,0,1};take(constants->write(0,std::as_bytes(std::span{color})));take(constants->write(256,std::as_bytes(std::span{color})));std::vector<std::byte> pattern(copyBytes,std::byte(index%251));take(input->write(0,pattern));
        auto& c=*commands;take(c.begin());take(c.transition(image,S::CopyDestination));take(c.copyBufferToTexture(imageUpload,image));take(c.transition(image,S::ShaderRead));take(c.transition(constants,S::ConstantBuffer));take(c.transition(a,S::RenderTarget));take(c.transition(b,S::RenderTarget));take(c.transition(z,S::DepthWrite));
        take(c.beginRenderPass({{{a},{b}},{z}}));take(c.setPipeline(pipeline));take(c.bindResources({{{0,0,image}},{{1,0,constants,0,256}}}));take(c.setScissor(0,0,side/2,side));take(c.draw(3));take(c.bindResources({{{0,0,image}},{{1,0,constants,256,256}}}));take(c.setScissor(side/2,0,side/2,side));take(c.draw(3));take(c.endRenderPass());
        take(c.transition(a,S::CopySource));take(c.copyTextureToBuffer(a,result.a));take(c.transition(b,S::CopySource));take(c.copyTextureToBuffer(b,result.b));take(c.transition(z,S::CopySource));take(c.copyTextureToBuffer(z,result.z));take(c.transition(device,S::CopyDestination));take(c.copyBuffer(input,device,copyBytes));take(c.transition(device,S::CopySource));take(c.copyBuffer(device,result.copy,copyBytes));take(c.writeTimestamp(0));take(c.writeTimestamp(1));take(c.resolveTimestamps());take(c.end());result.index=index;result.fence=take(d.queue().submit(c));
    }
};
