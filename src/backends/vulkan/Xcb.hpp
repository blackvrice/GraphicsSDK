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
struct XcbWindowInfo { std::uint32_t width{}, height{}; xcb_visualid_t visual{}; };
Result<XcbWindowInfo> xcbWindow(void* pointer, std::uint32_t window) {
    XcbApi xcb;
    if (!xcb.available()) return fail(ErrorCode::Unsupported, "libxcb native window queries unavailable");
    auto connection = static_cast<xcb_connection_t*>(pointer);
    if (!connection || xcb.connection_has_error(connection)) return fail(ErrorCode::InvalidState, "XCB connection is not connected");
    xcb_generic_error_t* error{};
    std::unique_ptr<xcb_get_geometry_reply_t, decltype(&std::free)> geometry(xcb.get_geometry_reply(connection, xcb.get_geometry(connection, window), &error), &std::free);
    std::free(error); error = nullptr;
    if (!geometry || !geometry->width || !geometry->height) return fail(ErrorCode::InvalidArgument, "XCB window geometry unavailable");
    std::unique_ptr<xcb_get_window_attributes_reply_t, decltype(&std::free)> attributes(xcb.get_window_attributes_reply(connection, xcb.get_window_attributes(connection, window), &error), &std::free);
    std::free(error);
    if (!attributes || attributes->_class != XCB_WINDOW_CLASS_INPUT_OUTPUT) return fail(ErrorCode::InvalidArgument, "XCB surface requires a live InputOutput window");
    return XcbWindowInfo{geometry->width, geometry->height, attributes->visual};
}
#endif
