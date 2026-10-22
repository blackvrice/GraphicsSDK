// A single-threaded, one-frame-in-flight public SDK consumer. No native GPU API.
#include <graphics/Graphics.hpp>
#include "PresentationPolicy.hpp"
#include <windows.h>
#include <array>
#include <charconv>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {
struct GpuFailure : std::runtime_error {
    gfx::Error error;
    explicit GpuFailure(gfx::Error e) : std::runtime_error(e.message), error(std::move(e)) {}
};
template<class T> T take(gfx::Result<T> r) {
    if (!r) throw GpuFailure(r.error());
    return std::move(*r);
}
void take(gfx::Result<void> r) { if (!r) throw GpuFailure(r.error()); }
void check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
void win32(BOOL ok, const char* message) { check(ok != FALSE, message); }

class MessageWaiter {
    HANDLE timer{CreateWaitableTimerExW(nullptr,nullptr,CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,TIMER_MODIFY_STATE|SYNCHRONIZE)};
public:
    MessageWaiter() { check(timer!=nullptr,"High-resolution waitable timer unavailable (Windows 10 1803+ required)"); }
    MessageWaiter(const MessageWaiter&)=delete;
    MessageWaiter& operator=(const MessageWaiter&)=delete;
    ~MessageWaiter() { if (timer) { CancelWaitableTimer(timer); CloseHandle(timer); } }
    void timed(std::int64_t ticks) {
        LARGE_INTEGER due{}; due.QuadPart=-ticks;
        win32(SetWaitableTimer(timer,&due,0,nullptr,nullptr,FALSE),"Arm frame timer failed");
        const auto result=MsgWaitForMultipleObjectsEx(1,&timer,INFINITE,QS_ALLINPUT,MWMO_INPUTAVAILABLE);
        // An input wake must not leave an active timer behind while minimized.
        const auto cancelled=CancelWaitableTimer(timer);
        check(result!=WAIT_FAILED,"Frame timer/message wait failed");
        win32(cancelled,"Cancel frame timer failed");
    }
    static void suspended() {
        check(MsgWaitForMultipleObjectsEx(0,nullptr,INFINITE,QS_ALLINPUT,MWMO_INPUTAVAILABLE)!=WAIT_FAILED,
              "Suspended window message wait failed");
    }
};

struct Options {
    bool selfTest{}, validation{}, warp{}, vsync{true};
    unsigned frames{};
    unsigned fps{60};
    std::string output;
};
Options parse(int argc, char** argv) {
    Options o;
    bool modeSet=false, fpsSet=false;
    for (int i=1; i<argc; ++i) {
        const std::string_view arg=argv[i];
        if (arg=="--self-test") o.selfTest=true;
        else if (arg=="--no-vsync") o.vsync=false;
        else if (arg=="--mode" && i+1<argc && !modeSet) {
            const std::string_view mode=argv[++i]; modeSet=true;
            check(mode=="gpu" || mode=="validation" || mode=="warp", "Unknown mode");
            o.validation=mode=="validation"; o.warp=mode=="warp";
        } else if (arg=="--frames" && i+1<argc && !o.frames) {
            const std::string_view value=argv[++i];
            const auto result=std::from_chars(value.data(),value.data()+value.size(),o.frames);
            check(result.ec==std::errc{} && result.ptr==value.data()+value.size() && o.frames>0 && o.frames<=1000000,
                  "--frames requires an integer in 1..1000000");
        } else if (arg=="--fps" && i+1<argc && !fpsSet) {
            const std::string_view value=argv[++i]; fpsSet=true;
            const auto result=std::from_chars(value.data(),value.data()+value.size(),o.fps);
            check(result.ec==std::errc{} && result.ptr==value.data()+value.size() && o.fps>0 && o.fps<=240,
                  "--fps requires an integer in 1..240");
        } else if (arg=="--output" && i+1<argc && o.output.empty()) o.output=argv[++i];
        else throw std::runtime_error("Usage: gfx_presentation [--mode gpu|validation|warp] [--no-vsync] [--fps 1..240] [--frames N] [--self-test] [--output new.jsonl]");
    }
    check(!o.selfTest || !o.frames, "--self-test and --frames cannot be combined");
    return o;
}

