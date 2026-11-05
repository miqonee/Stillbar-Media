#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "protocol.hpp"
#include <xamlom.h>
#include <sddl.h>
#include <cstdio>
#include <filesystem>
#include <memory>

namespace {
struct HandleCloser { void operator()(void* value) const { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); } };
using Handle = std::unique_ptr<void, HandleCloser>;
std::wstring executable_directory() {
    wchar_t path[32768]{};
    GetModuleFileNameW(nullptr, path, ARRAYSIZE(path));
    return std::filesystem::path(path).parent_path().wstring();
}

HRESULT attach(DWORD process_id, const std::wstring& module) {
    auto library = LoadLibraryExW(L"Windows.UI.Xaml.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!library) return HRESULT_FROM_WIN32(GetLastError());
    auto initialize = reinterpret_cast<decltype(&InitializeXamlDiagnosticsEx)>(GetProcAddress(library, "InitializeXamlDiagnosticsEx"));
    if (!initialize) { const auto error = HRESULT_FROM_WIN32(GetLastError()); FreeLibrary(library); return error; }
    HRESULT result = HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    for (int index = 1; index <= 256; ++index) {
        const auto connection = L"VisualDiagConnection" + std::to_wstring(index);
        result = initialize(connection.c_str(), process_id, L"", module.c_str(), islands::provider_id, nullptr);
        if (result != HRESULT_FROM_WIN32(ERROR_NOT_FOUND)) break;
    }
    FreeLibrary(library);
    return result;
}

void print_state(const islands::SharedState& state) {
    std::wprintf(L"phase=%ls hr=0x%08lX elements=%ld frames=%ld probe_passed=%ld\n%ls\n",
        islands::phase_name(static_cast<islands::Phase>(state.phase)), static_cast<unsigned long>(state.result),
        state.elements_seen, state.frames_seen, state.probe_passed, state.detail);
    std::fflush(stdout);
}
}

int wmain(int argc, wchar_t** argv) {
    if(argc!=2 || (wcscmp(argv[1],L"--probe") && wcscmp(argv[1],L"--status") && wcscmp(argv[1],L"--disable"))) {
        std::wprintf(L"Stillbar.Probe --probe | --status | --disable\n");return 1;
    }
    const auto apartment=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    if(FAILED(apartment)){std::fwprintf(stderr,L"COM initialization failed: 0x%08lX\n",static_cast<unsigned long>(apartment));return 1;}
    const auto taskbar = FindWindowW(L"Shell_TrayWnd", nullptr);
    DWORD process_id = 0;
    if (taskbar) GetWindowThreadProcessId(taskbar, &process_id);
    if (!process_id) { std::fwprintf(stderr, L"No native taskbar found in this desktop/session.\n"); return 2; }
    std::wprintf(L"explorer_pid=%lu taskbar=%p\n", process_id, taskbar);
    const bool status_only = argc > 1 && wcscmp(argv[1], L"--status") == 0;
    const auto map_name = islands::object_name(process_id, L".state");
    const bool disable=wcscmp(argv[1],L"--disable")==0;
    if (status_only || disable) {
        Handle mapping{OpenFileMappingW(disable?FILE_MAP_ALL_ACCESS:FILE_MAP_READ, FALSE, map_name.c_str())};
        if (!mapping) { std::wprintf(L"phase=disabled (no Stillbar controller)\n"); return 0; }
        const auto state = static_cast<islands::SharedState*>(MapViewOfFile(mapping.get(), disable?FILE_MAP_ALL_ACCESS:FILE_MAP_READ, 0, 0, sizeof(islands::SharedState)));
        if (!state) return 3;
        if(state->magic!=islands::protocol_magic || state->version!=islands::protocol_version){UnmapViewOfFile(state);return 3;}
        if(disable) {
            Handle stop_event{OpenEventW(EVENT_MODIFY_STATE,FALSE,islands::object_name(process_id,L".command").c_str())};
            if(!stop_event){UnmapViewOfFile(state);return 3;}
            InterlockedExchange(&state->command,static_cast<LONG>(islands::Command::stop));SetEvent(stop_event.get());
        }
        print_state(*state); UnmapViewOfFile(state); return 0;
    }
    Handle mapping{CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(islands::SharedState), map_name.c_str())};
    if (!mapping) return 3;
    if (GetLastError() == ERROR_ALREADY_EXISTS) { std::fwprintf(stderr, L"A controller is already connected.\n"); return 4; }
    auto state = static_cast<islands::SharedState*>(MapViewOfFile(mapping.get(), FILE_MAP_ALL_ACCESS, 0, 0, sizeof(islands::SharedState)));
    if (!state) return 3;
    new (state) islands::SharedState{};
    state->controller_pid = GetCurrentProcessId(); state->explorer_pid = process_id;
    const auto changed_name = islands::object_name(process_id, L".changed");
    const auto command_name = islands::object_name(process_id, L".command");
    Handle changed{CreateEventW(nullptr, FALSE, FALSE, changed_name.c_str())};
    Handle command{CreateEventW(nullptr, FALSE, FALSE, command_name.c_str())};
    state->command = static_cast<LONG>(islands::Command::probe);
    state->request = 1;
    const auto module = executable_directory() + L"\\Stillbar.Core.dll";
    std::wprintf(L"provider=%ls\n", module.c_str()); std::fflush(stdout);
    const auto result = attach(process_id, module);
    if (FAILED(result)) {
        std::fwprintf(stderr, L"InitializeXamlDiagnosticsEx failed: 0x%08lX\n", static_cast<unsigned long>(result));
        UnmapViewOfFile(state); return 5;
    }
    const auto deadline = GetTickCount64() + 15000;
    auto last_phase = -1L;
    while (GetTickCount64() < deadline) {
        if (state->phase != last_phase) { print_state(*state); last_phase = state->phase; }
        if (state->probe_passed || state->phase == static_cast<LONG>(islands::Phase::failed)) break;
        const auto now=GetTickCount64();
        if(now>=deadline)break;
        const auto remaining = static_cast<DWORD>(deadline - now);
        if (WaitForSingleObject(changed.get(), remaining) == WAIT_TIMEOUT) break;
    }
    print_state(*state);
    const bool passed = state->probe_passed == 1;
    InterlockedExchange(&state->command, static_cast<LONG>(islands::Command::stop));
    InterlockedIncrement(&state->request); SetEvent(command.get());
    const auto cleanup_deadline=GetTickCount64()+5000;
    while(state->phase!=static_cast<LONG>(islands::Phase::disabled)) {
        const auto now=GetTickCount64();if(now>=cleanup_deadline)break;
        WaitForSingleObject(changed.get(),static_cast<DWORD>(cleanup_deadline-now));
    }
    const bool cleaned=state->phase==static_cast<LONG>(islands::Phase::disabled);
    print_state(*state);
    UnmapViewOfFile(state);
    CoUninitialize();
    return passed && cleaned ? 0 : 6;
}
