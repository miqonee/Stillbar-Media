#pragma once
#include "audio_meter.hpp"
#include "wave_geometry.hpp"
#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Hosting.h>
#include <winrt/Windows.UI.Xaml.Automation.h>
#include <winrt/Windows.UI.Xaml.Automation.Peers.h>
#include <functional>
#include <memory>

namespace islands {
namespace wave_comp=winrt::Windows::UI::Composition;
class AudioVisualizer : public std::enable_shared_from_this<AudioVisualizer> {
    winrt::Windows::UI::Xaml::Controls::Canvas host_{nullptr};
    wave_comp::ContainerVisual layer_{nullptr};
    wave_comp::CompositionPropertySet state_{nullptr};
    std::array<wave_comp::ShapeVisual,3> waves_{nullptr,nullptr,nullptr};
    std::array<wave_comp::CompositionColorBrush,3> colors_{nullptr,nullptr,nullptr};
    std::array<winrt::Windows::UI::Color,3> last_colors_{};
    winrt::Windows::UI::Xaml::DispatcherTimer timer_{nullptr};
    winrt::event_token tick_{},size_changed_{};
    std::function<void(bool)> request_;
    std::function<float()> sample_;
    AudioHistory history_;
    winrt::Windows::UI::Color accent_{};
    bool enabled_=false,open_=false,running_=false,closed_=false,dark_=true;
    std::uint64_t ticks_=0;
    float peak_=0,target_=0,width_=0,height_=0;
    double radius_=0;
    HRESULT error_=S_OK;

