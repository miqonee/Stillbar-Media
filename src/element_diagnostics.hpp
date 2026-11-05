#pragma once
#include <windows.h>
#include <xamlom.h>
#include <winrt/base.h>
#include <atomic>
#include <cstring>

namespace islands::element_diagnostics {
using OpenKey=decltype(&RegOpenKeyExW);
using QueryValue=decltype(&RegQueryValueExW);
inline OpenKey original_open=&RegOpenKeyExW;
inline QueryValue original_query=&RegQueryValueExW;
inline thread_local bool elements_only=false;
inline std::atomic_uint reads=0;
inline std::atomic_bool imports_restored=true;
struct CallLifetime {
    CallLifetime(){++winrt::get_module_lock();}
    ~CallLifetime(){--winrt::get_module_lock();}
};
inline LSTATUS WINAPI open_key(HKEY key,LPCWSTR name,DWORD options,REGSAM access,PHKEY result) {
    CallLifetime held;
    const auto status=original_open(key,name,options,access,result);
    if(status==ERROR_SUCCESS || !elements_only || key!=HKEY_LOCAL_MACHINE || !name || _wcsicmp(name,L"Software\\Microsoft\\XAML\\Debug"))return status;
    return original_open(key,L"Software\\Microsoft",options,access,result);
}
inline LSTATUS WINAPI query_value(HKEY key,LPCWSTR name,LPDWORD reserved,LPDWORD type,LPBYTE data,LPDWORD bytes) {
    CallLifetime held;
    if(!elements_only || !name || _wcsicmp(name,L"DisableCompositionDiag"))return original_query(key,name,reserved,type,data,bytes);
    if(reserved || (data && !bytes))return ERROR_INVALID_PARAMETER;
    if(type)*type=REG_DWORD;
    if(!data){if(bytes)*bytes=sizeof(DWORD);return ERROR_SUCCESS;}
    if(*bytes<sizeof(DWORD)){*bytes=sizeof(DWORD);return ERROR_MORE_DATA;}
    const DWORD disabled=1;std::memcpy(data,&disabled,sizeof(disabled));*bytes=sizeof(disabled);++reads;return ERROR_SUCCESS;
}
inline void* volatile* import_slot(HMODULE module,const char* function) {
    const auto base=reinterpret_cast<BYTE*>(module);
    if(!base)return nullptr;
    const auto dos=reinterpret_cast<IMAGE_DOS_HEADER*>(base);if(dos->e_magic!=IMAGE_DOS_SIGNATURE)return nullptr;
    const auto nt=reinterpret_cast<IMAGE_NT_HEADERS*>(base+dos->e_lfanew);if(nt->Signature!=IMAGE_NT_SIGNATURE)return nullptr;
    const auto directory=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];if(!directory.VirtualAddress)return nullptr;
    for(auto descriptor=reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base+directory.VirtualAddress);descriptor->Name;++descriptor){
        if(!descriptor->OriginalFirstThunk)continue;
        auto names=reinterpret_cast<IMAGE_THUNK_DATA*>(base+descriptor->OriginalFirstThunk);
        auto slots=reinterpret_cast<IMAGE_THUNK_DATA*>(base+descriptor->FirstThunk);
        for(;names->u1.AddressOfData;++names,++slots){
            if(IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal))continue;
            const auto name=reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base+names->u1.AddressOfData);
            if(!std::strcmp(reinterpret_cast<const char*>(name->Name),function))return reinterpret_cast<void* volatile*>(&slots->u1.Function);
        }
    }
    return nullptr;
}
class ImportOverride {
    void* volatile* slot_=nullptr;
    void* previous_=nullptr;
    void* replacement_=nullptr;
public:
    ImportOverride()=default;
    ImportOverride(const ImportOverride&)=delete;
    ImportOverride& operator=(const ImportOverride&)=delete;
    ~ImportOverride(){restore();}
    HRESULT install(void* volatile* slot,void* replacement) {
        if(!slot)return HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
        DWORD protection=0;if(!VirtualProtect(const_cast<void**>(slot),sizeof(void*),PAGE_READWRITE,&protection))return HRESULT_FROM_WIN32(GetLastError());
        slot_=slot;replacement_=replacement;previous_=InterlockedExchangePointer(slot_,replacement_);
        DWORD ignored=0;VirtualProtect(const_cast<void**>(slot_),sizeof(void*),protection,&ignored);return S_OK;
    }
    bool restore()noexcept {
        if(!slot_)return true;
        DWORD protection=0;
        if(VirtualProtect(const_cast<void**>(slot_),sizeof(void*),PAGE_READWRITE,&protection)){
            const auto observed=InterlockedCompareExchangePointer(slot_,previous_,replacement_);
            DWORD ignored=0;VirtualProtect(const_cast<void**>(slot_),sizeof(void*),protection,&ignored);
            if(observed==replacement_){slot_=nullptr;return true;}
        }
        HMODULE pinned=nullptr;GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,reinterpret_cast<LPCWSTR>(replacement_),&pinned);
        ++winrt::get_module_lock();
        imports_restored=false;slot_=nullptr;return false;
    }
};
inline HRESULT advise(IVisualTreeService* service,IVisualTreeServiceCallback2* callback) {
    if(!imports_restored)return HRESULT_FROM_WIN32(ERROR_BUSY);
    const auto module=GetModuleHandleW(L"Windows.UI.Xaml.dll");
    const auto open=import_slot(module,"RegOpenKeyExW"),query=import_slot(module,"RegQueryValueExW");
    if(!open || !query)return HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
    original_open=reinterpret_cast<OpenKey>(*open);original_query=reinterpret_cast<QueryValue>(*query);
    ImportOverride open_override,query_override;
    auto result=open_override.install(open,reinterpret_cast<void*>(&open_key));if(FAILED(result))return result;
    result=query_override.install(query,reinterpret_cast<void*>(&query_value));if(FAILED(result))return result;
    // Composition diagnostics are unrelated to XAML element discovery and can fail-fast on reattachment.
    struct ElementScope {bool previous=elements_only;ElementScope(){elements_only=true;}~ElementScope(){elements_only=previous;}} scope;
    result=service->AdviseVisualTreeChange(callback);
    const bool query_restored=query_override.restore(),open_restored=open_override.restore();
    if(!query_restored || !open_restored)return HRESULT_FROM_WIN32(ERROR_WRITE_FAULT);
    return result;
}
}
