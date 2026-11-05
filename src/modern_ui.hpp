#pragma once
#include <windows.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <algorithm>
#include <string>

namespace islands::modern {
constexpr UINT focus_changed=WM_APP+120;
struct Palette {
    bool dark=false;
    COLORREF background=RGB(243,243,243),sidebar=RGB(237,238,240),card=RGB(255,255,255),line=RGB(222,224,228),text=RGB(28,30,34),muted=RGB(102,106,115),accent=RGB(0,103,192),selected=RGB(224,235,247);
    static Palette system(){
        Palette result;DWORD light=1,bytes=sizeof(light);RegGetValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",L"AppsUseLightTheme",RRF_RT_REG_DWORD,nullptr,&light,&bytes);result.dark=!light;
        if(result.dark){result.background=RGB(32,32,32);result.sidebar=RGB(27,27,29);result.card=RGB(43,43,45);result.line=RGB(60,60,64);result.text=RGB(245,245,247);result.muted=RGB(168,169,176);result.accent=RGB(76,194,255);result.selected=RGB(47,59,68);}
        return result;
    }
};
inline void fill(HDC dc,RECT rect,COLORREF color){const auto brush=CreateSolidBrush(color);FillRect(dc,&rect,brush);DeleteObject(brush);}
inline void rounded(HDC dc,RECT rect,int radius,COLORREF color,COLORREF edge){
    const auto brush=CreateSolidBrush(color);const auto pen=CreatePen(PS_SOLID,1,edge);const auto old_brush=SelectObject(dc,brush),old_pen=SelectObject(dc,pen);RoundRect(dc,rect.left,rect.top,rect.right,rect.bottom,radius*2,radius*2);SelectObject(dc,old_brush);SelectObject(dc,old_pen);DeleteObject(brush);DeleteObject(pen);
}
inline COLORREF blend(COLORREF a,COLORREF b,int percent){
    const auto channel=[&](int x,int y){return (x*(100-percent)+y*percent)/100;};
    return RGB(channel(GetRValue(a),GetRValue(b)),channel(GetGValue(a),GetGValue(b)),channel(GetBValue(a),GetBValue(b)));
}
inline void stroke(HDC dc,RECT rect,int radius,COLORREF color,int width=1){
    const auto pen=CreatePen(PS_SOLID,width,color);const auto previous_pen=SelectObject(dc,pen),previous_brush=SelectObject(dc,GetStockObject(NULL_BRUSH));
    RoundRect(dc,rect.left,rect.top,rect.right,rect.bottom,radius*2,radius*2);SelectObject(dc,previous_pen);SelectObject(dc,previous_brush);DeleteObject(pen);
}
inline bool keyboard_focus(HWND window){return GetFocus()==window && !(SendMessageW(window,WM_QUERYUISTATE,0,0)&UISF_HIDEFOCUS);}
inline bool hovered(HWND window){POINT point{};RECT rect{};GetCursorPos(&point);ScreenToClient(window,&point);GetClientRect(window,&rect);return PtInRect(&rect,point)!=FALSE;}
inline void button(DRAWITEMSTRUCT const& item,Palette const& palette,HFONT font,HFONT symbols,int page,int dpi){
    const auto px=[&](int value){return MulDiv(value,dpi,96);};const bool nav=item.CtlID>=60 && item.CtlID<=64,selected=nav && static_cast<int>(item.CtlID-60)==page;
    const bool pressed=(item.itemState&ODS_SELECTED)!=0,disabled=(item.itemState&ODS_DISABLED)!=0,primary=item.CtlID==10 || item.CtlID==57;
    const bool sidebar=nav || item.CtlID==14;wchar_t parent_type[64]{};GetClassNameW(GetParent(item.hwndItem),parent_type,64);
    auto rect=item.rcItem;const auto background=sidebar?palette.sidebar:!wcscmp(parent_type,L"Stillbar.SettingsPage")?palette.card:palette.background;
    fill(item.hDC,rect,background);InflateRect(&rect,-px(1),-px(1));
    const auto base=nav?(selected?palette.selected:palette.sidebar):primary?palette.accent:palette.card;
    const auto color=disabled?palette.card:pressed?blend(base,palette.background,24):hovered(item.hwndItem)?blend(base,palette.text,6):base;
    rounded(item.hDC,rect,px(5),color,nav || primary?color:palette.line);SetBkMode(item.hDC,TRANSPARENT);SetTextColor(item.hDC,disabled?palette.muted:primary?(palette.dark?palette.background:RGB(255,255,255)):palette.text);
    const auto old_font=SelectObject(item.hDC,font);wchar_t caption[256]{};GetWindowTextW(item.hwndItem,caption,256);
    if(nav){
        const wchar_t* icons[]{L"\xE7F4",L"\xE8A9",L"\xE8D6",L"\xE713",L"\xE774"};auto icon=rect;icon.left+=px(15);icon.right=icon.left+px(22);SelectObject(item.hDC,symbols);DrawTextW(item.hDC,icons[item.CtlID-60],-1,&icon,DT_CENTER|DT_VCENTER|DT_SINGLELINE);SelectObject(item.hDC,font);rect.left+=px(48);
        if(selected){auto mark=rect;mark.left=item.rcItem.left+px(2);mark.right=mark.left+px(3);mark.top+=px(12);mark.bottom-=px(12);rounded(item.hDC,mark,px(2),palette.accent,palette.accent);}
        DrawTextW(item.hDC,caption,-1,&rect,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    }else DrawTextW(item.hDC,caption,-1,&rect,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    if(keyboard_focus(item.hwndItem)){auto focus=item.rcItem;InflateRect(&focus,-px(3),-px(3));stroke(item.hDC,focus,px(4),primary?palette.background:palette.accent,px(1));}SelectObject(item.hDC,old_font);
}
inline void slider(HDC dc,HWND window,RECT rect,Palette const& palette){
    const auto px=[&](int value){return MulDiv(value,GetDpiForWindow(window),96);};
    RECT thumb{},channel{};SendMessageW(window,TBM_GETTHUMBRECT,0,reinterpret_cast<LPARAM>(&thumb));SendMessageW(window,TBM_GETCHANNELRECT,0,reinterpret_cast<LPARAM>(&channel));
    const int x=(thumb.left+thumb.right)/2,y=(thumb.top+thumb.bottom)/2;
    channel.top=y-px(2);channel.bottom=y+px(2);channel.left=std::max<LONG>(channel.left,px(7));channel.right=std::min<LONG>(channel.right,rect.right-px(7));
    rounded(dc,channel,px(2),blend(palette.card,palette.text,22),blend(palette.card,palette.text,22));
    auto active=channel;active.right=std::clamp<LONG>(x,channel.left,channel.right);const auto accent=IsWindowEnabled(window)?palette.accent:palette.muted;
    if(active.right>active.left)rounded(dc,active,px(2),accent,accent);
    RECT circle{x-px(7),y-px(7),x+px(7),y+px(7)};rounded(dc,circle,px(7),accent,accent);
    if(keyboard_focus(window)){InflateRect(&circle,px(3),px(3));stroke(dc,circle,px(10),palette.accent);}
}
inline void draw_control(HDC dc,HWND window,RECT rect,Palette const& palette){
    fill(dc,rect,palette.card);const int dpi=static_cast<int>(GetDpiForWindow(window));const auto px=[&](int value){return MulDiv(value,dpi,96);};
    const auto old_font=SelectObject(dc,reinterpret_cast<HFONT>(SendMessageW(window,WM_GETFONT,0,0)));SetBkMode(dc,TRANSPARENT);SetTextColor(dc,IsWindowEnabled(window)?palette.text:palette.muted);
    wchar_t caption[256]{},type[32]{};GetWindowTextW(window,caption,256);GetClassNameW(window,type,32);
    if(!_wcsicmp(type,TRACKBAR_CLASSW))slider(dc,window,rect,palette);
    else if(!_wcsicmp(type,L"Static")){
        const int id=GetDlgCtrlID(window);if(id==52 || id==53 || id==56 || id==71 || id==74)SetTextColor(dc,palette.muted);
        const auto style=GetWindowLongPtrW(window,GWL_STYLE)&SS_TYPEMASK;DrawTextW(dc,caption,-1,&rect,DT_NOPREFIX|DT_WORDBREAK|(style==SS_RIGHT?DT_RIGHT:style==SS_CENTER?DT_CENTER:DT_LEFT));
    }else if(!_wcsicmp(type,L"ComboBox")){
        rounded(dc,rect,px(5),palette.card,keyboard_focus(window)?palette.accent:palette.line);auto text=rect;text.left+=px(12);text.right-=px(32);DrawTextW(dc,caption,-1,&text,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS);
        const int x=rect.right-px(18),y=(rect.top+rect.bottom)/2;const auto pen=CreatePen(PS_SOLID,px(1),palette.text);const auto previous=SelectObject(dc,pen);MoveToEx(dc,x-px(4),y-px(2),nullptr);LineTo(dc,x,y+px(2));LineTo(dc,x+px(4),y-px(2));SelectObject(dc,previous);DeleteObject(pen);
    }else{
        const bool checked=SendMessageW(window,BM_GETCHECK,0,0)==BST_CHECKED;auto text=rect;text.right-=px(52);DrawTextW(dc,caption,-1,&text,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS);
        const int top=(rect.bottom-px(18))/2;RECT toggle{rect.right-px(39),top,rect.right-px(5),top+px(18)};rounded(dc,toggle,px(9),checked?palette.accent:palette.background,checked?palette.accent:palette.muted);
        const int left=checked?toggle.right-px(15):toggle.left+px(3);RECT dot{left,top+px(3),left+px(12),top+px(15)};const auto color=checked?(palette.dark?palette.background:RGB(255,255,255)):palette.muted;rounded(dc,dot,px(6),color,color);
        if(keyboard_focus(window)){auto focus=rect;InflateRect(&focus,-px(2),-px(2));stroke(dc,focus,px(5),palette.accent);}
    }
    SelectObject(dc,old_font);
}
inline void paint_control(HWND window,UINT message,WPARAM wparam,Palette const& palette){
    PAINTSTRUCT paint{};const auto target=message==WM_PAINT?BeginPaint(window,&paint):reinterpret_cast<HDC>(wparam);if(!target)return;
    RECT rect{};GetClientRect(window,&rect);const auto memory=CreateCompatibleDC(target);const auto bitmap=CreateCompatibleBitmap(target,std::max(1L,rect.right),std::max(1L,rect.bottom));
    if(memory && bitmap){const auto previous=SelectObject(memory,bitmap);draw_control(memory,window,rect,palette);BitBlt(target,0,0,rect.right,rect.bottom,memory,0,0,SRCCOPY);SelectObject(memory,previous);}else draw_control(target,window,rect,palette);
    if(bitmap)DeleteObject(bitmap);if(memory)DeleteDC(memory);if(message==WM_PAINT)EndPaint(window,&paint);
}
inline LRESULT CALLBACK control_paint(HWND window,UINT message,WPARAM wparam,LPARAM lparam,UINT_PTR id,DWORD_PTR data){
    if(message==WM_NCDESTROY){RemoveWindowSubclass(window,control_paint,id);return DefSubclassProc(window,message,wparam,lparam);}
    wchar_t type[32]{};GetClassNameW(window,type,32);const bool owner=!_wcsicmp(type,L"Button") && (GetWindowLongPtrW(window,GWL_STYLE)&BS_TYPEMASK)==BS_OWNERDRAW;
    if(!owner && (message==WM_PAINT || message==WM_PRINTCLIENT)){paint_control(window,message,wparam,*reinterpret_cast<const Palette*>(data));return 0;}
    if(!owner && message==WM_ERASEBKGND)return 1;
    if(message==WM_MOUSEWHEEL){if(_wcsicmp(type,L"ComboBox") || !SendMessageW(window,CB_GETDROPPEDSTATE,0,0))return SendMessageW(GetParent(window),message,wparam,lparam);}
    if(message==WM_MOUSEMOVE){TRACKMOUSEEVENT tracked{sizeof(tracked),TME_QUERY,window,0};TrackMouseEvent(&tracked);if(!(tracked.dwFlags&TME_LEAVE)){TRACKMOUSEEVENT watch{sizeof(watch),TME_LEAVE,window,0};TrackMouseEvent(&watch);InvalidateRect(window,nullptr,FALSE);}}
    const auto result=DefSubclassProc(window,message,wparam,lparam);
    if(message==WM_SETFOCUS && keyboard_focus(window))PostMessageW(GetParent(window),focus_changed,0,reinterpret_cast<LPARAM>(window));
    switch(message){
    case WM_MOUSELEAVE:case WM_LBUTTONDOWN:case WM_LBUTTONUP:case WM_KEYDOWN:case WM_KEYUP:case WM_SETFOCUS:case WM_KILLFOCUS:case WM_UPDATEUISTATE:case WM_ENABLE:case WM_SIZE:case WM_SETTEXT:case WM_THEMECHANGED:
    case BM_SETCHECK:case TBM_SETPOS:case TBM_SETRANGE:case TBM_SETRANGEMIN:case TBM_SETRANGEMAX:InvalidateRect(window,nullptr,FALSE);break;
    case WM_MOUSEMOVE:if(GetCapture()==window)InvalidateRect(window,nullptr,FALSE);break;
    }
    return result;
}
}
