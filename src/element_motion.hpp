#pragma once
#include "spring.hpp"
#include "composition_access.hpp"
#include <winrt/Windows.UI.Composition.h>
#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Hosting.h>
#include <winrt/Windows.UI.Xaml.Media.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace islands::motion {
namespace xaml=winrt::Windows::UI::Xaml;
namespace composition=winrt::Windows::UI::Composition;
using Vec3=winrt::Windows::Foundation::Numerics::float3;
using Preview=xaml::Hosting::ElementCompositionPreview;

inline winrt::Windows::Foundation::Point layout_position(xaml::UIElement element,xaml::UIElement root) {
    Vec3 total{};xaml::DependencyObject current=element;
    for(unsigned depth=0;current && current!=root && depth<40;++depth){
        if(auto item=current.try_as<xaml::UIElement>())total+=item.ActualOffset();
        current=xaml::Media::VisualTreeHelper::GetParent(current);
    }
    return {total.x,total.y};
}

class ElementMotion {
    struct ChildNative {
        winrt::weak_ref<xaml::FrameworkElement> element;
        composition::Visual visual{nullptr};
        composition::ImplicitAnimationCollection original{nullptr},owned{nullptr};
    };
    struct Entry {
        winrt::weak_ref<xaml::FrameworkElement> element;
        composition::Visual visual{nullptr};
        Vec3 translation{};
        bool translation_existed=false,translation_owned=false;
        composition::ImplicitAnimationCollection native_animations{nullptr},owned_animations{nullptr};
        bool native_owned=false,app=false,exiting=false;
        double width=0,opacity=1;
        std::vector<ChildNative> children;
        winrt::Windows::Foundation::Point position{};
        winrt::Windows::Foundation::Point native_offset{};
        std::uintptr_t context=0;
        Spring x,y,alpha;
    };
    xaml::FrameworkElement root_{nullptr};
    std::unordered_map<std::uintptr_t,Entry> entries_;
    bool initialized_=false;
    std::uint64_t moves_=0,enters_=0,exits_=0,failures_=0,retired_=0;
    HRESULT last_error_=S_OK;
    bool pending_=false;
    static std::uintptr_t context_key(xaml::FrameworkElement element){return reinterpret_cast<std::uintptr_t>(winrt::get_abi(element.DataContext()));}

