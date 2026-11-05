#pragma once
#include "protocol.hpp"
#include "media.hpp"
#include "expanded_player.hpp"
#include "track_header.hpp"
#include "taskbar_material.hpp"
#include "element_motion.hpp"
#include "repeater_animation.hpp"
#include "island_surface.hpp"
#include "badge_visibility.hpp"
#include "taskbar_tree.hpp"
#include "artwork_color.hpp"
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.UI.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.Xaml.Automation.h>
#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Controls.Primitives.h>
#include <winrt/Windows.UI.Xaml.Media.Animation.h>
#include <winrt/Windows.UI.Xaml.Media.Imaging.h>
#include <winrt/Windows.UI.Text.h>
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <memory>
#include <vector>

namespace islands {
namespace xaml=winrt::Windows::UI::Xaml;
namespace ui=xaml::Controls;
namespace anim=xaml::Media::Animation;

class IslandsView : public std::enable_shared_from_this<IslandsView> {
    xaml::FrameworkElement frame_{nullptr};
    xaml::FrameworkElement root_{nullptr};
    ui::Panel panel_{nullptr};
    xaml::FrameworkElement repeater_{nullptr};
    xaml::FrameworkElement tray_{nullptr};
    xaml::FrameworkElement native_background_{nullptr},native_edge_{nullptr};
    ui::Canvas canvas_{nullptr};
    ui::Canvas player_canvas_{nullptr};
    ui::Canvas player_hit_area_{nullptr};
    ui::Border apps_border_{nullptr},tray_border_{nullptr},media_border_{nullptr};
    ui::Grid media_grid_{nullptr};
    ui::Border media_surface_host_{nullptr};
    std::unique_ptr<motion::IslandSurface> media_surface_;
    ui::TextBlock play_glyph_{nullptr};
    std::shared_ptr<TrackHeader> header_;
    xaml::Media::ImageSource cover_source_{nullptr};
    std::optional<winrt::Windows::UI::Color> cover_accent_;
    MediaSnapshot latest_snapshot_;
    TaskbarMaterial material_;
    bool loading_art_=false;
    ui::Button previous_{nullptr},play_{nullptr},next_{nullptr},summary_{nullptr};
    std::vector<anim::Storyboard> native_overrides_;
    anim::Storyboard frame_width_{nullptr};
    std::unique_ptr<motion::IslandSurface> app_surface_,tray_surface_;
    std::shared_ptr<motion::ElementMotion> element_motion_;
    std::unique_ptr<motion::RepeaterAnimation> repeater_animation_;
    std::shared_ptr<ExpandedPlayer> expanded_;
    std::shared_ptr<BadgeVisibility> badge_;
    std::shared_ptr<AsyncTasks> tasks_;
    std::vector<std::pair<ui::Button,winrt::event_token>> button_tokens_;
    winrt::event_token layout_token_{},theme_token_{},player_tapped_{},render_token_{};
    Settings settings_;
    MediaCommands commands_;
    std::function<void(HRESULT)> failed_;
    bool active_=false,layout_queued_=false,media_available_=false;
    std::uint64_t image_generation_=0,artwork_revision_=0,layouts_=0,cover_decodes_=0;
    std::uint64_t media_shows_=0,media_hides_=0;
    double current_frame_width_=-1;
    std::uint64_t layout_us_last_=0,layout_us_max_=0,layout_us_total_=0;