    void drift() {
        if(!running_ || width_<=0)return;
        constexpr int durations[]{8500,12000,10000};
        for(std::size_t index=0;index<waves_.size();++index){
            auto animation=layer_.Compositor().CreateScalarKeyFrameAnimation();
            animation.InsertKeyFrame(0,index==1?-width_:0);animation.InsertKeyFrame(1,index==1?0:-width_,layer_.Compositor().CreateLinearEasingFunction());
            animation.Duration(std::chrono::milliseconds(durations[index]));animation.IterationBehavior(wave_comp::AnimationIterationBehavior::Forever);waves_[index].StartAnimation(L"Offset.X",animation);
        }
    }
    void resize() {
        if(!layer_)return;
        const auto size=host_.ActualSize();const bool width_changed=std::abs(size.x-width_)>.1f;width_=size.x;height_=size.y;
        layer_.Size(size);state_.InsertScalar(L"Width",width_);state_.InsertScalar(L"Height",height_);
        auto clip=layer_.Compositor().CreateRectangleClip();clip.Right(width_);clip.Bottom(height_);const auto corner=static_cast<float>(radius_);
        clip.TopLeftRadius({corner,corner});clip.TopRightRadius({corner,corner});clip.BottomLeftRadius({corner,corner});clip.BottomRightRadius({corner,corner});layer_.Clip(clip);
        for(auto& wave:waves_)wave.Size({width_*2,height_});if(width_changed)drift();
    }
    void initialize() {
        using Preview=winrt::Windows::UI::Xaml::Hosting::ElementCompositionPreview;
        const auto compositor=Preview::GetElementVisual(host_).Compositor();layer_=compositor.CreateContainerVisual();
        state_=compositor.CreatePropertySet();state_.InsertScalar(L"Level",0);state_.InsertScalar(L"Width",0);state_.InsertScalar(L"Height",0);
        winrt::com_ptr<ID2D1Factory> factory;winrt::check_hresult(D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED,factory.put()));
        constexpr float baselines[]{500,570,630},amplitudes[]{140,110,150},phases[]{0,2.1f,4.2f};
        for(std::size_t index=0;index<waves_.size();++index){
            auto geometry=compositor.CreatePathGeometry();geometry.Path(wave_comp::CompositionPath{winrt::make<WaveSource>(wave_geometry(factory.get(),baselines[index],amplitudes[index],phases[index]))});
            colors_[index]=compositor.CreateColorBrush(accent_);auto shape=compositor.CreateSpriteShape(geometry);shape.FillBrush(colors_[index]);
            auto scale=compositor.CreateExpressionAnimation(L"Vector2(State.Width / 1000, State.Height / 1000 * State.Level)");scale.SetReferenceParameter(L"State",state_);shape.StartAnimation(L"Scale",scale);
            auto offset=compositor.CreateExpressionAnimation(L"Vector2(0, State.Height * (1 - State.Level))");offset.SetReferenceParameter(L"State",state_);shape.StartAnimation(L"Offset",offset);
            waves_[index]=compositor.CreateShapeVisual();waves_[index].Shapes().Append(shape);layer_.Children().InsertAtTop(waves_[index]);
        }
        Preview::SetElementChildVisual(host_,layer_);resize();
        timer_=winrt::Windows::UI::Xaml::DispatcherTimer{};timer_.Interval(std::chrono::milliseconds(33));const auto weak=weak_from_this();
        tick_=timer_.Tick([weak](auto&&,auto&&){if(auto self=weak.lock();self && self->running_)try{self->peak_=self->sample_?self->sample_():0;++self->ticks_;self->draw(self->history_.update(self->peak_).back());}catch(...){self->error_=winrt::to_hresult();self->enabled_=false;try{self->update();}catch(...){}}});
        size_changed_=host_.SizeChanged([weak](auto&&,auto&&){if(auto self=weak.lock();self && !self->closed_)try{self->resize();}catch(...){self->error_=winrt::to_hresult();}});
        tint();
    }
    void tint() {
        constexpr float dark_alpha[]{.20f,.13f,.09f},light_alpha[]{.14f,.10f,.07f};
        for(std::size_t index=0;index<colors_.size();++index)if(colors_[index]){
            auto color=accent_;color.A=static_cast<BYTE>(255*(dark_?dark_alpha[index]:light_alpha[index]));if(color==last_colors_[index])continue;last_colors_[index]=color;
            if(!running_){colors_[index].Color(color);continue;}
            auto animation=layer_.Compositor().CreateColorKeyFrameAnimation();animation.InsertExpressionKeyFrame(0,L"this.StartingValue");animation.InsertKeyFrame(1,color);animation.Duration(std::chrono::milliseconds(320));animation.StopBehavior(wave_comp::AnimationStopBehavior::SetToFinalValue);colors_[index].StartAnimation(L"Color",animation);
        }
    }
    void draw(float level) {
        const float target=.5f+.5f*(level<.002f?0:std::sqrt(level));if(std::abs(target-target_)<.002f)return;target_=target;
        auto animation=layer_.Compositor().CreateScalarKeyFrameAnimation();animation.InsertExpressionKeyFrame(0,L"this.StartingValue");animation.InsertKeyFrame(1,target_,layer_.Compositor().CreateCubicBezierEasingFunction({.2f,0},{.2f,1}));animation.Duration(std::chrono::milliseconds(75));state_.StartAnimation(L"Level",animation);
    }
    void update() {
        if(closed_)return;
        const bool run=enabled_ && open_;host_.Visibility(enabled_?winrt::Windows::UI::Xaml::Visibility::Visible:winrt::Windows::UI::Xaml::Visibility::Collapsed);
        if(run && !layer_)initialize();if(run==running_)return;running_=run;if(request_)request_(run);
        if(run){draw(0);drift();timer_.Start();}
        else{if(timer_)timer_.Stop();history_.reset();peak_=0;target_=0;if(state_){state_.StopAnimation(L"Level");state_.InsertScalar(L"Level",0);}for(auto& wave:waves_)if(wave){wave.StopAnimation(L"Offset.X");wave.Offset({});}}
    }
public:
    AudioVisualizer(std::function<void(bool)> request,std::function<float()> sample):request_(std::move(request)),sample_(std::move(sample)) {
        host_=winrt::Windows::UI::Xaml::Controls::Canvas{};host_.IsHitTestVisible(false);host_.Visibility(winrt::Windows::UI::Xaml::Visibility::Collapsed);
        winrt::Windows::UI::Xaml::Automation::AutomationProperties::SetAccessibilityView(host_,winrt::Windows::UI::Xaml::Automation::Peers::AccessibilityView::Raw);
    }
    auto element()const{return host_;}
    void color(winrt::Windows::UI::Color value,bool dark)noexcept{if(value==accent_ && dark==dark_)return;accent_=value;dark_=dark;try{tint();}catch(...){error_=winrt::to_hresult();}}
    void configure(bool enabled,winrt::Windows::UI::Color color,double radius,bool dark)noexcept {
        try{enabled_=enabled;accent_=color;radius_=radius;dark_=dark;update();tint();resize();}catch(...){error_=winrt::to_hresult();enabled_=false;try{update();}catch(...){}}
    }
    void open(bool value)noexcept{try{open_=value;update();}catch(...){error_=winrt::to_hresult();enabled_=false;try{update();}catch(...){}}}
    void close()noexcept {
        try{open(false);closed_=true;if(timer_)timer_.Tick(tick_);if(host_){if(layer_)host_.SizeChanged(size_changed_);winrt::Windows::UI::Xaml::Hosting::ElementCompositionPreview::SetElementChildVisual(host_,nullptr);}}catch(...){}
        timer_=nullptr;for(auto& wave:waves_)wave=nullptr;for(auto& color:colors_)color=nullptr;state_=nullptr;layer_=nullptr;host_=nullptr;request_={};sample_={};
    }
    std::wstring describe()const{return L" visualizer_running="+std::to_wstring(running_)+L" visualizer_ticks="+std::to_wstring(ticks_)+L" audio_peak="+std::to_wstring(peak_)+L" visualizer_background=1 wave_level="+std::to_wstring(target_)+L" wave_rgb="+std::to_wstring(accent_.R)+L","+std::to_wstring(accent_.G)+L","+std::to_wstring(accent_.B)+L" wave_error="+std::to_wstring(static_cast<std::uint32_t>(error_));}
};
}
