#pragma once
#include <windows.h>
#include <cstdint>
#include <string>
#include <algorithm>
#include <cmath>

namespace islands {
inline constexpr std::uint32_t protocol_magic = 0x54424953;
inline constexpr std::uint32_t protocol_version = 11;
inline constexpr CLSID provider_id{0xa8a50f94, 0x9f11, 0x4f91, {0x97, 0x42, 0x62, 0xe7, 0xae, 0xc4, 0x81, 0xbd}};
enum class Command : LONG { none, probe, enable, disable, apply, stop, inspect, expand_media, select_media_source, seek_media, preview_media };
enum class Phase : LONG { waiting, attached, probing, verified, active, disabled, failed };
enum class AppearanceMode : std::uint32_t { player_only, islands };

struct Settings {
    double radius = 12;
    double inset = 4;
    double opacity = .9;
    std::uint32_t animation_ms = 180;
    std::uint32_t advanced = 0;
    AppearanceMode mode = AppearanceMode::player_only;
    std::uint32_t compact_background = 0;
    std::uint32_t player_outline = 0;
    std::uint32_t popup_blur = 24;
    double popup_opacity = .9;
    double popup_radius = 12;
    std::uint32_t popup_outline = 1;
    std::uint32_t popup_visualizer = 0;
    double popup_gap = 8;
    std::uint32_t island_blur = 24;
    std::uint32_t apps_autosize = 1;
    double apps_width = 480;
    double radii[3]{12, 12, 12};
    double opacities[3]{.9, .9, .9};
};
inline Settings normalize(Settings value) {
    const auto limit=[](double number,double low,double high,double fallback){return std::isfinite(number)?std::clamp(number,low,high):fallback;};
    value.radius=limit(value.radius,0,24,12);value.inset=limit(value.inset,0,16,4);value.opacity=limit(value.opacity,0,1,.9);
    value.animation_ms=std::min(value.animation_ms,400u);value.advanced=value.advanced?1:0;
    if(value.mode!=AppearanceMode::islands)value.mode=AppearanceMode::player_only;
    value.compact_background=value.compact_background?1:0;
    value.player_outline=value.player_outline?1:0;
    value.popup_blur=std::min(value.popup_blur,48u);
    value.popup_opacity=limit(value.popup_opacity,0,1,.9);value.popup_radius=limit(value.popup_radius,0,24,12);value.popup_outline=value.popup_outline?1:0;
    value.popup_visualizer=value.popup_visualizer?1:0;
    value.popup_gap=limit(value.popup_gap,0,48,8);value.island_blur=std::min(value.island_blur,48u);
    value.apps_autosize=value.apps_autosize?1:0;value.apps_width=limit(value.apps_width,200,1200,480);
    for(int index=0;index<3;++index){value.radii[index]=limit(value.radii[index],0,24,12);value.opacities[index]=limit(value.opacities[index],0,1,.9);}
    return value;
}

struct MediaRequest {std::uint64_t source=0;std::int64_t position=0;std::uint64_t revision=0;};
struct SharedState {
    std::uint32_t magic = protocol_magic;
    std::uint32_t version = protocol_version;
    DWORD controller_pid = 0;
    DWORD explorer_pid = 0;
    volatile LONG command = static_cast<LONG>(Command::none);
    volatile LONG request = 0;
    volatile LONG acknowledgement = 0;
    volatile LONG phase = static_cast<LONG>(Phase::waiting);
    volatile LONG result = S_OK;
    volatile LONG elements_seen = 0;
    volatile LONG frames_seen = 0;
    volatile LONG probe_passed = 0;
    volatile LONG cleanup_complete = 0;
    volatile LONG worker_thread_id = 0;
    MediaRequest media_request;
    volatile LONG media_sequence=0;
    volatile LONG visualizer_requested=0,audio_level=0,audio_tick=0,audio_error=0;
    std::uint64_t media_revision=0,active_source=0;
    wchar_t media_sources[4096]{};
    Settings settings;
    wchar_t detail[1024]{};
};

inline std::wstring object_name(DWORD explorer_pid, const wchar_t* suffix) {
    return L"Local\\Stillbar." + std::to_wstring(explorer_pid) + suffix;
}

inline const wchar_t* phase_name(Phase phase) {
    switch (phase) {
    case Phase::waiting: return L"waiting";
    case Phase::attached: return L"attached";
    case Phase::probing: return L"probing";
    case Phase::verified: return L"verified";
    case Phase::active: return L"active";
    case Phase::disabled: return L"disabled";
    case Phase::failed: return L"failed";
    }
    return L"unknown";
}
}
