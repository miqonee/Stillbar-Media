#pragma once
#include <algorithm>
#include <cstdint>
#include <span>

namespace islands {
struct MediaCandidate {std::uint64_t id=0;bool playing=false;bool resumable=true;};
struct MediaSelection {std::uint64_t active=0,requested=0;};
inline MediaSelection choose_media_source(std::span<const MediaCandidate> sources,std::uint64_t requested,std::uint64_t current,std::uint64_t previous) {
    const MediaCandidate *manual=nullptr,*system=nullptr,*retained=nullptr,*playing=nullptr;
    for(const auto& source:sources){
        if(source.id==requested)manual=&source;
        if(source.id==current)system=&source;
        if(source.id==previous)retained=&source;
        if(source.playing && !playing)playing=&source;
    }
    if(requested && manual && (manual->playing || manual->resumable || !playing))return {manual->id,requested};
    if(system && system->playing)return {system->id,0};
    if(retained && retained->playing)return {retained->id,0};
    if(playing)return {playing->id,0};
    if(retained)return {retained->id,0};
    if(system)return {system->id,0};
    return {sources.empty()?0:sources.front().id,0};
}
}
