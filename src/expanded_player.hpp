#pragma once
#include "media.hpp"
#include "protocol.hpp"
#include "track_header.hpp"
#include "taskbar_material.hpp"
#include "backdrop_blur.hpp"
#include "audio_visualizer.hpp"
#include "seek_thumb.hpp"
#include "popup_window.hpp"
#include <winrt/Windows.UI.h>
#include <winrt/Windows.UI.Composition.h>
#include <winrt/Windows.UI.Xaml.Automation.h>
#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Controls.Primitives.h>
#include <winrt/Windows.UI.Xaml.Hosting.h>
#include <winrt/Windows.UI.Xaml.Input.h>
#include <winrt/Windows.UI.Xaml.Interop.h>
#include <winrt/Windows.UI.Xaml.Media.h>
#include <functional>
#include <vector>

namespace islands {
namespace player_xaml=winrt::Windows::UI::Xaml;
namespace player_ui=player_xaml::Controls;
namespace player_primitives=player_ui::Primitives;
namespace player_composition=winrt::Windows::UI::Composition;

class ExpandedPlayer : public std::enable_shared_from_this<ExpandedPlayer> {
    player_primitives::Popup popup_{nullptr};
    player_ui::Border surface_{nullptr};
    player_ui::Slider slider_{nullptr};
    SeekThumb seek_thumb_;
    player_ui::Border blur_host_{nullptr},tint_{nullptr};
    BackdropBlur blur_;
    HostBackdropWindow host_backdrop_;
    PopupWindow native_popup_;
    TaskbarMaterial material_;
    player_ui::TextBlock elapsed_{nullptr},duration_{nullptr},hint_{nullptr},play_glyph_{nullptr};
    std::shared_ptr<TrackHeader> header_;
    std::shared_ptr<AudioVisualizer> visualizer_;
    player_ui::ComboBox sources_{nullptr};
    std::vector<std::uint64_t> source_ids_;
    winrt::event_token source_changed_{};
    bool updating_sources_=false;
    player_ui::Button previous_{nullptr},play_{nullptr},next_{nullptr};
    player_xaml::Media::ImageSource artwork_{nullptr};
    player_xaml::DispatcherTimer clock_{nullptr},commit_timer_{nullptr};
    player_composition::CompositionScopedBatch close_batch_{nullptr};
    winrt::event_token close_completed_{};
    std::uint64_t motion_generation_=0;
    player_composition::Visual visual_{nullptr};
    player_composition::RectangleClip reveal_{nullptr};
    player_composition::CompositionPropertySet motion_{nullptr};
    bool motion_bound_=false;
    winrt::event_token opened_{},closed_{},value_changed_{},clock_tick_{},commit_tick_{},size_changed_{};
    winrt::Windows::Foundation::IInspectable pressed_{nullptr},released_{nullptr},cancelled_{nullptr};
    std::vector<std::pair<player_ui::Button,winrt::event_token>> buttons_;
    MediaCommands commands_;
    player_xaml::FrameworkElement root_{nullptr},anchor_{nullptr};
    HWINEVENTHOOK foreground_hook_=nullptr;
    HMODULE hook_module_=nullptr;
    winrt::Windows::Foundation::IInspectable outside_pressed_{nullptr};
    winrt::event_token key_down_{};
    inline static std::weak_ptr<ExpandedPlayer> observed_popup_;
    MediaSnapshot snapshot_;
    std::optional<winrt::Windows::UI::Color> cover_accent_;
    winrt::Windows::UI::Color system_accent_{255,0,120,215};
    Settings settings_;
    bool dark_=true,open_=false,showing_=false,closing_animation_=false,disposed_=false,preview_=false;
    bool updating_=false,dragging_=false,interaction_cancelled_=false,clock_running_=false,previewing_=false;
    std::uint64_t captured_revision_=0,opens_=0,seeks_=0,ticks_=0;
    double preview_ratio_=0;
    HRESULT last_error_=S_OK;

