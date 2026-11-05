#pragma once
#include <winrt/Windows.UI.Xaml.h>
#include <winrt/Windows.UI.Xaml.Markup.h>
#include <vector>
#include <string>

namespace islands::motion {
namespace repeater_xaml=winrt::Windows::UI::Xaml;
class RepeaterAnimation {
    struct Saved {repeater_xaml::DependencyProperty property{nullptr};winrt::Windows::Foundation::IInspectable local{nullptr};std::wstring name;};
    repeater_xaml::FrameworkElement repeater_{nullptr};
    std::vector<Saved> saved_;
    std::wstring detected_;
public:
    explicit RepeaterAnimation(repeater_xaml::FrameworkElement repeater):repeater_(repeater){}
    void disable() {
        if(!saved_.empty())return;
        for(const auto name:{L"Animator",L"ItemTransitionProvider"})try{
            const std::wstring markup=L"<Style xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation' xmlns:x='http://schemas.microsoft.com/winfx/2006/xaml' xmlns:mux='using:Microsoft.UI.Xaml.Controls' TargetType='mux:ItemsRepeater'><Setter Property='"+std::wstring(name)+L"' Value='{x:Null}'/></Style>";
            const auto style=repeater_xaml::Markup::XamlReader::Load(markup).as<repeater_xaml::Style>();
            const auto property=style.Setters().GetAt(0).as<repeater_xaml::Setter>().Property();
            if(!property)continue;
            const auto value=repeater_.GetValue(property);if(!value)continue;
            detected_+=std::wstring(name)+L":"+std::wstring(winrt::get_class_name(value))+L";";
            saved_.push_back({property,repeater_.ReadLocalValue(property),name});repeater_.SetValue(property,nullptr);
        }catch(...){}
    }
    void restore()noexcept {
        for(const auto& saved:saved_)try{
            if(repeater_.GetValue(saved.property))continue;
            if(saved.local==repeater_xaml::DependencyProperty::UnsetValue())repeater_.ClearValue(saved.property);else repeater_.SetValue(saved.property,saved.local);
        }catch(...){}
        saved_.clear();
    }
    std::wstring describe()const{return L" native_animators="+std::to_wstring(saved_.size())+L" animator_type="+(detected_.empty()?L"none":detected_);}
};
}
