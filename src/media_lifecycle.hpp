#pragma once
#include "media_timeline.hpp"
#include <optional>

namespace islands {
enum class PlaybackState { closed,opened,changing,stopped,playing,paused };
enum class MediaHideReason { none,waiting_for_playback,closed,stopped,ended,empty_timeline,session_lost,inactive,changing,unavailable };

inline const wchar_t* playback_name(PlaybackState state) {
    switch(state){
    case PlaybackState::closed:return L"closed";case PlaybackState::opened:return L"opened";
    case PlaybackState::changing:return L"changing";case PlaybackState::stopped:return L"stopped";
    case PlaybackState::playing:return L"playing";case PlaybackState::paused:return L"paused";
    }return L"unknown";
}
inline const wchar_t* hide_reason_name(MediaHideReason reason) {
    switch(reason){
    case MediaHideReason::none:return L"none";case MediaHideReason::waiting_for_playback:return L"waiting_for_playback";
    case MediaHideReason::closed:return L"closed";case MediaHideReason::stopped:return L"stopped";
    case MediaHideReason::ended:return L"ended";case MediaHideReason::empty_timeline:return L"empty_timeline";
    case MediaHideReason::session_lost:return L"session_lost";case MediaHideReason::inactive:return L"inactive";
    case MediaHideReason::changing:return L"changing";case MediaHideReason::unavailable:return L"unavailable";
    }return L"unknown";
}

class PlaybackHistory {
    bool played_=false;
    MediaHideReason hidden_=MediaHideReason::waiting_for_playback;
public:
    bool played()const{return played_;}
    void observe(PlaybackState state){if(state==PlaybackState::playing){played_=true;hidden_=MediaHideReason::none;}}
    MediaHideReason reason(PlaybackState state,const MediaTimeline& timeline,bool timeline_valid=true,bool ordinary_pause=false)const {
        if(state==PlaybackState::playing)return MediaHideReason::none;
        if(state==PlaybackState::closed)return MediaHideReason::closed;
        if(state==PlaybackState::stopped)return MediaHideReason::stopped;
        if(!played_)return hidden_;
        if(state!=PlaybackState::paused && state!=PlaybackState::changing)return MediaHideReason::inactive;
        if(state==PlaybackState::changing)return MediaHideReason::changing;
        if(ordinary_pause)return MediaHideReason::none;
        if(timeline_valid && timeline.known_duration() && timeline.position>=timeline.end)return MediaHideReason::ended;
        if(timeline_valid && !timeline.known_duration() && timeline.position<=timeline.start)return MediaHideReason::empty_timeline;
        return MediaHideReason::none;
    }
    void confirm_hidden(MediaHideReason reason){if(reason!=MediaHideReason::none){played_=false;hidden_=reason;}}
};

class MediaVisibility {
public:
    using Clock=MediaTimeline::Clock;
    static constexpr auto grace=std::chrono::milliseconds(750);
    static constexpr auto handoff=std::chrono::milliseconds(1500);
private:
    bool visible_=false;
    MediaHideReason reason_=MediaHideReason::waiting_for_playback;
    std::optional<Clock::time_point> pending_;
    std::optional<Clock::time_point> transition_until_;
    Clock::time_point deadline()const {
        const auto normal=*pending_+grace;
        return transition_until_?std::max(normal,*transition_until_):normal;
    }
public:
    bool visible()const{return visible_;}
    bool pending()const{return pending_.has_value();}
    MediaHideReason reason()const{return reason_;}
    bool expired(Clock::time_point now=Clock::now())const{return pending_ && now>=deadline();}
    void begin_transition(Clock::time_point now=Clock::now(),bool restart=false) {
        if(visible_ && (restart || !transition_until_ || now>=*transition_until_))transition_until_=now+handoff;
    }
    void update(MediaHideReason reason,Clock::time_point now=Clock::now()) {
        reason_=reason;
        if(reason==MediaHideReason::none){visible_=true;pending_.reset();return;}
        if(!visible_){pending_.reset();transition_until_.reset();return;}
        if(!pending_)pending_=now;
        if(now>=deadline()){visible_=false;pending_.reset();transition_until_.reset();}
    }
    std::chrono::milliseconds remaining(Clock::time_point now=Clock::now())const {
        return pending_?std::max(std::chrono::milliseconds(1),std::chrono::ceil<std::chrono::milliseconds>(deadline()-now)):std::chrono::milliseconds(0);
    }
};
}
