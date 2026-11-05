#pragma once
#include "taskbar_tree.hpp"
#include <winrt/Windows.UI.h>
#include <winrt/Windows.UI.Xaml.Shapes.h>

namespace islands {
struct TaskbarMaterial {
    winrt::Windows::UI::Xaml::Media::Brush source{nullptr};
    winrt::Windows::UI::Color color{230,32,32,32};
    bool native=false;
};
inline TaskbarMaterial read_taskbar_material(tree_xaml::FrameworkElement background,bool dark) {
    namespace media=tree_xaml::Media;
    TaskbarMaterial result;result.color=dark?winrt::Windows::UI::Color{230,32,32,32}:winrt::Windows::UI::Color{230,243,243,243};
    const auto element=find_taskbar_element(background,L"BackgroundFill");
    if(const auto fill=element.try_as<tree_xaml::Shapes::Shape>()){
        result.source=fill.Fill();
        if(const auto solid=result.source.try_as<media::SolidColorBrush>()){const auto color=solid.Color();if(color.A){result.color=color;result.native=true;}}
        else if(const auto acrylic=result.source.try_as<media::AcrylicBrush>()){result.color=acrylic.TintColor();result.color.A=static_cast<std::uint8_t>(255*acrylic.TintOpacity());result.native=true;}
    }
    return result;
}
inline tree_xaml::Media::Brush taskbar_material(const TaskbarMaterial& profile,double opacity,bool on_native=false) {
    namespace media=tree_xaml::Media;
    if(on_native)return media::SolidColorBrush{winrt::Windows::UI::Colors::Transparent()};
    if(const auto acrylic=profile.source.try_as<media::AcrylicBrush>()){
        media::AcrylicBrush brush;brush.BackgroundSource(acrylic.BackgroundSource());brush.TintColor(acrylic.TintColor());brush.FallbackColor(acrylic.FallbackColor());brush.TintOpacity(acrylic.TintOpacity());brush.TintLuminosityOpacity(acrylic.TintLuminosityOpacity());brush.AlwaysUseFallback(acrylic.AlwaysUseFallback());brush.Opacity(opacity*acrylic.Opacity());return brush;
    }
    media::SolidColorBrush brush{profile.color};brush.Opacity(opacity);return brush;
}
}
