#include "common/Adapter.hpp"
#include "common/GpuTimestamp.hpp"
#include <iostream>
#include <stdexcept>
#include <vector>

int main(int argc, char** argv) try {
#ifndef NDEBUG
    throw std::runtime_error("Performance benchmark requires Release with NDEBUG");
#endif
    if (argc != 7) throw std::runtime_error("Usage: gfx_bench <llgl|sdl|graphicssdk|diligent_core|bgfx|the_forge> <buffers|textures|draw|instanced|draw_gpu|instanced_gpu|upload1080|upload4k|upload4k16f|pipeline> <count> <warmup> <samples> <validation:0|1>");
    std::string sdk = argv[1], scenario = argv[2];
    auto number = [](const char* s, unsigned max) { size_t n{}; auto v = std::stoul(s, &n); if (s[n] || v > max) throw std::runtime_error("Invalid numeric argument"); return static_cast<unsigned>(v); };
    unsigned count = number(argv[3], 100000), warmup = number(argv[4], 10000), samples = number(argv[5], 100000);
    bool validation = number(argv[6], 1) != 0;
    bool gpuTimestamps=scenario=="draw_gpu" || scenario=="instanced_gpu";
    bool draw = scenario == "draw" || scenario == "instanced" || gpuTimestamps;
    bool pipeline = scenario == "pipeline";
    bool upload = scenario == "upload1080" || scenario == "upload4k" || scenario == "upload4k16f";
    if (!count || !samples || (!pipeline && !draw && !upload && scenario != "buffers" && scenario != "textures")) throw std::runtime_error("Invalid workload");
    if ((upload || pipeline) && count != 1) throw std::runtime_error("Upload/pipeline scenarios currently require count=1");
    std::unique_ptr<bench::Adapter> adapter;
    if (sdk == "llgl") adapter=bench::llgl(validation);
    else if (sdk == "sdl") adapter=bench::sdl(validation);
#ifdef GFX_BENCH_HAS_FORGE
    else if (sdk == "the_forge") adapter=bench::forgeAdapter(validation);
#endif
#ifdef GFX_BENCH_HAS_BGFX
    else if (sdk == "bgfx") adapter=bench::bgfxAdapter(validation);
#endif
#ifdef GFX_BENCH_HAS_DILIGENT
    else if (sdk == "diligent_core") adapter=bench::diligent(validation);
#endif
#ifdef GFX_BENCH_HAS_SDK
    else if (sdk == "graphicssdk") adapter=bench::graphicsSDK(validation);
#endif
    if (!adapter) throw std::runtime_error("Unsupported SDK");
    if (auto reason=adapter->unsupportedReason(scenario); !reason.empty()) throw std::runtime_error(reason);
    if (gpuTimestamps) adapter->enableGpuTimestamps();
    auto env = bench::environment(adapter->deviceName());
    std::uint64_t shaderHash = 14695981039346656037ull;
    if (draw || pipeline) {
        auto shaders = bench::compileShaders();
        for (const auto* binary : {&shaders.vertex,&shaders.fragment}) for (auto byte : *binary) { shaderHash ^= std::to_integer<unsigned char>(byte); shaderHash *= 1099511628211ull; }
        adapter->prepareDraw(shaders); adapter->draw(1,false);
        if (!adapter->verifyDraw()) throw std::runtime_error("Triangle readback correctness check failed");
    }
    std::vector<std::byte> pixels;
    if (upload) {
        std::uint32_t width = scenario=="upload1080"?1920:3840, height = scenario=="upload1080"?1080:2160;
        bool half=scenario=="upload4k16f";
        pixels.resize(static_cast<size_t>(width)*height*(half?8:4));
        for (size_t i=0;i<pixels.size();++i) pixels[i]=half?static_cast<std::byte>(i%2?0x38:0):static_cast<std::byte>((i*17+13)%251);
        adapter->prepareUpload(width,height,half); adapter->upload(pixels);
        if (!adapter->verifyUpload(pixels)) throw std::runtime_error("Texture upload readback correctness check failed");
    }
    std::vector<void*> resources; resources.reserve(count);
    std::vector<std::string> results;
    for (unsigned frame = 0; frame < warmup + samples; ++frame) {
        adapter->idle();
        auto begin = bench::Clock::now();
        if (pipeline) {
            auto timing=adapter->recreatePipeline();
            const auto frameTime=bench::ms(begin,bench::Clock::now());
            if (frame>=warmup) results.push_back("{\"pipeline_creation_ms\":"+std::to_string(timing.creation)+",\"release_api_ms\":"+std::to_string(timing.release)+",\"frame_ms\":"+std::to_string(frameTime)+",\"gpu_ms\":null}");
            continue;
        }
        if (draw) {
            auto timing=adapter->draw(count,scenario=="instanced" || scenario=="instanced_gpu");
            if (gpuTimestamps && !timing.gpu) throw std::runtime_error("Missing GPU timestamp result");
            if (!gpuTimestamps && timing.gpu) throw std::runtime_error("Unexpected timestamp instrumentation in end-to-end workload");
            auto gpu=timing.gpu?std::to_string(bench::validGpuMilliseconds(*timing.gpu)):"null";
            if (frame>=warmup) results.push_back("{\"record_ms\":"+std::to_string(timing.record)+",\"submit_ms\":"+std::to_string(timing.submit)+",\"wait_ms\":"+std::to_string(timing.wait)+",\"frame_ms\":"+std::to_string(timing.frame)+",\"gpu_ms\":"+gpu+(gpuTimestamps?",\"query_read_ms\":"+std::to_string(timing.queryRead):"")+"}");
            continue;
        }
        if (upload) {
            adapter->upload(pixels); auto end=bench::Clock::now();
            if (frame>=warmup) results.push_back("{\"upload_latency_ms\":"+std::to_string(bench::ms(begin,end))+",\"frame_ms\":"+std::to_string(bench::ms(begin,end))+",\"gpu_ms\":null}");
            continue;
        }
        for (unsigned i = 0; i < count; ++i) resources.push_back(scenario == "buffers" ? adapter->buffer(65536) : adapter->texture(64, 64, false));
        auto allocated = bench::Clock::now();
        for (auto r : resources) { if (scenario == "buffers") adapter->releaseBuffer(r); else adapter->releaseTexture(r); }
        auto released = bench::Clock::now();
        adapter->idle();
        auto end = bench::Clock::now();
        resources.clear();
        if (frame >= warmup) {
            std::string row = "{\"allocation_ms\":" + std::to_string(bench::ms(begin, allocated))
                + ",\"release_api_ms\":" + std::to_string(bench::ms(allocated, released))
                + ",\"wait_ms\":" + std::to_string(bench::ms(released, end))
                + ",\"frame_ms\":" + std::to_string(bench::ms(begin, end)) + ",\"gpu_ms\":null}";
            results.push_back(std::move(row));
        }
    }
    if (((draw || pipeline) && !adapter->verifyDraw()) || (upload && !adapter->verifyUpload(pixels))) throw std::runtime_error("Post-measurement readback correctness failed");
    std::cout << "{\"schema_version\":1,\"sdk\":" << bench::quote(sdk) << ",\"api\":\"d3d12\",\"category\":" << bench::quote(gpuTimestamps?"gpu_timestamp":"end_to_end") << ",\"scenario\":" << bench::quote(scenario)
        << ",\"count\":" << count << ",\"validation\":" << (validation ? "true" : "false")
        << ",\"environment\":" << env << ",\"sdk_build_source_sha256\":" << bench::quote(adapter->sourceFingerprint()) << ",\"allocation_model\":" << bench::quote(adapter->allocationModel(scenario)) << ",\"warmup\":" << warmup << ",\"configuration\":\"Release\",\"shader_fnv1a64\":" << bench::quote(draw||pipeline?std::to_string(shaderHash):"not_applicable")
        << ",\"payload_bytes\":" << pixels.size() << ",\"gpu_timestamp_scope\":" << bench::quote(gpuTimestamps?bench::gpuTimestampScope:"disabled") << ",\"readback_verified\":" << (draw||upload||pipeline?"true":"null") << ",\"samples\":[";
    for (size_t i = 0; i < results.size(); ++i) { if (i) std::cout << ','; std::cout << results[i]; }
    std::cout << "]}\n";
    return 0;
} catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
