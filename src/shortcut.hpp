#pragma once
#include "config.hpp"
#include <shlobj.h>
#include <shobjidl.h>
#include <propkey.h>
#include <propvarutil.h>
#include <wrl/client.h>

namespace islands {
inline constexpr wchar_t app_id[]=L"Stillbar.Desktop";
inline HRESULT install_start_shortcut() {
    const auto initialized=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    if(FAILED(initialized))return initialized;
    HRESULT result=E_FAIL;
    {
        PWSTR programs=nullptr;
        result=SHGetKnownFolderPath(FOLDERID_Programs,0,nullptr,&programs);
        if(SUCCEEDED(result)) {
            const auto target=program_path();
            const auto link_path=std::filesystem::path(programs)/L"Stillbar.lnk";CoTaskMemFree(programs);
            Microsoft::WRL::ComPtr<IShellLinkW> link;
            result=CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&link));
            if(SUCCEEDED(result))result=link->SetPath(target.c_str());
            if(SUCCEEDED(result))result=link->SetWorkingDirectory(std::filesystem::path(target).parent_path().c_str());
            if(SUCCEEDED(result))result=link->SetIconLocation(target.c_str(),0);
            if(SUCCEEDED(result))result=link->SetDescription(L"Stillbar — настройки панели задач и медиаплеера");
            Microsoft::WRL::ComPtr<IPropertyStore> properties;
            if(SUCCEEDED(result))result=link.As(&properties);
            PROPVARIANT id{};
            if(SUCCEEDED(result))result=InitPropVariantFromString(app_id,&id);
            if(SUCCEEDED(result))result=properties->SetValue(PKEY_AppUserModel_ID,id);
            PropVariantClear(&id);
            if(SUCCEEDED(result))result=properties->Commit();
            Microsoft::WRL::ComPtr<IPersistFile> file;
            if(SUCCEEDED(result))result=link.As(&file);
            if(SUCCEEDED(result))result=file->Save(link_path.c_str(),TRUE);
            if(SUCCEEDED(result))SHChangeNotify(SHCNE_CREATE,SHCNF_PATHW,link_path.c_str(),nullptr);
        }
    }
    CoUninitialize();return result;
}
}
