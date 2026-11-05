#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "config.hpp"
#include "session.hpp"
#include "resource.h"
#include "shortcut.hpp"
#include "compatibility.hpp"
#include "modern_ui.hpp"
#include "browser_integration.hpp"
#include <vector>
#include <commctrl.h>
#include <shellapi.h>
#include <array>
#include <cstdio>
#include <cerrno>
#include <limits>

namespace {
constexpr wchar_t window_class[]=L"Stillbar.Settings";
constexpr UINT tray_message=WM_APP+32;
constexpr UINT apply_timer=1;
constexpr UINT browser_timer=2;
constexpr UINT reload_message=WM_APP+33;
constexpr UINT show_message=WM_APP+34;
constexpr UINT enable_id=10,disable_id=11,advanced_id=12,autostart_id=13,exit_id=14,mode_id=15,compact_id=16,outline_id=17,tabs_id=18,popup_outline_id=19,visualizer_id=55,preview_id=57;
LRESULT CALLBACK page_proc(HWND,UINT,WPARAM,LPARAM);
struct Slider {HWND label=nullptr,track=nullptr,value=nullptr;int minimum=0,maximum=0;};
struct App {
    HWND window=nullptr,status=nullptr,enable=nullptr,disable=nullptr,advanced=nullptr,autostart=nullptr,mode=nullptr,compact=nullptr,mode_label=nullptr,outline=nullptr,close_hint=nullptr;
    HFONT font=nullptr,title_font=nullptr;
    HWND title_label=nullptr,subtitle=nullptr,tabs=nullptr,page_hint=nullptr,popup_hint=nullptr,popup_outline=nullptr,divider=nullptr;
    HWND visualizer=nullptr,visualizer_hint=nullptr,preview=nullptr;
    bool popup_page=false;
    HICON icon=nullptr,small_icon=nullptr;
    UINT dpi=96,taskbar_created=0;
    islands::Settings settings=islands::load_settings();
    std::unique_ptr<islands::Session> session;
    std::array<Slider,16> sliders{};
    std::array<HWND,3> island_labels{};
    bool wanted=false,background=false;
    HRESULT local_error=S_OK;
    islands::WindowsEnvironment environment=islands::windows_environment();
    std::wstring local_detail,last_report;

