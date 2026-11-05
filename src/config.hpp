#pragma once
#include "protocol.hpp"
#include <algorithm>
#include <filesystem>

namespace islands {
inline std::wstring program_path() {
    wchar_t path[32768]{};GetModuleFileNameW(nullptr,path,ARRAYSIZE(path));return path;
}
inline std::wstring config_path() { return (std::filesystem::path(program_path()).parent_path()/L"settings.ini").wstring(); }
inline Settings load_settings() {
    Settings value;const auto path=config_path();
    const auto read=[&](const wchar_t* key,int fallback){return static_cast<int>(GetPrivateProfileIntW(L"Appearance",key,fallback,path.c_str()));};
    value.radius=read(L"Radius",12);value.inset=read(L"Inset",4);value.opacity=read(L"Opacity",90)/100.;value.animation_ms=read(L"AnimationMs",180);value.advanced=read(L"Advanced",0);
    value.mode=static_cast<AppearanceMode>(read(L"Mode",0));value.compact_background=read(L"CompactBackground",0);
    value.player_outline=read(L"PlayerOutline",0);
    value.popup_blur=read(L"PopupBlur",24);
    value.popup_opacity=read(L"PopupOpacity",90)/100.;value.popup_radius=read(L"PopupRadius",12);value.popup_outline=read(L"PopupOutline",1);
    value.popup_visualizer=read(L"PopupVisualizer",0);
    value.popup_gap=read(L"PopupGap",8);value.island_blur=read(L"IslandBlur",24);
    value.apps_autosize=read(L"AppsAutoSize",1);value.apps_width=read(L"AppsWidth",480);
    for(int index=0;index<3;++index) {
        const auto suffix=std::to_wstring(index);
        value.radii[index]=read((L"Radius"+suffix).c_str(),12);value.opacities[index]=read((L"Opacity"+suffix).c_str(),90)/100.;
    }
    return normalize(value);
}
inline bool save_settings(Settings value) {
    value=normalize(value);const auto path=config_path();bool ok=true;
    const auto write=[&](const wchar_t* key,int number){const auto text=std::to_wstring(number);ok=WritePrivateProfileStringW(L"Appearance",key,text.c_str(),path.c_str()) && ok;};
    write(L"Radius",static_cast<int>(value.radius));write(L"Inset",static_cast<int>(value.inset));write(L"Opacity",static_cast<int>(std::lround(value.opacity*100)));write(L"AnimationMs",value.animation_ms);write(L"Advanced",value.advanced);
    write(L"Mode",static_cast<int>(value.mode));write(L"CompactBackground",value.compact_background);
    write(L"PlayerOutline",value.player_outline);
    write(L"PopupBlur",value.popup_blur);
    write(L"PopupOpacity",static_cast<int>(std::lround(value.popup_opacity*100)));write(L"PopupRadius",static_cast<int>(value.popup_radius));write(L"PopupOutline",value.popup_outline);
    write(L"PopupVisualizer",value.popup_visualizer);
    write(L"PopupGap",static_cast<int>(value.popup_gap));write(L"IslandBlur",value.island_blur);
    write(L"AppsAutoSize",value.apps_autosize);write(L"AppsWidth",static_cast<int>(value.apps_width));
    for(int index=0;index<3;++index){const auto suffix=std::to_wstring(index);write((L"Radius"+suffix).c_str(),static_cast<int>(value.radii[index]));write((L"Opacity"+suffix).c_str(),static_cast<int>(std::lround(value.opacities[index]*100)));}
    return ok;
}
inline bool autostart_enabled() {
    DWORD bytes=0;return RegGetValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",L"Stillbar",RRF_RT_REG_SZ,nullptr,nullptr,&bytes)==ERROR_SUCCESS;
}
inline LONG set_autostart(bool enabled) {
    HKEY key=nullptr;const auto opened=RegCreateKeyExW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr);
    if(opened!=ERROR_SUCCESS)return opened;
    LONG result=ERROR_SUCCESS;
    if(enabled){const auto command=L"\""+program_path()+L"\" --background";result=RegSetValueExW(key,L"Stillbar",0,REG_SZ,reinterpret_cast<const BYTE*>(command.c_str()),static_cast<DWORD>((command.size()+1)*sizeof(wchar_t)));}
    else {result=RegDeleteValueW(key,L"Stillbar");if(result==ERROR_FILE_NOT_FOUND)result=ERROR_SUCCESS;}
    RegCloseKey(key);return result;
}
}
