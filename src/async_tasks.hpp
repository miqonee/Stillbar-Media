#pragma once
#include <windows.h>
#include <winrt/Windows.Foundation.h>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace islands {
class AsyncTasks : public std::enable_shared_from_this<AsyncTasks> {
    std::mutex mutex_;
    std::unordered_map<std::uint64_t,winrt::Windows::Foundation::IAsyncInfo> operations_;
    HANDLE drained_=CreateEventW(nullptr,TRUE,TRUE,nullptr);
    std::uint64_t next_=0;
    bool closed_=false;

    void finish(std::uint64_t key) noexcept {
        std::lock_guard lock(mutex_);
        operations_.erase(key);
        if(operations_.empty())SetEvent(drained_);
    }
public:
    struct Ticket {
        std::shared_ptr<AsyncTasks> owner;
        std::uint64_t key=0;
        Ticket(std::shared_ptr<AsyncTasks> value,std::uint64_t id):owner(std::move(value)),key(id){}
        Ticket(const Ticket&)=delete;
        ~Ticket(){if(owner)owner->finish(key);}
        explicit operator bool()const{return static_cast<bool>(owner);}
        void track(winrt::Windows::Foundation::IAsyncInfo operation) {
            if(!owner)return;
            if(!operation)throw winrt::hresult_error(E_PENDING);
            bool cancel=false;
            {
                std::lock_guard lock(owner->mutex_);
                owner->operations_[key]=operation;
                cancel=owner->closed_;
            }
            if(cancel)operation.Cancel();
        }
    };
    ~AsyncTasks(){if(drained_)CloseHandle(drained_);}
    Ticket begin() {
        std::lock_guard lock(mutex_);
        if(closed_)return Ticket{nullptr,0};
        const auto key=++next_;
        operations_.emplace(key,nullptr);ResetEvent(drained_);
        return Ticket{shared_from_this(),key};
    }
    HANDLE drained()const{return drained_;}
    void close() noexcept {
        std::vector<winrt::Windows::Foundation::IAsyncInfo> operations;
        {
            std::lock_guard lock(mutex_);closed_=true;
            for(const auto& [key,operation]:operations_)if(operation)operations.push_back(operation);
        }
        for(const auto& operation:operations)try{operation.Cancel();}catch(...){}
    }
};
}
