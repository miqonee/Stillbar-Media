#pragma once
#include <windows.h>
#include <limits>
#include <cmath>

namespace islands {
class PopupWindow {
    HWND window_=nullptr;
    DWORD thread_=0;
    bool promoted_=false;
    struct Search {RECT target;HWND found=nullptr;LONG score=LONG_MAX;};
    static BOOL CALLBACK candidate(HWND window,LPARAM data)noexcept {
        auto& search=*reinterpret_cast<Search*>(data);wchar_t name[64]{};
        if(!IsWindowVisible(window) || !GetClassNameW(window,name,ARRAYSIZE(name)) || wcscmp(name,L"Xaml_WindowedPopupClass"))return TRUE;
        RECT bounds{};if(!GetWindowRect(window,&bounds))return TRUE;
        const POINT center{(search.target.left+search.target.right)/2,(search.target.top+search.target.bottom)/2};
        if(!PtInRect(&bounds,center))return TRUE;
        const LONG score=std::abs(bounds.left-search.target.left)+std::abs(bounds.right-search.target.right)+std::abs(bounds.top-search.target.top)+std::abs(bounds.bottom-search.target.bottom);
        if(score<search.score && score<128){search.score=score;search.found=window;}return TRUE;
    }
public:
    HWND place(RECT bounds)noexcept {
        if(!window_){Search search{bounds};thread_=GetCurrentThreadId();EnumThreadWindows(thread_,candidate,reinterpret_cast<LPARAM>(&search));window_=search.found;}
        if(window_ && !(GetWindowLongPtrW(window_,GWL_EXSTYLE)&WS_EX_TOPMOST)){
            promoted_=SetWindowPos(window_,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE)!=FALSE;
        }
        return window_;
    }
    HWND get()const noexcept{return window_;}
    void close()noexcept {
        if(promoted_ && window_ && IsWindow(window_)){DWORD process=0;const auto thread=GetWindowThreadProcessId(window_,&process);if(thread==thread_ && process==GetCurrentProcessId())SetWindowPos(window_,HWND_NOTOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);}
        window_=nullptr;thread_=0;promoted_=false;
    }
};
}
