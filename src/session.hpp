#pragma once
#include "audio_meter.hpp"
#include "protocol.hpp"
#include <xamlom.h>
#include <sddl.h>
#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <thread>
#include <tlhelp32.h>

namespace islands {
inline constexpr UINT status_message=WM_APP+31;
struct HandleCloser {void operator()(void* handle)const{if(handle && handle!=INVALID_HANDLE_VALUE)CloseHandle(handle);}};
using Handle=std::unique_ptr<void,HandleCloser>;

inline DWORD taskbar_process() {
    DWORD pid=0;if(const auto window=FindWindowW(L"Shell_TrayWnd",nullptr))GetWindowThreadProcessId(window,&pid);return pid;
}
inline HRESULT attach_provider(DWORD pid,const std::wstring& path) {
    const auto library=LoadLibraryExW(L"Windows.UI.Xaml.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(!library)return HRESULT_FROM_WIN32(GetLastError());
    const auto initialize=reinterpret_cast<decltype(&InitializeXamlDiagnosticsEx)>(GetProcAddress(library,"InitializeXamlDiagnosticsEx"));
    HRESULT result=HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
    if(initialize)for(int index=1;index<=256;++index){const auto endpoint=L"VisualDiagConnection"+std::to_wstring(index);result=initialize(endpoint.c_str(),pid,L"",path.c_str(),provider_id,nullptr);if(result!=HRESULT_FROM_WIN32(ERROR_NOT_FOUND))break;}
    FreeLibrary(library);return result;
}

inline HRESULT existing_command(Command command,const Settings* settings=nullptr,SharedState* snapshot=nullptr,const MediaRequest* media=nullptr) {
    const auto pid=taskbar_process();if(!pid)return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    Handle mapping{OpenFileMappingW(command==Command::none?FILE_MAP_READ:FILE_MAP_ALL_ACCESS,FALSE,object_name(pid,L".state").c_str())};
    if(!mapping)return HRESULT_FROM_WIN32(GetLastError());
    const auto state=static_cast<SharedState*>(MapViewOfFile(mapping.get(),command==Command::none?FILE_MAP_READ:FILE_MAP_ALL_ACCESS,0,0,sizeof(SharedState)));
    if(!state)return HRESULT_FROM_WIN32(GetLastError());
    HRESULT result=S_OK;
    if(state->magic!=protocol_magic || state->version!=protocol_version)result=HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH);
    else if(command!=Command::none){
        Handle event{OpenEventW(EVENT_MODIFY_STATE,FALSE,object_name(pid,L".command").c_str())};
        if(!event)result=HRESULT_FROM_WIN32(GetLastError());
        else{if(settings)state->settings=*settings;if(media)state->media_request=*media;InterlockedExchange(&state->command,static_cast<LONG>(command));InterlockedIncrement(&state->request);SetEvent(event.get());}
    }
    if(snapshot && SUCCEEDED(result)){
        result=HRESULT_FROM_WIN32(ERROR_RETRY);
        for(int attempt=0;attempt<16;++attempt){const auto sequence=state->media_sequence;if(sequence&1)continue;MemoryBarrier();memcpy(snapshot,state,sizeof(SharedState));MemoryBarrier();if(sequence==state->media_sequence){result=S_OK;break;}}
    }
    UnmapViewOfFile(state);return result;
}

class Session {
    Handle mapping_;
    Handle command_;
    Handle changed_;
    Handle quit_{CreateEventW(nullptr,TRUE,FALSE,nullptr)};
    Handle stopped_{CreateEventW(nullptr,TRUE,FALSE,nullptr)};
    Handle explorer_;
    Handle ready_;
    Handle provider_thread_;
    SharedState* state_=nullptr;
    std::thread worker_;
    HWND window_=nullptr;
    std::atomic<LONG> failure_{S_OK};
    std::atomic<LONG> stop_error_{S_OK};
    std::atomic<LONG> stop_sequence_{0};
    std::atomic_bool auto_enable_{true};
    std::wstring module_path_;
    mutable std::mutex failure_mutex_;
    std::wstring failure_detail_;

    void fail(HRESULT result,const wchar_t* detail){std::lock_guard lock(failure_mutex_);failure_detail_=detail;failure_=result;}

