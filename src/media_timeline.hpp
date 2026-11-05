#pragma once
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>

namespace islands {
struct MediaTimeline {
    using Clock=std::chrono::steady_clock;
    static constexpr double ticks_per_second=10'000'000.;
    std::int64_t start=0,end=0,minimum=0,maximum=0,position=0;
    bool playing=false,can_seek=false;
    double rate=1;
    Clock::time_point observed=Clock::now();

    bool known_duration()const{return end>start;}
    bool seekable()const{return can_seek && known_duration() && std::min(end,maximum)>std::max(start,minimum);}
    std::int64_t current(Clock::time_point now=Clock::now())const {
        if(!known_duration())return position;
        const auto seconds=std::max(0.,std::chrono::duration<double>(now-observed).count());
        const long double offset=playing && std::isfinite(rate)?seconds*ticks_per_second*rate:0;
        return static_cast<std::int64_t>(std::clamp(static_cast<long double>(position)+offset,static_cast<long double>(start),static_cast<long double>(end)));
    }
    double ratio(std::int64_t value)const {
        return known_duration()?std::clamp(static_cast<double>((static_cast<long double>(value)-start)/(static_cast<long double>(end)-start)),0.,1.):0.;
    }
    std::optional<std::int64_t> seek(double ratio)const {
        if(!seekable() || !std::isfinite(ratio))return {};
        const long double value=start+(static_cast<long double>(end)-start)*std::clamp(ratio,0.,1.);
        return static_cast<std::int64_t>(std::clamp(value,static_cast<long double>(std::max(start,minimum)),static_cast<long double>(std::min(end,maximum))));
    }
    std::wstring elapsed(std::int64_t value)const {
        const auto seconds=static_cast<std::uint64_t>(std::max(0.L,(static_cast<long double>(value)-start)/ticks_per_second));
        const auto two=[](std::uint64_t part){return (part<10?L"0":L"")+std::to_wstring(part);};
        if(seconds>=3600)return std::to_wstring(seconds/3600)+L":"+two(seconds/60%60)+L":"+two(seconds%60);
        return std::to_wstring(seconds/60)+L":"+two(seconds%60);
    }
};
}
