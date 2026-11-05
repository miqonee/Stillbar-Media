#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shobjidl.h>
#include <wincrypt.h>
#include <SystemMediaTransportControlsInterop.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Media.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fcntl.h>
#include <io.h>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace media=winrt::Windows::Media;
namespace json=winrt::Windows::Data::Json;
namespace streams=winrt::Windows::Storage::Streams;
constexpr UINT incoming_message=WM_APP+1;
constexpr UINT stop_message=WM_APP+2;
constexpr UINT open_extensions_message=WM_APP+3;
constexpr std::uint32_t max_message=384*1024;
std::mutex output_mutex;

void send(const json::JsonObject& value) {
    const auto bytes=winrt::to_string(value.Stringify());
    if(bytes.size()>max_message)return;
    const auto length=static_cast<std::uint32_t>(bytes.size());
    std::lock_guard lock(output_mutex);
    if(std::fwrite(&length,1,sizeof(length),stdout)!=sizeof(length))return;
    std::fwrite(bytes.data(),1,bytes.size(),stdout);std::fflush(stdout);
}
void command(const std::wstring& id,const wchar_t* action,double position=-1) {
    json::JsonObject value;value.SetNamedValue(L"type",json::JsonValue::CreateStringValue(L"command"));
    value.SetNamedValue(L"id",json::JsonValue::CreateStringValue(id));value.SetNamedValue(L"action",json::JsonValue::CreateStringValue(action));
    if(position>=0)value.SetNamedValue(L"position",json::JsonValue::CreateNumberValue(position));send(value);
}
void artwork_status(const std::wstring& id,double revision,HRESULT result) {
    json::JsonObject value;value.SetNamedValue(L"type",json::JsonValue::CreateStringValue(L"artwork_status"));
    value.SetNamedValue(L"id",json::JsonValue::CreateStringValue(id));value.SetNamedValue(L"revision",json::JsonValue::CreateNumberValue(revision));
    value.SetNamedValue(L"result",json::JsonValue::CreateStringValue(SUCCEEDED(result)?L"applied":L"error"));
    if(FAILED(result)){wchar_t code[16]{};swprintf_s(code,L"0x%08lX",static_cast<unsigned long>(result));value.SetNamedValue(L"error",json::JsonValue::CreateStringValue(code));}
    send(value);
}
bool valid_id(const std::wstring& id) {
    if(id.size()!=36)return false;
    for(std::size_t i=0;i<id.size();++i){const auto c=id[i];if(i==8 || i==13 || i==18 || i==23){if(c!=L'-')return false;}else if(!((c>=L'0' && c<=L'9') || (c>=L'a' && c<=L'f')))return false;}
    return true;
}
bool is_boolean(const json::JsonObject& value,const wchar_t* name) {return value.GetNamedValue(name).ValueType()==json::JsonValueType::Boolean;}
struct Source {
    HWND window=nullptr;media::SystemMediaTransportControls controls{nullptr};
    winrt::event_token button{},seek{};
    std::wstring artwork;
    ~Source(){close();}
    void close() noexcept {
        try{if(controls){controls.ButtonPressed(button);controls.PlaybackPositionChangeRequested(seek);controls.IsEnabled(false);controls.DisplayUpdater().ClearAll();controls=nullptr;}}catch(...){}
        if(window){DestroyWindow(window);window=nullptr;}
    }
    void open(const std::wstring& id) {
        window=CreateWindowExW(WS_EX_TOOLWINDOW,L"STATIC",L"Stillbar Media",WS_POPUP,0,0,0,0,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        if(!window)throw winrt::hresult_error(HRESULT_FROM_WIN32(GetLastError()));
        const auto interop=winrt::get_activation_factory<media::SystemMediaTransportControls,ISystemMediaTransportControlsInterop>();
        winrt::check_hresult(interop->GetForWindow(window,winrt::guid_of<media::SystemMediaTransportControls>(),winrt::put_abi(controls)));
        controls.IsEnabled(true);
        button=controls.ButtonPressed([id](auto&&,const media::SystemMediaTransportControlsButtonPressedEventArgs& args){
            switch(args.Button()){
            case media::SystemMediaTransportControlsButton::Play:command(id,L"play");break;
            case media::SystemMediaTransportControlsButton::Pause:command(id,L"pause");break;
            case media::SystemMediaTransportControlsButton::Previous:command(id,L"previoustrack");break;
            case media::SystemMediaTransportControlsButton::Next:command(id,L"nexttrack");break;
            default:break;}
        });
        seek=controls.PlaybackPositionChangeRequested([id](auto&&,const media::PlaybackPositionChangeRequestedEventArgs& args){command(id,L"seekto",args.RequestedPlaybackPosition().count()/10000000.0);});
    }
    void set_artwork(const std::wstring& encoded) {
        if(artwork==encoded)return;
        auto updater=controls.DisplayUpdater();updater.Thumbnail(nullptr);
        if(encoded.empty()){artwork.clear();return;}
        DWORD length=0;if(!CryptStringToBinaryW(encoded.c_str(),static_cast<DWORD>(encoded.size()),CRYPT_STRING_BASE64,nullptr,&length,nullptr,nullptr) || !length || length>262144)throw winrt::hresult_invalid_argument();
        std::vector<std::uint8_t> bytes(length);
        if(!CryptStringToBinaryW(encoded.c_str(),static_cast<DWORD>(encoded.size()),CRYPT_STRING_BASE64,bytes.data(),&length,nullptr,nullptr))throw winrt::hresult_invalid_argument();
        streams::InMemoryRandomAccessStream stream;streams::DataWriter writer(stream.GetOutputStreamAt(0));writer.WriteBytes(bytes);writer.StoreAsync().get();writer.FlushAsync().get();writer.DetachStream();stream.Seek(0);
        const auto decoder=winrt::Windows::Graphics::Imaging::BitmapDecoder::CreateAsync(stream).get();
        if(!decoder.PixelWidth() || !decoder.PixelHeight() || decoder.PixelWidth()>8192 || decoder.PixelHeight()>8192)throw winrt::hresult_invalid_argument();stream.Seek(0);
        updater.Thumbnail(streams::RandomAccessStreamReference::CreateFromStream(stream));
        artwork=encoded;
    }
    void update(const std::wstring& id,const json::JsonObject& value) {
        const auto title=value.GetNamedString(L"title"),artist=value.GetNamedString(L"artist"),image=value.GetNamedString(L"artworkData",L"");
        const double position=value.GetNamedNumber(L"position"),duration=value.GetNamedNumber(L"duration");
        if(title.size()>1024 || artist.size()>1024 || image.size()>350000 || !std::isfinite(position) || !std::isfinite(duration) || position<0 || duration<0 || position>86400000 || duration>86400000 || !is_boolean(value,L"playing") || !is_boolean(value,L"seek") || !is_boolean(value,L"previous") || !is_boolean(value,L"next"))throw winrt::hresult_invalid_argument();
        const bool playing=value.GetNamedBoolean(L"playing");controls.IsPlayEnabled(!playing);controls.IsPauseEnabled(playing);
        controls.IsPreviousEnabled(value.GetNamedBoolean(L"previous"));controls.IsNextEnabled(value.GetNamedBoolean(L"next"));
        media::SystemMediaTransportControlsTimelineProperties timeline;timeline.StartTime(std::chrono::seconds(0));timeline.EndTime(std::chrono::duration_cast<winrt::Windows::Foundation::TimeSpan>(std::chrono::duration<double>(duration)));
        timeline.MinSeekTime(std::chrono::seconds(0));timeline.MaxSeekTime(value.GetNamedBoolean(L"seek")?timeline.EndTime():std::chrono::seconds(0));timeline.Position(std::chrono::duration_cast<winrt::Windows::Foundation::TimeSpan>(std::chrono::duration<double>(position)));controls.UpdateTimelineProperties(timeline);
        const auto updater=controls.DisplayUpdater();updater.Type(media::MediaPlaybackType::Music);updater.MusicProperties().Title(title);updater.MusicProperties().Artist(artist);
        const bool image_changed=artwork!=image;HRESULT image_result=S_OK;
        try{set_artwork(std::wstring(image));}catch(const winrt::hresult_error& error){image_result=error.code();artwork.clear();updater.Thumbnail(nullptr);}
        updater.Update();
        if(image_changed && !image.empty())artwork_status(id,value.GetNamedNumber(L"artworkRevision",0),image_result);
        controls.PlaybackStatus(playing?media::MediaPlaybackStatus::Playing:media::MediaPlaybackStatus::Paused);
    }
};
struct Host {
    std::map<std::wstring,std::unique_ptr<Source>> sources;
    bool hello=false;HANDLE connected=nullptr;
    HANDLE page_opened=CreateEventW(nullptr,TRUE,FALSE,L"Local\\Stillbar.Yandex.ExtensionsOpened.v1");
    HANDLE page_failed=CreateEventW(nullptr,TRUE,FALSE,L"Local\\Stillbar.Yandex.ExtensionsFailed.v1");
    ~Host(){if(connected)CloseHandle(connected);if(page_opened)CloseHandle(page_opened);if(page_failed)CloseHandle(page_failed);}
    void receive(const std::string& text) {
        try{
            const auto value=json::JsonObject::Parse(winrt::to_hstring(text));const auto type=value.GetNamedString(L"type");
            if(type==L"hello"){
                if(value.GetNamedNumber(L"version")!=1)return;hello=true;
                if(!connected)connected=CreateEventW(nullptr,TRUE,TRUE,L"Local\\Stillbar.Yandex.Connected.v1");
                json::JsonObject reply;reply.SetNamedValue(L"type",json::JsonValue::CreateStringValue(L"hello"));reply.SetNamedValue(L"version",json::JsonValue::CreateNumberValue(1));send(reply);return;
            }
            if(!hello)return;
            if(type==L"extensions_opened"){if(value.GetNamedBoolean(L"opened")){if(page_opened)SetEvent(page_opened);}else if(page_failed)SetEvent(page_failed);return;}
            const std::wstring id(value.GetNamedString(L"id"));if(!valid_id(id))return;
            if(type==L"remove"){sources.erase(id);return;}
            if(type!=L"state")return;
            auto found=sources.find(id);
            if(found==sources.end()){
                if(sources.size()>=64)return;auto source=std::make_unique<Source>();source->open(id);source->update(id,value);sources.emplace(id,std::move(source));
            }else found->second->update(id,value);
        }catch(...){}
    }
};
LRESULT CALLBACK host_window(HWND window,UINT message,WPARAM wparam,LPARAM lparam) {
    auto host=reinterpret_cast<Host*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE){host=static_cast<Host*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(host));}
    if(message==incoming_message){std::unique_ptr<std::string> data(reinterpret_cast<std::string*>(lparam));if(data)host->receive(*data);else DestroyWindow(window);return 0;}
    if(message==stop_message){DestroyWindow(window);return 0;}
    if(message==open_extensions_message){json::JsonObject value;value.SetNamedValue(L"type",json::JsonValue::CreateStringValue(L"open_extensions"));send(value);return 0;}
    if(message==WM_DESTROY){PostQuitMessage(0);return 0;}
    return DefWindowProcW(window,message,wparam,lparam);
}
int main() {
    _setmode(_fileno(stdin),_O_BINARY);_setmode(_fileno(stdout),_O_BINARY);
    winrt::init_apartment(winrt::apartment_type::multi_threaded);SetCurrentProcessExplicitAppUserModelID(L"Stillbar.Yandex.Media");
    Host host;WNDCLASSW type{};type.lpfnWndProc=host_window;type.hInstance=GetModuleHandleW(nullptr);type.lpszClassName=L"Stillbar.BrowserHost";RegisterClassW(&type);
    const auto window=CreateWindowW(type.lpszClassName,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,type.hInstance,&host);if(!window)return 1;
    const auto disable=CreateEventW(nullptr,TRUE,FALSE,L"Local\\Stillbar.Yandex.Disable.v1");
    const auto open_extensions=CreateEventW(nullptr,FALSE,FALSE,L"Local\\Stillbar.Yandex.OpenExtensions.v1");
    const auto done=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!disable || !done || !open_extensions)return 1;
    std::thread input([window]{
        for(;;){std::uint32_t length=0;if(std::fread(&length,1,sizeof(length),stdin)!=sizeof(length) || !length || length>max_message)break;
            auto data=std::make_unique<std::string>(length,'\0');if(std::fread(data->data(),1,length,stdin)!=length)break;
            if(!PostMessageW(window,incoming_message,0,reinterpret_cast<LPARAM>(data.get())))break;data.release();}
        PostMessageW(window,incoming_message,0,0);
    });
    std::thread watcher([window,disable,done,open_extensions]{const HANDLE events[]{disable,done,open_extensions};for(;;){const auto result=WaitForMultipleObjects(3,events,FALSE,INFINITE);if(result==WAIT_OBJECT_0+2){PostMessageW(window,open_extensions_message,0,0);continue;}if(result==WAIT_OBJECT_0)PostMessageW(window,stop_message,0,0);break;}});
    MSG message{};while(GetMessageW(&message,nullptr,0,0)>0){TranslateMessage(&message);DispatchMessageW(&message);}
    SetEvent(done);CancelSynchronousIo(input.native_handle());input.join();watcher.join();CloseHandle(disable);CloseHandle(done);CloseHandle(open_extensions);return 0;
}