    bool module_resident()const {
        if(explorer_ && WaitForSingleObject(explorer_.get(),0)==WAIT_OBJECT_0)return false;
        Handle snapshot{CreateToolhelp32Snapshot(TH32CS_SNAPMODULE,state_->explorer_pid)};
        if(snapshot.get()==INVALID_HANDLE_VALUE)return true;
        MODULEENTRY32W item{sizeof(item)};
        if(Module32FirstW(snapshot.get(),&item))do{if(!_wcsicmp(item.szExePath,module_path_.c_str()))return true;}while(Module32NextW(snapshot.get(),&item));
        return false;
    }

    static std::wstring user_acl() {
        Handle token;HANDLE raw=nullptr;if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&raw))return {};
        token.reset(raw);DWORD bytes=0;GetTokenInformation(token.get(),TokenUser,nullptr,0,&bytes);
        std::unique_ptr<BYTE[]> storage{new BYTE[bytes]};
        if(!GetTokenInformation(token.get(),TokenUser,storage.get(),bytes,&bytes))return {};
        LPWSTR sid=nullptr;if(!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(storage.get())->User.Sid,&sid))return {};
        const auto descriptor=L"D:P(A;;GA;;;SY)(A;;GA;;;"+std::wstring(sid)+L")";LocalFree(sid);return descriptor;
    }
    void notify(){if(window_)PostMessageW(window_,status_message,0,0);}
    void listen(std::wstring module) noexcept {
        CoInitializeEx(nullptr,COINIT_MULTITHREADED);
        const auto attached=attach_provider(state_->explorer_pid,module);
        if(FAILED(attached)){fail(attached,L"Подключение XAML diagnostics к Explorer: интерфейс или загрузка Core.dll недоступны.");notify();SetEvent(ready_.get());SetEvent(stopped_.get());CoUninitialize();return;}
        if(state_->worker_thread_id)provider_thread_.reset(OpenThread(SYNCHRONIZE,FALSE,static_cast<DWORD>(state_->worker_thread_id)));
        const auto deadline=GetTickCount64()+15000;
        bool enabled=false;
        AudioMeter audio;bool notify_status=true;
        HANDLE events[]{quit_.get(),changed_.get(),explorer_.get()};
        for(;;){
            if(!provider_thread_ && state_->worker_thread_id)provider_thread_.reset(OpenThread(SYNCHRONIZE,FALSE,static_cast<DWORD>(state_->worker_thread_id)));
            const auto phase=static_cast<Phase>(state_->phase);
            if(phase==Phase::verified && !enabled && auto_enable_ && !stop_sequence_){enabled=true;send(Command::enable);}
            if(phase==Phase::active || phase==Phase::disabled || phase==Phase::failed)SetEvent(ready_.get());
            if(notify_status)notify();
            if(stop_sequence_ && state_->cleanup_complete && state_->acknowledgement>=stop_sequence_){
                if(provider_thread_ && WaitForSingleObject(provider_thread_.get(),7000)!=WAIT_OBJECT_0)stop_error_=HRESULT_FROM_WIN32(WAIT_TIMEOUT);
                else if(module_resident())stop_error_=HRESULT_FROM_WIN32(ERROR_BUSY);
                SetEvent(stopped_.get());break;
            }
            const auto now=GetTickCount64();
            const bool starting=phase==Phase::waiting || phase==Phase::attached || phase==Phase::probing;
            if(starting && now>=deadline){fail(HRESULT_FROM_WIN32(WAIT_TIMEOUT),L"Истекло время ожидания структуры панели или проверки XAML.");notify();break;}
            const bool visualize=phase==Phase::active && state_->visualizer_requested && !stop_sequence_;
            if(visualize){float peak=0;const auto result=audio.sample(peak);InterlockedExchange(&state_->audio_level,static_cast<LONG>(peak*65535));InterlockedExchange(&state_->audio_tick,static_cast<LONG>(GetTickCount()));InterlockedExchange(&state_->audio_error,result);}
            else{audio.close();InterlockedExchange(&state_->audio_level,0);}
            const auto wait=WaitForMultipleObjects(3,events,FALSE,starting?static_cast<DWORD>(deadline-now):visualize?33:INFINITE);
            notify_status=wait!=WAIT_TIMEOUT;
            if(wait==WAIT_OBJECT_0)break;
            if(wait==WAIT_OBJECT_0+2){failure_=HRESULT_FROM_WIN32(ERROR_PROCESS_ABORTED);notify();SetEvent(stopped_.get());break;}
        }
        audio.close();CoUninitialize();
    }
