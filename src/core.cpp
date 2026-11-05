#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "module_lifetime.hpp"
#include "protocol.hpp"
#include <ocidl.h>
#include <xamlom.h>
#undef GetCurrentTime
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.UI.Xaml.h>
#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Media.h>
#include <winrt/Windows.UI.Xaml.Media.Animation.h>
#include "islands_view.hpp"
#include "element_diagnostics.hpp"
#include <atomic>
#include <memory>
#include <mutex>
#include <fstream>
#include <filesystem>
#include <tlhelp32.h>

namespace wux = winrt::Windows::UI::Xaml;
namespace controls = wux::Controls;
namespace animation = wux::Media::Animation;
using winrt::Windows::Foundation::TimeSpan;

namespace {
struct HandleCloser { void operator()(void* value) const { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); } };
using Handle = std::unique_ptr<void, HandleCloser>;
struct Runtime;
struct WaitContext {std::shared_ptr<Runtime> runtime;HMODULE module=nullptr;};
std::shared_ptr<Runtime> runtime;
std::mutex runtime_mutex;

struct Runtime : std::enable_shared_from_this<Runtime> {
    Handle mapping;
    Handle changed;
    Handle command;
    Handle owner;
    Handle restored{CreateEventW(nullptr,TRUE,FALSE,nullptr)};
    Handle root_found{CreateEventW(nullptr,TRUE,FALSE,nullptr)};
    Handle registration;
    islands::SharedState* shared = nullptr;
    winrt::com_ptr<IXamlDiagnostics> diagnostics;
    winrt::com_ptr<IVisualTreeService> service;
    winrt::com_ptr<IVisualTreeServiceCallback2> callback;
    wux::FrameworkElement frame{nullptr};
    controls::Grid grid{nullptr};
    animation::Storyboard storyboard{nullptr};
    wux::DispatcherTimer timer{nullptr};
    winrt::event_token tick_token{};
    wux::DispatcherTimer readiness_timer{nullptr};
    winrt::event_token readiness_tick{};
    std::vector<winrt::weak_ref<wux::FrameworkElement>> candidates;
    std::atomic<DWORD> discovery_thread{0};
    std::chrono::steady_clock::time_point readiness_started{};
    std::wstring missing_part=L"TaskbarFrame";
    unsigned readiness_checks=0;
    winrt::Windows::Foundation::IInspectable original_local{nullptr};
    wux::CornerRadius original_effective{};
    std::atomic_bool stopping{false};
    bool discovery_finished = false;
    bool applied_checked = false;
    std::shared_ptr<islands::IslandsView> view;
    std::shared_ptr<islands::MediaService> media;
    std::vector<std::shared_ptr<islands::AsyncTasks>> tasks;