    int px(int value)const{return MulDiv(value,dpi,96);}
    HWND control(const wchar_t* type,const wchar_t* text,DWORD style,int id,bool page_control=true) {
        const auto handle=CreateWindowExW(0,type,text,WS_CHILD|WS_VISIBLE|WS_CLIPSIBLINGS|style,0,0,0,0,page_control && body?body:window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);
        SendMessageW(handle,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);return handle;
    }
    void move(HWND handle,int x,int y,int width,int height){MoveWindow(handle,px(x),px(y-(body && GetParent(handle)==body?scroll:0)),px(width),px(height),TRUE);}
    void slider(int index,const wchar_t* label,int low,int high,int value) {
        auto& item=sliders[index];item.minimum=low;item.maximum=high;
        item.label=control(L"STATIC",label,SS_LEFT,300+index);
        item.value=control(L"STATIC",L"",SS_RIGHT,400+index);
        item.track=control(TRACKBAR_CLASSW,L"",TBS_HORZ|TBS_NOTICKS|WS_TABSTOP,100+index);
        SendMessageW(item.track,TBM_SETRANGE,TRUE,MAKELPARAM(low,high));SendMessageW(item.track,TBM_SETPOS,TRUE,value);
    }
    void update_values() {
        for(int index=0;index<16;++index){const int value=static_cast<int>(SendMessageW(sliders[index].track,TBM_GETPOS,0,0));const bool blur=index==10 || index==13;const auto suffix=index==2 || (index>=7 && index<=9) || index==12 || blur?L"%":index==3?L" мс":L" DIP";const auto text=std::to_wstring(blur?MulDiv(value,100,48):value)+suffix;SetWindowTextW(sliders[index].value,text.c_str());}
    }
#include "settings_ui.inc"
    void update_status() {
        const auto phase=session?session->phase():islands::Phase::disabled;
        const auto error=FAILED(local_error)?local_error:session?session->result():S_OK;
        const wchar_t* label=L"Отключено — штатная панель Windows";
        if(phase==islands::Phase::waiting || phase==islands::Phase::attached || phase==islands::Phase::probing)label=L"Проверка совместимости и обратимости…";
        else if(phase==islands::Phase::active)label=settings.mode==islands::AppearanceMode::islands?L"Включены три островка":settings.compact_background?L"Включён плеер и тонкий фон панели":L"Включён только плеер — штатное оформление сохранено";
        std::wstring text=label;
        if(FAILED(error))text=environment.describe()+L": "+(FAILED(local_error)?local_detail:session?session->detail():L"Ошибка настройки")+L" ["+islands::error_code(error)+L"]";
        const auto detail=environment.describe()+L" | "+(FAILED(local_error)?local_detail:session?session->detail():std::wstring(label));
        const auto report=detail+L" | "+islands::error_code(error);
        if(report!=last_report){last_report=report;islands::save_compatibility_report(std::filesystem::path(islands::program_path()).parent_path(),detail,error);}
        SetWindowTextW(status,text.c_str());
        const bool starting=phase==islands::Phase::waiting || phase==islands::Phase::attached || phase==islands::Phase::probing;
        EnableWindow(enable,!starting && phase!=islands::Phase::active);EnableWindow(disable,!starting && session && phase==islands::Phase::active);
        EnableWindow(preview,phase==islands::Phase::active);ShowWindow(enable,phase==islands::Phase::active?SW_HIDE:SW_SHOW);ShowWindow(disable,phase==islands::Phase::active?SW_SHOW:SW_HIDE);
    }
    HRESULT enable_now() {
        environment=islands::windows_environment();local_error=S_OK;local_detail.clear();
        if(!environment.supported()){local_error=FAILED(environment.result)?environment.result:HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);local_detail=L"Требуется Windows 11 x64; не удалось подтвердить платформу.";update_status();return local_error;}
        const auto module=(std::filesystem::path(islands::program_path()).parent_path()/L"Stillbar.Core.dll").wstring();
        local_error=islands::verify_core_version(module);
        if(FAILED(local_error)){local_detail=local_error==HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH)?L"Версии EXE и Core.dll не совпадают. Распакуйте комплект целиком.":L"Проверка Core.dll: файл отсутствует или не содержит версию комплекта.";update_status();return local_error;}
        if(session && session->phase()==islands::Phase::failed)session.reset();
        if(!session)session=std::make_unique<islands::Session>();
        local_error=session->start(window,settings,module);local_detail=L"Создание канала управления Explorer.";wanted=SUCCEEDED(local_error);update_status();return local_error;
    }
    void settings_changed() {
        settings.radius=static_cast<double>(SendMessageW(sliders[0].track,TBM_GETPOS,0,0));settings.inset=static_cast<double>(SendMessageW(sliders[1].track,TBM_GETPOS,0,0));settings.opacity=static_cast<double>(SendMessageW(sliders[2].track,TBM_GETPOS,0,0))/100.;settings.animation_ms=static_cast<std::uint32_t>(SendMessageW(sliders[3].track,TBM_GETPOS,0,0));
        settings.popup_blur=static_cast<std::uint32_t>(SendMessageW(sliders[10].track,TBM_GETPOS,0,0));
        settings.popup_radius=static_cast<double>(SendMessageW(sliders[11].track,TBM_GETPOS,0,0));settings.popup_opacity=static_cast<double>(SendMessageW(sliders[12].track,TBM_GETPOS,0,0))/100.;
        for(int index=0;index<3;++index){settings.radii[index]=static_cast<double>(SendMessageW(sliders[4+index].track,TBM_GETPOS,0,0));settings.opacities[index]=static_cast<double>(SendMessageW(sliders[7+index].track,TBM_GETPOS,0,0))/100.;}
        settings.island_blur=static_cast<std::uint32_t>(SendMessageW(sliders[13].track,TBM_GETPOS,0,0));settings.popup_gap=static_cast<double>(SendMessageW(sliders[14].track,TBM_GETPOS,0,0));settings.apps_width=static_cast<double>(SendMessageW(sliders[15].track,TBM_GETPOS,0,0));
        settings=islands::normalize(settings);update_values();
        if(!islands::save_settings(settings))local_error=HRESULT_FROM_WIN32(GetLastError());
        SetTimer(window,apply_timer,60,nullptr);
    }
    void reload_settings(){
        settings=islands::load_settings();const int values[]{static_cast<int>(settings.radius),static_cast<int>(settings.inset),static_cast<int>(std::lround(settings.opacity*100)),static_cast<int>(settings.animation_ms),static_cast<int>(settings.radii[0]),static_cast<int>(settings.radii[1]),static_cast<int>(settings.radii[2]),static_cast<int>(std::lround(settings.opacities[0]*100)),static_cast<int>(std::lround(settings.opacities[1]*100)),static_cast<int>(std::lround(settings.opacities[2]*100)),static_cast<int>(settings.popup_blur),static_cast<int>(settings.popup_radius),static_cast<int>(std::lround(settings.popup_opacity*100)),static_cast<int>(settings.island_blur),static_cast<int>(settings.popup_gap),static_cast<int>(settings.apps_width)};
        for(int index=0;index<16;++index)SendMessageW(sliders[index].track,TBM_SETPOS,TRUE,values[index]);
        SendMessageW(mode,CB_SETCURSEL,settings.mode==islands::AppearanceMode::islands?1:0,0);SendMessageW(compact,BM_SETCHECK,settings.compact_background?BST_CHECKED:BST_UNCHECKED,0);SendMessageW(outline,BM_SETCHECK,settings.player_outline?BST_CHECKED:BST_UNCHECKED,0);SendMessageW(advanced,BM_SETCHECK,settings.advanced?BST_CHECKED:BST_UNCHECKED,0);
        SendMessageW(popup_outline,BM_SETCHECK,settings.popup_outline?BST_CHECKED:BST_UNCHECKED,0);SendMessageW(visualizer,BM_SETCHECK,settings.popup_visualizer?BST_CHECKED:BST_UNCHECKED,0);
        SendMessageW(apps_auto,BM_SETCHECK,settings.apps_autosize?BST_CHECKED:BST_UNCHECKED,0);
        update_values();layout();if(session)session->send(islands::Command::apply,&settings);update_status();
    }
    void tray(bool add) {
        NOTIFYICONDATAW data{sizeof(data)};data.hWnd=window;data.uID=1;data.uFlags=NIF_ICON|NIF_MESSAGE|NIF_TIP;data.uCallbackMessage=tray_message;data.hIcon=small_icon;wcscpy_s(data.szTip,L"Stillbar");Shell_NotifyIconW(add?NIM_ADD:NIM_DELETE,&data);
    }
    void exit() {
        wanted=false;KillTimer(window,apply_timer);
        if(session){const auto result=session->stop();if(FAILED(result)){local_error=result;update_status();ShowWindow(window,SW_RESTORE);return;}session.reset();}
        DestroyWindow(window);
    }
};

