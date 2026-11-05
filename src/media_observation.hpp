#pragma once
#include "media_lifecycle.hpp"
#include <optional>

namespace islands {
// GSMTC can succeed with a null playback object while a session is being replaced.
template<class Info> std::optional<PlaybackState> playback_state(const Info& info) {
    if(!info)return std::nullopt;
    return static_cast<PlaybackState>(info.PlaybackStatus());
}
enum class MediaReadFailure {missing,exception};
struct PlaybackOptions {
    bool previous=false,next=false,toggle=false,can_seek=false;
    double rate=1.;
};
template<class Info,class Failed> PlaybackOptions playback_options(const Info& info,bool playing,Failed failed) {
    PlaybackOptions value;
    try{
        if(const auto controls=info.Controls()){
            const PlaybackOptions observed{controls.IsPreviousEnabled(),controls.IsNextEnabled(),
                controls.IsPlayPauseToggleEnabled() || (playing?controls.IsPauseEnabled():controls.IsPlayEnabled()),
                controls.IsPlaybackPositionEnabled(),1.};
            value=observed;
        }else failed(MediaReadFailure::missing);
    }catch(...){failed(MediaReadFailure::exception);}
    try{const auto rate=info.PlaybackRate();if(rate)value.rate=rate.Value();}
    catch(...){failed(MediaReadFailure::exception);}
    return value;
}
}