    static void CALLBACK foreground_changed(HWINEVENTHOOK,DWORD,HWND window,LONG,LONG,DWORD,DWORD) noexcept {
        if(auto self=observed_popup_.lock();self && self->open_ && !self->disposed_ && window)try{
            DWORD process=0;GetWindowThreadProcessId(window,&process);
            if(process==self->commands_.settings_process)return;
            wchar_t name[64]{};GetClassNameW(window,name,ARRAYSIZE(name));
            const bool desktop=!wcscmp(name,L"Progman") || !wcscmp(name,L"WorkerW") || !wcscmp(name,L"CabinetWClass");
            if(desktop || process!=GetCurrentProcessId())self->dismiss();
        }catch(...){}
    }
    void attach_dismiss() {
        if(outside_pressed_)return;
        const auto weak=weak_from_this();observed_popup_=weak;
        outside_pressed_=winrt::box_value(player_xaml::Input::PointerEventHandler{[weak](auto&&,const player_xaml::Input::PointerRoutedEventArgs& args){if(auto self=weak.lock();self && self->open_ && !self->sources_.IsDropDownOpen())try{
            auto node=args.OriginalSource().try_as<player_xaml::DependencyObject>();
            const auto anchor_area=player_xaml::Media::VisualTreeHelper::GetParent(self->anchor_);
            for(unsigned depth=0;node && depth<40;++depth){if(node==self->anchor_ || node==anchor_area || node==self->surface_)return;node=player_xaml::Media::VisualTreeHelper::GetParent(node);}self->dismiss();
        }catch(...){}}});
        root_.AddHandler(player_xaml::UIElement::PointerPressedEvent(),outside_pressed_,true);
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,reinterpret_cast<LPCWSTR>(&foreground_changed),&hook_module_))winrt::throw_last_error();
        foreground_hook_=SetWinEventHook(EVENT_SYSTEM_FOREGROUND,EVENT_SYSTEM_FOREGROUND,nullptr,foreground_changed,0,0,WINEVENT_OUTOFCONTEXT);
        if(!foreground_hook_){const auto error=GetLastError();FreeLibrary(hook_module_);hook_module_=nullptr;winrt::throw_hresult(HRESULT_FROM_WIN32(error?error:ERROR_ACCESS_DENIED));}
    }
    void detach_dismiss() noexcept {
        try{if(root_ && outside_pressed_)root_.RemoveHandler(player_xaml::UIElement::PointerPressedEvent(),outside_pressed_);}catch(...){}outside_pressed_=nullptr;
        if(foreground_hook_){if(!UnhookWinEvent(foreground_hook_)){last_error_=HRESULT_FROM_WIN32(GetLastError());return;}foreground_hook_=nullptr;}
        observed_popup_.reset();if(hook_module_){FreeLibrary(hook_module_);hook_module_=nullptr;}
    }
    void position_popup() {
        if(!popup_ || !anchor_ || !root_)return;
        const auto point=anchor_.TransformToVisual(root_).TransformPoint({0,0});
        const double height=surface_.ActualHeight()>0?surface_.ActualHeight():surface_.DesiredSize().Height;
        popup_.HorizontalOffset(std::clamp(static_cast<double>(point.X),0.,std::max(0.,root_.ActualWidth()-surface_.Width())));
        popup_.VerticalOffset(std::min(0.,static_cast<double>(point.Y))-height-settings_.popup_gap);
        if(open_){
            const auto taskbar=FindWindowW(L"Shell_TrayWnd",nullptr);RECT bounds{};
            if(taskbar && GetWindowRect(taskbar,&bounds)){
                const auto scale=root_.XamlRoot().RasterizationScale();
                const LONG left=bounds.left+static_cast<LONG>(popup_.HorizontalOffset()*scale),top=bounds.top+static_cast<LONG>(popup_.VerticalOffset()*scale);
                native_popup_.place({left,top,left+static_cast<LONG>(surface_.ActualWidth()*scale),top+static_cast<LONG>(height*scale)});
            }
        }
    }
    void finish_close() noexcept {
        open_=false;showing_=false;closing_animation_=false;dragging_=false;previewing_=false;interaction_cancelled_=false;
        blur_.close();
        host_backdrop_.close();
        native_popup_.close();
        if(visualizer_)visualizer_->open(false);
        try{if(commit_timer_)commit_timer_.Stop();if(sources_)sources_.IsDropDownOpen(false);update_clock();stop_motion();if(header_)header_->finish();if(!disposed_)commands_.browse_sources(false);}catch(...){}
        detach_dismiss();
    }

    static void text(player_ui::TextBlock block,const std::wstring& value){if(block.Text()!=value)block.Text(value);}
    static player_ui::TextBlock glyph(const wchar_t* value,double size) {
        player_ui::TextBlock result;result.Text(value);result.FontFamily(player_xaml::Media::FontFamily{L"Segoe Fluent Icons"});result.FontSize(size);return result;
    }
    player_ui::Button button(const wchar_t* value,const wchar_t* label,MediaAction action,double size) {
        player_ui::Button result;result.Content(glyph(value,action==MediaAction::toggle?16:12));result.MinWidth(0);result.MinHeight(0);
        result.Width(size);result.Height(size);result.Padding({0});result.CornerRadius({size/2,size/2,size/2,size/2});result.BorderThickness({0});
        player_xaml::Automation::AutomationProperties::SetName(result,label);
        const auto weak=weak_from_this();const auto token=result.Click([weak,action](auto&&,auto&&){if(auto self=weak.lock();self && !self->disposed_)self->commands_.action(action);});
        buttons_.emplace_back(result,token);return result;
    }
    bool animations()const {BOOL enabled=TRUE;SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&enabled,0);return settings_.animation_ms && enabled;}
    void cancel_close() noexcept {
        ++motion_generation_;
        try{if(close_batch_ && close_completed_.value)close_batch_.Completed(close_completed_);}catch(...){}
        close_batch_=nullptr;close_completed_={};
    }
    void stop_motion() noexcept {
        cancel_close();
        try{if(motion_)motion_.StopAnimation(L"Reveal");if(visual_){visual_.StopAnimation(L"Opacity");visual_.Properties().StopAnimation(L"Translation");}if(reveal_)reveal_.StopAnimation(L"Top");}catch(...){}
        motion_bound_=false;
    }
    void bind_motion() {
        const auto compositor=visual_.Compositor();
        if(!motion_){motion_=compositor.CreatePropertySet();motion_.InsertScalar(L"Reveal",0);}
        const float height=static_cast<float>(surface_.ActualHeight()),radius=static_cast<float>(surface_.CornerRadius().TopLeft);
        motion_.InsertScalar(L"Extent",std::max(0.f,height-36));
        if(!reveal_)reveal_=compositor.CreateRectangleClip();
        reveal_.Left(0);reveal_.Right(static_cast<float>(surface_.ActualWidth()));reveal_.Bottom(height);reveal_.TopLeftRadius({radius,radius});reveal_.TopRightRadius({radius,radius});reveal_.BottomLeftRadius({radius,radius});reveal_.BottomRightRadius({radius,radius});visual_.Clip(reveal_);
        if(motion_bound_)return;
        auto clip=compositor.CreateExpressionAnimation(L"motion.Extent * (1 - motion.Reveal)");clip.SetReferenceParameter(L"motion",motion_);reveal_.StartAnimation(L"Top",clip);
        auto translation=compositor.CreateExpressionAnimation(L"Vector3(0, 3 * (1 - motion.Reveal), 32)");translation.SetReferenceParameter(L"motion",motion_);visual_.Properties().StartAnimation(L"Translation",translation);
        auto opacity=compositor.CreateExpressionAnimation(L"Min(1, Max(0, motion.Reveal * 6))");opacity.SetReferenceParameter(L"motion",motion_);visual_.StartAnimation(L"Opacity",opacity);motion_bound_=true;
    }
    void animate(bool opening) {
        cancel_close();
        if(!animations()){stop_motion();if(!opening){popup_.IsOpen(false);finish_close();}else{visual_.Clip(nullptr);visual_.Opacity(1);visual_.Properties().InsertVector3(L"Translation",{});}return;}
        bind_motion();
        const auto compositor=visual_.Compositor();auto animation=compositor.CreateScalarKeyFrameAnimation();
        animation.InsertExpressionKeyFrame(0,L"this.StartingValue");
        animation.InsertKeyFrame(1,opening?1.f:0.f,compositor.CreateCubicBezierEasingFunction(opening?winrt::Windows::Foundation::Numerics::float2{.12f,.8f}:winrt::Windows::Foundation::Numerics::float2{.18f,.7f},{.25f,1}));
        const auto duration=opening?std::clamp(settings_.animation_ms*65/100,70u,130u):std::clamp(settings_.animation_ms/2,55u,90u);
        animation.Duration(std::chrono::milliseconds(duration));animation.StopBehavior(player_composition::AnimationStopBehavior::LeaveCurrentValue);
        if(!opening){
            close_batch_=compositor.CreateScopedBatch(player_composition::CompositionBatchTypes::Animation);
            const auto weak=weak_from_this();const auto dispatcher=root_.Dispatcher();const auto generation=motion_generation_;
            close_completed_=close_batch_.Completed([weak,dispatcher,generation](auto&&,auto&&){try{dispatcher.RunAsync(winrt::Windows::UI::Core::CoreDispatcherPriority::Normal,[weak,generation]{if(auto self=weak.lock();self && !self->disposed_ && self->closing_animation_ && generation==self->motion_generation_){self->popup_.IsOpen(false);self->finish_close();}});}catch(...){}});
        }
        motion_.StartAnimation(L"Reveal",animation);if(close_batch_)close_batch_.End();
    }
    void update_clock() {
        if(!clock_)return;
        const bool run=open_ && !closing_animation_ && snapshot_.playing && snapshot_.timeline.known_duration() && !dragging_;
        if(run==clock_running_)return;
        clock_running_=run;if(run)clock_.Start();else clock_.Stop();
    }
    void render_progress() {
        if(!slider_)return;
        const auto& timeline=snapshot_.timeline;
        const auto current=timeline.current();
        const auto ratio=dragging_ || snapshot_.seek_pending || previewing_?preview_ratio_:timeline.ratio(current);
        const auto display=timeline.known_duration()?static_cast<std::int64_t>(timeline.start+(static_cast<long double>(timeline.end)-timeline.start)*ratio):current;
        updating_=true;
        const double low=timeline.seekable()?timeline.ratio(std::max(timeline.start,timeline.minimum))*1000:0;
        const double high=timeline.seekable()?timeline.ratio(std::min(timeline.end,timeline.maximum))*1000:1000;
        if(slider_.Minimum()!=low || slider_.Maximum()!=high){slider_.Minimum(0);slider_.Maximum(std::max(1.,high));slider_.Minimum(low);}
        if(!dragging_)slider_.Value(std::clamp(ratio*1000,low,std::max(low,high)));
        slider_.IsEnabled(timeline.seekable() && !snapshot_.seek_pending);
        updating_=false;
        text(elapsed_,timeline.known_duration()?timeline.elapsed(display):L"—:—");
        text(duration_,timeline.known_duration()?timeline.elapsed(timeline.end):L"—:—");
        text(hint_,snapshot_.seek_failed?L"Приложение отклонило перемотку":!timeline.seekable()?L"Перемотка недоступна у источника":L"");
        hint_.Visibility(hint_.Text().empty()?player_xaml::Visibility::Collapsed:player_xaml::Visibility::Visible);
    }
    void render_sources() {
        const bool show=snapshot_.sources.size()>1 || snapshot_.selected_source!=0;
        sources_.Visibility(show?player_xaml::Visibility::Visible:player_xaml::Visibility::Collapsed);
        if(!show)return;
        std::vector<std::uint64_t> ids{0};for(const auto& source:snapshot_.sources)ids.push_back(source.id);
        updating_sources_=true;
        if(ids!=source_ids_){
            sources_.Items().Clear();source_ids_=ids;
            for(std::size_t index=0;index<ids.size();++index){player_ui::ComboBoxItem item;item.FontSize(11);item.MinHeight(0);item.Padding({8,5,8,5});sources_.Items().Append(item);}
        }
        sources_.Items().GetAt(0).as<player_ui::ComboBoxItem>().Content(winrt::box_value(L"Автоматически"));
        int selected=0;
        for(std::size_t index=0;index<snapshot_.sources.size();++index){
            const auto& source=snapshot_.sources[index];const auto label=source.title.empty()?source.name:source.name+L" · "+source.title;
            sources_.Items().GetAt(static_cast<std::uint32_t>(index+1)).as<player_ui::ComboBoxItem>().Content(winrt::box_value(label));
            if(source.id==snapshot_.selected_source)selected=static_cast<int>(index+1);
        }
        if(sources_.SelectedIndex()!=selected)sources_.SelectedIndex(selected);
        updating_sources_=false;
    }
    void render() {
        header_->show(snapshot_.title,snapshot_.artist,artwork_,settings_.animation_ms);
        render_sources();
        const bool ready=snapshot_.available && snapshot_.playback_valid;
        previous_.IsEnabled(ready && snapshot_.previous);next_.IsEnabled(ready && snapshot_.next);play_.IsEnabled(ready && snapshot_.toggle);
        previous_.Visibility(snapshot_.previous?player_xaml::Visibility::Visible:player_xaml::Visibility::Collapsed);next_.Visibility(snapshot_.next?player_xaml::Visibility::Visible:player_xaml::Visibility::Collapsed);
        play_.Margin({snapshot_.previous?8.:0.,0,snapshot_.next?8.:0.,0});
        text(play_glyph_,snapshot_.playing?L"\xE769":L"\xE768");
        render_progress();update_clock();
    }
    void restyle() {
        if(!surface_)return;
        surface_.RequestedTheme(dark_?player_xaml::ElementTheme::Dark:player_xaml::ElementTheme::Light);
        if(slider_)seek_thumb_.configure(slider_,dark_);
        const double radius=settings_.popup_radius;
        system_accent_=winrt::Windows::UI::ViewManagement::UISettings{}.GetColorValue(winrt::Windows::UI::ViewManagement::UIColorType::Accent);
        if(visualizer_){visualizer_->configure(settings_.popup_visualizer && animations(),wave_color(),radius,dark_);visualizer_->open(open_ && !closing_animation_);}
        surface_.CornerRadius({radius,radius,radius,radius});
        if(reveal_){const float rounded=static_cast<float>(radius);reveal_.TopLeftRadius({rounded,rounded});reveal_.TopRightRadius({rounded,rounded});reveal_.BottomLeftRadius({rounded,rounded});reveal_.BottomRightRadius({rounded,rounded});}
        const double opacity=settings_.popup_opacity;
        surface_.Background(player_xaml::Media::SolidColorBrush{winrt::Windows::UI::Colors::Transparent()});
        auto tint_color=material_.color;tint_color.A=255;auto tint_brush=player_xaml::Media::SolidColorBrush{tint_color};tint_brush.Opacity(opacity);tint_.Background(tint_brush);tint_.CornerRadius({radius,radius,radius,radius});
        update_backdrop();
        surface_.BorderThickness(!settings_.popup_outline?player_xaml::Thickness{0,0,0,0}:player_xaml::Thickness{1,1,1,1});
        const auto outline=player_xaml::Media::SolidColorBrush{dark_?winrt::Windows::UI::Color{100,105,108,116}:winrt::Windows::UI::Color{95,135,140,150}};outline.Opacity(opacity);surface_.BorderBrush(outline);
    }
    void update_backdrop() {
        if(!open_ || !surface_ || !blur_host_)return;
        if(settings_.popup_blur==0){blur_.close();host_backdrop_.close();return;}
        if(const auto window=native_popup_.get())host_backdrop_.enable(window);
        blur_.update(blur_host_,static_cast<float>(settings_.popup_blur),surface_.CornerRadius().TopLeft,1.f,host_backdrop_.enabled());
    }
    void commit_seek() {
        commit_timer_.Stop();
        if(disposed_ || !open_ || interaction_cancelled_ || captured_revision_!=snapshot_.media_revision)return;
        if(const auto target=snapshot_.timeline.seek(preview_ratio_)){++seeks_;commands_.seek(*target,captured_revision_);}previewing_=false;
    }
    void create() {
        using namespace player_xaml;
        surface_=player_ui::Border{};surface_.Name(L"Islands.ExpandedPlayer");surface_.Width(300);
        Automation::AutomationProperties::SetName(surface_,L"Stillbar — медиаплеер");
        player_ui::Grid layers;surface_.Child(layers);
        blur_host_=player_ui::Border{};blur_host_.IsHitTestVisible(false);layers.Children().Append(blur_host_);
        tint_=player_ui::Border{};tint_.IsHitTestVisible(false);layers.Children().Append(tint_);
        visualizer_=std::make_shared<AudioVisualizer>(commands_.visualizer_request,commands_.audio_level);layers.Children().Append(visualizer_->element());
        player_ui::StackPanel content;content.Margin({12,10,12,10});layers.Children().Append(content);
        sources_=player_ui::ComboBox{};sources_.FontSize(11);sources_.Height(26);sources_.MinHeight(0);sources_.MinWidth(0);sources_.MaxWidth(274);sources_.Padding({6,2,6,2});sources_.MaxDropDownHeight(220);sources_.Margin({0,0,0,6});sources_.Visibility(Visibility::Collapsed);Automation::AutomationProperties::SetName(sources_,L"Источник медиа");content.Children().Append(sources_);
        header_=std::make_shared<TrackHeader>(40,13,11,10);content.Children().Append(header_->element());
        slider_=player_ui::Slider{};slider_.Name(L"Islands.Seek");slider_.Minimum(0);slider_.Maximum(1000);slider_.StepFrequency(1);slider_.Height(40);slider_.Margin({0,2,0,0});slider_.IsThumbToolTipEnabled(false);
        slider_.MinHeight(40);
        Automation::AutomationProperties::SetName(slider_,L"Позиция воспроизведения");content.Children().Append(slider_);
        player_ui::Grid times;elapsed_=player_ui::TextBlock{};duration_=player_ui::TextBlock{};elapsed_.FontSize(10);duration_.FontSize(10);elapsed_.Opacity(.65);duration_.Opacity(.65);duration_.HorizontalAlignment(HorizontalAlignment::Right);times.Children().Append(elapsed_);times.Children().Append(duration_);content.Children().Append(times);
        player_ui::StackPanel controls;controls.Orientation(player_ui::Orientation::Horizontal);controls.HorizontalAlignment(HorizontalAlignment::Center);controls.Margin({0,5,0,0});
        previous_=button(L"\xE892",L"Предыдущий трек",MediaAction::previous,26);play_=button(L"\xE769",L"Пауза / воспроизведение",MediaAction::toggle,30);next_=button(L"\xE893",L"Следующий трек",MediaAction::next,26);
        play_.Margin({8,0,8,0});play_glyph_=play_.Content().as<player_ui::TextBlock>();previous_.VerticalAlignment(VerticalAlignment::Center);next_.VerticalAlignment(VerticalAlignment::Center);
        controls.Children().Append(previous_);controls.Children().Append(play_);controls.Children().Append(next_);content.Children().Append(controls);
        hint_=player_ui::TextBlock{};hint_.FontSize(10);hint_.Opacity(.6);hint_.HorizontalAlignment(HorizontalAlignment::Center);hint_.Margin({0,3,0,0});hint_.Visibility(Visibility::Collapsed);content.Children().Append(hint_);
        popup_=player_primitives::Popup{};popup_.XamlRoot(root_.XamlRoot());popup_.Child(surface_);popup_.ShouldConstrainToRootBounds(false);popup_.IsLightDismissEnabled(false);surface_.Shadow(Media::ThemeShadow{});
        visual_=Hosting::ElementCompositionPreview::GetElementVisual(surface_);Hosting::ElementCompositionPreview::SetIsTranslationEnabled(surface_,true);
        clock_=DispatcherTimer{};clock_.Interval(std::chrono::milliseconds(250));commit_timer_=DispatcherTimer{};commit_timer_.Interval(std::chrono::milliseconds(180));
        const auto weak=weak_from_this();
        source_changed_=sources_.SelectionChanged([weak](auto&&,auto&&){if(auto self=weak.lock();self && !self->disposed_ && !self->updating_sources_){
            if(!self->sources_.IsDropDownOpen() && self->sources_.FocusState()==FocusState::Unfocused)return;
            const int selected=self->sources_.SelectedIndex();
            if(selected>=0 && static_cast<std::size_t>(selected)<self->source_ids_.size() && self->source_ids_[selected]!=self->snapshot_.selected_source)self->commands_.select_source(self->source_ids_[selected]);
        }});
        key_down_=surface_.KeyDown([weak](auto&&,const Input::KeyRoutedEventArgs& args){if(static_cast<int>(args.Key())==VK_ESCAPE)if(auto self=weak.lock();self && !self->sources_.IsDropDownOpen()){args.Handled(true);self->dismiss();}});
        size_changed_=surface_.SizeChanged([weak](auto&&,auto&&){if(auto self=weak.lock();self){self->position_popup();self->update_backdrop();if(!self->reveal_)return;self->reveal_.Right(static_cast<float>(self->surface_.ActualWidth()));self->reveal_.Bottom(static_cast<float>(self->surface_.ActualHeight()));if(self->motion_)self->motion_.InsertScalar(L"Extent",std::max(0.f,static_cast<float>(self->surface_.ActualHeight())-36));self->position_popup();}});
        clock_tick_=clock_.Tick([weak](auto&&,auto&&){if(auto self=weak.lock();self && self->open_){++self->ticks_;self->render_progress();}});
        commit_tick_=commit_timer_.Tick([weak](auto&&,auto&&){if(auto self=weak.lock())self->commit_seek();});
        opened_=popup_.Opened([weak](auto&&,auto&&){if(auto self=weak.lock();self && !self->disposed_ && self->popup_.IsOpen()){self->showing_=false;self->open_=true;++self->opens_;try{const auto actual=player_xaml::Hosting::ElementCompositionPreview::GetElementVisual(self->surface_);if(actual!=self->visual_){self->stop_motion();self->visual_=actual;self->reveal_=nullptr;self->motion_=nullptr;player_xaml::Hosting::ElementCompositionPreview::SetIsTranslationEnabled(self->surface_,true);}self->blur_.close();self->restyle();self->commands_.browse_sources(true);self->render();self->surface_.UpdateLayout();self->position_popup();self->update_backdrop();self->attach_dismiss();if(!self->preview_)self->play_.Focus(player_xaml::FocusState::Pointer);self->animate(true);}catch(...){self->last_error_=winrt::to_hresult();self->stop_motion();self->visual_.Clip(nullptr);self->visual_.Opacity(1);}}});
        closed_=popup_.Closed([weak](auto&&,auto&&){if(auto self=weak.lock())self->finish_close();});
        value_changed_=slider_.ValueChanged([weak](auto&&,auto&&){if(auto self=weak.lock();self && !self->updating_ && self->open_ && !self->interaction_cancelled_){self->previewing_=true;self->preview_ratio_=self->slider_.Value()/1000.;self->captured_revision_=self->snapshot_.media_revision;self->render_progress();if(!self->dragging_){self->commit_timer_.Stop();self->commit_timer_.Start();}}});
        pressed_=winrt::box_value(Input::PointerEventHandler{[weak](auto&&,auto&&){if(auto self=weak.lock();self && self->snapshot_.timeline.seekable()){self->dragging_=true;self->interaction_cancelled_=false;self->captured_revision_=self->snapshot_.media_revision;self->commit_timer_.Stop();self->update_clock();}}});
        released_=winrt::box_value(Input::PointerEventHandler{[weak](auto&&,auto&&){if(auto self=weak.lock()){const bool commit=self->dragging_ && !self->interaction_cancelled_;self->dragging_=false;self->interaction_cancelled_=false;if(commit)self->commit_seek();self->render_progress();self->update_clock();}}});
        cancelled_=winrt::box_value(Input::PointerEventHandler{[weak](auto&&,auto&&){if(auto self=weak.lock()){self->dragging_=false;self->interaction_cancelled_=false;self->commit_timer_.Stop();self->render_progress();self->update_clock();}}});
        slider_.AddHandler(UIElement::PointerPressedEvent(),pressed_,true);slider_.AddHandler(UIElement::PointerReleasedEvent(),released_,true);slider_.AddHandler(UIElement::PointerCanceledEvent(),cancelled_,true);
        restyle();
    }