    ~Runtime() { if (shared) UnmapViewOfFile(shared); }
    void report(islands::Phase phase, HRESULT result, const wchar_t* detail) {
        if (!shared) return;
        wcsncpy_s(shared->detail, detail, _TRUNCATE);
        InterlockedExchange(&shared->result, result);
        InterlockedExchange(&shared->phase, static_cast<LONG>(phase));
        SetEvent(changed.get());
    }
    bool connect() {
        const auto pid = GetCurrentProcessId();
        mapping.reset(OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, islands::object_name(pid,L".state").c_str()));
        if (!mapping) return false;
        shared = static_cast<islands::SharedState*>(MapViewOfFile(mapping.get(), FILE_MAP_ALL_ACCESS, 0, 0, sizeof(islands::SharedState)));
        if (!shared || shared->magic != islands::protocol_magic || shared->version != islands::protocol_version) return false;
        changed.reset(OpenEventW(EVENT_MODIFY_STATE, FALSE, islands::object_name(pid,L".changed").c_str()));
        command.reset(OpenEventW(SYNCHRONIZE, FALSE, islands::object_name(pid,L".command").c_str()));
        owner.reset(OpenProcess(SYNCHRONIZE, FALSE, shared->controller_pid));
        return changed && command && owner;
    }
    static bool same(wux::CornerRadius a, wux::CornerRadius b) {
        return a.TopLeft==b.TopLeft && a.TopRight==b.TopRight && a.BottomLeft==b.BottomLeft && a.BottomRight==b.BottomRight;
    }
    bool local_unchanged() {
        const auto after = grid.ReadLocalValue(controls::Grid::CornerRadiusProperty());
        if (winrt::get_abi(after) == winrt::get_abi(original_local)) return true;
        if (auto first = original_local.try_as<winrt::Windows::Foundation::IPropertyValue>()) {
            if (after.try_as<winrt::Windows::Foundation::IPropertyValue>()) {
                return same(winrt::unbox_value<wux::CornerRadius>(original_local),winrt::unbox_value<wux::CornerRadius>(after));
            }
        }
        return false;
    }
    void stop_readiness() noexcept {
        try{if(readiness_timer){readiness_timer.Stop();if(readiness_tick.value)readiness_timer.Tick(readiness_tick);}}catch(...){}
        readiness_tick={};readiness_timer=nullptr;candidates.clear();
    }
    void check_readiness() noexcept {
        if(stopping || discovery_finished)return;
        try{
            ++readiness_checks;
            for(const auto& weak:candidates)if(const auto candidate=weak.get()){
                const auto tree=islands::resolve_taskbar_tree(candidate);
                if(tree.ready()){
                    frame=candidate;discovery_finished=true;stop_readiness();probe(tree.grid);return;
                }
                missing_part=tree.missing();
            }
            if(std::chrono::steady_clock::now()-readiness_started>=std::chrono::seconds(3)){
                discovery_finished=true;stop_readiness();inspect_tree();
                const auto detail=L"Готовность панели: "+missing_part+L" недоступен после ожидания 3000 мс. Диагностика: native-tree.txt.";
                report(islands::Phase::failed,HRESULT_FROM_WIN32(ERROR_NOT_FOUND),detail.c_str());SetEvent(root_found.get());
            }
        }catch(...){discovery_finished=true;stop_readiness();report(islands::Phase::failed,winrt::to_hresult(),L"Не удалось проверить готовность XAML-дерева панели.");SetEvent(root_found.get());}
    }
    void consider_frame(wux::FrameworkElement candidate) {
        DWORD expected=0;const auto thread=GetCurrentThreadId();
        if(!discovery_thread.compare_exchange_strong(expected,thread) && expected!=thread)return;
        if(stopping || discovery_finished)return;
        for(const auto& weak:candidates)if(weak.get()==candidate)return;
        if(!frame)frame=candidate;
        candidates.push_back(winrt::make_weak(candidate));
        if(const auto control=candidate.try_as<controls::Control>())control.ApplyTemplate();
        if(!readiness_timer){
            readiness_started=std::chrono::steady_clock::now();readiness_timer=wux::DispatcherTimer{};readiness_timer.Interval(std::chrono::milliseconds(50));
            const auto weak=weak_from_this();readiness_tick=readiness_timer.Tick([weak](auto&&,auto&&){if(auto self=weak.lock())self->check_readiness();});
            report(islands::Phase::attached,S_OK,L"TaskbarFrame найден. Ожидание RootGrid, списка приложений и системного трея…");readiness_timer.Start();
        }
        check_readiness();
    }
    void dump_tree(wux::DependencyObject node,std::wofstream& output,wux::UIElement root,unsigned depth=0) {
        if(depth>30)return;
        for(unsigned index=0;index<depth;++index)output<<L"  ";
        output<<winrt::get_class_name(node).c_str();
        if(auto element=node.try_as<wux::FrameworkElement>()) {
            output<<L" name="<<element.Name().c_str()<<L" size="<<element.ActualWidth()<<L"x"<<element.ActualHeight();
            try {const auto point=element.TransformToVisual(root).TransformPoint({0,0});output<<L" at="<<point.X<<L","<<point.Y;}catch(...){}
            try{
                const auto offset=element.ActualOffset();output<<L" layout="<<offset.x<<L","<<offset.y<<L" opacity="<<element.Opacity();
                if(const auto transitions=element.Transitions())for(const auto& transition:transitions)output<<L" transition="<<winrt::get_class_name(transition).c_str();
                if(const auto panel=element.try_as<controls::Panel>())if(const auto transitions=panel.ChildrenTransitions())for(const auto& transition:transitions)output<<L" child_transition="<<winrt::get_class_name(transition).c_str();
                const auto visual=wux::Hosting::ElementCompositionPreview::GetElementVisual(element);
                if(!visual)throw winrt::hresult_error(RO_E_CLOSED);
                output<<L" visual="<<visual.Offset().x<<L","<<visual.Offset().y<<L" scale="<<visual.Scale().x<<L","<<visual.Scale().y<<L" visual_opacity="<<visual.Opacity();
                if(const auto implicit=islands::animation_object(visual).ImplicitAnimations())for(const auto& item:implicit)output<<L" implicit="<<item.Key().c_str();
            }catch(...){}
        }
        if(auto border=node.try_as<controls::Border>()){
            const auto radius=border.CornerRadius();const auto stroke=border.BorderThickness();
            output<<L" radius="<<radius.TopLeft<<L","<<radius.TopRight<<L","<<radius.BottomRight<<L","<<radius.BottomLeft<<L" stroke="<<stroke.Left<<L","<<stroke.Top<<L","<<stroke.Right<<L","<<stroke.Bottom;
            if(border.Background())output<<L" backgroundOpacity="<<border.Background().Opacity();
        }
        output<<L"\n";
        const auto count=wux::Media::VisualTreeHelper::GetChildrenCount(node);
        for(int index=0;index<count;++index)dump_tree(wux::Media::VisualTreeHelper::GetChild(node,index),output,root,depth+1);
    }
    void inspect_tree() noexcept {
        try {
            HMODULE module=nullptr;
            GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&DllCanUnloadNow),&module);
            wchar_t path[32768]{};GetModuleFileNameW(module,path,ARRAYSIZE(path));
            std::wofstream output(std::filesystem::path(path).parent_path()/L"native-tree.txt");
            wux::UIElement root=frame;
            if(frame && frame.XamlRoot() && frame.XamlRoot().Content())root=frame.XamlRoot().Content();
            if(!root)return;
            dump_tree(root,output,root);
            output<<L"discovery checks="<<readiness_checks<<L" missing="<<missing_part<<L" composition_diag_disabled_reads="<<islands::element_diagnostics::reads.load()<<L" registry_imports_restored="<<islands::element_diagnostics::imports_restored.load()<<L"\n";
            if(view)output<<view->describe()<<L"\n";
        }catch(...){}
    }
    void finish_probe() {
        timer.Stop();
        if(tick_token.value){timer.Tick(tick_token);tick_token={};}
        storyboard.Stop();
        const bool value_restored=same(grid.CornerRadius(),original_effective) && local_unchanged();
        if(!islands::element_diagnostics::imports_restored) {
            report(islands::Phase::failed,HRESULT_FROM_WIN32(ERROR_WRITE_FAULT),L"Не удалось восстановить временные импорты диагностики XAML. Сеанс остановлен.");
        }else if(value_restored && applied_checked) {
            InterlockedExchange(&shared->probe_passed,1);
            report(islands::Phase::verified,S_OK,L"PASS: taskbar CornerRadius changed through an animation and restored; original local value/binding preserved.");
        } else report(islands::Phase::failed,E_FAIL,L"FAIL: the radius override or exact restoration could not be verified.");
        SetEvent(root_found.get());
    }
    void probe(controls::Grid ready_grid) try {
        if(stopping || !frame || storyboard) return;
        grid=ready_grid;missing_part=L"none";
        inspect_tree();
        original_local=grid.ReadLocalValue(controls::Grid::CornerRadiusProperty());
        original_effective=grid.CornerRadius();
        storyboard=animation::Storyboard{};
        animation::ObjectAnimationUsingKeyFrames override_radius;
        animation::DiscreteObjectKeyFrame key;
        const double test_radius=original_effective.TopLeft==12?13:12;
        key.KeyTime(animation::KeyTimeHelper::FromTimeSpan(TimeSpan{0}));
        key.Value(winrt::box_value(wux::CornerRadius{test_radius,test_radius,test_radius,test_radius}));
        override_radius.KeyFrames().Append(key);
        override_radius.Duration(wux::DurationHelper::Forever());
        animation::Storyboard::SetTarget(override_radius,grid);
        animation::Storyboard::SetTargetProperty(override_radius,L"CornerRadius");
        storyboard.Children().Append(override_radius);
        storyboard.Begin();
        report(islands::Phase::probing,S_OK,L"Native taskbar root found. Testing reversible CornerRadius override.");
        timer=wux::DispatcherTimer{};
        timer.Interval(std::chrono::milliseconds(200));
        const std::weak_ptr<Runtime> weak=shared_from_this();
        tick_token=timer.Tick([weak,test_radius](auto&&,auto&&) {
            if(auto self=weak.lock()) try {
                if(!self->applied_checked) {
                    self->applied_checked=same(self->grid.CornerRadius(),wux::CornerRadius{test_radius,test_radius,test_radius,test_radius}) && self->local_unchanged();
                    if(!self->applied_checked) { self->finish_probe(); return; }
                    self->storyboard.Stop();
                } else self->finish_probe();
            } catch(...) { self->restore(); self->report(islands::Phase::failed,winrt::to_hresult(),L"XAML rejected the reversible property test.");SetEvent(self->root_found.get()); }
        });
        timer.Start();
    } catch(...) { restore(); report(islands::Phase::failed,winrt::to_hresult(),L"Unable to create the reversible CornerRadius override.");SetEvent(root_found.get()); }

    void restore() noexcept {
        stop_readiness();
        if(media){media->close();media.reset();}
        if(view){view->close();view.reset();}
        try { if(timer){timer.Stop();if(tick_token.value)timer.Tick(tick_token);} if(storyboard)storyboard.Stop(); } catch(...) {}
        tick_token={};
    }
    void enable_ui(islands::Settings settings) {
        if(!shared->probe_passed)winrt::throw_hresult(HRESULT_FROM_WIN32(ERROR_NOT_READY));
        if(view){view->apply(islands::normalize(settings));return;}
        std::erase_if(tasks,[](const auto& group){return WaitForSingleObject(group->drained(),0)==WAIT_OBJECT_0;});
        const auto group=std::make_shared<islands::AsyncTasks>();tasks.push_back(group);
        const auto weak=weak_from_this();
        islands::MediaCommands commands;
        commands.settings_process=shared->controller_pid;
        commands.action=[weak](islands::MediaAction action){if(auto self=weak.lock();self && self->media)self->media->act(action);};
        commands.seek=[weak](std::int64_t position,std::uint64_t revision){if(auto self=weak.lock();self && self->media)self->media->seek(position,revision);};
        commands.select_source=[weak](std::uint64_t id){if(auto self=weak.lock();self && self->media)self->media->select_source(id);};
        commands.browse_sources=[weak](bool open){if(auto self=weak.lock();self && self->media)self->media->browse_sources(open);};
        commands.visualizer_request=[weak](bool enabled){if(auto self=weak.lock()){InterlockedExchange(&self->shared->visualizer_requested,enabled?1:0);SetEvent(self->changed.get());}};
        commands.audio_level=[weak]{if(auto self=weak.lock()){
            const auto age=GetTickCount()-static_cast<DWORD>(self->shared->audio_tick);
            if(age<250)return std::clamp(static_cast<float>(self->shared->audio_level)/65535.f,0.f,1.f);
        }return 0.f;};
        view=std::make_shared<islands::IslandsView>(frame,islands::normalize(settings),group,std::move(commands),[weak](HRESULT error){if(auto self=weak.lock()){self->restore();self->report(islands::Phase::failed,error,L"Taskbar layout update failed. Original taskbar restored.");}});
        view->start();
        media=std::make_shared<islands::MediaService>(frame.Dispatcher(),group,[weak](islands::MediaSnapshot snapshot){if(auto self=weak.lock();self && self->view){
            std::wstring sources;
            for(const auto& source:snapshot.sources){auto title=source.title;for(auto& character:title)if(character==L'\t' || character==L'\r' || character==L'\n')character=L' ';sources+=std::to_wstring(source.id)+L"\t"+source.app_id+L"\t"+title+L"\n";}
            InterlockedIncrement(&self->shared->media_sequence);self->shared->media_revision=snapshot.media_revision;self->shared->active_source=snapshot.active_source;wcsncpy_s(self->shared->media_sources,sources.c_str(),_TRUNCATE);MemoryBarrier();InterlockedIncrement(&self->shared->media_sequence);
            self->view->media(std::move(snapshot));
        }});
        media->start();
        report(islands::Phase::active,S_OK,L"Native taskbar islands enabled. The media island follows the system's current media session.");
    }
    void execute(islands::Command requested,islands::Settings settings,LONG sequence,islands::MediaRequest media_request={}) noexcept {
        try {
            if(requested==islands::Command::enable)enable_ui(settings);
            else if(requested==islands::Command::disable){restore();report(islands::Phase::disabled,S_OK,L"The native taskbar is restored. Settings remain available.");}
            else if(requested==islands::Command::apply){if(view)view->apply(islands::normalize(settings));}
            else if(requested==islands::Command::inspect)inspect_tree();
            else if(requested==islands::Command::expand_media){if(view)view->expand_player();}
            else if(requested==islands::Command::preview_media){if(view)view->expand_player(true);}
            else if(requested==islands::Command::select_media_source){if(media)media->select_source(media_request.source);}
            else if(requested==islands::Command::seek_media){if(media)media->seek_elapsed(media_request.position,media_request.revision,media_request.source);}
        }catch(const winrt::hresult_error& error){const auto detail=L"XAML / структура панели: "+std::wstring(error.message())+L" Изменения сняты.";restore();report(islands::Phase::failed,error.code(),detail.c_str());}
        catch(...){const auto error=winrt::to_hresult();restore();report(islands::Phase::failed,error,L"Native taskbar structure or a requested XAML override is incompatible. All own elements were removed.");}
        InterlockedExchange(&shared->acknowledgement,sequence);SetEvent(changed.get());
    }
    void stop_ui() noexcept {
        restore();
        for(const auto& group:tasks)group->close();
        frame=nullptr;grid=nullptr;timer=nullptr;storyboard=nullptr;original_local=nullptr;
        winrt::clear_factory_cache();
        SetEvent(restored.get());
    }
    HRESULT detach_tree() noexcept {
        if(registration && WaitForSingleObject(registration.get(),5000)!=WAIT_OBJECT_0)return HRESULT_FROM_WIN32(WAIT_TIMEOUT);
        if(service && callback) {
            // Advise can retain the callback before returning a failure.
            const auto result=service->UnadviseVisualTreeChange(callback.get());
            if(FAILED(result))return result;
        }
        callback=nullptr;return S_OK;
    }
    bool drain_async() noexcept {
        const auto deadline=GetTickCount64()+5000;
        for(const auto& group:tasks){
            const auto now=GetTickCount64();
            if(WaitForSingleObject(group->drained(),now<deadline?static_cast<DWORD>(deadline-now):0)!=WAIT_OBJECT_0)return false;
        }
        tasks.clear();return true;
    }
    void write_cleanup(HRESULT result) noexcept {
        try {
            HMODULE module=nullptr;
            GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&DllCanUnloadNow),&module);
            wchar_t path[32768]{};GetModuleFileNameW(module,path,ARRAYSIZE(path));
            std::wofstream output(std::filesystem::path(path).parent_path()/L"last-exit.txt");
            output<<L"cleanup_hresult="<<static_cast<unsigned long>(result)<<L"\nui_restored="<<(WaitForSingleObject(restored.get(),0)==WAIT_OBJECT_0)
                <<L"\ntree_unsubscribed="<<(!callback)<<L"\nasync_groups="<<tasks.size()
                <<L"\nmodule_locks="<<static_cast<std::uint32_t>(winrt::get_module_lock())<<L"\n";
            Handle snapshot{CreateToolhelp32Snapshot(TH32CS_SNAPMODULE,GetCurrentProcessId())};
            MODULEENTRY32W item{sizeof(item)};
            if(snapshot && Module32FirstW(snapshot.get(),&item))do{
                if(_wcsicmp(item.szModule,L"Stillbar.Core.dll"))continue;
                const auto can_unload=reinterpret_cast<HRESULT(WINAPI*)()>(GetProcAddress(item.hModule,"DllCanUnloadNow"));
                output<<L"resident="<<item.szExePath<<L" can_unload="<<(can_unload?can_unload():E_NOINTERFACE)<<L"\n";
            }while(Module32NextW(snapshot.get(),&item));
        }catch(...){}
    }
    void dispatch_stop() noexcept {
        if(stopping.exchange(true))return;
        try {
            if(frame) {
                const auto self=shared_from_this();
                frame.Dispatcher().RunAsync(winrt::Windows::UI::Core::CoreDispatcherPriority::Normal,[self]{self->stop_ui();});
            } else stop_ui();
        } catch(...) { report(islands::Phase::failed,winrt::to_hresult(),L"Unable to dispatch cleanup onto the taskbar UI thread."); }
    }
    static HRESULT run_owner_loop(void* context) {
        const auto apartment=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
        std::unique_ptr<WaitContext> held{static_cast<WaitContext*>(context)};
        const auto self=held->runtime;
        HANDLE handles[]{self->owner.get(),self->command.get(),self->root_found.get()};
        DWORD handle_count=3;
        for(;;){
            const auto wait=WaitForMultipleObjects(handle_count,handles,FALSE,INFINITE);
            if(wait==WAIT_OBJECT_0)break;
            if(wait==WAIT_OBJECT_0+2){
                handle_count=2;
                const auto result=self->detach_tree();
                if(FAILED(result)){self->report(islands::Phase::failed,result,L"Unable to detach the discovery callback.");break;}
                continue;
            }
            if(wait!=WAIT_OBJECT_0+1)break;
            const auto requested=static_cast<islands::Command>(self->shared->command);
            if(requested==islands::Command::stop)break;
            const auto sequence=self->shared->request;const auto settings=self->shared->settings;const auto media_request=self->shared->media_request;
            try{if(self->frame)self->frame.Dispatcher().RunAsync(winrt::Windows::UI::Core::CoreDispatcherPriority::Normal,[self,requested,settings,sequence,media_request]{if(!self->stopping)self->execute(requested,settings,sequence,media_request);});}catch(...){self->report(islands::Phase::failed,winrt::to_hresult(),L"Unable to dispatch the requested change onto the taskbar UI thread.");}
        }
        self->dispatch_stop();
        const auto detached=self->detach_tree();
        HRESULT result=detached;
        if(WaitForSingleObject(self->restored.get(),5000)!=WAIT_OBJECT_0 || !self->drain_async())result=HRESULT_FROM_WIN32(WAIT_TIMEOUT);
        if(SUCCEEDED(result)) {
            self->callback=nullptr;self->service=nullptr;self->diagnostics=nullptr;
            winrt::clear_factory_cache();
            self->registration.reset();
            self->write_cleanup(result);
            InterlockedExchange(&self->shared->acknowledgement,self->shared->request);
            InterlockedExchange(&self->shared->cleanup_complete,1);
            self->report(islands::Phase::disabled,S_OK,L"UI restored, callbacks revoked, asynchronous work drained. The worker is preparing to unload the module.");
            std::lock_guard lock(runtime_mutex);
            if(runtime==self)runtime.reset();
        }else{self->write_cleanup(result);self->report(islands::Phase::failed,result,L"Cleanup did not complete within the deadline. A full Explorer restart is required.");}
        if(SUCCEEDED(apartment))CoUninitialize();
        return result;
    }
    static DWORD WINAPI wait_for_owner(void* context) {
        const auto module=static_cast<WaitContext*>(context)->module;
        const auto result=run_owner_loop(context);
        const bool idle=SUCCEEDED(result) && winrt::get_module_lock().wait_idle(5000);
        if(idle){
            // A final COM Release can still be returning when its object counter reaches zero.
            Sleep(200);
            if(!winrt::get_module_lock())FreeLibrary(module);
        }
        FreeLibraryAndExitThread(module,SUCCEEDED(result)?0:static_cast<DWORD>(result));
    }
    void start_waiter() {
        HMODULE module=nullptr;
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,reinterpret_cast<LPCWSTR>(&wait_for_owner),&module)){
            report(islands::Phase::failed,HRESULT_FROM_WIN32(GetLastError()),L"Unable to acquire the cleanup thread's module reference.");return;
        }
        auto held=new WaitContext{shared_from_this(),module};
        DWORD id=0;
        Handle thread{CreateThread(nullptr,0,wait_for_owner,held,0,&id)};
        if(!thread) {const auto error=GetLastError();delete held;FreeLibrary(module);report(islands::Phase::failed,HRESULT_FROM_WIN32(error),L"Unable to start the controller-lifetime cleanup watcher.");}
        else InterlockedExchange(&shared->worker_thread_id,static_cast<LONG>(id));
    }
    static DWORD WINAPI register_callback(void* context) {
        std::unique_ptr<std::shared_ptr<Runtime>> held{static_cast<std::shared_ptr<Runtime>*>(context)};
        const auto self=*held;
        try {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
            auto result=islands::element_diagnostics::advise(self->service.get(),self->callback.get());
            for(int attempt=0;result==E_UNEXPECTED && !self->shared->elements_seen && attempt<2;++attempt){
                if(WaitForSingleObject(self->owner.get(),150)==WAIT_OBJECT_0)break;
                result=islands::element_diagnostics::advise(self->service.get(),self->callback.get());
            }
            if(FAILED(result))self->report(islands::Phase::failed,result,result==E_UNEXPECTED?L"Служба XAML Windows отклонила подключение к панели. Для восстановления диагностики перезапустите Проводник и включите панель повторно.":L"Не удалось подписаться на структуру XAML панели: IVisualTreeService::AdviseVisualTreeChange.");
            winrt::uninit_apartment();
        } catch(...) { self->report(islands::Phase::failed,winrt::to_hresult(),L"Unable to register the visual-tree callback."); }
        return 0;
    }
    HRESULT start_registration() {
        auto held=new std::shared_ptr<Runtime>(shared_from_this());
        registration.reset(CreateThread(nullptr,0,register_callback,held,0,nullptr));
        if(!registration){delete held;return HRESULT_FROM_WIN32(GetLastError());}
        return S_OK;
    }
};

