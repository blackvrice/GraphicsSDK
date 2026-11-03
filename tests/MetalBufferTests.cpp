#include <graphics/Graphics.hpp>
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace {
unsigned assertions{}, fullCases{}, partialCases{};
std::uint64_t verifiedBytes{};
std::filesystem::path output;
void check(bool value, const char* message) { ++assertions; if (!value) throw std::runtime_error(message); }
template<class T> T take(gfx::Result<T> r) { if (!r) throw std::runtime_error(r.error().message+" native="+std::to_string(r.error().nativeCode)); return std::move(*r); }
void take(gfx::Result<void> r) { if (!r) throw std::runtime_error(r.error().message); }
template<class T> void reject(const gfx::Result<T>& r, gfx::ErrorCode code, const char* message) { check(!r && r.error().code==code,message); }
gfx::DeviceDesc desc() { gfx::DeviceDesc d; d.backend=gfx::Backend::Metal; return d; }
std::vector<std::byte> pattern(std::uint64_t size) {
    std::vector<std::byte> result(static_cast<std::size_t>(size));
    for (std::size_t i=0;i<result.size();++i) result[i]=std::byte((i*37+size*13+17)%251);
    return result;
}
void verify(std::string name, const std::vector<std::byte>& actual, const std::vector<std::byte>& expected) {
    check(actual==expected,"Every logical byte including copy guard must match"); verifiedBytes+=actual.size();
    if (!output.empty()) {
        std::ofstream file(output/(name+".bin"),std::ios::binary);
        file.write(reinterpret_cast<const char*>(actual.data()),static_cast<std::streamsize>(actual.size()));
        check(file.good(),"Save complete native readback payload");
    }
}
void empty(gfx::Device& d) {
    take(d.queue().waitIdle()); auto s=take(d.resourceStatistics());
    check(!s.trackedObjects && !s.uniqueResources && !s.resourceAllocationBytes && !s.pendingSubmissions && !s.pendingObjectReferences,"Public wrappers, native allocations and submissions fully reclaimed");
}
void full(gfx::Device& d,std::uint64_t size) {
    auto expected=pattern(size);
    auto up=take(d.createBuffer({size,gfx::Memory::Upload})), gpu=take(d.createBuffer({size})), back=take(d.createBuffer({size,gfx::Memory::Readback}));
    check(up->description().size==size && up->description().memory==gfx::Memory::Upload,"Immutable upload description");
    take(up->write(0,expected));
    const auto last=expected.back(); take(up->write(size-1,std::span<const std::byte>(&last,1)));
    auto c=take(d.createCommandList()); take(c->begin());
    take(c->transition(gpu,gfx::ResourceState::CopyDestination)); take(c->copyBuffer(up,gpu,size));
    take(c->transition(gpu,gfx::ResourceState::CopySource)); take(c->copyBuffer(gpu,back,size)); take(c->end());
    auto fence=take(d.queue().submit(*c));
    std::weak_ptr<gfx::Buffer> u=up,g=gpu; up.reset();gpu.reset();c.reset();
    check(u.expired() && g.expired(),"Native submission retention must not keep public wrappers alive");
    auto poll=d.queue().wait(fence,std::chrono::milliseconds(0));
    check(poll || poll.error().code==gfx::ErrorCode::Timeout,"Zero poll preserves pending transfer");
    take(d.queue().wait(fence));
    verify("full-"+std::to_string(size),take(back->read(0,size)),expected);
    take(back->read(size,0));
    if (size>4) check(take(back->read(1,3))==std::vector<std::byte>(expected.begin()+1,expected.begin()+4),"Unaligned CPU read range");
    back.reset();empty(d);++fullCases;
}
void partial(gfx::Device& d,std::uint64_t size) {
    const auto capacity=size+19;
    std::vector<std::byte> expected(static_cast<std::size_t>(capacity),std::byte{0xd7});auto source=pattern(size);
    auto init=take(d.createBuffer({capacity,gfx::Memory::Upload})),up=take(d.createBuffer({size,gfx::Memory::Upload}));
    auto gpu=take(d.createBuffer({capacity})),back=take(d.createBuffer({capacity,gfx::Memory::Readback}));
    take(init->write(0,expected));take(up->write(0,source));
    std::copy(source.begin(),source.end(),expected.begin());
    auto c=take(d.createCommandList());take(c->begin());
    take(c->transition(gpu,gfx::ResourceState::CopyDestination));take(c->copyBuffer(init,gpu,capacity));
    take(c->copyBuffer(up,gpu,size));take(c->transition(gpu,gfx::ResourceState::CopySource));take(c->copyBuffer(gpu,back,capacity));take(c->end());
    auto fence=take(d.queue().submit(*c));init.reset();up.reset();gpu.reset();c.reset();take(d.queue().wait(fence));
    verify("partial-"+std::to_string(size),take(back->read(0,capacity)),expected);
    back.reset();empty(d);++partialCases;
}
void contracts(gfx::Device& d) {
    auto other=take(gfx::createDevice(desc()));
    reject(d.createBuffer({0}),gfx::ErrorCode::InvalidArgument,"Zero allocation");
    reject(d.createBuffer({16,static_cast<gfx::Memory>(99)}),gfx::ErrorCode::InvalidArgument,"Invalid memory enum");
    reject(d.createBuffer({UINT64_MAX}),gfx::ErrorCode::Unsupported,"Device limit checked before native allocation");
    auto up=take(d.createBuffer({32,gfx::Memory::Upload})),gpu=take(d.createBuffer({32})),back=take(d.createBuffer({32,gfx::Memory::Readback}));
    auto foreign=take(other->createBuffer({32,gfx::Memory::Readback}));auto expected=pattern(32);take(up->write(0,expected));
    reject(up->read(0,1),gfx::ErrorCode::InvalidArgument,"Upload is not readback");
    reject(back->write(0,expected),gfx::ErrorCode::InvalidArgument,"Readback is not upload");
    reject(gpu->write(0,expected),gfx::ErrorCode::InvalidArgument,"Private memory cannot be host written");
    reject(gpu->read(0,1),gfx::ErrorCode::InvalidArgument,"Private memory cannot be host read");
    reject(up->write(UINT64_MAX,expected),gfx::ErrorCode::InvalidArgument,"Overflowing host write");
    reject(back->read(UINT64_MAX,1),gfx::ErrorCode::InvalidArgument,"Overflowing host read offset");
    reject(back->read(1,UINT64_MAX),gfx::ErrorCode::InvalidArgument,"Overflowing host read size");
    auto c=take(d.createCommandList());
    reject(c->copyBuffer(up,back,32),gfx::ErrorCode::InvalidState,"Copy requires recording");take(c->begin());
    reject(c->transition(gpu,gfx::ResourceState::ShaderRead),gfx::ErrorCode::Unsupported,"Non-transfer state unsupported");
    reject(c->transition(up,gfx::ResourceState::CopyDestination),gfx::ErrorCode::InvalidArgument,"Upload fixed source state");
    reject(c->transition(back,gfx::ResourceState::CopySource),gfx::ErrorCode::InvalidArgument,"Readback fixed destination state");
    reject(c->copyBuffer({},back,32),gfx::ErrorCode::InvalidArgument,"Null source");
    reject(c->copyBuffer(up,foreign,32),gfx::ErrorCode::WrongDevice,"Wrong generation buffer");
    reject(c->copyBuffer(up,back,0),gfx::ErrorCode::InvalidArgument,"Zero copy");
    reject(c->copyBuffer(up,back,33),gfx::ErrorCode::InvalidArgument,"Oversized copy");
    reject(c->copyBuffer(up,up,1),gfx::ErrorCode::InvalidArgument,"Aliased copy");
    reject(c->copyBuffer(up,gpu,32),gfx::ErrorCode::InvalidState,"Explicit private destination state required");
    take(c->copyBuffer(up,back,32));take(c->end());
    take(up->write(32,{}));reject(d.queue().submit(*c),gfx::ErrorCode::InvalidState,"Empty CPU write invalidates a prior observation");
    take(c->begin());take(c->copyBuffer(up,back,32));take(c->end());
    auto sibling=take(d.createCommandList());take(sibling->begin());take(sibling->copyBuffer(up,back,32));take(sibling->end());
    auto fence=take(d.queue().submit(*sibling));reject(d.queue().submit(*c),gfx::ErrorCode::InvalidState,"Submission invalidates a sibling observation");
    take(d.queue().wait(fence));check(take(back->read(0,32))==expected,"Rejected submissions preserve bytes and versions");
    take(c->begin());take(c->transition(gpu,gfx::ResourceState::CopyDestination));take(c->end());
    auto stale=take(d.createCommandList());take(stale->begin());take(stale->transition(gpu,gfx::ResourceState::CopySource));take(stale->end());
    fence=take(d.queue().submit(*c));reject(d.queue().submit(*stale),gfx::ErrorCode::InvalidState,"Stale transition rejected");take(d.queue().wait(fence));
    bool rejected{};
    std::thread worker([&] { auto a=up->write(0,expected);auto b=back->read(0,32);auto r=c->transition(gpu,gfx::ResourceState::Common);rejected=!a&&!b&&!r&&a.error().code==gfx::ErrorCode::InvalidState&&b.error().code==gfx::ErrorCode::InvalidState&&r.error().code==gfx::ErrorCode::InvalidState; });worker.join();
    check(rejected,"Host buffer and transfer thread guards");
    c.reset();sibling.reset();stale.reset();up.reset();gpu.reset();back.reset();foreign.reset();empty(d);
    // A rejected argument must acquire neither a native reference nor an observed version.
    up=take(d.createBuffer({32,gfx::Memory::Upload}));gpu=take(d.createBuffer({32}));c=take(d.createCommandList());take(c->begin());
    reject(c->copyBuffer(up,gpu,32),gfx::ErrorCode::InvalidState,"Rejected copy preserves recording and captures no arguments");
    reject(c->transition(up,gfx::ResourceState::Common),gfx::ErrorCode::InvalidArgument,"Rejected transition captures no arguments");
    take(up->write(0,expected));up.reset();gpu.reset();auto s=take(d.resourceStatistics());
    check(!s.trackedObjects&&!s.uniqueResources&&!s.resourceAllocationBytes,"Rejected operations retain no allocation");
    take(c->end());fence=take(d.queue().submit(*c));c.reset();take(d.queue().wait(fence));empty(d);
    up=take(d.createBuffer({32,gfx::Memory::Upload}));back=take(d.createBuffer({32,gfx::Memory::Readback}));take(up->write(0,expected));
    c=take(d.createCommandList());take(c->begin());take(c->copyBuffer(up,back,32));take(c->end());up.reset();back.reset();
    s=take(d.resourceStatistics());check(!s.trackedObjects&&s.uniqueResources==2&&s.resourceAllocationBytes>=64,"Unsubmitted recording owns allocations independently of wrappers");
    fence=take(d.queue().submit(*c));c.reset();take(d.queue().wait(fence));empty(d);
}
}
int main(int argc,char** argv) try {
    check(argc>=1&&argc<=3,"Optional source fingerprint and fresh payload directory");
    if(argc>=2)check(gfx::buildFingerprint()==argv[1],"Installed/source fingerprint mismatch");
    if(argc==3) { output=argv[2];check(std::filesystem::create_directory(output),"Fresh payload directory required"); }
    check(gfx::backendCompiled(gfx::Backend::Metal),"Native Metal required, no skip");auto d=take(gfx::createDevice(desc()));
    constexpr std::array<std::uint64_t,11> sizes{1,2,3,4,5,7,257,259,4096,65539,1048579};
    for(auto n:sizes)full(*d,n);
    for(auto n:sizes)partial(*d,n);
    contracts(*d);empty(*d);
    auto old=take(d->createBuffer({32,gfx::Memory::Upload}));auto oldCommands=take(d->createCommandList());take(oldCommands->begin());
    take(oldCommands->transition(old,gfx::ResourceState::CopySource));take(oldCommands->end());d.reset();
    check(old->description().size==32,"Retired handle preserves CPU description");
    reject(old->write(0,{}),gfx::ErrorCode::DeviceLost,"Retired upload native operation rejected");
    reject(old->read(0,0),gfx::ErrorCode::DeviceLost,"Retired read native operation rejected");
    reject(oldCommands->begin(),gfx::ErrorCode::DeviceLost,"Retired recording rejected");
    std::cout<<"{\"schema_version\":1,\"contract\":\"metal_buffer_transfer_v1\",\"SDK_source_fingerprint\":\""<<gfx::buildFingerprint()
        <<"\",\"full_cases\":"<<fullCases<<",\"partial_cases\":"<<partialCases<<",\"verified_bytes\":"<<verifiedBytes
        <<",\"assertions\":"<<assertions<<",\"buffer_transfer_executed\":true,\"internal_tail_dispatch_executed\":true,\"draw_executed\":false,"
        <<"\"native_validation_accepted\":false,\"physical_GPU_accepted\":false,\"performance_accepted\":false}\n";
    return 0;
} catch(const std::exception& e) { std::cerr<<"FAIL: "<<e.what()<<'\n';return 1; }
