#include "Adapter.hpp"
#include <Windows.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <sstream>
#include <stdexcept>

namespace bench {
std::string quote(std::string_view text) {
    std::string out = "\"";
    constexpr char hex[] = "0123456789abcdef";
    for (unsigned char c : text) {
        if (c == '"' || c == '\\') { out += '\\'; out += static_cast<char>(c); }
        else if (c < 32) { out += "\\u00"; out += hex[c >> 4]; out += hex[c & 15]; }
        else out += static_cast<char>(c);
    }
    return out + '"';
}
std::string utf8(const wchar_t* src) {
    int count = WideCharToMultiByte(CP_UTF8, 0, src, -1, nullptr, 0, nullptr, nullptr);
    if (count <= 0) throw std::runtime_error("Invalid adapter name");
    std::string result(static_cast<size_t>(count), '\0');
    WideCharToMultiByte(CP_UTF8, 0, src, -1, result.data(), count, nullptr, nullptr);
    result.pop_back();
    return result;
}
std::string environment(std::string_view selected) {
    using Microsoft::WRL::ComPtr;
    ComPtr<IDXGIFactory4> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) throw std::runtime_error("DXGI unavailable");
    ComPtr<IDXGIAdapter1> adapter;
    DXGI_ADAPTER_DESC1 desc{};
    unsigned matches = 0;
    LARGE_INTEGER driver{};
    for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 current{};
        adapter->GetDesc1(&current);
        if (utf8(current.Description) == selected) {
            ++matches; desc = current;
            if (FAILED(adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &driver))) driver.QuadPart = 0;
        }
        adapter.Reset();
    }
    // A name alone is ambiguous on multi-adapter systems; never silently compare them.
    if (matches != 1) throw std::runtime_error("Selected GPU cannot be uniquely matched to a DXGI LUID");
    MEMORYSTATUSEX memory{sizeof(memory)};
    if (!GlobalMemoryStatusEx(&memory)) throw std::runtime_error("RAM query failed");
    char cpu[256]{}; DWORD size = sizeof(cpu);
    if (RegGetValueA(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", "ProcessorNameString", RRF_RT_REG_SZ, nullptr, cpu, &size) != ERROR_SUCCESS)
        throw std::runtime_error("CPU query failed");
    using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
    auto versionFn = reinterpret_cast<RtlGetVersionFn>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"));
    OSVERSIONINFOW version{sizeof(version)};
    if (!versionFn || versionFn(&version) != 0) throw std::runtime_error("OS query failed");
    std::ostringstream out;
    out << "{\"cpu\":" << quote(cpu) << ",\"ram_bytes\":" << memory.ullTotalPhys
        << ",\"os\":\"Windows " << version.dwMajorVersion << '.' << version.dwMinorVersion << '.' << version.dwBuildNumber
        << "\",\"gpu\":" << quote(selected) << ",\"vendor_id\":" << desc.VendorId << ",\"device_id\":" << desc.DeviceId
        << ",\"luid\":\"" << desc.AdapterLuid.HighPart << ':' << desc.AdapterLuid.LowPart
        << "\",\"driver\":\"" << HIWORD(driver.HighPart) << '.' << LOWORD(driver.HighPart) << '.' << HIWORD(driver.LowPart) << '.' << LOWORD(driver.LowPart)
        << "\",\"driver_query_available\":" << (driver.QuadPart != 0 ? "true" : "false")
        << ",\"software_renderer\":" << ((desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) ? "true" : "false")
        << ",\"compiler\":\"MSVC " << _MSC_FULL_VER << "\",\"pointer_bits\":" << sizeof(void*)*8 << '}';
    return out.str();
}
}