struct Watcher : winrt::implements<Watcher,IVisualTreeServiceCallback2,winrt::non_agile> {
    std::weak_ptr<Runtime> owner;
    explicit Watcher(std::shared_ptr<Runtime> value):owner(value){}
    HRESULT STDMETHODCALLTYPE OnVisualTreeChange(ParentChildRelation,VisualElement element,VisualMutationType mutation) noexcept override {
        const auto self=owner.lock();
        if(!self || self->stopping || mutation!=Add)return S_OK;
        InterlockedIncrement(&self->shared->elements_seen);
        if(!element.Type || wcscmp(element.Type,L"Taskbar.TaskbarFrame"))return S_OK;
        try {
            winrt::com_ptr<IInspectable> object;
            const auto resolved=self->diagnostics->GetIInspectableFromHandle(element.Handle,reinterpret_cast<::IInspectable**>(object.put_void()));
            if(FAILED(resolved))return S_OK;
            if(winrt::get_class_name(object.as<winrt::Windows::Foundation::IInspectable>())!=L"Taskbar.TaskbarFrame")return S_OK;
            InterlockedIncrement(&self->shared->frames_seen);
            const auto candidate=object.as<wux::FrameworkElement>();const auto weak_frame=winrt::make_weak(candidate);const auto weak=std::weak_ptr<Runtime>{self};
            candidate.Dispatcher().RunAsync(winrt::Windows::UI::Core::CoreDispatcherPriority::Low,[weak,weak_frame]{if(auto held=weak.lock();held && !held->stopping)try{if(auto element=weak_frame.get())held->consider_frame(element);}catch(...){held->report(islands::Phase::failed,winrt::to_hresult(),L"Не удалось применить шаблон панели перед проверкой готовности.");SetEvent(held->root_found.get());}});
        } catch(...) { self->report(islands::Phase::failed,winrt::to_hresult(),L"The taskbar root is incompatible with Windows.UI.Xaml."); }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnElementStateChanged(InstanceHandle,VisualElementState,LPCWSTR) noexcept override { return S_OK; }
};

struct Provider : winrt::implements<Provider,IObjectWithSite,winrt::non_agile> {
    std::weak_ptr<Runtime> active;
    HRESULT STDMETHODCALLTYPE SetSite(IUnknown* value) noexcept override {
        try {
            if(!value) { if(auto self=active.lock())self->dispatch_stop();active.reset();return S_OK; }
            std::lock_guard lock(runtime_mutex);
            if(runtime && !runtime->stopping)return HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
            auto context=std::make_shared<Runtime>();
            if(!context->connect())return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
            winrt::com_ptr<IUnknown> site;site.copy_from(value);
            context->diagnostics=site.as<IXamlDiagnostics>();context->service=site.as<IVisualTreeService>();
            context->callback=winrt::make_self<Watcher>(context).as<IVisualTreeServiceCallback2>();
            runtime=context;active=context;
            context->report(islands::Phase::attached,S_OK,L"Own XAML diagnostics provider attached to Explorer; waiting for the native taskbar root.");
            const auto registered=context->start_registration();
            if(FAILED(registered))return registered;
            context->start_waiter();return S_OK;
        } catch(...) { const auto error=winrt::to_hresult(); if(runtime)runtime->report(islands::Phase::failed,error,L"Unable to initialize our XAML diagnostics provider.");return error; }
    }
    HRESULT STDMETHODCALLTYPE GetSite(REFIID iid,void** result) noexcept override {
        if(!result)return E_POINTER;*result=nullptr;
        if(auto self=active.lock();self && self->diagnostics)return self->diagnostics->QueryInterface(iid,result);
        return E_FAIL;
    }
};
struct Factory : winrt::implements<Factory,IClassFactory,winrt::non_agile> {
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer,REFIID iid,void** result) noexcept override {
        if(outer)return CLASS_E_NOAGGREGATION;
        try{return winrt::make<Provider>().as(iid,result);}catch(...){return winrt::to_hresult();}
    }
    HRESULT STDMETHODCALLTYPE LockServer(BOOL) noexcept override { return S_OK; }
};
}

STDAPI DllGetClassObject(REFCLSID id,REFIID iid,void** result) {
    if(id!=islands::provider_id)return CLASS_E_CLASSNOTAVAILABLE;
    try{return winrt::make<Factory>().as(iid,result);}catch(...){return winrt::to_hresult();}
}
STDAPI DllCanUnloadNow() { return winrt::get_module_lock()?S_FALSE:S_OK; }