LRESULT CALLBACK page_proc(HWND window,UINT message,WPARAM wparam,LPARAM lparam){
    auto app=reinterpret_cast<App*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE){app=static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(app));}
    if(!app)return DefWindowProcW(window,message,wparam,lparam);
    switch(message){
    case WM_PAINT:app->paint(window,true);return 0;
    case WM_ERASEBKGND:return 1;
    case islands::modern::focus_changed:app->reveal_control(reinterpret_cast<HWND>(lparam));return 0;
    case WM_LBUTTONDOWN:SetFocus(window);return 0;
    case WM_VSCROLL:app->scroll_page(LOWORD(wparam));return 0;
    case WM_MOUSEWHEEL:app->scroll_page(0,GET_WHEEL_DELTA_WPARAM(wparam));return 0;
    case WM_COMMAND:case WM_NOTIFY:case WM_HSCROLL:case WM_DRAWITEM:case WM_CTLCOLORSTATIC:case WM_CTLCOLORBTN:case WM_CTLCOLORLISTBOX:return SendMessageW(app->window,message,wparam,lparam);
    }
    return DefWindowProcW(window,message,wparam,lparam);
}

LRESULT CALLBACK window_proc(HWND window,UINT message,WPARAM wparam,LPARAM lparam) {
    auto app=reinterpret_cast<App*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE){app=static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);app->window=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(app));}
    if(!app)return DefWindowProcW(window,message,wparam,lparam);
    if(message==app->taskbar_created && app->taskbar_created){app->tray(true);if(app->wanted){app->session.reset();app->enable_now();}return 0;}
    switch(message){
    case WM_CREATE:app->create_controls();app->tray(true);return 0;
    case islands::status_message:app->update_status();return 0;
    case reload_message:app->reload_settings();return 0;
    case show_message:ShowWindow(window,SW_RESTORE);SetForegroundWindow(window);return 0;
    case WM_CTLCOLORSTATIC:case WM_CTLCOLORBTN:case WM_CTLCOLORLISTBOX:{const auto dc=reinterpret_cast<HDC>(wparam);const auto item=reinterpret_cast<HWND>(lparam);const bool content=GetParent(item)==app->body || message==WM_CTLCOLORLISTBOX;SetBkMode(dc,TRANSPARENT);SetBkColor(dc,content?app->palette.card:app->palette.background);SetTextColor(dc,!IsWindowEnabled(item) || item==app->subtitle || item==app->close_hint || item==app->visualizer_hint || item==app->popup_hint || item==app->page_hint?app->palette.muted:app->palette.text);return reinterpret_cast<LRESULT>(content?app->card_brush:app->background_brush);}
    case WM_PAINT:app->paint(window,false);return 0;
    case WM_ERASEBKGND:return 1;
    case WM_EXITSIZEMOVE:app->save_window_size();return 0;
    case WM_GETMINMAXINFO:{const auto limits=reinterpret_cast<MINMAXINFO*>(lparam);RECT minimum{0,0,app->px(720),app->px(520)};AdjustWindowRectExForDpi(&minimum,WS_OVERLAPPEDWINDOW,FALSE,0,app->dpi);limits->ptMinTrackSize={minimum.right-minimum.left,minimum.bottom-minimum.top};return 0;}
    case WM_SIZE:if(app->body && wparam!=SIZE_MINIMIZED)app->layout(false);return 0;
    case WM_SHOWWINDOW:if(!wparam)KillTimer(window,browser_timer);else if(app->body && app->page==4){SetTimer(window,browser_timer,2000,nullptr);app->browser_status();}return 0;
    case WM_MOUSEWHEEL:app->scroll_page(0,GET_WHEEL_DELTA_WPARAM(wparam));return 0;
    case WM_DRAWITEM:islands::modern::button(*reinterpret_cast<DRAWITEMSTRUCT*>(lparam),app->palette,app->font,app->symbols,app->page,app->dpi);return TRUE;
    case WM_HSCROLL:app->settings_changed();return 0;
    case WM_TIMER:if(wparam==apply_timer){KillTimer(window,apply_timer);if(app->session)app->session->send(islands::Command::apply,&app->settings);app->update_status();}else if(wparam==browser_timer)app->browser_status();return 0;
    case WM_COMMAND:
        if(LOWORD(wparam)>=60 && LOWORD(wparam)<=64){app->page=LOWORD(wparam)-60;app->scroll=0;app->layout(false);return 0;}
        switch(LOWORD(wparam)){
        case enable_id:app->enable_now();return 0;
        case disable_id:app->wanted=false;if(app->session)app->session->disable();return 0;
        case advanced_id:app->settings.advanced=SendMessageW(app->advanced,BM_GETCHECK,0,0)==BST_CHECKED;app->layout();app->settings_changed();return 0;
        case mode_id:if(HIWORD(wparam)==CBN_SELCHANGE){app->settings.mode=SendMessageW(app->mode,CB_GETCURSEL,0,0)==1?islands::AppearanceMode::islands:islands::AppearanceMode::player_only;app->layout();app->settings_changed();}return 0;
        case compact_id:app->settings.compact_background=SendMessageW(app->compact,BM_GETCHECK,0,0)==BST_CHECKED;app->settings_changed();return 0;
        case outline_id:app->settings.player_outline=SendMessageW(app->outline,BM_GETCHECK,0,0)==BST_CHECKED;app->settings_changed();return 0;
        case popup_outline_id:app->settings.popup_outline=SendMessageW(app->popup_outline,BM_GETCHECK,0,0)==BST_CHECKED;app->settings_changed();return 0;
        case visualizer_id:app->settings.popup_visualizer=SendMessageW(app->visualizer,BM_GETCHECK,0,0)==BST_CHECKED;app->settings_changed();return 0;
        case 58:app->settings.apps_autosize=SendMessageW(app->apps_auto,BM_GETCHECK,0,0)==BST_CHECKED;app->layout(false);app->settings_changed();return 0;
        case 73:app->page=0;app->scroll=0;app->layout(false);SetFocus(app->mode);return 0;
        case 72:{const auto folder=std::filesystem::path(islands::program_path()).parent_path().wstring();ShellExecuteW(window,L"open",folder.c_str(),nullptr,nullptr,SW_SHOWNORMAL);return 0;}
        case 75:app->prepare_browser();return 0;
        case 76:app->browser_status();return 0;
        case 80:app->prepare_browser_developer();return 0;
        case 77:app->disable_browser();return 0;
        case preview_id:if(app->session)app->session->send(islands::Command::preview_media);return 0;
        case autostart_id:{const bool checked=SendMessageW(app->autostart,BM_GETCHECK,0,0)==BST_CHECKED;const auto result=islands::set_autostart(checked);if(result!=ERROR_SUCCESS){app->local_error=HRESULT_FROM_WIN32(result);SendMessageW(app->autostart,BM_SETCHECK,islands::autostart_enabled()?BST_CHECKED:BST_UNCHECKED,0);app->update_status();}return 0;}
        case exit_id:app->exit();return 0;
        }
        break;
    case tray_message:
        if(lparam==WM_LBUTTONDBLCLK){ShowWindow(window,SW_RESTORE);SetForegroundWindow(window);}
        if(lparam==WM_RBUTTONUP){const auto menu=CreatePopupMenu();AppendMenuW(menu,MF_STRING,1,L"Настройки");AppendMenuW(menu,MF_STRING,2,L"Отключить");AppendMenuW(menu,MF_STRING,3,L"Выход и восстановление панели");POINT point{};GetCursorPos(&point);SetForegroundWindow(window);const auto selected=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,point.x,point.y,0,window,nullptr);DestroyMenu(menu);if(selected==1){ShowWindow(window,SW_RESTORE);SetForegroundWindow(window);}if(selected==2){app->wanted=false;if(app->session)app->session->disable();}if(selected==3)app->exit();}
        return 0;
    case WM_SETTINGCHANGE:app->theme();if(app->session)app->session->send(islands::Command::apply,&app->settings);return 0;
    case WM_DPICHANGED:{app->dpi=HIWORD(wparam);app->update_fonts();const auto bounds=reinterpret_cast<RECT*>(lparam);SetWindowPos(window,nullptr,bounds->left,bounds->top,bounds->right-bounds->left,bounds->bottom-bounds->top,SWP_NOZORDER|SWP_NOACTIVATE);app->layout(false);return 0;}
    case WM_CLOSE:ShowWindow(window,SW_HIDE);return 0;
    case WM_DESTROY:if(app->background_brush)DeleteObject(app->background_brush);if(app->card_brush)DeleteObject(app->card_brush);if(app->symbols)DeleteObject(app->symbols);KillTimer(window,apply_timer);app->tray(false);app->session.reset();if(app->font)DeleteObject(app->font);if(app->title_font)DeleteObject(app->title_font);if(app->icon)DestroyIcon(app->icon);if(app->small_icon)DestroyIcon(app->small_icon);PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(window,message,wparam,lparam);
}