    static anim::Storyboard double_override(xaml::DependencyObject target,const wchar_t* property,double value) {
        anim::DoubleAnimation animation;animation.To(value);animation.Duration(xaml::DurationHelper::FromTimeSpan(winrt::Windows::Foundation::TimeSpan{0}));animation.EnableDependentAnimation(true);
        anim::Storyboard::SetTarget(animation,target);anim::Storyboard::SetTargetProperty(animation,property);
        anim::Storyboard storyboard;storyboard.Children().Append(animation);storyboard.Begin();return storyboard;
    }
    static anim::Storyboard object_override(xaml::DependencyObject target,const wchar_t* property,winrt::Windows::Foundation::IInspectable value) {
        anim::DiscreteObjectKeyFrame key;key.KeyTime(anim::KeyTimeHelper::FromTimeSpan(winrt::Windows::Foundation::TimeSpan{0}));key.Value(value);
        anim::ObjectAnimationUsingKeyFrames animation;animation.KeyFrames().Append(key);animation.Duration(xaml::DurationHelper::Forever());
        anim::Storyboard::SetTarget(animation,target);anim::Storyboard::SetTargetProperty(animation,property);
        anim::Storyboard storyboard;storyboard.Children().Append(animation);storyboard.Begin();return storyboard;
    }
    ui::Border border(bool interactive) {
        ui::Border value;value.IsHitTestVisible(interactive);value.BorderThickness({1,1,1,1});return value;
    }
    xaml::Media::Brush background(double opacity) {
        return taskbar_material(material_,opacity,settings_.mode==AppearanceMode::player_only);
    }
    void restyle() {
        const bool dark=frame_.ActualTheme()==xaml::ElementTheme::Dark;
        material_=read_taskbar_material(native_background_,dark);
        if(expanded_)expanded_->configure(settings_,dark,material_);
        const ui::Border borders[]{media_border_,apps_border_,tray_border_};
        const bool advanced=settings_.mode==AppearanceMode::islands && settings_.advanced;
        for(int index=0;index<3;++index){const double radius=advanced?settings_.radii[index]:settings_.radius;const double opacity=advanced?settings_.opacities[index]:settings_.opacity;const auto outline=xaml::Media::SolidColorBrush{dark?winrt::Windows::UI::Color{100,105,108,116}:winrt::Windows::UI::Color{95,135,140,150}};outline.Opacity(opacity);borders[index].CornerRadius({radius,radius,radius,radius});borders[index].Background(background(opacity));borders[index].BorderBrush(outline);}
        media_border_.BorderThickness(settings_.mode==AppearanceMode::player_only && settings_.player_outline?xaml::Thickness{1,1,1,1}:xaml::Thickness{0,0,0,0});
        const auto outline_color=dark?winrt::Windows::UI::Color{100,105,108,116}:winrt::Windows::UI::Color{95,135,140,150};
        if(app_surface_)app_surface_->style(advanced?settings_.radii[1]:settings_.radius,outline_color,advanced?settings_.opacities[1]:settings_.opacity);
        if(tray_surface_)tray_surface_->style(advanced?settings_.radii[2]:settings_.radius,outline_color,advanced?settings_.opacities[2]:settings_.opacity);
        if(app_surface_)app_surface_->material(material_.color,advanced?settings_.opacities[1]:settings_.opacity,static_cast<float>(settings_.island_blur));
        if(tray_surface_)tray_surface_->material(material_.color,advanced?settings_.opacities[2]:settings_.opacity,static_cast<float>(settings_.island_blur));
        if(media_surface_){media_surface_->style(advanced?settings_.radii[0]:settings_.radius,outline_color,advanced?settings_.opacities[0]:settings_.opacity);media_surface_->material(material_.color,advanced?settings_.opacities[0]:settings_.opacity,static_cast<float>(settings_.island_blur));}
        if(media_surface_host_)media_surface_host_.Visibility(settings_.mode==AppearanceMode::islands?xaml::Visibility::Visible:xaml::Visibility::Collapsed);
        media_border_.Background(xaml::Media::SolidColorBrush{winrt::Windows::UI::Colors::Transparent()});
        const auto foreground=xaml::Media::SolidColorBrush{dark?winrt::Windows::UI::Color{255,243,244,247}:winrt::Windows::UI::Color{255,25,28,34}};
        if(header_)header_->foreground(foreground);
        for(const auto& button:{previous_,play_,next_})button.Foreground(foreground);
    }
    ui::Button media_button(const wchar_t* glyph,const wchar_t* label,MediaAction action) {
        ui::TextBlock text;text.Text(glyph);text.FontFamily(xaml::Media::FontFamily{L"Segoe Fluent Icons"});text.FontSize(13);
        ui::Button button;button.Content(text);button.MinWidth(0);button.MinHeight(0);button.Width(26);button.Height(30);button.Padding({0});button.BorderThickness({0});
        button.Background(xaml::Media::SolidColorBrush{winrt::Windows::UI::Colors::Transparent()});
        xaml::Automation::AutomationProperties::SetName(button,label);
        const auto weak=weak_from_this();const auto token=button.Click([weak,action](auto&&,auto&&){if(auto self=weak.lock();self && self->active_)self->commands_.action(action);});button_tokens_.emplace_back(button,token);return button;
    }
    void create_player() {
        media_grid_=ui::Grid{};
        for(int index=0;index<5;++index){ui::ColumnDefinition column;column.Width(index==1?xaml::GridLength{1,xaml::GridUnitType::Star}:xaml::GridLength{1,xaml::GridUnitType::Auto});media_grid_.ColumnDefinitions().Append(column);}
        header_=std::make_shared<TrackHeader>(28,12,10,7);
        const auto summary=header_->element();
        ui::Button open;open.Content(summary);open.Padding({0});open.BorderThickness({0});open.HorizontalAlignment(xaml::HorizontalAlignment::Stretch);open.HorizontalContentAlignment(xaml::HorizontalAlignment::Stretch);open.Background(xaml::Media::SolidColorBrush{winrt::Windows::UI::Colors::Transparent()});ui::Grid::SetColumnSpan(open,2);
        open.MinWidth(0);open.Margin({0,0,10,0});open.Name(L"Islands.MediaSummary");summary_=open;
        xaml::Automation::AutomationProperties::SetName(open,L"Открыть расширенный плеер");
        const auto weak=weak_from_this();const auto token=open.Click([weak](auto&&,auto&&){if(auto self=weak.lock();self && self->active_ && self->expanded_)self->expanded_->toggle(self->media_border_);});button_tokens_.emplace_back(open,token);media_grid_.Children().Append(open);
        previous_=media_button(L"\xE892",L"Предыдущий трек",MediaAction::previous);play_=media_button(L"\xE769",L"Пауза / воспроизведение",MediaAction::toggle);next_=media_button(L"\xE893",L"Следующий трек",MediaAction::next);
        play_glyph_=play_.Content().as<ui::TextBlock>();
        int column=2;for(const auto& button:{previous_,play_,next_}){ui::Grid::SetColumn(button,column++);media_grid_.Children().Append(button);}
        ui::Grid layers;media_surface_host_=ui::Border{};media_surface_host_.IsHitTestVisible(false);layers.Children().Append(media_surface_host_);media_surface_=std::make_unique<motion::IslandSurface>(media_surface_host_);media_grid_.Margin({6,0,6,0});layers.Children().Append(media_grid_);
        media_border_.Child(layers);media_border_.Visibility(xaml::Visibility::Collapsed);
    }
    void create_player_hit_area() {
        player_hit_area_=ui::Canvas{};player_hit_area_.Name(L"Islands.PlayerHitArea");player_hit_area_.Background(xaml::Media::SolidColorBrush{winrt::Windows::UI::Colors::Transparent()});
        player_hit_area_.Children().Append(media_border_);
        const auto weak=weak_from_this();
        player_tapped_=player_hit_area_.Tapped([weak](auto&&,const xaml::Input::TappedRoutedEventArgs& args){
            if(auto self=weak.lock();self && self->active_ && self->expanded_)try{
                auto node=args.OriginalSource().try_as<xaml::DependencyObject>();
                for(unsigned depth=0;node && node!=self->player_hit_area_ && depth<20;++depth){
                    if(node.try_as<ui::Button>())return;
                    node=xaml::Media::VisualTreeHelper::GetParent(node);
                }
                self->expanded_->toggle(self->media_border_);
            }catch(...){}
        });
    }
    void request_layout() noexcept {
        if(!active_ || layout_queued_)return;layout_queued_=true;
        const auto weak=weak_from_this();
        try{render_token_=xaml::Media::CompositionTarget::Rendering([weak](auto&&,auto&&){if(auto self=weak.lock()){xaml::Media::CompositionTarget::Rendering(self->render_token_);self->render_token_={};self->layout_queued_=false;if(self->active_)try{self->layout();}catch(...){const auto error=winrt::to_hresult();self->close();if(self->failed_)self->failed_(error);}}});}catch(...){layout_queued_=false;}
    }
    static void set_double(xaml::DependencyObject target,xaml::DependencyProperty property,double value) {
        const auto previous=winrt::unbox_value_or<double>(target.GetValue(property),-1);
        if(!std::isfinite(previous) || std::abs(previous-value)>.1)target.SetValue(property,winrt::box_value(value));
    }
    static void place(ui::Border border,double left,double top,double width,double height) {
        set_double(border,ui::Canvas::LeftProperty(),left);set_double(border,ui::Canvas::TopProperty(),top);
        if(std::abs(border.Width()-width)>.1 || std::isnan(border.Width()))border.Width(width);
        if(std::abs(border.Height()-height)>.1 || std::isnan(border.Height()))border.Height(height);
    }
    static void tray_elements(xaml::DependencyObject node,std::vector<xaml::FrameworkElement>& output,unsigned depth=0) {
        if(depth>24)return;
        if(auto element=node.try_as<xaml::FrameworkElement>();element && element.Visibility()==xaml::Visibility::Visible && element.ActualWidth()>1){
            const auto type=winrt::get_class_name(element);
            if(type==L"SystemTray.NotifyIconView" || type==L"SystemTray.ChevronIconView" || type==L"SystemTray.OmniButton"){output.push_back(element);return;}
        }
        const auto count=xaml::Media::VisualTreeHelper::GetChildrenCount(node);
        for(int index=0;index<count;++index)tray_elements(xaml::Media::VisualTreeHelper::GetChild(node,index),output,depth+1);
    }
    void layout() {
        ++layouts_;
        struct Timing {
            IslandsView& view;motion::Clock::time_point started=motion::Clock::now();
            ~Timing(){const auto elapsed=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(motion::Clock::now()-started).count());view.layout_us_last_=elapsed;view.layout_us_max_=std::max(view.layout_us_max_,elapsed);view.layout_us_total_+=elapsed;}
        } timing{*this};
        const double width=root_.ActualWidth(),height=root_.ActualHeight();if(width<200 || height<24)return;
        const auto now=motion::Clock::now();BOOL system_animations=TRUE;SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&system_animations,0);const bool animate=settings_.animation_ms && system_animations;
        const auto tray_point=motion::layout_position(tray_,root_);
        const double reserve=std::max(width-tray_point.X+12,settings_.inset+284);
        const double available_frame=std::max(120.,width-2*reserve);
        const double frame_width=settings_.apps_autosize?available_frame:std::clamp(settings_.apps_width,120.,available_frame);
        if(settings_.mode==AppearanceMode::islands && std::abs(frame_width-current_frame_width_)>.5){
            if(frame_width_)frame_width_.Stop();frame_width_=double_override(frame_,L"Width",frame_width);current_frame_width_=frame_width;request_layout();return;
        }
        const double inset=std::min(settings_.inset,std::max(0.,(height-32)/2));
        const double island_height=height-2*inset;
        double first=width,last=0;
        bool widget=false;
        std::vector<xaml::FrameworkElement> animated_elements;
        const auto count=xaml::Media::VisualTreeHelper::GetChildrenCount(repeater_);
        for(int index=0;index<count;++index){
            const auto child=xaml::Media::VisualTreeHelper::GetChild(repeater_,index).try_as<xaml::FrameworkElement>();
            if(!child || child.Visibility()!=xaml::Visibility::Visible || child.ActualWidth()<1)continue;
            const auto type=winrt::get_class_name(child);const auto point=motion::layout_position(child,root_);
            if(point.X+child.ActualWidth()<=0 || point.X>=width || point.Y+child.ActualHeight()<=0 || point.Y>=height)continue;
            if(type==L"Taskbar.AugmentedEntryPointButton"){widget=true;continue;}
            animated_elements.push_back(child);
            first=std::min(first,static_cast<double>(point.X));last=std::max(last,point.X+child.ActualWidth());
        }
        if(last<=first)return;
        if(settings_.mode==AppearanceMode::islands){
            const double app_width=settings_.apps_autosize?last-first+16:frame_width+16;
            app_surface_->place(width,width/2,.5,inset,app_width,island_height,settings_.animation_ms,animate,now);
            const double tray_left=std::max(0.,static_cast<double>(tray_point.X)-2);
            tray_surface_->place(width,width-settings_.inset,1,inset,std::max(1.,width-settings_.inset-tray_left),island_height,settings_.animation_ms,animate,now);
        }
        double player_width=272;
        if(first-8<settings_.inset+player_width+12)player_width=126;
        const bool show=media_available_ && !widget && first-8>=settings_.inset+player_width+12;
        if(header_)header_->text_visible(player_width>=200);
        const double caption_margin=player_width>=200?10.:0.;
        if(summary_ && std::abs(summary_.Margin().Right-caption_margin)>.1)summary_.Margin({0,0,caption_margin,0});
        if(!show && expanded_)expanded_->dismiss();
        const bool widget_mode=settings_.mode==AppearanceMode::player_only;
        const double hit_left=widget_mode?std::max(0.,settings_.inset-8):settings_.inset;
        const double hit_top=widget_mode?0:inset;
        const double hit_width=widget_mode?std::min(first-12,settings_.inset+player_width+8)-hit_left:player_width;
        set_double(player_hit_area_,ui::Canvas::LeftProperty(),hit_left);set_double(player_hit_area_,ui::Canvas::TopProperty(),hit_top);
        set_double(player_hit_area_,xaml::FrameworkElement::WidthProperty(),std::max(player_width,hit_width));set_double(player_hit_area_,xaml::FrameworkElement::HeightProperty(),widget_mode?height:island_height);
        place(media_border_,settings_.inset-hit_left,inset-hit_top,player_width,island_height);
        if(settings_.mode==AppearanceMode::islands && media_surface_)media_surface_->place(player_width,0,0,0,player_width,island_height,settings_.animation_ms,animate,now);
        if(expanded_)expanded_->anchor_width(player_width);
        if(badge_)badge_->show(show,animate);
        tray_elements(tray_,animated_elements);
        if(element_motion_){element_motion_->update(animated_elements,settings_.animation_ms,animate && settings_.mode==AppearanceMode::islands,now,first,last);if(element_motion_->pending())request_layout();}
    }
    void configure_native() {
        if(app_surface_)app_surface_->reset();if(tray_surface_)tray_surface_->reset();
        if(frame_width_)frame_width_.Stop();frame_width_=nullptr;current_frame_width_=-1;
        for(auto&& override:native_overrides_)override.Stop();native_overrides_.clear();
        const bool islands=settings_.mode==AppearanceMode::islands;
        if(repeater_animation_){if(islands)repeater_animation_->disable();else repeater_animation_->restore();}
        canvas_.Visibility(islands?xaml::Visibility::Visible:xaml::Visibility::Collapsed);
        if(islands){
            native_overrides_.push_back(double_override(native_background_,L"Opacity",0));
            native_overrides_.push_back(object_override(frame_,L"HorizontalAlignment",winrt::box_value(xaml::HorizontalAlignment::Center)));
        }else if(settings_.compact_background){
            native_overrides_.push_back(double_override(native_background_,L"Height",std::max(32.,root_.ActualHeight()-8)));
            native_overrides_.push_back(object_override(native_background_,L"VerticalAlignment",winrt::box_value(xaml::VerticalAlignment::Center)));
        }
        if(native_edge_ && (islands || settings_.compact_background))native_overrides_.push_back(double_override(native_edge_,L"Opacity",0));
    }
    void present_header() {
        auto display=latest_snapshot_;
        if(header_)header_->show(display.title,display.artist,cover_source_,settings_.animation_ms);
        if(expanded_)expanded_->update(std::move(display),cover_source_,cover_accent_);
    }
    winrt::fire_and_forget load_cover_color(winrt::Windows::Storage::Streams::IRandomAccessStream stream,std::uint64_t generation) {
        const auto held=shared_from_this();auto work=tasks_->begin();if(!work)co_return;
        try{
            namespace imaging=winrt::Windows::Graphics::Imaging;
            auto create=imaging::BitmapDecoder::CreateAsync(stream);work.track(create);const auto decoder=co_await create;if(!active_ || generation!=image_generation_)co_return;if(!decoder)throw winrt::hresult_error(E_PENDING);
            imaging::BitmapTransform transform;transform.ScaledWidth(32);transform.ScaledHeight(32);
            auto read=decoder.GetPixelDataAsync(imaging::BitmapPixelFormat::Bgra8,imaging::BitmapAlphaMode::Straight,transform,imaging::ExifOrientationMode::IgnoreExifOrientation,imaging::ColorManagementMode::ColorManageToSRgb);work.track(read);
            const auto pixels=co_await read;if(!pixels)throw winrt::hresult_error(E_PENDING);if(active_ && generation==image_generation_){const auto data=pixels.DetachPixelData();cover_accent_=artwork_color(std::span<const std::uint8_t>{data.data(),data.size()});present_header();}
        }catch(...){if(active_ && generation==image_generation_){cover_accent_.reset();present_header();}}
    }
    winrt::fire_and_forget load_cover(winrt::Windows::Storage::Streams::IRandomAccessStream stream,std::uint64_t generation) {
        const auto held=shared_from_this();auto work=tasks_->begin();if(!work)co_return;
        try{auto palette=stream.CloneStream();palette.Seek(0);xaml::Media::Imaging::BitmapImage bitmap;bitmap.DecodePixelWidth(160);auto decode=bitmap.SetSourceAsync(stream);work.track(decode);++cover_decodes_;co_await decode;if(active_ && generation==image_generation_){cover_source_=bitmap;loading_art_=false;present_header();load_cover_color(palette,generation);}}catch(...){if(active_ && generation==image_generation_){cover_source_=nullptr;cover_accent_.reset();loading_art_=false;present_header();}}
    }
