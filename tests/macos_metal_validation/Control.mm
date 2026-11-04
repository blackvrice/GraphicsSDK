#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string_view>

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const std::string_view mode(argv[1]);
    if (mode != "--valid" && mode != "--invalid") return 2;
    const bool invalid = mode == "--invalid";
    if (invalid) {
        const char* enabled = std::getenv("MTL_DEBUG_LAYER");
        const char* action = std::getenv("MTL_DEBUG_LAYER_ERROR_MODE");
        if (!enabled || std::strcmp(enabled, "1") || !action || std::strcmp(action, "assert")) return 2;
    }

    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        id<MTLCommandQueue> queue = [device newCommandQueue];
        id<MTLBuffer> source = [device newBufferWithLength:16 options:MTLResourceStorageModeShared];
        id<MTLBuffer> destination = [device newBufferWithLength:16 options:MTLResourceStorageModeShared];
        id<MTLCommandBuffer> commands = [queue commandBuffer];
        id<MTLBlitCommandEncoder> encoder = [commands blitCommandEncoder];
        if (!encoder || !source.contents || !destination.contents) return 3;
        std::memset(source.contents, 0x37, 16);
        if (invalid) {
            std::puts("CONTROL: invalid blit source length=16 copy size=20 before commit");
            std::fflush(stdout);
        }
        [encoder copyFromBuffer:source sourceOffset:0 toBuffer:destination destinationOffset:0 size:invalid ? 20 : 16];
        [encoder endEncoding];
        // Even if validation unexpectedly permits encoding, never submit the invalid copy.
        if (invalid) {
            std::fputs("CONTROL: invalid copy unexpectedly encoded\n", stderr);
            return 4;
        }
        [commands commit];
        [commands waitUntilCompleted];
        if (commands.status != MTLCommandBufferStatusCompleted || std::memcmp(source.contents, destination.contents, 16)) return 5;
        std::puts("{\"contract\":\"native_metal_valid_blit_v1\",\"verified_bytes\":16}");
        return 0;
    }
}