void console_line(std::wstring text) {
    auto output=GetStdHandle(STD_OUTPUT_HANDLE);if(!output || output==INVALID_HANDLE_VALUE){AttachConsole(ATTACH_PARENT_PROCESS);output=GetStdHandle(STD_OUTPUT_HANDLE);}
    text+=L"\r\n";DWORD mode=0,written=0;
    if(GetConsoleMode(output,&mode)){WriteConsoleW(output,text.c_str(),static_cast<DWORD>(text.size()),&written,nullptr);return;}
    const int bytes=WideCharToMultiByte(CP_UTF8,0,text.data(),static_cast<int>(text.size()),nullptr,0,nullptr,nullptr);std::string utf8(bytes,'\0');WideCharToMultiByte(CP_UTF8,0,text.data(),static_cast<int>(text.size()),utf8.data(),bytes,nullptr,nullptr);WriteFile(output,utf8.data(),bytes,&written,nullptr);
}
int command_line(const std::wstring& command) {
    islands::SharedState state;
    if(command==L"--sources"){
        if(FAILED(islands::existing_command(islands::Command::none,nullptr,&state)))return 2;
        console_line(state.media_sources);return 0;
    }
    if(command.starts_with(L"--source=")){
        const auto value=command.substr(9);wchar_t* end=nullptr;errno=0;
        const auto id=value==L"auto"?0:wcstoull(value.c_str(),&end,10);
        if(value!=L"auto" && (value.empty() || value[0]<L'0' || value[0]>L'9' || !end || *end || errno==ERANGE))return 1;
        islands::MediaRequest request;request.source=id;
        return FAILED(islands::existing_command(islands::Command::select_media_source,nullptr,nullptr,&request))?2:0;
    }
    if(command.starts_with(L"--seek=")){
        const auto value=command.substr(7);wchar_t* end=nullptr;const double seconds=wcstod(value.c_str(),&end);
        if(value.empty() || !end || *end || !std::isfinite(seconds) || seconds<0 || seconds>static_cast<double>(std::numeric_limits<std::int64_t>::max()/10'000'000))return 1;
        if(FAILED(islands::existing_command(islands::Command::none,nullptr,&state)))return 2;
        islands::MediaRequest request;request.position=static_cast<std::int64_t>(std::llround(seconds*10'000'000));request.revision=state.media_revision;request.source=state.active_source;
        return FAILED(islands::existing_command(islands::Command::seek_media,nullptr,nullptr,&request))?2:0;
    }
    if(command==L"--media")return FAILED(islands::existing_command(islands::Command::expand_media))?2:0;
    if(command==L"--preview")return FAILED(islands::existing_command(islands::Command::preview_media))?2:0;
    if(command==L"--install-shortcut"){
        const auto result=islands::install_start_shortcut();console_line(SUCCEEDED(result)?L"Start menu shortcut installed":L"Unable to create Start menu shortcut");return FAILED(result)?2:0;
    }
    if(command==L"--status"){
        const auto result=islands::existing_command(islands::Command::none,nullptr,&state);
        if(FAILED(result)){console_line(L"disabled | "+islands::windows_environment().describe()+(result==HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH)?L" | IPC: версия запущенного контроллера не совпадает":L""));return result==HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH)?2:0;}
        const auto window=FindWindowW(window_class,nullptr);console_line(std::wstring(islands::phase_name(static_cast<islands::Phase>(state.phase)))+L" | window="+(window && IsWindowVisible(window)?L"shown":L"hidden")+L" | source="+std::to_wstring(state.active_source)+L" | "+state.detail);return state.phase==static_cast<LONG>(islands::Phase::failed)?1:0;
    }
    if(command==L"--hide" || command==L"--exit" || command==L"--show"){
        if(const auto window=FindWindowW(window_class,nullptr))PostMessageW(window,command==L"--show"?show_message:command==L"--hide"?WM_CLOSE:WM_COMMAND,command==L"--exit"?exit_id:0,0);
        return 0;
    }
    if(command==L"--inspect" || command==L"--apply"){
        if(command==L"--apply"){if(const auto window=FindWindowW(window_class,nullptr)){PostMessageW(window,reload_message,0,0);return 0;}}
        const auto settings=islands::load_settings();
        return FAILED(islands::existing_command(command==L"--inspect"?islands::Command::inspect:islands::Command::apply,command==L"--apply"?&settings:nullptr))?2:0;
    }
    if(command!=L"--enable" && command!=L"--disable"){console_line(L"Stillbar.exe [--enable | --disable | --status]");return 1;}
    const auto result=islands::existing_command(command==L"--enable"?islands::Command::enable:islands::Command::disable);
    if(SUCCEEDED(result)){console_line(command==L"--enable"?L"enable requested":L"disable requested");return 0;}
    if(command==L"--disable"){console_line(L"disabled");return 0;}
    if(const auto window=FindWindowW(window_class,nullptr)){PostMessageW(window,WM_COMMAND,enable_id,0);console_line(L"enable requested");return 0;}
    auto launch=L"\""+islands::program_path()+L"\" --background";STARTUPINFOW start{sizeof(start)};PROCESS_INFORMATION process{};
    if(!CreateProcessW(nullptr,launch.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&start,&process))return 2;
    CloseHandle(process.hThread);CloseHandle(process.hProcess);console_line(L"starting");return 0;
}
}