public:
    ~Session(){stop();if(worker_.joinable()){SetEvent(quit_.get());worker_.join();}if(state_)UnmapViewOfFile(state_);}
    HRESULT start(HWND window,Settings settings,const std::wstring& module) {
        if(state_){failure_=S_OK;auto_enable_=true;send(Command::enable,&settings);return S_OK;}
        const auto pid=taskbar_process();if(!pid)return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
        const auto acl=user_acl();PSECURITY_DESCRIPTOR descriptor=nullptr;
        if(acl.empty() || !ConvertStringSecurityDescriptorToSecurityDescriptorW(acl.c_str(),SDDL_REVISION_1,&descriptor,nullptr))return HRESULT_FROM_WIN32(GetLastError());
        SECURITY_ATTRIBUTES attributes{sizeof(attributes),descriptor,FALSE};
        mapping_.reset(CreateFileMappingW(INVALID_HANDLE_VALUE,&attributes,PAGE_READWRITE,0,sizeof(SharedState),object_name(pid,L".state").c_str()));
        const auto existed=GetLastError()==ERROR_ALREADY_EXISTS;
        if(mapping_ && !existed){changed_.reset(CreateEventW(&attributes,FALSE,FALSE,object_name(pid,L".changed").c_str()));command_.reset(CreateEventW(&attributes,FALSE,FALSE,object_name(pid,L".command").c_str()));ready_.reset(CreateEventW(&attributes,TRUE,FALSE,object_name(pid,L".ready").c_str()));}
        LocalFree(descriptor);
        if(!mapping_ || existed){mapping_.reset();return HRESULT_FROM_WIN32(existed?ERROR_ALREADY_EXISTS:GetLastError());}
        if(!changed_ || !command_)return HRESULT_FROM_WIN32(GetLastError());
        if(!ready_ || !ResetEvent(ready_.get()))return HRESULT_FROM_WIN32(GetLastError());
        explorer_.reset(OpenProcess(SYNCHRONIZE,FALSE,pid));if(!explorer_)return HRESULT_FROM_WIN32(GetLastError());
        state_=static_cast<SharedState*>(MapViewOfFile(mapping_.get(),FILE_MAP_ALL_ACCESS,0,0,sizeof(SharedState)));
        if(!state_)return HRESULT_FROM_WIN32(GetLastError());
        new(state_)SharedState{};state_->controller_pid=GetCurrentProcessId();state_->explorer_pid=pid;state_->settings=settings;
        state_->command=static_cast<LONG>(Command::probe);state_->request=1;
        window_=window;module_path_=module;worker_=std::thread([this,module]{listen(module);});notify();return S_OK;
    }
    LONG send(Command command,const Settings* settings=nullptr) {
        if(!state_)return 0;
        if(settings)state_->settings=*settings;
        InterlockedExchange(&state_->command,static_cast<LONG>(command));
        const auto sequence=InterlockedIncrement(&state_->request);SetEvent(command_.get());return sequence;
    }
    void disable(){auto_enable_=false;send(Command::disable);}
    HRESULT stop() noexcept {
        if(worker_.joinable()){
            if(state_ && !state_->cleanup_complete){const auto sequence=state_->request+1;stop_sequence_=sequence;send(Command::stop);if(WaitForSingleObject(stopped_.get(),12000)!=WAIT_OBJECT_0)return HRESULT_FROM_WIN32(WAIT_TIMEOUT);}
            SetEvent(quit_.get());worker_.join();
        }
        if(state_ && state_->cleanup_complete && !module_resident())stop_error_=S_OK;
        return stop_error_;
    }
    bool connected()const{return state_!=nullptr;}
    Phase phase()const{return FAILED(failure_)?Phase::failed:(state_?static_cast<Phase>(state_->phase):Phase::disabled);}
    HRESULT result()const{return FAILED(failure_)?failure_.load():(state_?state_->result:S_OK);}
    DWORD explorer_pid()const{return state_?state_->explorer_pid:0;}
    std::wstring detail()const{std::lock_guard lock(failure_mutex_);return failure_detail_.empty()?(state_?std::wstring(state_->detail):L""):failure_detail_;}
};
}
