#pragma once
#include <winrt/Windows.UI.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <span>

namespace islands {
inline std::optional<winrt::Windows::UI::Color> artwork_color(std::span<const std::uint8_t> bgra) {
    struct Bin {float weight=0,red=0,green=0,blue=0;};std::array<Bin,24> bins{};
    for(std::size_t index=0;index+3<bgra.size();index+=4){
        if(bgra[index+3]<128)continue;
        const float red=bgra[index+2]/255.f,green=bgra[index+1]/255.f,blue=bgra[index]/255.f;
        const float high=std::max({red,green,blue}),low=std::min({red,green,blue}),chroma=high-low;
        if(chroma<.12f || high<.16f || low>.85f)continue;
        float hue=high==red?(green-blue)/chroma:high==green?2+(blue-red)/chroma:4+(red-green)/chroma;
        if(hue<0)hue+=6;const auto bin=std::min<std::size_t>(23,static_cast<std::size_t>(hue*4));
        const float weight=chroma*(.4f+.6f*high);auto& value=bins[bin];value.weight+=weight;value.red+=red*weight;value.green+=green*weight;value.blue+=blue*weight;
    }
    const auto best=std::max_element(bins.begin(),bins.end(),[](const auto& a,const auto& b){return a.weight<b.weight;});
    if(best->weight<1)return std::nullopt;
    std::array<float,3> color{best->red/best->weight,best->green/best->weight,best->blue/best->weight};
    const float high=*std::max_element(color.begin(),color.end());const float gain=std::clamp(.78f/high,.8f,2.f);
    const auto channel=[&](float value){return static_cast<std::uint8_t>(std::clamp(value*gain,0.f,1.f)*255+.5f);};
    return winrt::Windows::UI::Color{255,channel(color[0]),channel(color[1]),channel(color[2])};
}
}
