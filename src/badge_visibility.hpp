#pragma once
#include <winrt/Windows.UI.Composition.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Hosting.h>
#include <memory>

namespace islands {
namespace badge_xaml=winrt::Windows::UI::Xaml;
namespace badge_composition=winrt::Windows::UI::Composition;
class BadgeVisibility : public std::enable_shared_from_this<BadgeVisibility> {
    badge_xaml::Controls::Canvas hit_{nullptr};
    badge_xaml::Controls::Border border_{nullptr};
    badge_composition::Visual visual_{nullptr};
    badge_composition::CompositionPropertySet motion_{nullptr};
    badge_composition::CompositionScopedBatch batch_{nullptr};
    winrt::event_token completed_{};
    std::uint64_t generation_=0;
    bool target_=false,running_=false,closed_=false;

    void cancel_completion() {
        ++generation_;if(batch_ && completed_.value)batch_.Completed(completed_);batch_=nullptr;completed_={};
    }
    void finish() {
        cancel_completion();motion_.StopAnimation(L"Reveal");motion_.InsertScalar(L"Reveal",target_?1.f:0.f);running_=false;
        const auto visibility=target_?badge_xaml::Visibility::Visible:badge_xaml::Visibility::Collapsed;
        border_.Visibility(visibility);hit_.Visibility(visibility);hit_.IsHitTestVisible(target_);
    }
    void initialize() {
        using Preview=badge_xaml::Hosting::ElementCompositionPreview;
        Preview::SetIsTranslationEnabled(hit_,true);visual_=Preview::GetElementVisual(hit_);
        const auto compositor=visual_.Compositor();motion_=compositor.CreatePropertySet();motion_.InsertScalar(L"Reveal",0);
        auto opacity=compositor.CreateExpressionAnimation(L"motion.Reveal");opacity.SetReferenceParameter(L"motion",motion_);visual_.StartAnimation(L"Opacity",opacity);
        auto offset=compositor.CreateExpressionAnimation(L"Vector3(0, 4 * (1 - motion.Reveal), 0)");offset.SetReferenceParameter(L"motion",motion_);visual_.Properties().StartAnimation(L"Translation",offset);
    }
public:
    BadgeVisibility(badge_xaml::Controls::Canvas hit,badge_xaml::Controls::Border border):hit_(hit),border_(border){}
    bool target()const{return target_;}
    bool running()const{return running_;}
    bool rendered()const{return hit_ && hit_.Visibility()==badge_xaml::Visibility::Visible;}
    void show(bool value,bool animate) {
        if(closed_)return;
        if(!motion_)initialize();
        if(!animate){target_=value;finish();return;}
        if(target_==value)return;
        cancel_completion();target_=value;running_=true;hit_.IsHitTestVisible(value);
        border_.Visibility(badge_xaml::Visibility::Visible);hit_.Visibility(badge_xaml::Visibility::Visible);
        const auto compositor=visual_.Compositor();auto animation=compositor.CreateScalarKeyFrameAnimation();
        animation.InsertExpressionKeyFrame(0,L"this.StartingValue");
        animation.InsertKeyFrame(1,value?1.f:0.f,compositor.CreateCubicBezierEasingFunction({.2f,0},{.2f,1}));
        const auto duration=std::chrono::milliseconds(value?140:120);animation.Duration(duration);
        animation.StopBehavior(badge_composition::AnimationStopBehavior::LeaveCurrentValue);
        batch_=compositor.CreateScopedBatch(badge_composition::CompositionBatchTypes::Animation);
        const auto generation=generation_;const auto weak=weak_from_this();const auto dispatcher=hit_.Dispatcher();
        completed_=batch_.Completed([weak,dispatcher,generation](auto&&,auto&&){try{dispatcher.RunAsync(winrt::Windows::UI::Core::CoreDispatcherPriority::Normal,[weak,generation]{if(auto self=weak.lock();self && !self->closed_ && generation==self->generation_)self->finish();});}catch(...){}});
        motion_.StartAnimation(L"Reveal",animation);batch_.End();
    }
    void close()noexcept {
        closed_=true;
        try{cancel_completion();if(motion_)motion_.StopAnimation(L"Reveal");if(visual_){visual_.StopAnimation(L"Opacity");visual_.Properties().StopAnimation(L"Translation");}}catch(...){}
        motion_=nullptr;visual_=nullptr;hit_=nullptr;border_=nullptr;
    }
};
}
