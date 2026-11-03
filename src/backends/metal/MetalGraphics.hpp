#pragma once
// Included in the private Metal implementation namespace after State and native types.
unsigned vertexBytes(VertexFormat f) {
    switch(f){case VertexFormat::Float:return 4;case VertexFormat::Float2:return 8;case VertexFormat::Float3:return 12;case VertexFormat::Float4:return 16;case VertexFormat::UNorm8x4:return 4;}
    return 0;
}
MTLVertexFormat vertexFormat(VertexFormat f) {
    switch(f){case VertexFormat::Float:return MTLVertexFormatFloat;case VertexFormat::Float2:return MTLVertexFormatFloat2;
    case VertexFormat::Float3:return MTLVertexFormatFloat3;case VertexFormat::Float4:return MTLVertexFormatFloat4;case VertexFormat::UNorm8x4:return MTLVertexFormatUChar4Normalized;}
    return MTLVertexFormatInvalid;
}
MTLDataType vertexType(VertexFormat f) {
    switch(f){case VertexFormat::Float:return MTLDataTypeFloat;case VertexFormat::Float2:return MTLDataTypeFloat2;
    case VertexFormat::Float3:return MTLDataTypeFloat3;case VertexFormat::Float4:case VertexFormat::UNorm8x4:return MTLDataTypeFloat4;}
    return MTLDataTypeNone;
}
Result<id<MTLFunction>> function(State& owner,const ShaderBinary& input,ShaderStage stage) {
    if(input.format!=ShaderFormat::MSL)return fail(ErrorCode::Unsupported,"Metal graphics requires reviewed MSL UTF-8 source");
    if(input.stage!=stage||input.bytes.empty())return fail(ErrorCode::InvalidArgument,"Metal shader stage/source invalid");
    std::string_view text(reinterpret_cast<const char*>(input.bytes.data()),input.bytes.size());
    if(text.find('\0')!=std::string_view::npos)return fail(ErrorCode::InvalidArgument,"Metal shader source contains NUL");
    NSString* source=[[NSString alloc] initWithBytes:text.data() length:text.size() encoding:NSUTF8StringEncoding];
    if(!source)return fail(ErrorCode::InvalidArgument,"Metal shader source is not UTF-8");
    NSError* error=nil;id<MTLLibrary> library=[owner.device newLibraryWithSource:source options:nil error:&error];
    if(!library){const char* message=error.localizedDescription.UTF8String;return fail(ErrorCode::BackendFailure,message?message:"MSL compilation failed",error.code);}
    id<MTLFunction> entry=[library newFunctionWithName:stage==ShaderStage::Vertex?@"gfx_vertex":@"gfx_fragment"];
    if(!entry||entry.functionType!=(stage==ShaderStage::Vertex?MTLFunctionTypeVertex:MTLFunctionTypeFragment))return fail(ErrorCode::InvalidArgument,"Missing/wrong Metal graphics entry point");
    return entry;
}
Result<std::shared_ptr<NativePipeline>> graphics(State& owner,const GraphicsPipelineDesc& desc) {
    if(auto ok=owner.check();!ok)return std::unexpected(ok.error());
    if(auto ok=validateVertexLayout(desc.vertexLayout);!ok)return std::unexpected(ok.error());
    auto p=std::make_shared<NativePipeline>();p->vertices=desc.vertexLayout;p->bindings=desc.bindingLayout;
    p->targets=desc.renderTargets.value_or(RenderTargetLayout{{desc.colorFormat},{}});p->depthState=desc.depthState;p->sampled=desc.sampledTexture;
    if(auto ok=validateRenderTargets(p->targets,p->depthState);!ok)return std::unexpected(ok.error());
    if(desc.sampledTexture){
        if(!p->bindings.entries.empty()||!p->bindings.samplers.empty())return fail(ErrorCode::InvalidArgument,"sampledTexture and custom Metal layout are exclusive");
        p->bindings.entries={{BindingType::Texture2D,0,1,0,ShaderVisibility::Fragment}};
        p->bindings.samplers={{0,0,ShaderVisibility::Fragment,SamplerFilter::Linear,SamplerAddress::Clamp}};
    }
    if(auto ok=validateBindingLayout(p->bindings);!ok)return std::unexpected(ok.error());
    for(const auto& e:p->bindings.entries)if(e.space)return fail(ErrorCode::Unsupported,"Metal binding profile requires space zero");
    for(const auto& s:p->bindings.samplers)if(s.space)return fail(ErrorCode::Unsupported,"Metal sampler profile requires space zero");
    for(const auto& stream:p->vertices.buffers)if(stream.slot>=15)return fail(ErrorCode::Unsupported,"Metal stream slots are 0..14 (native buffers 16..30)");
    p->minimumBytes.resize(p->bindings.entries.size());
    @autoreleasepool {
        auto vertex=function(owner,desc.vertex,ShaderStage::Vertex);if(!vertex)return std::unexpected(vertex.error());
        id<MTLFunction> fragment=nil;
        if(!desc.fragment.bytes.empty()){auto f=function(owner,desc.fragment,ShaderStage::Fragment);if(!f)return std::unexpected(f.error());fragment=*f;}
        else if(!p->targets.colors.empty())return fail(ErrorCode::InvalidArgument,"Metal color pipeline requires fragment shader");
        std::vector<bool> attributes(p->vertices.attributes.size());
        for(MTLVertexAttribute* a in (*vertex).vertexAttributes)if(a.active){
            if(a.attributeIndex>=attributes.size()||a.attributeType!=vertexType(p->vertices.attributes[a.attributeIndex].format))
                return fail(ErrorCode::InvalidArgument,"MSL vertex attribute interface/layout mismatch");
            attributes[a.attributeIndex]=true;
        }
        if(std::find(attributes.begin(),attributes.end(),false)!=attributes.end())return fail(ErrorCode::InvalidArgument,"Unused/missing MSL vertex layout attributes");
        MTLRenderPipelineDescriptor* descriptor=[[MTLRenderPipelineDescriptor alloc] init];
        descriptor.vertexFunction=*vertex;descriptor.fragmentFunction=fragment;
        if(!p->vertices.attributes.empty()){
            MTLVertexDescriptor* layout=[MTLVertexDescriptor vertexDescriptor];
            for(const auto& s:p->vertices.buffers){auto item=layout.layouts[16+s.slot];item.stride=s.stride;item.stepRate=1;item.stepFunction=s.rate==VertexInputRate::Instance?MTLVertexStepFunctionPerInstance:MTLVertexStepFunctionPerVertex;}
            for(std::size_t i=0;i<p->vertices.attributes.size();++i){const auto& a=p->vertices.attributes[i];auto item=layout.attributes[i];item.bufferIndex=16+a.slot;item.offset=a.offset;item.format=vertexFormat(a.format);}
            descriptor.vertexDescriptor=layout;
        }
        for(std::size_t i=0;i<p->targets.colors.size();++i){auto color=descriptor.colorAttachments[i];color.pixelFormat=pixelFormat(p->targets.colors[i]);
            color.blendingEnabled=desc.alphaBlend; color.sourceRGBBlendFactor=MTLBlendFactorSourceAlpha;color.destinationRGBBlendFactor=MTLBlendFactorOneMinusSourceAlpha;
            color.sourceAlphaBlendFactor=MTLBlendFactorOne;color.destinationAlphaBlendFactor=MTLBlendFactorOneMinusSourceAlpha;}
        if(p->targets.depth)descriptor.depthAttachmentPixelFormat=pixelFormat(*p->targets.depth);
        NSError* error=nil;MTLRenderPipelineReflection* reflection=nil;
        p->pipeline=[owner.device newRenderPipelineStateWithDescriptor:descriptor options:MTLPipelineOptionArgumentInfo|MTLPipelineOptionBufferTypeInfo reflection:&reflection error:&error];
        if(!p->pipeline||!reflection){const char* message=error.localizedDescription.UTF8String;return fail(ErrorCode::BackendFailure,message?message:"Metal render pipeline/reflection creation failed",error.code);}
        // Use the Metal 3-compatible argument reflection, including full buffer type metadata.
        std::array<NSArray<MTLArgument*>*,2> stages{reflection.vertexArguments,reflection.fragmentArguments};
        std::vector<std::array<unsigned,2>> coverage(p->bindings.entries.size());
        std::vector<std::array<bool,2>> samplers(p->bindings.samplers.size());
        for(unsigned stage=0;stage<2;++stage)for(MTLArgument* a in stages[stage])if(a.active){
            if(a.type==MTLArgumentTypeBuffer&&stage==0&&a.index>=16){
                if(std::none_of(p->vertices.buffers.begin(),p->vertices.buffers.end(),[&](const auto& s){return 16+s.slot==a.index;}))return fail(ErrorCode::Unsupported,"Undeclared Metal vertex buffer argument");
                continue;
            }
            if(a.type==MTLArgumentTypeSampler){
                bool found=false;
                for(std::size_t i=0;i<p->bindings.samplers.size();++i){const auto& s=p->bindings.samplers[i];if(s.slot==a.index&&visible(s.visibility,stage)){
                    if(samplers[i][stage]||a.arrayLength>1)return fail(ErrorCode::Unsupported,"Metal sampler arrays/duplicate arguments unsupported");samplers[i][stage]=true;found=true;break;}}
                if(!found)return fail(ErrorCode::InvalidArgument,"MSL sampler missing from layout/visibility");continue;
            }
            if(a.type!=MTLArgumentTypeTexture&&a.type!=MTLArgumentTypeBuffer)return fail(ErrorCode::Unsupported,"MSL resource type outside the graphics binding profile");
            if(a.access!=MTLArgumentAccessReadOnly)return fail(ErrorCode::Unsupported,"Writable MSL graphics resources unsupported");
            if(a.type==MTLArgumentTypeTexture&&(a.textureType!=MTLTextureType2D||a.isDepthTexture||a.textureDataType!=MTLDataTypeFloat))return fail(ErrorCode::Unsupported,"Only non-depth float texture2d arguments supported");
            if(a.type==MTLArgumentTypeBuffer){
                if(a.index>=14||a.bufferDataType!=MTLDataTypeStruct||!a.bufferDataSize||a.bufferDataSize>65536||a.bufferAlignment>256)
                    return fail(ErrorCode::Unsupported,"Metal constants require a bounded flat struct at buffer 0..13");
                for(MTLStructMember* member in a.bufferStructType.members)if(member.dataType==MTLDataTypePointer||member.dataType==MTLDataTypeTexture||member.dataType==MTLDataTypeSampler||member.dataType==MTLDataTypeArray||member.dataType==MTLDataTypeStruct)
                    return fail(ErrorCode::Unsupported,"Nested/pointer/argument-buffer constants unsupported");
            }
            const auto count=std::max<NSUInteger>(1,a.arrayLength);bool found=false;
            for(std::size_t i=0;i<p->bindings.entries.size();++i){const auto& e=p->bindings.entries[i];
                if(e.type!=(a.type==MTLArgumentTypeTexture?BindingType::Texture2D:BindingType::ConstantBuffer)||!visible(e.visibility,stage)||a.index<e.slot||a.index-e.slot+count>e.count)continue;
                coverage[i][stage]+=static_cast<unsigned>(count);if(a.type==MTLArgumentTypeBuffer)p->minimumBytes[i]=std::max<std::uint64_t>(p->minimumBytes[i],a.bufferDataSize);found=true;break;}
            if(!found)return fail(ErrorCode::InvalidArgument,"MSL resource range/type/visibility missing from layout");
        }
        for(std::size_t i=0;i<p->bindings.entries.size();++i)for(unsigned stage=0;stage<2;++stage)if(visible(p->bindings.entries[i].visibility,stage)&&coverage[i][stage]!=p->bindings.entries[i].count)
            return fail(ErrorCode::InvalidArgument,"MSL/layout resource interface coverage differs");
        for(std::size_t i=0;i<p->bindings.samplers.size();++i)for(unsigned stage=0;stage<2;++stage)if(visible(p->bindings.samplers[i].visibility,stage)&&!samplers[i][stage])return fail(ErrorCode::InvalidArgument,"Unused MSL static sampler layout entry");
        MTLDepthStencilDescriptor* depth=[[MTLDepthStencilDescriptor alloc] init];depth.depthWriteEnabled=p->depthState.writeEnabled;
        constexpr std::array compares{MTLCompareFunctionNever,MTLCompareFunctionLess,MTLCompareFunctionEqual,MTLCompareFunctionLessEqual,MTLCompareFunctionGreater,MTLCompareFunctionNotEqual,MTLCompareFunctionGreaterEqual,MTLCompareFunctionAlways};
        depth.depthCompareFunction=p->depthState.testEnabled?compares[unsigned(p->depthState.compare)]:MTLCompareFunctionAlways;
        p->depth=[owner.device newDepthStencilStateWithDescriptor:depth];if(!p->depth)return fail(ErrorCode::OutOfMemory,"Metal depth state creation failed");
        for(const auto& s:p->bindings.samplers){MTLSamplerDescriptor* d=[[MTLSamplerDescriptor alloc] init];d.minFilter=d.magFilter=s.filter==SamplerFilter::Point?MTLSamplerMinMagFilterNearest:MTLSamplerMinMagFilterLinear;
            d.sAddressMode=d.tAddressMode=d.rAddressMode=s.address==SamplerAddress::Clamp?MTLSamplerAddressModeClampToEdge:MTLSamplerAddressModeRepeat;
            id<MTLSamplerState> sampler=[owner.device newSamplerStateWithDescriptor:d];if(!sampler)return fail(ErrorCode::OutOfMemory,"Metal sampler creation failed");p->samplers.push_back(sampler);}
    }
    return p;
}
