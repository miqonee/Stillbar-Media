#pragma once
#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Media.h>

namespace islands {
namespace tree_xaml=winrt::Windows::UI::Xaml;
inline tree_xaml::FrameworkElement find_taskbar_element(tree_xaml::DependencyObject node,const wchar_t* name,const wchar_t* type=nullptr,unsigned depth=0) {
    if(!node || depth>24)return nullptr;
    if(auto element=node.try_as<tree_xaml::FrameworkElement>())if((!name || element.Name()==name) && (!type || winrt::get_class_name(element)==type))return element;
    const auto count=tree_xaml::Media::VisualTreeHelper::GetChildrenCount(node);
    for(int index=0;index<count;++index)if(auto found=find_taskbar_element(tree_xaml::Media::VisualTreeHelper::GetChild(node,index),name,type,depth+1))return found;
    return nullptr;
}
struct TaskbarTree {
    tree_xaml::FrameworkElement root{nullptr},repeater{nullptr},tray{nullptr},background{nullptr},edge{nullptr};
    tree_xaml::Controls::Panel panel{nullptr};
    tree_xaml::Controls::Grid grid{nullptr};
    const wchar_t* missing()const {
        if(!root || !panel)return L"XamlRoot / Panel";
        if(!grid)return L"Grid#RootGrid";
        if(!repeater)return L"TaskbarFrameRepeater";
        if(!background)return L"TaskbarBackground";
        if(!tray)return L"SystemTrayFrame";
        if(root.ActualWidth()<200 || root.ActualHeight()<24)return L"размеры корневого элемента";
        if(root.ActualHeight()>200)return L"поддерживаемая высота панели";
        return nullptr;
    }
    bool ready()const{return missing()==nullptr;}
};
inline TaskbarTree resolve_taskbar_tree(tree_xaml::FrameworkElement frame) {
    TaskbarTree value;
    if(!frame || !frame.XamlRoot() || !frame.XamlRoot().Content())return value;
    value.root=frame.XamlRoot().Content().try_as<tree_xaml::FrameworkElement>();
    if(!value.root)return value;
    value.panel=value.root.try_as<tree_xaml::Controls::Panel>();
    if(const auto element=find_taskbar_element(frame,L"RootGrid"))value.grid=element.try_as<tree_xaml::Controls::Grid>();
    if(!value.grid)return value;
    value.repeater=find_taskbar_element(frame,L"TaskbarFrameRepeater");
    value.background=find_taskbar_element(frame,L"BackgroundControl",L"Taskbar.TaskbarBackground");
    value.edge=find_taskbar_element(frame,L"ScreenEdgeStroke");
    value.tray=find_taskbar_element(value.root,nullptr,L"SystemTray.SystemTrayFrame");
    return value;
}
}