public:
    IslandsView(xaml::FrameworkElement frame,Settings settings,std::shared_ptr<AsyncTasks> tasks,MediaCommands commands,std::function<void(HRESULT)> failed={}):frame_(frame),tasks_(std::move(tasks)),settings_(settings),commands_(std::move(commands)),failed_(std::move(failed)){}
    void start() {
        const auto tree=resolve_taskbar_tree(frame_);
        if(const auto missing=tree.missing())throw winrt::hresult_error(HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH),L"Структура панели: "+std::wstring(missing)+L" ещё недоступна.");
        root_=tree.root;panel_=tree.panel;repeater_=tree.repeater;tray_=tree.tray;native_background_=tree.background;native_edge_=tree.edge;
        canvas_=ui::Canvas{};ui::Grid::SetColumnSpan(canvas_,128);ui::Grid::SetRowSpan(canvas_,128);ui::Canvas::SetZIndex(canvas_,-100);canvas_.IsHitTestVisible(false);
        player_canvas_=ui::Canvas{};ui::Grid::SetColumnSpan(player_canvas_,128);ui::Grid::SetRowSpan(player_canvas_,128);ui::Canvas::SetZIndex(player_canvas_,500);
        apps_border_=border(false);tray_border_=border(false);media_border_=border(true);create_player();
        create_player_hit_area();
        player_hit_area_.Visibility(xaml::Visibility::Collapsed);player_hit_area_.IsHitTestVisible(false);
        badge_=std::make_shared<BadgeVisibility>(player_hit_area_,media_border_);
        apps_border_.Name(L"Islands.Apps");tray_border_.Name(L"Islands.Tray");media_border_.Name(L"Islands.Player");
        canvas_.Children().Append(apps_border_);canvas_.Children().Append(tray_border_);player_canvas_.Children().Append(player_hit_area_);panel_.Children().Append(canvas_);panel_.Children().Append(player_canvas_);
        element_motion_=std::make_shared<motion::ElementMotion>(root_);
        repeater_animation_=std::make_unique<motion::RepeaterAnimation>(repeater_);
        app_surface_=std::make_unique<motion::IslandSurface>(apps_border_);tray_surface_=std::make_unique<motion::IslandSurface>(tray_border_);
        expanded_=std::make_shared<ExpandedPlayer>(commands_,root_);
        configure_native();
        const auto weak=weak_from_this();
        layout_token_=root_.LayoutUpdated([weak](auto&&,auto&&){if(auto self=weak.lock())self->request_layout();});
        theme_token_=frame_.ActualThemeChanged([weak](auto&&,auto&&){if(auto self=weak.lock();self && self->active_)try{self->restyle();}catch(...){}});
        active_=true;restyle();layout();
    }
    void apply(Settings settings){const bool changed=settings.mode!=settings_.mode || settings.compact_background!=settings_.compact_background;settings_=settings;if(changed)configure_native();if(!settings_.animation_ms && header_)header_->finish();restyle();request_layout();}
    void expand_player(bool preview=false){if(active_ && expanded_ && badge_ && badge_->target())expanded_->toggle(media_border_,preview);}
    void media(MediaSnapshot snapshot){
        if(!active_)return;if(snapshot.visible!=media_available_){if(snapshot.visible)++media_shows_;else ++media_hides_;}media_available_=snapshot.visible;latest_snapshot_=snapshot;
        const bool ready=snapshot.available && snapshot.playback_valid;
        previous_.IsEnabled(ready && snapshot.previous);next_.IsEnabled(ready && snapshot.next);play_.IsEnabled(ready && snapshot.toggle);play_glyph_.Text(snapshot.playing?L"\xE769":L"\xE768");
        previous_.Visibility(snapshot.previous?xaml::Visibility::Visible:xaml::Visibility::Collapsed);next_.Visibility(snapshot.next?xaml::Visibility::Visible:xaml::Visibility::Collapsed);
        if(snapshot.artwork_revision!=artwork_revision_){
            artwork_revision_=snapshot.artwork_revision;const auto generation=++image_generation_;
            if(snapshot.thumbnail){loading_art_=true;auto stream=snapshot.thumbnail.CloneStream();stream.Seek(0);load_cover(stream,generation);}else{loading_art_=false;cover_source_=nullptr;cover_accent_.reset();}
        }
        present_header();request_layout();
    }
    void close() noexcept {
        active_=false;++image_generation_;
        try{if(render_token_.value)xaml::Media::CompositionTarget::Rendering(render_token_);}catch(...){}render_token_={};layout_queued_=false;
        if(expanded_){expanded_->close();expanded_.reset();}if(header_){header_->close();header_.reset();}
        if(badge_){badge_->close();badge_.reset();}
        if(media_surface_){media_surface_->close();media_surface_.reset();}media_surface_host_=nullptr;
        try{if(root_ && layout_token_.value)root_.LayoutUpdated(layout_token_);}catch(...){}
        try{if(frame_ && theme_token_.value)frame_.ActualThemeChanged(theme_token_);}catch(...){}
        layout_token_={};theme_token_={};
        try{if(player_hit_area_ && player_tapped_.value)player_hit_area_.Tapped(player_tapped_);}catch(...){}player_tapped_={};
        for(const auto& [button,token]:button_tokens_)try{button.Click(token);}catch(...){}
        button_tokens_.clear();
        if(app_surface_){app_surface_->close();app_surface_.reset();}if(tray_surface_){tray_surface_->close();tray_surface_.reset();}
        if(element_motion_){element_motion_->close();element_motion_.reset();}
        if(repeater_animation_){repeater_animation_->restore();repeater_animation_.reset();}
        try{if(frame_width_)frame_width_.Stop();}catch(...){}
        for(auto&& override:native_overrides_)try{override.Stop();}catch(...){}
        native_overrides_.clear();
        frame_width_=nullptr;
        try{if(panel_ && canvas_){std::uint32_t index=0;if(panel_.Children().IndexOf(canvas_,index))panel_.Children().RemoveAt(index);}}catch(...){}
        try{if(panel_ && player_canvas_){std::uint32_t index=0;if(panel_.Children().IndexOf(player_canvas_,index))panel_.Children().RemoveAt(index);}}catch(...){}
        try{if(media_border_)media_border_.Child(nullptr);if(media_grid_)media_grid_.Children().Clear();if(canvas_)canvas_.Children().Clear();if(player_canvas_)player_canvas_.Children().Clear();}catch(...){}
        apps_border_=nullptr;tray_border_=nullptr;media_border_=nullptr;media_grid_=nullptr;
        play_glyph_=nullptr;cover_source_=nullptr;latest_snapshot_=MediaSnapshot{};previous_=nullptr;play_=nullptr;next_=nullptr;summary_=nullptr;
        player_hit_area_=nullptr;canvas_=nullptr;player_canvas_=nullptr;panel_=nullptr;root_=nullptr;frame_=nullptr;repeater_=nullptr;tray_=nullptr;native_background_=nullptr;native_edge_=nullptr;
        commands_={};tasks_->close();
    }
    double caption_gap()const {
        if(!summary_ || summary_.ActualWidth()<1 || !root_)return -1;
        const auto control=previous_.Visibility()==xaml::Visibility::Visible?previous_:play_;
        return motion::layout_position(control,root_).X-motion::layout_position(summary_,root_).X-summary_.ActualWidth();
    }
    std::wstring describe()const{return (element_motion_?element_motion_->describe():L"motion inactive")+(repeater_animation_?repeater_animation_->describe():L"")+L" material_native="+std::to_wstring(material_.native)+L" material_argb="+std::to_wstring((static_cast<std::uint32_t>(material_.color.A)<<24)|(material_.color.R<<16)|(material_.color.G<<8)|material_.color.B)+L" layouts="+std::to_wstring(layouts_)+L" layout_us_last="+std::to_wstring(layout_us_last_)+L" layout_us_max="+std::to_wstring(layout_us_max_)+L" layout_us_mean="+std::to_wstring(layouts_?layout_us_total_/layouts_:0)+L" cover_decodes="+std::to_wstring(cover_decodes_)+L" media_shows="+std::to_wstring(media_shows_)+L" media_hides="+std::to_wstring(media_hides_)+L" media_available="+std::to_wstring(latest_snapshot_.available)+L" media_state="+playback_name(latest_snapshot_.playback)+L" media_played="+std::to_wstring(latest_snapshot_.played)+L" manual_losses="+std::to_wstring(latest_snapshot_.manual_losses)+L" source_requests="+std::to_wstring(latest_snapshot_.source_requests)+L" media_read_failures="+std::to_wstring(latest_snapshot_.read_failures)+L" media_read_error="+std::to_wstring(static_cast<std::uint32_t>(latest_snapshot_.read_error))+L" hide_reason="+hide_reason_name(latest_snapshot_.hide_reason)+L" hide_pending="+std::to_wstring(latest_snapshot_.hide_pending)+L" badge_target="+std::to_wstring(badge_ && badge_->target())+L" badge_rendered="+std::to_wstring(badge_ && badge_->rendered())+L" badge_transition="+std::to_wstring(badge_ && badge_->running())+L" badge_hit="+std::to_wstring(player_hit_area_ && player_hit_area_.IsHitTestVisible())+L" media_width="+std::to_wstring(media_border_?media_border_.ActualWidth():0)+L" caption_gap="+std::to_wstring(caption_gap())+L" previous_visible="+std::to_wstring(previous_ && previous_.Visibility()==xaml::Visibility::Visible)+L" next_visible="+std::to_wstring(next_ && next_.Visibility()==xaml::Visibility::Visible)+(header_?header_->describe():L"")+(expanded_?expanded_->describe():L"")+L"\napps"+(app_surface_?app_surface_->describe():L"")+L"\ntray"+(tray_surface_?tray_surface_->describe():L"");}
};
}