struct Window {
    static constexpr const wchar_t* className=L"GraphicsSDKPresentation";
    HWND handle{};
    HINSTANCE instance{GetModuleHandleW(nullptr)};
    unsigned width{}, height{}, sizeEvents{}, dpiEvents{};
    bool minimized{}, closing{};
    DWORD eventError{};
    static LRESULT CALLBACK procedure(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) noexcept {
        auto* self=reinterpret_cast<Window*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
        if (message==WM_NCCREATE) {
            self=static_cast<Window*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
            SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));
        }
        if (!self) return DefWindowProcW(hwnd,message,wp,lp);
        switch (message) {
        case WM_SIZE: {
            // Read the full client rectangle instead of WM_SIZE's packed 16-bit extent.
            RECT r{};
            if (!GetClientRect(hwnd,&r)) self->eventError=GetLastError();
            self->minimized=wp==SIZE_MINIMIZED;
            self->width=self->minimized ? 0u : static_cast<unsigned>(r.right-r.left);
            self->height=self->minimized ? 0u : static_cast<unsigned>(r.bottom-r.top);
            ++self->sizeEvents;
            return 0;
        }
        case WM_DPICHANGED: {
            const auto& r=*reinterpret_cast<const RECT*>(lp);
            if (!SetWindowPos(hwnd,nullptr,r.left,r.top,r.right-r.left,r.bottom-r.top,SWP_NOZORDER|SWP_NOACTIVATE))
                self->eventError=GetLastError();
            ++self->dpiEvents;
            return 0;
        }
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: { PAINTSTRUCT p{}; BeginPaint(hwnd,&p); EndPaint(hwnd,&p); return 0; }
        // Keep HWND alive until the application has drained and released GPU objects.
        case WM_CLOSE: self->closing=true; return 0;
        case WM_KEYDOWN: if (wp==VK_ESCAPE) { self->closing=true; return 0; } break;
        case WM_DESTROY: self->closing=true; return 0;
        }
        return DefWindowProcW(hwnd,message,wp,lp);
    }
    Window() {
        WNDCLASSW c{}; c.lpfnWndProc=procedure; c.hInstance=instance; c.lpszClassName=className;
        c.hCursor=LoadCursorW(nullptr,IDC_ARROW);
        check(RegisterClassW(&c)!=0,"RegisterClass failed");
        handle=CreateWindowExW(0,className,L"GraphicsSDK — resize, minimize, restore; Esc to close",
            WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,680,480,nullptr,nullptr,instance,this);
        if (!handle) { UnregisterClassW(className,instance); throw std::runtime_error("CreateWindow failed"); }
        ShowWindow(handle,SW_SHOWNOACTIVATE);
    }
    ~Window() { if (handle) DestroyWindow(handle); UnregisterClassW(className,instance); }
    void pump() {
        MSG message{};
        while (PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
            if (message.message==WM_QUIT) closing=true;
            else { TranslateMessage(&message); DispatchMessageW(&message); }
        }
        check(!eventError,"A Win32 window event failed");
    }
    bool suspended() const { return minimized || width==0 || height==0 || !IsWindowVisible(handle); }
    void clientSize(unsigned w, unsigned h) {
        RECT r{0,0,static_cast<LONG>(w),static_cast<LONG>(h)};
        win32(AdjustWindowRectExForDpi(&r,WS_OVERLAPPEDWINDOW,FALSE,0,GetDpiForWindow(handle)),"AdjustWindowRect failed");
        win32(SetWindowPos(handle,nullptr,0,0,r.right-r.left,r.bottom-r.top,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE),"SetWindowPos failed");
    }
};

constexpr std::string_view shader=R"(
float4 VS(uint id : SV_VertexID) : SV_Position {
    float2 p[3]={float2(-1,-1),float2(-1,3),float2(3,-1)};
    return float4(p[id],0,1);
}
float4 PS() : SV_Target0 { return float4(1,.25,0,1); }
)";

