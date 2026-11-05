#pragma once
#include "spring.hpp"
#include "backdrop_blur.hpp"
#include <winrt/Windows.UI.h>
#include <winrt/Windows.UI.Composition.h>
#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Hosting.h>
#include <string>

namespace islands::motion {
namespace surface_composition=winrt::Windows::UI::Composition;
namespace surface_xaml=winrt::Windows::UI::Xaml;
class IslandSurface {
    surface_xaml::Controls::Border border_{nullptr};
    surface_composition::Visual visual_{nullptr};
    surface_composition::CompositionRoundedRectangleGeometry geometry_{nullptr};
    surface_composition::ShapeVisual outline_{nullptr};
    surface_composition::SpriteVisual tint_{nullptr};
    surface_composition::ContainerVisual layers_{nullptr};
    surface_composition::CompositionColorBrush stroke_{nullptr};
    surface_composition::CompositionEffectBrush blur_brush_{nullptr};
    surface_xaml::Media::Brush blur_paint_{nullptr};
    Spring width_,anchor_motion_;
    double anchor_=0,alignment_=0,frame_width_=-1,height_=-1,top_=-1,radius_=12;
    std::uint64_t changes_=0;

    void stop() noexcept {
        try{if(geometry_){geometry_.StopAnimation(L"Size");geometry_.StopAnimation(L"Offset");}}catch(...){}
    }
public:
    explicit IslandSurface(surface_xaml::Controls::Border border):border_(border) {
        using Preview=surface_xaml::Hosting::ElementCompositionPreview;
        visual_=Preview::GetElementVisual(border_);const auto compositor=visual_.Compositor();
        geometry_=compositor.CreateRoundedRectangleGeometry();
        visual_.Clip(compositor.CreateGeometricClip(geometry_));
        outline_=compositor.CreateShapeVisual();stroke_=compositor.CreateColorBrush();
        const auto shape=compositor.CreateSpriteShape(geometry_);shape.StrokeBrush(stroke_);shape.StrokeThickness(2);
        outline_.Shapes().Append(shape);
        layers_=compositor.CreateContainerVisual();tint_=compositor.CreateSpriteVisual();
        layers_.Children().InsertAtTop(tint_);layers_.Children().InsertAtTop(outline_);Preview::SetElementChildVisual(border_,layers_);
        border_.BorderThickness({0,0,0,0});border_.CornerRadius({0,0,0,0});
    }
    void material(winrt::Windows::UI::Color color,double opacity,float blur) {
        if(blur>0 && winrt::Windows::UI::ViewManagement::UISettings{}.AdvancedEffectsEnabled()){
            if(!blur_brush_){blur_brush_=make_backdrop_blur(visual_.Compositor(),blur);blur_paint_=winrt::make<BackdropPaint>(blur_brush_);}
            blur_brush_.Properties().InsertScalar(L"Blur.StandardDeviation",blur);blur_paint_.Opacity(blur_strength(blur));border_.Background(blur_paint_);
        }else border_.Background(nullptr);
        color.A=255;tint_.Opacity(static_cast<float>(opacity));tint_.Brush(visual_.Compositor().CreateColorBrush(color));
    }
    void style(double radius,winrt::Windows::UI::Color color,double opacity) {
        radius_=radius;
        const float rounded=static_cast<float>(std::min(radius_,height_>0?height_/2:radius_));geometry_.CornerRadius({rounded,rounded});
        color.A=static_cast<std::uint8_t>(std::round(color.A*std::clamp(opacity,0.,1.)));stroke_.Color(color);
        border_.BorderThickness({0,0,0,0});border_.CornerRadius({0,0,0,0});
    }
    void place(double frame_width,double anchor,double alignment,double top,double width,double height,
               std::uint32_t period_ms,bool animate,Clock::time_point now) {
        const bool resized=std::abs(frame_width-frame_width_)>.2 || std::abs(height-height_)>.2 || std::abs(top-top_)>.2;
        const bool initialized=width_.initialized() && !resized && std::abs(alignment-alignment_)<.001;
        const bool unchanged=initialized && std::abs(width-width_.target())<.05 && std::abs(anchor-anchor_)<.05;
        if(unchanged && animate)return;
        const double period=std::max(80u,period_ms)/1000.;
        if(resized){
            border_.Width(frame_width);border_.Height(height);surface_xaml::Controls::Canvas::SetLeft(border_,0);surface_xaml::Controls::Canvas::SetTop(border_,top);
            outline_.Size({static_cast<float>(frame_width),static_cast<float>(height)});
            tint_.Size(outline_.Size());
            frame_width_=frame_width;height_=height;top_=top;
            const float rounded=static_cast<float>(std::min(radius_,height/2));geometry_.CornerRadius({rounded,rounded});
        }
        anchor_=anchor;alignment_=alignment;
        if(!initialized || !animate){width_.reset(width,width,0,period,now);anchor_motion_.reset(anchor,anchor,0,period,now);}
        else{width_.retarget(width,period,now);anchor_motion_.retarget(anchor,period,now);}
        stop();geometry_.Size({static_cast<float>(width),static_cast<float>(height)});geometry_.Offset({static_cast<float>(anchor-alignment*width),0});
        if(!initialized || !animate)return;
        ++changes_;
        const auto compositor=visual_.Compositor();const auto easing=compositor.CreateLinearEasingFunction();
        auto size=compositor.CreateVector2KeyFrameAnimation(),offset=compositor.CreateVector2KeyFrameAnimation();
        const double duration=width_.settling_seconds();
        sample_frames(duration,now,[&](float progress,Clock::time_point at,bool final){
            const double value=final?width:std::clamp(width_.at(at).position,1.,frame_width);
            size.InsertKeyFrame(progress,{static_cast<float>(value),static_cast<float>(height)},easing);
            const double position=final?anchor:anchor_motion_.at(at).position;
            offset.InsertKeyFrame(progress,{static_cast<float>(position-alignment*value),0},easing);
        });
        const auto timespan=std::chrono::duration_cast<winrt::Windows::Foundation::TimeSpan>(std::chrono::duration<double>(duration));
        size.Duration(timespan);offset.Duration(timespan);geometry_.StartAnimation(L"Size",size);geometry_.StartAnimation(L"Offset",offset);
    }
    void reset() noexcept {stop();width_=Spring{};anchor_motion_=Spring{};}
    void close() noexcept {
        stop();try{if(border_){border_.Background(nullptr);surface_xaml::Hosting::ElementCompositionPreview::SetElementChildVisual(border_,nullptr);}if(visual_)visual_.Clip(nullptr);}catch(...){}
        border_=nullptr;visual_=nullptr;geometry_=nullptr;outline_=nullptr;stroke_=nullptr;layers_=nullptr;tint_=nullptr;blur_paint_=nullptr;blur_brush_=nullptr;
    }
    std::wstring describe()const {
        const auto now=Clock::now();const double width=width_.at(now).position,anchor=anchor_motion_.at(now).position;
        return L" width="+std::to_wstring(width)+L" left="+std::to_wstring(anchor-alignment_*width)+L" anchor="+std::to_wstring(anchor)+L" changes="+std::to_wstring(changes_);
    }
};
}
