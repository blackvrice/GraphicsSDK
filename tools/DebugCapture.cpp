// Windows debugger output collector for isolated GPU validation diagnostics.
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <iostream>
#include <string>
#include <vector>

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) return 2;
    std::wstring command = L"\"" + std::wstring(argv[1]) + L"\"";
    for (int i = 2; i < argc; ++i) { command += L" \""; command += argv[i]; command += L"\""; }
    STARTUPINFOW startup{sizeof(startup)}; PROCESS_INFORMATION process{};
    if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, DEBUG_ONLY_THIS_PROCESS | CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
        std::cerr << "CreateProcess failed: " << GetLastError() << '\n'; return 2;
    }
    CloseHandle(process.hThread);
    DWORD code = 1;
    bool finished = false;
    while (!finished) {
        DEBUG_EVENT event{};
        if (!WaitForDebugEvent(&event, 120000)) { std::cerr << "Debug wait failed: " << GetLastError() << '\n'; TerminateProcess(process.hProcess, 2); break; }
        DWORD status = DBG_CONTINUE;
        switch (event.dwDebugEventCode) {
        case CREATE_PROCESS_DEBUG_EVENT: if (event.u.CreateProcessInfo.hFile) CloseHandle(event.u.CreateProcessInfo.hFile); break;
        case LOAD_DLL_DEBUG_EVENT: if (event.u.LoadDll.hFile) CloseHandle(event.u.LoadDll.hFile); break;
        case OUTPUT_DEBUG_STRING_EVENT: {
            const auto& output = event.u.DebugString;
            SIZE_T bytes = output.nDebugStringLength * (output.fUnicode ? sizeof(wchar_t) : 1), read{};
            std::vector<char> data(bytes + 2, 0);
            if (ReadProcessMemory(process.hProcess, output.lpDebugStringData, data.data(), bytes, &read)) {
                if (output.fUnicode) std::wcerr << reinterpret_cast<const wchar_t*>(data.data());
                else std::cerr << data.data();
            }
            break;
        }
        case EXCEPTION_DEBUG_EVENT:
            if (event.u.Exception.ExceptionRecord.ExceptionCode != EXCEPTION_BREAKPOINT) status = DBG_EXCEPTION_NOT_HANDLED;
            break;
        case EXIT_PROCESS_DEBUG_EVENT: code = event.u.ExitProcess.dwExitCode; finished = true; break;
        default: break;
        }
        ContinueDebugEvent(event.dwProcessId, event.dwThreadId, status);
    }
    CloseHandle(process.hProcess); std::cerr << "Child exit: " << code << '\n'; return static_cast<int>(code);
}
