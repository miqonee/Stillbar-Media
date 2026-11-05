#pragma once
#include <windows.h>
#include <cstdint>
#define WINRT_CUSTOM_MODULE_LOCK

namespace winrt {
class ModuleLock {
    mutable SRWLOCK mutex_=SRWLOCK_INIT;
    std::uint32_t count_=0;
    HANDLE idle_=CreateEventW(nullptr,TRUE,TRUE,nullptr);
public:
    ~ModuleLock(){if(idle_)CloseHandle(idle_);}
    std::uint32_t operator++() noexcept {
        AcquireSRWLockExclusive(&mutex_);
        const auto result=++count_;if(result==1)ResetEvent(idle_);
        ReleaseSRWLockExclusive(&mutex_);return result;
    }
    std::uint32_t operator--() noexcept {
        AcquireSRWLockExclusive(&mutex_);
        const auto result=--count_;if(result==0)SetEvent(idle_);
        ReleaseSRWLockExclusive(&mutex_);return result;
    }
    operator std::uint32_t()const noexcept {
        AcquireSRWLockShared(&mutex_);const auto result=count_;ReleaseSRWLockShared(&mutex_);return result;
    }
    bool wait_idle(DWORD timeout)const noexcept{return idle_ && WaitForSingleObject(idle_,timeout)==WAIT_OBJECT_0;}
};
inline ModuleLock& get_module_lock() noexcept {static ModuleLock value;return value;}
}
