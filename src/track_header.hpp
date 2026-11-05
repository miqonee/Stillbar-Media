#pragma once
#include <algorithm>
#include <chrono>
#include <string>
#include <winrt/Windows.UI.h>
#include <winrt/Windows.UI.Composition.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Hosting.h>
#include <winrt/Windows.UI.Xaml.Media.h>
#include <memory>

namespace islands {
namespace header_xaml=winrt::Windows::UI::Xaml;
class TrackHeader : public std::enable_shared_from_this<TrackHeader> {
    struct Caption {std::wstring title,artist;header_xaml::Media::ImageSource art{nullptr};};
    struct Layer {
        header_xaml::Controls::Grid grid{nullptr};header_xaml::Controls::Image art{nullptr};
        header_xaml::Controls::TextBlock title{nullptr},artist{nullptr},fallback{nullptr};header_xaml::Controls::StackPanel text{nullptr};
        winrt::Windows::UI::Composition::Visual visual{nullptr};
    };
    header_xaml::Controls::Grid root_{nullptr};Layer current_,outgoing_;
    winrt::Windows::UI::Composition::CompositionScopedBatch batch_{nullptr};winrt::event_token completed_{};
    Caption caption_;
    bool initialized_=false,animating_=false,closed_=false;std::uint32_t period_=180;
    std::uint64_t transitions_=0,generation_=0;