public:
    ExpandedPlayer(MediaCommands commands,player_xaml::FrameworkElement root):commands_(std::move(commands)),root_(root){}
    void anchor_width(double width){if(surface_){const double value=std::max(120.,width);if(std::abs(surface_.Width()-value)>.1)surface_.Width(value);sources_.MaxWidth(std::max(94.,value-24));}}
    void configure(Settings settings,bool dark,TaskbarMaterial material){settings_=settings;dark_=dark;material_=std::move(material);if(open_){restyle();position_popup();}if(!animations()){if(closing_animation_)dismiss(true);else if(open_)animate(true);}}
    winrt::Windows::UI::Color wave_color()const{return cover_accent_.value_or(system_accent_);}
    void update(MediaSnapshot snapshot,player_xaml::Media::ImageSource artwork,std::optional<winrt::Windows::UI::Color> accent={}) {
        if(disposed_)return;
        if(snapshot.media_revision!=snapshot_.media_revision){if(dragging_)interaction_cancelled_=true;dragging_=false;previewing_=false;if(commit_timer_)commit_timer_.Stop();}
        snapshot_=std::move(snapshot);artwork_=artwork;cover_accent_=accent;
        if(visualizer_)visualizer_->color(wave_color(),dark_);
        if(!snapshot_.visible){dismiss();return;}
        if(open_ || showing_)render();
    }
    void toggle(player_xaml::FrameworkElement anchor,bool preview=false) noexcept {
        try{
            if(disposed_ || !snapshot_.available || !snapshot_.visible)return;
            if(closing_animation_ && popup_.IsOpen()){closing_animation_=false;cancel_close();update_clock();animate(true);return;}
            if(open_ || showing_){if(!preview)dismiss();return;}
            preview_=preview;
            if(!popup_)create();anchor_width(anchor.ActualWidth());restyle();render();showing_=true;
            anchor_=anchor;surface_.Measure({static_cast<float>(surface_.Width()),1000});position_popup();visual_.Opacity(0);popup_.IsOpen(true);
        }catch(...){last_error_=winrt::to_hresult();showing_=false;}
    }
    void dismiss(bool immediate=false) noexcept {
        if(!popup_)return;
        try{if(immediate || !animations()){stop_motion();popup_.IsOpen(false);finish_close();return;}if(!open_ && !showing_)return;if(closing_animation_)return;closing_animation_=true;if(visualizer_)visualizer_->open(false);commit_timer_.Stop();if(sources_)sources_.IsDropDownOpen(false);update_clock();animate(false);}catch(...){last_error_=winrt::to_hresult();popup_.IsOpen(false);finish_close();}
    }
    void close() noexcept {
        disposed_=true;dismiss(true);if(visualizer_){visualizer_->close();visualizer_.reset();}blur_.close();host_backdrop_.close();blur_host_=nullptr;tint_=nullptr;if(header_){header_->close();header_.reset();}
        try{if(clock_){clock_.Stop();clock_.Tick(clock_tick_);}if(commit_timer_){commit_timer_.Stop();commit_timer_.Tick(commit_tick_);}}catch(...){}
        clock_running_=false;
        detach_dismiss();
        try{if(popup_){popup_.Opened(opened_);popup_.Closed(closed_);popup_.Child(nullptr);}if(surface_){surface_.SizeChanged(size_changed_);surface_.KeyDown(key_down_);}}catch(...){}
        try{if(slider_){slider_.ValueChanged(value_changed_);slider_.RemoveHandler(player_xaml::UIElement::PointerPressedEvent(),pressed_);slider_.RemoveHandler(player_xaml::UIElement::PointerReleasedEvent(),released_);slider_.RemoveHandler(player_xaml::UIElement::PointerCanceledEvent(),cancelled_);}}catch(...){}
        for(const auto& [button,token]:buttons_)try{button.Click(token);}catch(...){}
        buttons_.clear();pressed_=nullptr;released_=nullptr;cancelled_=nullptr;
        try{if(sources_)sources_.SelectionChanged(source_changed_);if(surface_)surface_.Child(nullptr);}catch(...){}
        seek_thumb_.close();popup_=nullptr;surface_=nullptr;slider_=nullptr;clock_=nullptr;commit_timer_=nullptr;visual_=nullptr;reveal_=nullptr;motion_=nullptr;artwork_=nullptr;
        sources_=nullptr;source_ids_.clear();elapsed_=nullptr;duration_=nullptr;hint_=nullptr;play_glyph_=nullptr;previous_=nullptr;play_=nullptr;next_=nullptr;
        snapshot_=MediaSnapshot{};commands_={};root_=nullptr;anchor_=nullptr;
    }
    std::wstring describe()const{return L" popup_open="+std::to_wstring(open_)+L" popup_opens="+std::to_wstring(opens_)+L" seeks="+std::to_wstring(seeks_)+L" clock_running="+std::to_wstring(clock_running_)+L" clock_ticks="+std::to_wstring(ticks_)+L" sources="+std::to_wstring(snapshot_.sources.size())+L" selected_source="+std::to_wstring(snapshot_.selected_source)+L" seekable="+std::to_wstring(snapshot_.timeline.seekable())+L" position="+std::to_wstring(snapshot_.timeline.current()/10'000'000)+L" popup_size="+std::to_wstring(surface_?surface_.ActualWidth():0)+L"x"+std::to_wstring(surface_?surface_.ActualHeight():0)+L" popup_opacity="+std::to_wstring(surface_?settings_.popup_opacity:0)+L" popup_gap="+std::to_wstring(settings_.popup_gap)+L" popup_native="+std::to_wstring(native_popup_.get()!=nullptr)+L" popup_radius="+std::to_wstring(settings_.popup_radius)+L" popup_outline="+std::to_wstring(settings_.popup_outline)+L" popup_blur="+std::to_wstring(settings_.popup_blur)+L" blur_error="+std::to_wstring(static_cast<std::uint32_t>(blur_.error()))+L" popup_closing="+std::to_wstring(closing_animation_)+L" popup_previous_visible="+std::to_wstring(previous_ && previous_.Visibility()==player_xaml::Visibility::Visible)+L" popup_next_visible="+std::to_wstring(next_ && next_.Visibility()==player_xaml::Visibility::Visible)+L" blur_size="+std::to_wstring(blur_.width())+L"x"+std::to_wstring(blur_.height())+L" host_backdrop="+std::to_wstring(host_backdrop_.enabled())+L" host_error="+std::to_wstring(static_cast<std::uint32_t>(host_backdrop_.error()))+L" seek_height="+std::to_wstring(slider_?slider_.ActualHeight():0)+L" wave_color_source="+(cover_accent_?L"cover":L"system")+seek_thumb_.describe()+(visualizer_?visualizer_->describe():L"")+L" popup_error="+std::to_wstring(static_cast<std::uint32_t>(last_error_));}
};
}
