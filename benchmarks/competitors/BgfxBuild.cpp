namespace bench {
// Lives in the same static library as the compiled bgfx backend.
const char* bgfxBuildFingerprint() { return GFX_BGFX_BUILD_SHA256; }
}