int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show) {
    SetCurrentProcessExplicitAppUserModelID(islands::app_id);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_BAR_CLASSES|ICC_STANDARD_CLASSES|ICC_TAB_CLASSES};InitCommonControlsEx(&controls);
    int count=0;const auto arguments=CommandLineToArgvW(GetCommandLineW(),&count);const std::wstring argument=count>1?arguments[1]:L"";LocalFree(arguments);
    if(const auto legacy=FindWindowW(L"TaskbarIslands.Settings",nullptr);legacy && (argument.empty() || argument==L"--background" || argument==L"--enable")){
        if(argument!=L"--background")MessageBoxW(nullptr,L"Сначала завершите предыдущую TaskbarIslands через «Завершить работу», затем запустите Stillbar.",L"Stillbar — переход на новую версию",MB_OK|MB_ICONINFORMATION);
        return 2;
    }
    if(!argument.empty() && argument!=L"--background")return command_line(argument);
    if(const auto existing=FindWindowW(window_class,nullptr)){
        DWORD running_id=0;GetWindowThreadProcessId(existing,&running_id);const auto process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,running_id);
        wchar_t running_path[32768]{};DWORD length=ARRAYSIZE(running_path);bool mismatch=false;
        if(process){if(QueryFullProcessImageNameW(process,0,running_path,&length))mismatch=islands::verify_core_version(running_path)==HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH);CloseHandle(process);}
        if(mismatch){
            if(argument!=L"--background"){const auto message=L"Уже запущена другая версия Stillbar:\n"+std::wstring(running_path)+L"\n\nЗавершите её через «Завершить работу», затем запустите новый комплект ещё раз.";MessageBoxW(nullptr,message.c_str(),L"Stillbar — другая версия",MB_OK|MB_ICONINFORMATION);ShowWindow(existing,SW_RESTORE);SetForegroundWindow(existing);}return 2;
        }
        if(argument==L"--background")PostMessageW(existing,WM_COMMAND,enable_id,0);else{ShowWindow(existing,SW_RESTORE);SetForegroundWindow(existing);}return 0;
    }
    App app;app.background=argument==L"--background";app.taskbar_created=RegisterWindowMessageW(L"TaskbarCreated");
    WNDCLASSW type{};type.lpfnWndProc=window_proc;type.hInstance=instance;type.lpszClassName=window_class;type.hCursor=LoadCursorW(nullptr,IDC_ARROW);type.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(IDI_STILLBAR));type.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);RegisterClassW(&type);
    const auto window=CreateWindowExW(0,window_class,L"Stillbar — настройки",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,680,620,nullptr,nullptr,instance,&app);
    if(!window)return 3;
    if(app.background){ShowWindow(window,SW_HIDE);app.enable_now();}else ShowWindow(window,show);
    MSG message{};while(GetMessageW(&message,nullptr,0,0)>0){
        if(message.message==WM_LBUTTONDOWN || message.message==WM_RBUTTONDOWN)SendMessageW(window,WM_CHANGEUISTATE,MAKEWPARAM(UIS_SET,UISF_HIDEFOCUS),0);
        else if(message.message==WM_KEYDOWN)SendMessageW(window,WM_CHANGEUISTATE,MAKEWPARAM(UIS_CLEAR,UISF_HIDEFOCUS),0);
        if(message.message==WM_KEYDOWN){
            const int id=GetDlgCtrlID(message.hwnd);
            if(id>=60 && id<=64 && (message.wParam==VK_UP || message.wParam==VK_DOWN)){app.page=(id-60+(message.wParam==VK_UP?4:1))%5;app.scroll=0;app.layout(false);SetFocus(app.navigation[app.page]);continue;}
            if(message.hwnd==app.body && (message.wParam==VK_PRIOR || message.wParam==VK_NEXT || message.wParam==VK_HOME || message.wParam==VK_END)){app.scroll_page(message.wParam==VK_PRIOR?SB_PAGEUP:message.wParam==VK_NEXT?SB_PAGEDOWN:message.wParam==VK_HOME?SB_TOP:SB_BOTTOM);continue;}
        }
        if(!IsDialogMessageW(window,&message)){TranslateMessage(&message);DispatchMessageW(&message);}
    }
    return 0;
}