    bool restore(Entry& entry) noexcept {
        try{
            if(entry.visual && entry.translation_owned){entry.visual.Properties().StopAnimation(L"Translation");entry.visual.Properties().InsertVector3(L"Translation",entry.translation);}
            if(entry.visual && entry.native_owned){
                entry.visual.StopAnimation(L"Opacity");entry.visual.Opacity(static_cast<float>(entry.opacity));
                const auto object=animation_object(entry.visual);if(object.ImplicitAnimations()==entry.owned_animations)object.ImplicitAnimations(entry.native_animations);
            }
            for(auto& child:entry.children)if(child.visual){const auto object=animation_object(child.visual);if(object.ImplicitAnimations()==child.owned)object.ImplicitAnimations(child.original);}
            if(auto element=entry.element.get();element && entry.translation_owned && !entry.translation_existed)Preview::SetIsTranslationEnabled(element,false);
        }catch(...){const auto error=winrt::to_hresult();if(error==RO_E_CLOSED || error==RPC_E_DISCONNECTED){++retired_;entry.translation_owned=false;entry.native_owned=false;entry.children.clear();entry.visual=nullptr;return true;}++failures_;last_error_=error;return false;}
        entry.translation_owned=false;entry.native_owned=false;entry.owned_animations=nullptr;entry.children.clear();return true;
    }
    static void own_children(Entry& entry,xaml::DependencyObject parent,unsigned depth=0) {
        if(depth>8)return;
        const auto count=xaml::Media::VisualTreeHelper::GetChildrenCount(parent);
        for(int index=0;index<count;++index){
            const auto node=xaml::Media::VisualTreeHelper::GetChild(parent,index);
            if(const auto element=node.try_as<xaml::FrameworkElement>()){
                const auto visual=Preview::GetElementVisual(element);if(!visual)continue;const auto object=animation_object(visual);const auto current=object.ImplicitAnimations();
                auto found=std::find_if(entry.children.begin(),entry.children.end(),[&](const auto& child){return child.visual==visual;});
                if(found==entry.children.end() && current && current.HasKey(L"Offset")){
                    ChildNative child;child.element=winrt::make_weak(element);child.visual=visual;child.original=current;child.owned=visual.Compositor().CreateImplicitAnimationCollection();
                    for(const auto& item:current)if(item.Key()!=L"Offset")child.owned.Insert(item.Key(),item.Value());
                    object.ImplicitAnimations(child.owned);entry.children.push_back(std::move(child));found=std::prev(entry.children.end());
                }
                if(found!=entry.children.end()){visual.StopAnimation(L"Offset");visual.Offset(element.ActualOffset());}
            }
            own_children(entry,node,depth+1);
        }
    }
    static void own_native(Entry& entry,xaml::FrameworkElement element) {
        if(!entry.app)return;
        const auto object=animation_object(entry.visual);const auto current=object.ImplicitAnimations();
        if(!entry.native_owned || current!=entry.owned_animations){
            entry.native_animations=current;entry.owned_animations=entry.visual.Compositor().CreateImplicitAnimationCollection();
            if(current)for(const auto& item:current)if(item.Key()!=L"Offset" && item.Key()!=L"Opacity")entry.owned_animations.Insert(item.Key(),item.Value());
            object.ImplicitAnimations(entry.owned_animations);entry.native_owned=true;
        }
        // Native Offset springs otherwise run underneath our FLIP translation.
        entry.visual.StopAnimation(L"Offset");entry.visual.Offset(element.ActualOffset());
        own_children(entry,element);
    }
    static void own_translation(Entry& entry,xaml::UIElement element) {
        if(entry.translation_owned)return;
        entry.translation_existed=entry.visual.Properties().TryGetVector3(L"Translation",entry.translation)==composition::CompositionGetValueStatus::Succeeded;
        Preview::SetIsTranslationEnabled(element,true);entry.translation_owned=true;
    }
    void animate_entry(Entry& entry,Clock::time_point now,bool fade) {
        if(auto element=entry.element.get())own_translation(entry,element);else return;
        const auto compositor=entry.visual.Compositor();auto animation=compositor.CreateVector3KeyFrameAnimation();
        auto opacity=compositor.CreateScalarKeyFrameAnimation();
        const auto easing=compositor.CreateLinearEasingFunction();const double duration=entry.x.settling_seconds();
        sample_frames(duration,now,[&](float progress,Clock::time_point at,bool final){
            const auto sx=final?Sample{entry.x.target(),0}:entry.x.at(at),sy=final?Sample{entry.y.target(),0}:entry.y.at(at);
            animation.InsertKeyFrame(progress,entry.translation+Vec3{static_cast<float>(sx.position),static_cast<float>(sy.position),0},easing);
            if(fade)opacity.InsertKeyFrame(progress,static_cast<float>(entry.opacity*std::clamp(final?entry.alpha.target():entry.alpha.at(at).position,0.,1.)),easing);
        });
        animation.Duration(std::chrono::duration_cast<winrt::Windows::Foundation::TimeSpan>(std::chrono::duration<double>(duration)));animation.StopBehavior(composition::AnimationStopBehavior::SetToFinalValue);
        entry.visual.Properties().StartAnimation(L"Translation",animation);++moves_;
        if(fade){opacity.Duration(animation.Duration());opacity.StopBehavior(composition::AnimationStopBehavior::SetToFinalValue);entry.visual.StartAnimation(L"Opacity",opacity);}
    }
    void reposition(Entry& entry,winrt::Windows::Foundation::Point point,double period,Clock::time_point now) {
        const double dx=entry.position.X-point.X,dy=entry.position.Y-point.Y;
        if(std::abs(dx)<.05 && std::abs(dy)<.05)return;
        const auto x=entry.x.at(now),y=entry.y.at(now);
        entry.x.reset(x.position+dx,0,x.velocity,period,now);entry.y.reset(y.position+dy,0,y.velocity,period,now);
        animate_entry(entry,now,false);
    }
public:
    explicit ElementMotion(xaml::FrameworkElement root):root_(root){}
    bool pending()const{return pending_;}
    void update(const std::vector<xaml::FrameworkElement>& elements,std::uint32_t period_ms,bool animate,Clock::time_point now,double first,double last) noexcept {
        const double period=std::max(80u,period_ms)/1000.;
        double old_first=first,old_last=last;bool old_bounds=false;pending_=false;
        for(const auto& [key,entry]:entries_)if(entry.app && !entry.exiting){
            const double left=entry.position.X+entry.x.at(now).position;
            if(!old_bounds){old_first=left;old_last=left+entry.width;old_bounds=true;}
            else{old_first=std::min(old_first,left);old_last=std::max(old_last,left+entry.width);}
        }
        std::unordered_set<std::uintptr_t> present;present.reserve(elements.size());
        for(auto element:elements)try{
            const auto point=layout_position(element,root_);
            if(point.X+element.ActualWidth()<=0 || point.X>=root_.ActualWidth() || point.Y+element.ActualHeight()<=0 || point.Y>=root_.ActualHeight())continue;
            const auto key=reinterpret_cast<std::uintptr_t>(winrt::get_abi(element));present.insert(key);
            auto found=entries_.find(key);
            const auto context=context_key(element);
            if(found!=entries_.end() && (found->second.element.get()!=element || found->second.context!=context)){restore(found->second);entries_.erase(found);found=entries_.end();}
            if(found==entries_.end()){
                Entry entry;entry.element=winrt::make_weak(element);entry.visual=Preview::GetElementVisual(element);entry.position=point;
                if(!entry.visual){++retired_;continue;}
                entry.app=std::wstring_view(winrt::get_class_name(element)).starts_with(L"Taskbar.");entry.width=element.ActualWidth();entry.opacity=element.Opacity();
                entry.context=context;const auto actual=element.TransformToVisual(root_).TransformPoint({0,0});entry.native_offset={actual.X-point.X,actual.Y-point.Y};
                if(animate){
                    own_native(entry,element);
                    if(initialized_ && entry.app){
                        const double start=std::clamp(static_cast<double>(point.X),old_first,std::max(old_first,old_last-entry.width));
                        entry.x.reset(start-point.X,0,0,period,now);entry.y.reset(0,0,0,period,now);entry.alpha.reset(0,1,0,period,now);
                        animate_entry(entry,now,true);
                    }
                }
                entries_.emplace(key,std::move(entry));if(initialized_)++enters_;
            }else{
                if(animate){
                    auto& entry=found->second;own_native(entry,element);
                    if(entry.exiting){
                        const auto x=entry.x.at(now),y=entry.y.at(now);
                        entry.x.reset(entry.position.X+x.position-point.X,0,x.velocity,period,now);
                        entry.y.reset(entry.position.Y+y.position-point.Y,0,y.velocity,period,now);
                        entry.alpha.retarget(1,period,now);entry.exiting=false;animate_entry(entry,now,true);
                    }else reposition(entry,point,period,now);
                }
                else{restore(found->second);found->second.exiting=false;found->second.x.reset(0,0,0,period,now);found->second.y.reset(0,0,0,period,now);}
                found->second.position=point;found->second.width=element.ActualWidth();
            }
        }catch(...){const auto error=winrt::to_hresult();if(error==RO_E_CLOSED || error==RPC_E_DISCONNECTED){++retired_;entries_.erase(reinterpret_cast<std::uintptr_t>(winrt::get_abi(element)));}else{++failures_;last_error_=error;}}
        for(auto item=entries_.begin();item!=entries_.end();){
            if(present.contains(item->first)){++item;continue;}
            auto& entry=item->second;
            if(animate && entry.app)try{
                if(auto element=entry.element.get();element && xaml::Media::VisualTreeHelper::GetParent(element)){
                    if(!entry.exiting){
                        const auto point=layout_position(element,root_);const auto x=entry.x.at(now),y=entry.y.at(now);
                        const double visible_x=entry.position.X+x.position,visible_y=entry.position.Y+y.position;
                        const double destination=std::clamp(visible_x,first,std::max(first,last-entry.width));
                        own_native(entry,element);
                        entry.x.reset(visible_x-point.X,destination-point.X,x.velocity,period,now);
                        entry.y.reset(visible_y-point.Y,visible_y-point.Y,y.velocity,period,now);
                        entry.alpha.reset(1,0,0,period,now);entry.position=point;entry.exiting=true;animate_entry(entry,now,true);
                    }
                    if(!entry.alpha.settled(now)){pending_=true;++item;continue;}
                }
            }catch(...){const auto error=winrt::to_hresult();if(error==RO_E_CLOSED || error==RPC_E_DISCONNECTED)++retired_;else{++failures_;last_error_=error;}}
            if(restore(item->second)){item=entries_.erase(item);++exits_;}else ++item;
        }
        if(animate)for(const auto& [key,entry]:entries_)if(entry.app && ((!entry.x.settled(now)) || (!entry.alpha.settled(now))))pending_=true;
        initialized_=true;
    }
    std::wstring describe()const{
        std::size_t residuals=0,unmeasured=0;double maximum=0;const auto now=Clock::now();
        for(const auto& [key,entry]:entries_)if(entry.translation_owned && entry.x.settled(now) && entry.y.settled(now))try{
            if(auto element=entry.element.get()){
                const auto point=layout_position(element,root_),actual=element.TransformToVisual(root_).TransformPoint({0,0});
                const double residual=std::hypot(actual.X-point.X-entry.native_offset.X,actual.Y-point.Y-entry.native_offset.Y);
                maximum=std::max(maximum,residual);if(residual>.5)++residuals;
            }
        }catch(...){++unmeasured;}
        return L"motion moves="+std::to_wstring(moves_)+L" enters="+std::to_wstring(enters_)+L" exits="+std::to_wstring(exits_)+L" tracked="+std::to_wstring(entries_.size())+L" ghosts="+std::to_wstring(residuals)+L" failures="+std::to_wstring(failures_)+L" residual_max="+std::to_wstring(maximum)+L" motion_unmeasured="+std::to_wstring(unmeasured)+L" retired_native="+std::to_wstring(retired_)+L" last_error="+std::to_wstring(static_cast<std::uint32_t>(last_error_));
    }
    void close()noexcept{for(auto& [key,entry]:entries_)restore(entry);entries_.clear();root_=nullptr;}
};
}