struct Application {
    Window& window;
    Options options;
    std::ostream& log;
    std::unique_ptr<gfx::Device> device;
    gfx::ShaderBinary vertex,fragment;
    gfx::PipelinePtr pipeline;
    std::unique_ptr<gfx::SwapChain> swap;
    std::unique_ptr<gfx::CommandList> commands;
    gfx::BufferPtr readback;
    unsigned width{},height{},frames{},resizes{},recoveries{},images{},pausedChecks{};
    std::uint64_t pixels{},validationErrors{};
    std::int64_t removalReason{};
    Application(Window& w, Options o, std::ostream& output) : window(w),options(std::move(o)),log(output) {
        device=take(gfx::createDevice({gfx::Backend::D3D12,options.validation,options.warp,options.selfTest}));
        vertex=take(gfx::compileHlsl(shader,"VS",gfx::ShaderStage::Vertex));
        fragment=take(gfx::compileHlsl(shader,"PS",gfx::ShaderStage::Fragment));
        check(!options.selfTest || device->caps().deviceLossInjection,"Native RemoveDevice unavailable: lifecycle test cannot pass");
        log << "{\"event\":\"device\",\"adapter\":" << std::quoted(device->caps().adapterName)
            << ",\"warp\":" << options.warp << ",\"gbv\":" << options.validation << ",\"vsync\":" << options.vsync << "}\n";
    }
    ~Application() {
        if (device) { auto ignored=device->queue().waitIdle(); (void)ignored; }
        // Members are released before Window, even during exception unwinding.
    }
    void checkValidation() {
        if (options.validation) {
            const auto errors=take(device->validationErrorCount());
            validationErrors+=errors;
            check(errors==0,"Native ERROR/CORRUPTION received");
        }
    }
    void releaseGraphics() { commands.reset(); readback.reset(); pipeline.reset(); swap.reset(); width=height=0; }
    void resize() {
        if (swap && width==window.width && height==window.height) return;
        // Previous frames have completed. Drop the recorded command's native buffer references,
        // as well as every acquired wrapper, before ResizeBuffers. Never resize in WndProc.
        commands.reset(); readback.reset();
        if (!pipeline) pipeline=take(device->createGraphicsPipeline({vertex,fragment}));
        if (swap) { take(swap->resize(window.width,window.height)); ++resizes; }
        else swap=take(device->createSwapChain({gfx::SurfaceKind::Win32,window.handle},
                                             {window.width,window.height,2,options.vsync}));
        width=window.width; height=window.height;
        commands=take(device->createCommandList());
        log << "{\"event\":\"extent\",\"width\":" << width << ",\"height\":" << height << "}\n";
    }
    bool step(bool verify=false) {
        if (window.closing || window.suspended()) return false;
        try {
            resize();
            auto back=take(swap->acquire());
            check(back->description().width==width && back->description().height==height,"Backbuffer extent is stale");
            const auto footprint=back->footprint();
            if (verify && !readback) readback=take(device->createBuffer({footprint.size,gfx::Memory::Readback}));
            const auto rect=*presentation::rectangle(width,height,frames);
            const auto [x,y,w,h]=rect;
            take(commands->begin());
            take(commands->transition(back,gfx::ResourceState::RenderTarget));
            take(commands->beginRenderPass(back,{0,0,.25f,1}));
            take(commands->setPipeline(pipeline));
            take(commands->setScissor(x,y,w,h)); take(commands->draw(3)); take(commands->endRenderPass());
            if (verify) {
                take(commands->transition(back,gfx::ResourceState::CopySource));
                take(commands->copyTextureToBuffer(back,readback));
            }
            take(commands->transition(back,gfx::ResourceState::Present)); take(commands->end());
            const auto fence=take(device->queue().submit(*commands));
            take(swap->present());
            // This example prioritizes a clear ownership contract over throughput.
            take(device->queue().wait(fence));
            if (verify) {
                const auto bytes=take(readback->read(0,footprint.size));
                for (unsigned py=0; py<height; ++py) for (unsigned px=0; px<width; ++px) {
                    const bool inside=px>=x && px<x+w && py>=y && py<y+h;
                    const std::array<unsigned char,4> expected=inside ? std::array<unsigned char,4>{255,64,0,255}
                                                                 : std::array<unsigned char,4>{0,0,64,255};
                    for (unsigned c=0; c<4; ++c)
                        check(std::to_integer<unsigned char>(bytes[static_cast<std::size_t>(py)*footprint.rowPitch+px*4+c])==expected[c],
                              "Full backbuffer reference mismatch");
                }
                ++images; pixels+=static_cast<std::uint64_t>(width)*height;
                log << "{\"event\":\"image\",\"frame\":" << frames << ",\"width\":" << width
                    << ",\"height\":" << height << ",\"row_pitch\":" << footprint.rowPitch << "}\n";
            }
            ++frames;
            return true;
        } catch (const GpuFailure& failure) {
            if (failure.error.code!=gfx::ErrorCode::DeviceLost) throw;
            removalReason=failure.error.nativeCode;
            log << "{\"event\":\"observed_loss\",\"native_reason\":" << removalReason << "}\n";
            releaseGraphics();
            device=take(device->recover());
            ++recoveries;
            // CPU shaders survive. All GPU objects are rebuilt on the next active frame.
            return false;
        }
    }
    void rendered() { window.pump(); check(step(true),"Expected an active rendered frame"); }
    void shutdown() {
        take(device->queue().waitIdle()); checkValidation();
        releaseGraphics(); take(device->queue().waitIdle()); checkValidation();
        const auto stats=take(device->resourceStatistics());
        check(stats.trackedObjects==0 && stats.uniqueResources==0 && stats.pendingSubmissions==0 && stats.pendingObjectReferences==0,
              "SDK objects retained after shutdown");
        check(IsWindow(window.handle),"HWND destroyed before graphics shutdown");
        log << "{\"event\":\"shutdown\",\"sdk_objects\":0,\"sdk_resources\":0,\"pending_submissions\":0,\"hwnd_alive\":true}\n";
    }
    void selfTest() {
        check(AreDpiAwarenessContextsEqual(GetThreadDpiAwarenessContext(),DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2),
              "Per-monitor V2 manifest was not applied");
        rendered(); rendered();
        constexpr std::array<std::array<unsigned,2>,4> sizes{{{321,181},{641,359},{127,95},{803,451}}};
        for (unsigned cycle=0; cycle<8; ++cycle) {
            const auto size=sizes[cycle%4];
            window.clientSize(size[0],size[1]);
            rendered(); rendered();
            check(width==size[0] && height==size[1],"Client-pixel resize mismatch");
            const auto before=frames;
            ShowWindow(window.handle,SW_MINIMIZE); window.pump();
            check(IsIconic(window.handle) && window.suspended(),"Native minimize was not observed");
            for (unsigned i=0; i<3; ++i) { check(!step(true),"Rendered while minimized"); ++pausedChecks; }
            check(frames==before,"Minimized window submitted a frame");
            ShowWindow(window.handle,SW_RESTORE);
            rendered(); rendered();
            check(!IsIconic(window.handle) && width==size[0] && height==size[1],"Restored client extent mismatch");
            log << "{\"event\":\"minimize_restore\",\"cycle\":" << cycle << ",\"paused_submissions\":0}\n";
        }
        // Actual narrow client windows; the default nonclient frame imposes a minimum width.
        for (const auto& size : std::array<std::array<unsigned,2>,4>{{{127,1},{129,2},{127,3},{257,9}}}) {
            window.clientSize(size[0],size[1]); rendered(); rendered();
            check(width==size[0] && height==size[1],"Tiny client extent mismatch");
        }
        log << "{\"event\":\"tiny_extents\",\"cases\":4,\"full_images\":8}\n";
        // Several synchronous WM_SIZE events collapse into one GPU resize in step().
        const auto before=resizes;
        window.clientSize(333,199); window.clientSize(511,277); window.clientSize(639,357);
        rendered();
        check(resizes==before+1 && width==639 && height==357,"Resize events were not coalesced");
        log << "{\"event\":\"coalesced_resize\",\"events\":3,\"gpu_resizes\":1}\n";
        // Real native device removal is opt-in and belongs only to --self-test.
        checkValidation(); take(device->removeForTesting());
        check(!step(true) && recoveries==1 && removalReason!=0,"Loss was not recovered by the application loop");
        rendered(); rendered();
        check(device->caps().softwareAdapter==options.warp,"Recovery changed adapter policy");
        // A loss while minimized is discovered after restore; the paused loop does no GPU work.
        checkValidation();
        ShowWindow(window.handle,SW_MINIMIZE); window.pump();
        check(IsIconic(window.handle) && window.suspended(),"Second minimize was not observed");
        take(device->removeForTesting());
        check(!step(true) && recoveries==1,"Paused loop attempted loss recovery"); ++pausedChecks;
        ShowWindow(window.handle,SW_RESTORE); window.pump();
        check(!step(true) && recoveries==2,"Restore did not discover the paused loss");
        rendered(); rendered();
        log << "{\"event\":\"minimized_loss_restore\",\"paused_submissions\":0,\"recovery_cycles\":1}\n";
        const auto visibleFrames=frames;
        ShowWindow(window.handle,SW_HIDE); window.pump();
        check(!IsWindowVisible(window.handle) && !step(true) && frames==visibleFrames,"Hidden window rendered"); ++pausedChecks;
        ShowWindow(window.handle,SW_SHOWNOACTIVATE); rendered(); rendered();
        log << "{\"event\":\"hide_restore\",\"paused_submissions\":0}\n";
        // Close is processed before rendering, with HWND still valid for GPU teardown.
        const auto last=frames;
        win32(PostMessageW(window.handle,WM_CLOSE,0,0),"Post close failed"); window.pump();
        check(window.closing && !step(true) && frames==last,"Close submitted an extra frame");
        shutdown();
        log << "{\"event\":\"result\",\"functional_passed\":true,\"frames\":" << frames
            << ",\"full_images\":" << images << ",\"verified_pixels\":" << pixels
            << ",\"resizes\":" << resizes << ",\"minimize_restore_cycles\":9,\"paused_checks\":" << pausedChecks
            << ",\"recovery_cycles\":" << recoveries << ",\"validation_errors\":";
        if (options.validation) log << validationErrors; else log << "null";
        log << ",\"size_events\":" << window.sizeEvents << ",\"dpi_events\":" << window.dpiEvents
            << ",\"per_monitor_v2\":true,\"interactive_acceptance\":false,\"unexpected_loss_acceptance\":false}\n";
    }
};
}

