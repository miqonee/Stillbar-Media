#pragma once
#include <winrt/Windows.Foundation.h>
#include <memory>

namespace islands::media_diagnostics {
template<class Operation> auto bounded_result(const Operation& operation,DWORD milliseconds) {
    struct Signal {winrt::handle event{CreateEventW(nullptr,TRUE,FALSE,nullptr)};};
    auto signal=std::make_shared<Signal>();if(!signal->event)winrt::throw_last_error();
    operation.Completed([signal](auto&&,auto&&){SetEvent(signal->event.get());});
    const auto waited=WaitForSingleObject(signal->event.get(),milliseconds);
    if(waited!=WAIT_OBJECT_0){
        const HRESULT error=waited==WAIT_TIMEOUT?HRESULT_FROM_WIN32(ERROR_TIMEOUT):HRESULT_FROM_WIN32(GetLastError());
        try{operation.Cancel();}catch(...){}
        throw winrt::hresult_error(error);
    }
    return operation.GetResults();
}
}
