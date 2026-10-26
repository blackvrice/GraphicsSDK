// Private, optional native-window queries. No hard libxcb link in Core.
#ifdef GFX_HAS_VULKAN_XCB
struct XcbApi {
    void* module{dlopen("libxcb.so.1", RTLD_NOW | RTLD_LOCAL)};
#define XCB_FUNCTIONS(X) X(connection_has_error) X(get_geometry) X(get_geometry_reply) X(get_window_attributes) X(get_window_attributes_reply)
#define XCB_LOAD(name) decltype(&xcb_##name) name{module ? reinterpret_cast<decltype(&xcb_##name)>(dlsym(module, "xcb_" #name)) : nullptr};
    XCB_FUNCTIONS(XCB_LOAD)
#undef XCB_LOAD
    bool available() const {
#define XCB_CHECK(name) if (!name) return false;
        XCB_FUNCTIONS(XCB_CHECK)
#undef XCB_CHECK
        return true;
    }
#undef XCB_FUNCTIONS
    ~XcbApi() { if (module) dlclose(module); }
};
#endif
