#pragma once
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <filesystem>
#include <cstring>
#include <fstream>
#include <string>
#include "browser_extension_id.hpp"
#include "browser_store.hpp"

namespace islands::browser {
inline constexpr wchar_t registry_path[]=L"Software\\Chromium\\NativeMessagingHosts\\org.stillbar.media";
inline constexpr wchar_t connected_event[]=L"Local\\Stillbar.Yandex.Connected.v1";
inline constexpr wchar_t disable_event[]=L"Local\\Stillbar.Yandex.Disable.v1";
inline std::filesystem::path manifest_path(){wchar_t path[MAX_PATH]{};if(FAILED(SHGetFolderPathW(nullptr,CSIDL_LOCAL_APPDATA,nullptr,SHGFP_TYPE_CURRENT,path)))return {};return std::filesystem::path(path)/L"Stillbar"/L"Browser"/L"org.stillbar.media.json";}
inline std::filesystem::path extension_path(){const auto manifest=manifest_path();return manifest.empty()?std::filesystem::path{}:manifest.parent_path()/L"Extension";}
inline std::string utf8(const std::wstring& value){const int size=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);if(!size)return {};std::string result(size,'\0');WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),result.data(),size,nullptr,nullptr);return result;}
inline std::string json_string(const std::wstring& value){std::string result="\"";for(const unsigned char c:utf8(value)){if(c=='\\' || c=='\"')result+='\\';if(c<32)return {};result+=static_cast<char>(c);}return result+'\"';}
inline bool prepared_at(const std::filesystem::path& path){const auto manifest=path.wstring();if(manifest.empty())return false;wchar_t actual[32768]{};DWORD size=sizeof(actual);return RegGetValueW(HKEY_CURRENT_USER,registry_path,nullptr,RRF_RT_REG_SZ,nullptr,actual,&size)==ERROR_SUCCESS && _wcsicmp(actual,manifest.c_str())==0;}
inline bool prepared(){return prepared_at(manifest_path());}
inline bool connected(){const auto event=OpenEventW(SYNCHRONIZE,FALSE,connected_event);if(!event)return false;const bool result=WaitForSingleObject(event,0)==WAIT_OBJECT_0;CloseHandle(event);return result;}
inline LONG prepare_at(const std::filesystem::path& directory,const std::filesystem::path& manifest,std::wstring_view extension_id=STILLBAR_BROWSER_EXTENSION_ID,bool unpacked=true){
    const auto host=directory/L"Stillbar.BrowserHost.exe",extension=directory/L"browser-extension"/L"manifest.json";
    if(manifest.empty() || !std::filesystem::is_regular_file(host) || !std::filesystem::is_regular_file(extension))return ERROR_FILE_NOT_FOUND;
    try{
        const wchar_t* files[]{L"manifest.json",L"background.js",L"content.js",L"page.js",L"popup.html",L"popup.js"};
        for(const auto file:files)if(!std::filesystem::is_regular_file(directory/L"browser-extension"/file))return ERROR_FILE_NOT_FOUND;
        const auto installed=manifest.parent_path()/L"Extension";std::filesystem::create_directories(manifest.parent_path());
        if(unpacked){std::filesystem::create_directories(installed);for(const auto file:files)std::filesystem::copy_file(directory/L"browser-extension"/file,installed/file,std::filesystem::copy_options::overwrite_existing);}
        const auto stamp=std::filesystem::last_write_time(host).time_since_epoch().count();
        const auto host_folder=manifest.parent_path()/L"Host"/std::to_wstring(stamp);std::filesystem::create_directories(host_folder);
        const auto installed_host=host_folder/host.filename();
        if(!std::filesystem::exists(installed_host))std::filesystem::copy_file(host,installed_host);
        const auto target=json_string(installed_host.wstring());if(target.empty())return ERROR_INVALID_NAME;
        std::ofstream file(manifest,std::ios::binary|std::ios::trunc);if(!file)return ERROR_WRITE_FAULT;
        file<<"{\"name\":\"org.stillbar.media\",\"description\":\"Stillbar Media browser bridge\",\"type\":\"stdio\",\"path\":"<<target<<",\"allowed_origins\":[\"chrome-extension://"<<utf8(std::wstring(extension_id))<<"/\"]}";file.close();if(!file)return ERROR_WRITE_FAULT;
        HKEY key=nullptr;const auto result=RegCreateKeyExW(HKEY_CURRENT_USER,registry_path,0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr);if(result!=ERROR_SUCCESS)return result;
        const auto path=manifest.wstring();const auto saved=RegSetValueExW(key,nullptr,0,REG_SZ,reinterpret_cast<const BYTE*>(path.c_str()),static_cast<DWORD>((path.size()+1)*sizeof(wchar_t)));RegCloseKey(key);return saved;
    }catch(...){return ERROR_WRITE_FAULT;}
}
inline LONG prepare(const std::filesystem::path& directory){return prepare_at(directory,manifest_path());}
inline LONG prepare_store(const std::filesystem::path& directory){return store_available()?prepare_at(directory,manifest_path(),store_extension_id,false):ERROR_NOT_SUPPORTED;}
inline LONG disable_at(const std::filesystem::path& manifest,bool notify){
    if(!prepared_at(manifest))return ERROR_FILE_NOT_FOUND;const auto result=RegDeleteKeyW(HKEY_CURRENT_USER,registry_path);if(result!=ERROR_SUCCESS)return result;
    if(notify)if(const auto event=OpenEventW(EVENT_MODIFY_STATE,FALSE,disable_event)){SetEvent(event);CloseHandle(event);}
    return ERROR_SUCCESS;
}
inline LONG disable(){return disable_at(manifest_path(),true);}
inline std::filesystem::path executable(){
    for(const auto folder:{CSIDL_LOCAL_APPDATA,CSIDL_PROGRAM_FILESX86,CSIDL_PROGRAM_FILES}){wchar_t path[MAX_PATH]{};if(FAILED(SHGetFolderPathW(nullptr,folder,nullptr,SHGFP_TYPE_CURRENT,path)))continue;const auto exe=std::filesystem::path(path)/L"Yandex"/L"YandexBrowser"/L"Application"/L"browser.exe";if(std::filesystem::is_regular_file(exe))return exe;}
    return {};
}
inline bool copy_extension_path(HWND parent){
    const auto text=extension_path().wstring();if(text.empty())return false;const auto size=(text.size()+1)*sizeof(wchar_t);const auto memory=GlobalAlloc(GMEM_MOVEABLE,size);if(!memory)return false;
    const auto destination=GlobalLock(memory);if(!destination){GlobalFree(memory);return false;}std::memcpy(destination,text.c_str(),size);GlobalUnlock(memory);
    if(!OpenClipboard(parent)){GlobalFree(memory);return false;}bool result=false;
    if(EmptyClipboard() && SetClipboardData(CF_UNICODETEXT,memory))result=true;else GlobalFree(memory);CloseClipboard();return result;
}
inline bool open_extension_page(HWND parent){
    (void)parent;
    const auto opened=OpenEventW(EVENT_MODIFY_STATE,FALSE,L"Local\\Stillbar.Yandex.ExtensionsOpened.v1");
    const auto failed=OpenEventW(EVENT_MODIFY_STATE,FALSE,L"Local\\Stillbar.Yandex.ExtensionsFailed.v1");
    if(!opened || !failed){if(opened)CloseHandle(opened);if(failed)CloseHandle(failed);return false;}
    ResetEvent(opened);ResetEvent(failed);CloseHandle(opened);CloseHandle(failed);
    if(const auto event=OpenEventW(EVENT_MODIFY_STATE,FALSE,L"Local\\Stillbar.Yandex.OpenExtensions.v1")){const bool sent=SetEvent(event)!=FALSE;CloseHandle(event);return sent;}
    return false;
}
inline bool event_signalled(const wchar_t* name){const auto event=OpenEventW(SYNCHRONIZE,FALSE,name);if(!event)return false;const bool result=WaitForSingleObject(event,0)==WAIT_OBJECT_0;CloseHandle(event);return result;}
inline bool open_store(HWND parent){const auto browser=executable();const auto url=store_url();if(browser.empty() || url.empty())return false;const auto arguments=L"--new-tab "+url;return reinterpret_cast<INT_PTR>(ShellExecuteW(parent,L"open",browser.c_str(),arguments.c_str(),nullptr,SW_SHOWNORMAL))>32;}
}