int main(int argc, char** argv) try {
    const auto options=parse(argc,argv);
    std::ofstream file;
    if (!options.output.empty()) {
        // C++23 exclusive creation protects earlier observations, including failed runs.
        file.open(options.output,std::ios::out|std::ios::noreplace);
        check(file.is_open(),"Output must be a new writable file");
    }
    auto& log=options.output.empty() ? std::cout : file;
    log << std::boolalpha;
    Window window;
    Application application(window,options,log);
    if (options.selfTest) application.selfTest();
    else {
        using Clock=presentation::FramePacer::Clock;
        MessageWaiter waiter;
        const auto loopStart=Clock::now();
        presentation::FramePacer pacer(options.fps,loopStart);
        unsigned timedWaits{},suspendedWaits{};
        while (!window.closing && (!options.frames || application.frames<options.frames)) {
            window.pump();
            if (window.closing) break;
            if (window.suspended()) {
                ++suspendedWaits; MessageWaiter::suspended();
                pacer.reset(Clock::now());
                continue;
            }
            const auto now=Clock::now();
            if (pacer.due(now)) {
                pacer.started(now);
                if (application.step() && application.frames==1)
                    log << "{\"event\":\"loop_ready\",\"frames\":1}\n" << std::flush;
            }
            if (options.frames && application.frames>=options.frames) break;
            const auto remaining=pacer.waitTicks100ns(Clock::now());
            if (remaining) { ++timedWaits; waiter.timed(remaining); }
        }
        const auto elapsed=std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-loopStart).count();
        application.shutdown();
        log << "{\"event\":\"interactive_exit\",\"frames\":" << application.frames
            << ",\"target_fps\":" << options.fps << ",\"loop_elapsed_ns\":" << elapsed
            << ",\"timed_waits\":" << timedWaits << ",\"suspended_waits\":" << suspendedWaits
            << ",\"high_resolution_timer\":true,\"interactive_acceptance\":false}\n";
    }
    log.flush(); check(log.good(),"Output write failed");
    return 0;
} catch (const GpuFailure& e) {
    std::cerr << "FAIL: " << e.what() << " native_reason=" << e.error.nativeCode << '\n'; return 1;
} catch (const std::exception& e) {
    std::cerr << "FAIL: " << e.what() << '\n'; return 1;
}
