#pragma once
#include "version.hpp"
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace islands {
struct WindowsEnvironment {
    DWORD build=0,revision=0;
    WORD architecture=0;
    HRESULT result=S_OK;
    bool supported()const{return SUCCEEDED(result) && build>=22000 && architecture==PROCESSOR_ARCHITECTURE_AMD64;}
    std::wstring describe()const{return L"Windows "+std::to_wstring(build)+L"."+std::to_wstring(revision)+(architecture==PROCESSOR_ARCHITECTURE_AMD64?L" x64":L" (native architecture "+std::to_wstring(architecture)+L")");}
};
inline WindowsEnvironment windows_environment() {
    WindowsEnvironment value;SYSTEM_INFO info{};GetNativeSystemInfo(&info);value.architecture=info.wProcessorArchitecture;
    constexpr auto key=L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
    wchar_t build[32]{};DWORD bytes=sizeof(build);
    const auto result=RegGetValueW(HKEY_LOCAL_MACHINE,key,L"CurrentBuildNumber",RRF_RT_REG_SZ,nullptr,build,&bytes);
    if(result!=ERROR_SUCCESS){value.result=HRESULT_FROM_WIN32(result);return value;}
    wchar_t* end=nullptr;value.build=wcstoul(build,&end,10);
    if(!value.build || !end || *end)value.result=HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    bytes=sizeof(value.revision);RegGetValueW(HKEY_LOCAL_MACHINE,key,L"UBR",RRF_RT_REG_DWORD,nullptr,&value.revision,&bytes);
    return value;
}
inline HRESULT verify_core_version(const std::wstring& module) {
    DWORD ignored=0;const auto bytes=GetFileVersionInfoSizeW(module.c_str(),&ignored);
    if(!bytes){const auto error=GetLastError();return HRESULT_FROM_WIN32(error==ERROR_RESOURCE_TYPE_NOT_FOUND || error==ERROR_RESOURCE_DATA_NOT_FOUND?ERROR_REVISION_MISMATCH:error);}
    std::vector<BYTE> storage(bytes);
    if(!GetFileVersionInfoW(module.c_str(),0,bytes,storage.data()))return HRESULT_FROM_WIN32(GetLastError());
    VS_FIXEDFILEINFO* info=nullptr;UINT length=0;
    if(!VerQueryValueW(storage.data(),L"\\",reinterpret_cast<void**>(&info),&length) || length<sizeof(*info) || info->dwSignature!=0xfeef04bd)return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    return info->dwFileVersionMS==STILLBAR_VERSION_MS && info->dwFileVersionLS==STILLBAR_VERSION_LS?S_OK:HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH);
}
inline std::wstring error_code(HRESULT result) {
    wchar_t buffer[32]{};swprintf_s(buffer,L"0x%08lX",static_cast<unsigned long>(result));return buffer;
}
inline void save_compatibility_report(const std::filesystem::path& directory,const std::wstring& detail,HRESULT result) {
    std::ofstream output(directory/L"compatibility.txt",std::ios::binary);
    const auto value=detail+L" | "+error_code(result)+L"\r\n";
    const int bytes=WideCharToMultiByte(CP_UTF8,0,value.c_str(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);
    std::string utf8(bytes,'\0');WideCharToMultiByte(CP_UTF8,0,value.c_str(),static_cast<int>(value.size()),utf8.data(),bytes,nullptr,nullptr);output<<utf8;
}
}
