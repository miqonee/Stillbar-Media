#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <unknwn.h>
#include <shellapi.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include "compatibility.hpp"
#include "media_async_wait.hpp"
#include <chrono>
#include <memory>
#include <sstream>
#include <thread>

namespace media=winrt::Windows::Media::Control;
namespace foundation=winrt::Windows::Foundation;
using Session=media::GlobalSystemMediaTransportControlsSession;

namespace {
std::string quoted(const std::wstring_view value) {
    std::string output="\"";
    for(const auto ch:winrt::to_string(value))switch(ch){
        case '\\':output+="\\\\";break;case '"':output+="\\\"";break;
        case '\r':output+="\\r";break;case '\n':output+="\\n";break;case '\t':output+="\\t";break;
        default:if(static_cast<unsigned char>(ch)<32)output+='?';else output+=ch;
    }
    return output+'"';
}
using islands::media_diagnostics::bounded_result;
struct Report {
    std::ostringstream output;
    void field(const std::string& key,const std::wstring_view value){output<<key<<'='<<quoted(value)<<'\n';}
    template<class Value> void number(const std::string& key,Value value){output<<key<<'='<<value<<'\n';}
    template<class Read> void read(const std::string& key,Read read){
        try{read();field(key+".result",L"0x00000000");}
        catch(...){field(key+".result",islands::error_code(winrt::to_hresult()));}
    }
};
const wchar_t* status_name(media::GlobalSystemMediaTransportControlsSessionPlaybackStatus status) {
    using Status=media::GlobalSystemMediaTransportControlsSessionPlaybackStatus;
    switch(status){case Status::Closed:return L"Closed";case Status::Opened:return L"Opened";
        case Status::Changing:return L"Changing";case Status::Stopped:return L"Stopped";
        case Status::Playing:return L"Playing";case Status::Paused:return L"Paused";default:return L"Unknown";}
}
void inspect(Report& report,const std::string& key,const Session& session,const Session& current,bool metadata) {
    report.number(key+".current",session==current);
    report.read(key+".current_identity",[&]{report.number(key+".same_current_identity",current && session.as<IUnknown>().get()==current.as<IUnknown>().get());});
    report.read(key+".identity",[&]{report.field(key+".app_id",session.SourceAppUserModelId());});
    report.read(key+".playback",[&]{
        const auto info=session.GetPlaybackInfo();report.number(key+".playback.present",static_cast<bool>(info));
        if(!info)return;
        report.read(key+".status",[&]{const auto status=info.PlaybackStatus();report.field(key+".status",status_name(status));report.number(key+".status.value",static_cast<int>(status));});
        report.read(key+".controls",[&]{
            const auto controls=info.Controls();report.number(key+".controls.present",static_cast<bool>(controls));if(!controls)return;
            report.number(key+".previous",controls.IsPreviousEnabled());report.number(key+".next",controls.IsNextEnabled());
            report.number(key+".play",controls.IsPlayEnabled());report.number(key+".pause",controls.IsPauseEnabled());
            report.number(key+".toggle",controls.IsPlayPauseToggleEnabled());report.number(key+".seek",controls.IsPlaybackPositionEnabled());
        });
    });
    report.read(key+".timeline",[&]{
        const auto value=session.GetTimelineProperties();report.number(key+".timeline.present",static_cast<bool>(value));if(!value)return;
        report.number(key+".start_ticks",value.StartTime().count());report.number(key+".end_ticks",value.EndTime().count());
        report.number(key+".position_ticks",value.Position().count());report.number(key+".minimum_ticks",value.MinSeekTime().count());
        report.number(key+".maximum_ticks",value.MaxSeekTime().count());report.number(key+".updated_ticks",value.LastUpdatedTime().time_since_epoch().count());
    });
    if(metadata)report.read(key+".metadata",[&]{
        const auto value=bounded_result(session.TryGetMediaPropertiesAsync(),300);
        report.number(key+".metadata.present",static_cast<bool>(value));if(!value)return;
        report.number(key+".title_present",!value.Title().empty());report.number(key+".artist_present",!value.Artist().empty());
        const auto thumbnail=value.Thumbnail();report.number(key+".thumbnail_present",static_cast<bool>(thumbnail));
        if(thumbnail)report.read(key+".thumbnail_open",[&]{
            const auto stream=bounded_result(thumbnail.OpenReadAsync(),1500);report.number(key+".thumbnail_stream",static_cast<bool>(stream));if(!stream)return;
            report.number(key+".thumbnail_bytes",stream.Size());report.field(key+".thumbnail_type",stream.ContentType());
            report.read(key+".thumbnail_decode",[&]{
                const auto decoder=bounded_result(winrt::Windows::Graphics::Imaging::BitmapDecoder::CreateAsync(stream),1500);
                report.number(key+".thumbnail_decoder",static_cast<bool>(decoder));if(!decoder)return;
                report.number(key+".thumbnail_width",decoder.PixelWidth());report.number(key+".thumbnail_height",decoder.PixelHeight());
            });
        });
    });
}
bool collect(Report& report) {
    bool success=false;
    report.field("tool",L"Stillbar.MediaDiagnostics");report.field("version",winrt::to_hstring(STILLBAR_VERSION_STRING));
    report.number("format_revision",3);
    SYSTEMTIME time{};GetSystemTime(&time);wchar_t stamp[40]{};
    swprintf_s(stamp,L"%04u-%02u-%02uT%02u:%02u:%02uZ",time.wYear,time.wMonth,time.wDay,time.wHour,time.wMinute,time.wSecond);
    report.field("utc",stamp);const auto environment=islands::windows_environment();report.field("windows",environment.describe());
    report.field("windows.result",islands::error_code(environment.result));
    report.field("privacy",L"No titles, artists, URLs, audio, browser history or command lines are collected. Local file only.");
    report.read("manager",[&]{
        const auto manager=bounded_result(media::GlobalSystemMediaTransportControlsSessionManager::RequestAsync(),3000);
        report.number("manager.present",static_cast<bool>(manager));if(!manager)return;
        for(unsigned sample=0;sample<3;++sample){
            if(sample)std::this_thread::sleep_for(std::chrono::milliseconds(500));
            const auto prefix="sample."+std::to_string(sample);Session current{nullptr};
            report.read(prefix+".current",[&]{current=manager.GetCurrentSession();report.number(prefix+".current.present",static_cast<bool>(current));});
            if(current)report.read(prefix+".current_detail",[&]{inspect(report,prefix+".current_detail",current,current,sample==0);});
            report.read(prefix+".sessions",[&]{
                const auto sessions=manager.GetSessions();report.number(prefix+".sessions.present",static_cast<bool>(sessions));
                if(!sessions)return;
                report.number(prefix+".count",sessions.Size());unsigned index=0;
                for(const auto& session:sessions){
                    if(index>=32){report.number(prefix+".truncated",true);break;}
                    const auto key=prefix+".session."+std::to_string(index++);
                    if(session)report.read(key,[&]{inspect(report,key,session,current,sample==0 && index<=8);});
                    else report.number(key+".present",false);
                }
                success=true;
            });
        }
    });
    return success;
}
std::filesystem::path default_path() {
    wchar_t executable[32768]{};if(!GetModuleFileNameW(nullptr,executable,32768))winrt::throw_last_error();
    SYSTEMTIME time{};GetLocalTime(&time);wchar_t name[96]{};
    swprintf_s(name,L"media-diagnostics-%04u%02u%02u-%02u%02u%02u-%lu.txt",time.wYear,time.wMonth,time.wDay,time.wHour,time.wMinute,time.wSecond,GetCurrentProcessId());
    return std::filesystem::path(executable).parent_path()/name;
}
void save(const std::filesystem::path& path,const std::string& text) {
    std::ofstream file(path,std::ios::binary);file.exceptions(std::ios::badbit|std::ios::failbit);file<<"\xEF\xBB\xBF"<<text;file.close();
}
}
int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int) {
    bool quiet=false,explicit_path=false;std::filesystem::path path;
    try{
        int count=0;auto arguments=CommandLineToArgvW(GetCommandLineW(),&count);if(!arguments)winrt::throw_last_error();
        const auto owned=std::unique_ptr<wchar_t*,decltype(&LocalFree)>(arguments,LocalFree);
        for(int index=1;index<count;++index){
            const std::wstring argument=arguments[index];
            if(argument==L"--quiet")quiet=true;
            else if(argument.starts_with(L"--output=")){path=argument.substr(9);explicit_path=true;}
            else throw winrt::hresult_invalid_argument(L"Неизвестный аргумент диагностики.");
        }
        if(path.empty())path=default_path();
        winrt::init_apartment(winrt::apartment_type::multi_threaded);Report report;const auto success=collect(report);
        try{save(path,report.output.str());}catch(...){if(explicit_path)throw;path=std::filesystem::temp_directory_path()/path.filename();save(path,report.output.str());}
        if(!quiet){const auto message=L"Отчёт сохранён:\n"+std::filesystem::absolute(path).wstring()+L"\n\nПередайте этот файл разработчику. Панель задач и воспроизведение не изменялись.";
            MessageBoxW(nullptr,message.c_str(),L"Диагностика медиа Stillbar",MB_OK|(success?MB_ICONINFORMATION:MB_ICONWARNING));}
        return success?0:1;
    }catch(...){if(!quiet)MessageBoxW(nullptr,L"Не удалось сохранить диагностику. Запустите утилиту из доступной для записи папки.",L"Диагностика медиа Stillbar",MB_OK|MB_ICONERROR);return 2;}
}