    static Layer layer(double art_size,double title_size,double artist_size,double gap) {
        using namespace header_xaml;namespace ui=Controls;Layer value;value.grid=ui::Grid{};value.grid.Height(art_size);
        for(int index=0;index<2;++index){ui::ColumnDefinition column;column.Width({index==0?art_size+gap:1.,index==0?GridUnitType::Pixel:GridUnitType::Star});value.grid.ColumnDefinitions().Append(column);}
        ui::Grid art;art.Width(art_size);art.Height(art_size);art.HorizontalAlignment(HorizontalAlignment::Left);
        value.art=ui::Image{};value.art.Stretch(Media::Stretch::UniformToFill);
        value.fallback=ui::TextBlock{};value.fallback.Text(L"\xE8D6");value.fallback.FontFamily(Media::FontFamily{L"Segoe Fluent Icons"});value.fallback.FontSize(art_size*.68);value.fallback.HorizontalAlignment(HorizontalAlignment::Center);value.fallback.VerticalAlignment(VerticalAlignment::Center);
        art.Children().Append(value.fallback);art.Children().Append(value.art);value.grid.Children().Append(art);
        value.text=ui::StackPanel{};value.text.VerticalAlignment(VerticalAlignment::Center);ui::Grid::SetColumn(value.text,1);
        value.title=ui::TextBlock{};value.title.FontSize(title_size);value.title.TextTrimming(TextTrimming::CharacterEllipsis);
        value.artist=ui::TextBlock{};value.artist.FontSize(artist_size);value.artist.Opacity(.65);value.artist.TextTrimming(TextTrimming::CharacterEllipsis);
        value.text.Children().Append(value.title);value.text.Children().Append(value.artist);value.grid.Children().Append(value.text);
        value.visual=Hosting::ElementCompositionPreview::GetElementVisual(value.grid);return value;
    }
    static void apply(Layer& layer,const Caption& value) {
        if(layer.title.Text()!=value.title)layer.title.Text(value.title);
        if(layer.artist.Text()!=value.artist)layer.artist.Text(value.artist);
        if(layer.art.Source()!=value.art)layer.art.Source(value.art);
        layer.fallback.Visibility(value.art?header_xaml::Visibility::Collapsed:header_xaml::Visibility::Visible);
    }
    static bool same(const Caption& a,const Caption& b){return a.title==b.title && a.artist==b.artist && a.art==b.art;}
    void stop() noexcept {
        ++generation_;
        try{if(batch_ && completed_.value)batch_.Completed(completed_);current_.visual.StopAnimation(L"Opacity");outgoing_.visual.StopAnimation(L"Opacity");}catch(...){}
        batch_=nullptr;completed_={};animating_=false;
    }
    void transition(Caption next) {
        current_.visual=header_xaml::Hosting::ElementCompositionPreview::GetElementVisual(current_.grid);
        outgoing_.visual=header_xaml::Hosting::ElementCompositionPreview::GetElementVisual(outgoing_.grid);
        BOOL enabled=TRUE;SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&enabled,0);
        if(!initialized_ || !period_ || !enabled){caption_=std::move(next);apply(current_,caption_);current_.visual.Opacity(1);outgoing_.visual.Opacity(0);initialized_=true;return;}
        if(next.title==caption_.title && next.artist==caption_.artist){caption_=std::move(next);apply(current_,caption_);return;}
        apply(outgoing_,caption_);caption_=std::move(next);apply(current_,caption_);outgoing_.grid.Visibility(header_xaml::Visibility::Visible);animating_=true;++transitions_;
        const auto compositor=current_.visual.Compositor();const auto easing=compositor.CreateCubicBezierEasingFunction({.2f,0},{.2f,1});
        auto appear=compositor.CreateScalarKeyFrameAnimation(),disappear=compositor.CreateScalarKeyFrameAnimation();
        appear.InsertKeyFrame(0,0);appear.InsertKeyFrame(1,1,easing);disappear.InsertKeyFrame(0,1);disappear.InsertKeyFrame(1,0,easing);
        const auto duration=std::chrono::milliseconds(std::clamp(period_,140u,200u));appear.Duration(duration);disappear.Duration(duration);
        batch_=compositor.CreateScopedBatch(winrt::Windows::UI::Composition::CompositionBatchTypes::Animation);
        current_.visual.StartAnimation(L"Opacity",appear);outgoing_.visual.StartAnimation(L"Opacity",disappear);
        const auto weak=weak_from_this();const auto dispatcher=root_.Dispatcher();const auto generation=++generation_;
        completed_=batch_.Completed([weak,dispatcher,generation](auto&&,auto&&){try{dispatcher.RunAsync(winrt::Windows::UI::Core::CoreDispatcherPriority::Normal,[weak,generation]{if(auto self=weak.lock();self && !self->closed_ && generation==self->generation_){self->finish();}});}catch(...){}});
        batch_.End();
    }
public:
    TrackHeader(double art_size,double title_size,double artist_size,double gap) {
        root_=header_xaml::Controls::Grid{};root_.Height(art_size);current_=layer(art_size,title_size,artist_size,gap);outgoing_=layer(art_size,title_size,artist_size,gap);outgoing_.grid.IsHitTestVisible(false);outgoing_.grid.Visibility(header_xaml::Visibility::Collapsed);root_.Children().Append(outgoing_.grid);root_.Children().Append(current_.grid);
    }
    header_xaml::Controls::Grid element()const{return root_;}
    const std::wstring& title()const{return caption_.title;}
    const std::wstring& artist()const{return caption_.artist;}
    std::wstring describe()const{return L" header_swaps="+std::to_wstring(transitions_)+L" header_transition="+std::to_wstring(animating_);}
    void foreground(header_xaml::Media::Brush brush){for(auto* layer:{&current_,&outgoing_}){layer->title.Foreground(brush);layer->artist.Foreground(brush);layer->fallback.Foreground(brush);}}
    void text_visible(bool visible){const auto value=visible?header_xaml::Visibility::Visible:header_xaml::Visibility::Collapsed;for(auto* layer:{&current_,&outgoing_})if(layer->text.Visibility()!=value)layer->text.Visibility(value);}
    void show(std::wstring title,std::wstring artist,header_xaml::Media::ImageSource art,std::uint32_t period) {
        if(closed_)return;period_=period;Caption next{title.empty()?L"Медиаплеер":std::move(title),std::move(artist),art};
        BOOL enabled=TRUE;SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&enabled,0);
        if(animating_ && (!period_ || !enabled))finish();
        if(animating_){
            // Repeated skip commands replace the destination without replaying obsolete tracks.
            caption_=std::move(next);apply(current_,caption_);return;
        }
        if(!same(caption_,next) || !initialized_)transition(std::move(next));
    }
    void finish() noexcept {
        stop();try{current_.visual.Opacity(1);outgoing_.visual.Opacity(0);outgoing_.grid.Visibility(header_xaml::Visibility::Collapsed);outgoing_.art.Source(nullptr);}catch(...){}
    }
    void close() noexcept {
        closed_=true;finish();try{current_.art.Source(nullptr);outgoing_.art.Source(nullptr);root_.Children().Clear();}catch(...){}
        current_=Layer{};outgoing_=Layer{};root_=nullptr;caption_=Caption{};
    }
};
}
