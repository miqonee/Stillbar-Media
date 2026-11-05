#pragma once
#include <cmath>
#include <string>
#include "taskbar_tree.hpp"
#include <winrt/Windows.UI.Xaml.Controls.Primitives.h>
#include <winrt/Windows.UI.Xaml.Markup.h>
#include <winrt/Windows.UI.Xaml.Shapes.h>

namespace islands {
class SeekThumb {
    winrt::Windows::UI::Xaml::Controls::Primitives::Thumb thumb_{nullptr};
    winrt::Windows::UI::Xaml::Controls::Border ring_{nullptr};
    winrt::Windows::UI::Xaml::Shapes::Ellipse dot_{nullptr};
public:
    void configure(winrt::Windows::UI::Xaml::Controls::Slider slider,bool dark) {
        namespace xaml=winrt::Windows::UI::Xaml;
        slider.ApplyTemplate();
        const auto candidate=find_taskbar_element(slider,L"HorizontalThumb").try_as<xaml::Controls::Primitives::Thumb>();
        if(!candidate)return;
        if(candidate!=thumb_){
            close();thumb_=candidate;
            thumb_.Width(14);thumb_.Height(14);thumb_.UseLayoutRounding(false);
            thumb_.Template(xaml::Markup::XamlReader::Load(LR"(<ControlTemplate xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml" TargetType="Thumb">
<Grid UseLayoutRounding="False">
 <VisualStateManager.VisualStateGroups><VisualStateGroup x:Name="CommonStates">
  <VisualState x:Name="Normal"/><VisualState x:Name="PointerOver"/>
  <VisualState x:Name="Pressed"><VisualState.Setters><Setter Target="IslandsSeekDot.Width" Value="8"/><Setter Target="IslandsSeekDot.Height" Value="8"/></VisualState.Setters></VisualState>
  <VisualState x:Name="Disabled"><VisualState.Setters><Setter Target="IslandsSeekDot.Opacity" Value="0.4"/></VisualState.Setters></VisualState>
 </VisualStateGroup></VisualStateManager.VisualStateGroups>
 <Border x:Name="IslandsSeekRing" Width="14" Height="14" CornerRadius="7" Background="{TemplateBinding Background}" BorderBrush="{TemplateBinding BorderBrush}" BorderThickness="0.666667" HorizontalAlignment="Center" VerticalAlignment="Center" UseLayoutRounding="False"/>
 <Ellipse x:Name="IslandsSeekDot" Width="10" Height="10" Fill="{TemplateBinding Foreground}" HorizontalAlignment="Center" VerticalAlignment="Center" UseLayoutRounding="False"/>
</Grid></ControlTemplate>)").as<xaml::Controls::ControlTemplate>());
            thumb_.ApplyTemplate();
            ring_=find_taskbar_element(thumb_,L"IslandsSeekRing").as<xaml::Controls::Border>();
            dot_=find_taskbar_element(thumb_,L"IslandsSeekDot").as<xaml::Shapes::Ellipse>();
        }
        thumb_.Background(xaml::Media::SolidColorBrush{dark?winrt::Windows::UI::Color{255,67,67,67}:winrt::Windows::UI::Color{255,250,250,250}});
        thumb_.BorderBrush(xaml::Media::SolidColorBrush{dark?winrt::Windows::UI::Color{255,89,89,89}:winrt::Windows::UI::Color{255,195,195,195}});
        thumb_.Foreground(slider.Foreground());
    }
    std::wstring describe()const {
        if(!thumb_ || !ring_ || !dot_)return L" seek_thumb=0";
        const auto centre=[&](auto element){return element.TransformToVisual(thumb_).TransformPoint({static_cast<float>(element.ActualWidth()/2),static_cast<float>(element.ActualHeight()/2)});};
        const auto ring=centre(ring_),dot=centre(dot_);
        return L" seek_thumb="+std::to_wstring(thumb_.ActualWidth())+L" seek_ring="+std::to_wstring(ring_.ActualWidth())+L" seek_dot="+std::to_wstring(dot_.ActualWidth())+L" seek_center_error="+std::to_wstring(thumb_.IsLoaded()?std::hypot(ring.X-dot.X,ring.Y-dot.Y):-1.f);
    }
    void close()noexcept{dot_=nullptr;ring_=nullptr;thumb_=nullptr;}
};
}
