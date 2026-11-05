#pragma once
#include <algorithm>
#include <chrono>
#include <cmath>
#include <numbers>

namespace islands::motion {
using Clock = std::chrono::steady_clock;
inline constexpr double damping_ratio=.86;
struct Sample { double position = 0; double velocity = 0; };

inline Sample spring_sample(double start, double target, double velocity,
                            double seconds, double period, double damping = damping_ratio) {
    const double omega = 2 * std::numbers::pi / std::max(.08, period);
    const double decay = damping * omega;
    const double frequency = omega * std::sqrt(1 - damping * damping);
    const double displacement = start - target;
    const double sine_weight = (velocity + decay * displacement) / frequency;
    const double cosine = std::cos(frequency * seconds);
    const double sine = std::sin(frequency * seconds);
    const double envelope = std::exp(-decay * seconds);
    return {
        target + envelope * (displacement * cosine + sine_weight * sine),
        envelope * ((sine_weight * frequency - decay * displacement) * cosine
                    - (displacement * frequency + decay * sine_weight) * sine)
    };
}

class Spring {
    double start_ = 0, target_ = 0, velocity_ = 0, period_ = .18;
    Clock::time_point started_{};
    bool initialized_ = false;
public:
    bool initialized() const { return initialized_; }
    double target() const { return target_; }
    double settling_seconds() const { return std::max(.16, 8 * period_ / (damping_ratio * 2 * std::numbers::pi)); }
    bool settled(Clock::time_point now)const{return !initialized_ || std::chrono::duration<double>(now-started_).count()>=settling_seconds();}
    Sample at(Clock::time_point now) const {
        if (!initialized_) return {};
        if(settled(now))return {target_,0};
        return spring_sample(start_, target_, velocity_,
            std::max(0., std::chrono::duration<double>(now - started_).count()), period_);
    }
    void reset(double start, double target, double velocity, double period, Clock::time_point now) {
        start_ = start; target_ = target; velocity_ = velocity;
        period_ = std::max(.08, period); started_ = now; initialized_ = true;
    }
    void retarget(double target, double period, Clock::time_point now) {
        const auto current = initialized_ ? at(now) : Sample{target, 0};
        reset(current.position, target, current.velocity, period, now);
    }
};

template<class Emit> void sample_frames(double duration,Clock::time_point now,Emit emit) {
    const int samples=std::max(2,static_cast<int>(std::ceil(duration*120)));
    for(int index=0;index<=samples;++index){
        const auto delta=std::chrono::ceil<Clock::duration>(std::chrono::duration<double>(duration*index/samples));
        emit(static_cast<float>(index)/static_cast<float>(samples),now+delta,index==samples);
    }
}
}
