#pragma once
#include "async_tasks.hpp"
#include "media_timeline.hpp"
#include "media_selection.hpp"
#include "media_lifecycle.hpp"
#include "media_observation.hpp"
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.UI.Xaml.h>
#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace islands {
struct MediaSourceInfo {std::uint64_t id=0;std::wstring name,title,app_id;};
struct MediaSnapshot {
    bool available=false,visible=false,playing=false,previous=false,next=false,toggle=false;
    bool timeline_valid=false,played=false,hide_pending=false;
    bool playback_valid=false;
    std::uint64_t read_failures=0;
    std::uint64_t manual_losses=0,source_requests=0;
    HRESULT read_error=S_OK;
    PlaybackState playback=PlaybackState::closed;
    MediaHideReason hide_reason=MediaHideReason::waiting_for_playback;
    std::wstring title,artist;
    std::uint64_t artwork_revision=0,media_revision=0,selected_source=0,active_source=0;
    bool seek_pending=false,seek_failed=false;
    MediaTimeline timeline;
    std::vector<MediaSourceInfo> sources;
    winrt::Windows::Storage::Streams::IRandomAccessStream thumbnail{nullptr};
};
enum class MediaAction {previous,toggle,next};
struct MediaCommands {
    DWORD settings_process=0;
    std::function<void(MediaAction)> action;
    std::function<void(std::int64_t,std::uint64_t)> seek;
    std::function<void(std::uint64_t)> select_source;
    std::function<void(bool)> browse_sources;
    std::function<void(bool)> visualizer_request;
    std::function<float()> audio_level;
};

class MediaService : public std::enable_shared_from_this<MediaService> {
    using Manager=winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionManager;
    using Session=winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSession;
    using PlaybackInfo=winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionPlaybackInfo;
    struct Source {
        Session session{nullptr};MediaSourceInfo info;
        winrt::event_token metadata_token{},playback_token{};
        winrt::Windows::Foundation::IAsyncInfo loading{nullptr};
        bool dirty=true;
        PlaybackHistory history;
    };
    winrt::Windows::UI::Core::CoreDispatcher dispatcher_{nullptr};
    std::shared_ptr<AsyncTasks> tasks_;
    std::function<void(MediaSnapshot)> changed_;
    Manager manager_{nullptr};Session session_{nullptr};
    winrt::event_token current_token_{},sessions_token_{},properties_token_{},playback_token_{},timeline_token_{},loss_tick_{};
    winrt::Windows::UI::Xaml::DispatcherTimer loss_timer_{nullptr};
    winrt::Windows::UI::Xaml::DispatcherTimer retry_timer_{nullptr};
    winrt::event_token retry_tick_{};
    winrt::Windows::Foundation::IAsyncInfo metadata_operation_{nullptr};
    MediaSnapshot snapshot_;
    std::vector<Source> sources_;
    std::uint64_t generation_=0,artwork_revision_=0,next_source_=0,selected_source_=0;
    std::atomic_bool closed_{false},refresh_queued_{false},metadata_requested_{false};
    bool browsing_=false,starting_=false;
    MediaVisibility visibility_;

    Source* find_source(std::uint64_t id){for(auto& source:sources_)if(source.info.id==id)return &source;return nullptr;}
    void publish() {
        if(closed_ || !changed_)return;
        update_visibility();
        snapshot_.sources.clear();for(const auto& source:sources_)snapshot_.sources.push_back(source.info);
        snapshot_.selected_source=selected_source_;changed_(snapshot_);
    }
    void update_visibility() {
        auto source=find_source(snapshot_.active_source);
        if(source && snapshot_.available && snapshot_.playback_valid)source->history.observe(snapshot_.playback);
        const bool browser=source && (_wcsicmp(source->info.app_id.c_str(),L"Stillbar.Yandex.Media")==0 || _wcsicmp(std::filesystem::path(source->info.app_id).filename().c_str(),L"Stillbar.BrowserHost.exe")==0);
        const auto reason=source && snapshot_.available?(snapshot_.playback_valid?source->history.reason(snapshot_.playback,snapshot_.timeline,snapshot_.timeline_valid,browser):MediaHideReason::unavailable):MediaHideReason::session_lost;
        visibility_.update(reason);
        snapshot_.visible=visibility_.visible();snapshot_.hide_reason=reason;snapshot_.hide_pending=visibility_.pending();
        if(!snapshot_.visible && source)source->history.confirm_hidden(reason);
        snapshot_.played=source && source->history.played();
        if(loss_timer_)loss_timer_.Stop();
        if(!visibility_.pending())return;
        if(!loss_timer_){
            loss_timer_=winrt::Windows::UI::Xaml::DispatcherTimer{};const auto weak=weak_from_this();
            loss_tick_=loss_timer_.Tick([weak](auto&&,auto&&){if(auto self=weak.lock();self && !self->closed_){self->loss_timer_.Stop();self->refresh(false);}});
        }
        loss_timer_.Interval(visibility_.remaining());loss_timer_.Start();
    }
    void retry(HRESULT error,bool metadata=false) {
        ++snapshot_.read_failures;snapshot_.read_error=error;
        if(metadata)metadata_requested_=true;
        if(!retry_timer_){
            retry_timer_=winrt::Windows::UI::Xaml::DispatcherTimer{};const auto weak=weak_from_this();
            retry_tick_=retry_timer_.Tick([weak](auto&&,auto&&){if(auto self=weak.lock();self && !self->closed_){self->retry_timer_.Stop();if(self->manager_)self->refresh(self->metadata_requested_.exchange(false));else self->start();}});
        }
        if(!retry_timer_.IsEnabled()){retry_timer_.Interval(std::chrono::milliseconds(1000));retry_timer_.Start();}
    }
    void detach_session() noexcept {
        try{if(session_ && properties_token_.value)session_.MediaPropertiesChanged(properties_token_);}catch(...){}
        try{if(session_ && playback_token_.value)session_.PlaybackInfoChanged(playback_token_);}catch(...){}
        try{if(session_ && timeline_token_.value)session_.TimelinePropertiesChanged(timeline_token_);}catch(...){}
        properties_token_={};playback_token_={};timeline_token_={};session_=nullptr;
    }
    static std::wstring source_name(const winrt::hstring& id) {
        if(id==L"Stillbar.Yandex.Media")return L"Stillbar Media";
        try{const auto info=winrt::Windows::ApplicationModel::AppInfo::GetFromAppUserModelId(id);if(info)if(const auto display=info.DisplayInfo()){const auto name=display.DisplayName();if(!name.empty())return std::wstring(name);}}catch(...){}
        const auto file=std::filesystem::path(std::wstring(id)).filename().stem().wstring();
        const auto separator=file.find_first_of(L"._!");return file.substr(0,separator);
    }
    void queue_refresh(bool metadata) noexcept {
        if(closed_)return;if(metadata)metadata_requested_=true;
        if(refresh_queued_.exchange(true))return;
        const auto weak=weak_from_this();
        try{dispatcher_.RunAsync(winrt::Windows::UI::Core::CoreDispatcherPriority::Low,[weak]{if(auto self=weak.lock();self && !self->closed_){self->refresh_queued_=false;self->refresh(self->metadata_requested_.exchange(false));}});}catch(...){refresh_queued_=false;}
    }
    void queue_source(std::uint64_t id) noexcept {
        if(closed_)return;const auto weak=weak_from_this();
        try{dispatcher_.RunAsync(winrt::Windows::UI::Core::CoreDispatcherPriority::Low,[weak,id]{if(auto self=weak.lock();self && !self->closed_)if(auto source=self->find_source(id)){
            source->dirty=true;
            if(source->session!=self->session_)self->queue_refresh(false);
        }});}catch(...){}
    }
    void queue_playback(std::uint64_t id) noexcept {
        if(closed_)return;const auto weak=weak_from_this();
        try{dispatcher_.RunAsync(winrt::Windows::UI::Core::CoreDispatcherPriority::Low,[weak,id]{if(auto self=weak.lock();self && !self->closed_)if(auto source=self->find_source(id)){
            try{if(const auto state=playback_state(source->session.GetPlaybackInfo())){
                source->history.observe(*state);
            }}catch(...){}
            self->queue_refresh(false);
        }});}catch(...){}
    }
    void sync_sources() {
        const auto current=manager_.GetSessions();
        if(!current)throw winrt::hresult_error(E_PENDING);
        for(auto item=sources_.begin();item!=sources_.end();){
            bool present=false;for(const auto& value:current)if(value==item->session){present=true;break;}
            if(present){++item;continue;}
            try{item->session.MediaPropertiesChanged(item->metadata_token);item->session.PlaybackInfoChanged(item->playback_token);if(item->loading)item->loading.Cancel();}catch(...){}
            item=sources_.erase(item);
        }
        for(const auto& value:current)try{
            if(!value)continue;
            bool known=false;for(const auto& source:sources_)if(source.session==value){known=true;break;}
            if(known)continue;
            Source source;source.session=value;source.info.id=++next_source_;source.info.name=L"Медиаплеер";
            sources_.push_back(std::move(source));
        }catch(...){retry(winrt::to_hresult());}
        for(auto& source:sources_)if(source.info.app_id.empty())try{
            const auto id=source.session.SourceAppUserModelId();source.info.app_id=id;
            if(!id.empty()){const auto name=source_name(id);if(!name.empty())source.info.name=name;}
        }catch(...){retry(winrt::to_hresult());}
        for(auto& source:sources_)try{
            const auto id=source.info.id;const auto weak=weak_from_this();
            if(!source.metadata_token.value)source.metadata_token=source.session.MediaPropertiesChanged([weak,id](auto&&,auto&&){if(auto self=weak.lock())self->queue_source(id);});
            if(!source.playback_token.value)source.playback_token=source.session.PlaybackInfoChanged([weak,id](auto&&,auto&&){if(auto self=weak.lock())self->queue_playback(id);});
        }catch(...){retry(winrt::to_hresult());}
        if(selected_source_ && !find_source(selected_source_)){++snapshot_.manual_losses;selected_source_=0;}
    }
    winrt::fire_and_forget load_source_title(std::uint64_t id) {
        const auto held=shared_from_this();auto work=tasks_->begin();if(!work)co_return;
        try{
            auto source=find_source(id);if(!source || source->loading || !browsing_)co_return;
            auto operation=source->session.TryGetMediaPropertiesAsync();source->loading=operation;source->dirty=false;work.track(operation);
            const auto properties=co_await operation;
            if(closed_)co_return;
            if(!properties)throw winrt::hresult_error(E_PENDING);
            if(auto current=find_source(id)){current->loading=nullptr;current->info.title=properties.Title();if(browsing_){publish();if(current->dirty)load_source_title(id);}}
        }catch(...){if(!closed_)if(auto source=find_source(id))source->loading=nullptr;}
    }
    void update_playback(PlaybackInfo playback=nullptr,std::optional<PlaybackState> observed=std::nullopt) {
        snapshot_.playback_valid=false;
        if(!session_)return;if(!playback)playback=session_.GetPlaybackInfo();const auto state=observed?observed:playback_state(playback);
        if(!state){retry(E_PENDING);return;}
        if(*state==PlaybackState::changing && snapshot_.playback!=PlaybackState::changing)visibility_.begin_transition(MediaTimeline::Clock::now(),true);
        snapshot_.playback_valid=true;snapshot_.playback=*state;snapshot_.playing=snapshot_.playback==PlaybackState::playing;
        const auto options=playback_options(playback,snapshot_.playing,[this](MediaReadFailure failure){retry(failure==MediaReadFailure::missing?E_PENDING:static_cast<HRESULT>(winrt::to_hresult()));});
        snapshot_.previous=options.previous;snapshot_.next=options.next;snapshot_.toggle=options.toggle;
        snapshot_.timeline.playing=snapshot_.playing;snapshot_.timeline.can_seek=options.can_seek;snapshot_.timeline.rate=options.rate;
    }
    void update_timeline() noexcept {
        try{
            if(!session_)return;const auto value=session_.GetTimelineProperties();if(!value){snapshot_.timeline_valid=false;retry(E_PENDING);return;}auto& timeline=snapshot_.timeline;
            timeline.start=value.StartTime().count();timeline.end=value.EndTime().count();timeline.minimum=value.MinSeekTime().count();timeline.maximum=value.MaxSeekTime().count();timeline.position=value.Position().count();timeline.observed=MediaTimeline::Clock::now();
            snapshot_.timeline_valid=true;
            const auto updated=value.LastUpdatedTime(),now=winrt::clock::now();
            if(timeline.playing && updated.time_since_epoch().count()>0 && now>updated && timeline.known_duration()){
                timeline.observed-=std::chrono::duration_cast<MediaTimeline::Clock::duration>(now-updated);timeline.position=timeline.current();timeline.observed=MediaTimeline::Clock::now();
            }
        }catch(...){snapshot_.timeline_valid=false;retry(winrt::to_hresult());}
    }
    winrt::fire_and_forget load_metadata(Session session,std::uint64_t generation) {
        const auto held=shared_from_this();auto work=tasks_->begin();if(!work)co_return;
        try{
            auto operation=session.TryGetMediaPropertiesAsync();metadata_operation_=operation;work.track(operation);const auto properties=co_await operation;
            if(closed_ || generation!=generation_)co_return;
            if(!properties)throw winrt::hresult_error(E_PENDING);
            const std::wstring title(properties.Title()),artist(properties.Artist());
            const bool changed=title!=snapshot_.title || artist!=snapshot_.artist;
            if(changed && !title.empty())visibility_.begin_transition(MediaTimeline::Clock::now(),true);
            if(!title.empty() || !visibility_.visible()){
                snapshot_.title=title;snapshot_.artist=artist;
                if(changed){snapshot_.thumbnail=nullptr;snapshot_.artwork_revision=++artwork_revision_;}
            }
            for(auto& source:sources_)if(source.session==session){source.info.title=title;source.dirty=false;}
            refresh(false);if(generation!=generation_)co_return;
            winrt::Windows::Storage::Streams::IRandomAccessStream stream{nullptr};
            if(const auto thumbnail=properties.Thumbnail())try{auto read=thumbnail.OpenReadAsync();metadata_operation_=read;work.track(read);stream=co_await read;}catch(...){}
            if(closed_ || generation!=generation_)co_return;
            metadata_operation_=nullptr;snapshot_.thumbnail=stream;snapshot_.artwork_revision=++artwork_revision_;publish();
        }catch(...){if(!closed_ && generation==generation_){metadata_operation_=nullptr;retry(winrt::to_hresult(),true);}}
    }
    void defer_loss() {
        if(session_){detach_session();++generation_;snapshot_.media_revision=generation_;if(metadata_operation_)try{metadata_operation_.Cancel();}catch(...){}metadata_operation_=nullptr;}
        snapshot_.available=false;snapshot_.playback_valid=false;snapshot_.playing=false;snapshot_.playback=PlaybackState::closed;snapshot_.active_source=0;publish();
    }
    void refresh(bool metadata) noexcept {
        try{
            if(closed_ || !manager_)return;if(retry_timer_)retry_timer_.Stop();
            const auto weak=weak_from_this();
            try{if(!current_token_.value)current_token_=manager_.CurrentSessionChanged([weak](auto&&,auto&&){if(auto self=weak.lock())self->queue_refresh(false);});}catch(...){retry(winrt::to_hresult());}
            try{if(!sessions_token_.value)sessions_token_=manager_.SessionsChanged([weak](auto&&,auto&&){if(auto self=weak.lock())self->queue_refresh(false);});}catch(...){retry(winrt::to_hresult());}
            sync_sources();
            Session system_current{nullptr};
            try{system_current=manager_.GetCurrentSession();}catch(...){retry(winrt::to_hresult());}
            std::vector<MediaCandidate> candidates;candidates.reserve(sources_.size());
            std::vector<PlaybackInfo> observations;observations.reserve(sources_.size());std::vector<std::optional<PlaybackState>> states;states.reserve(sources_.size());std::uint64_t current_id=0,previous_id=0;
            for(auto& source:sources_){
                bool playing=false,resumable=false;PlaybackInfo info{nullptr};std::optional<PlaybackState> observed;
                try{
                    info=source.session.GetPlaybackInfo();
                    if(const auto state=playback_state(info)){
                        observed=state;
                        source.history.observe(*state);playing=*state==PlaybackState::playing;
                        if(source.info.id==selected_source_ && !playing){
                            MediaTimeline timeline;bool valid=false;
                            if(const auto value=source.session.GetTimelineProperties()){timeline.start=value.StartTime().count();timeline.end=value.EndTime().count();timeline.position=value.Position().count();valid=true;}
                            const auto reason=source.history.reason(*state,timeline,valid);resumable=reason==MediaHideReason::none || (reason==MediaHideReason::changing && !visibility_.expired());
                        }
                    }else retry(E_PENDING);
                }catch(...){retry(winrt::to_hresult());}
                observations.push_back(info);states.push_back(observed);candidates.push_back({source.info.id,playing,resumable});if(source.session==system_current)current_id=source.info.id;if(source.session==session_)previous_id=source.info.id;
            }
            const auto selection=choose_media_source(candidates,selected_source_,current_id,previous_id);selected_source_=selection.requested;
            snapshot_.active_source=selection.active;
            Session current{nullptr};if(const auto selected=find_source(selection.active))current=selected->session;
            if(!current){defer_loss();return;}
            if(loss_timer_)loss_timer_.Stop();
            if(current!=session_){
                detach_session();session_=current;metadata=true;
                visibility_.begin_transition(MediaTimeline::Clock::now(),true);snapshot_.timeline=MediaTimeline{};snapshot_.timeline_valid=false;
            }
            if(!properties_token_.value)properties_token_=session_.MediaPropertiesChanged([weak](auto&&,auto&&){if(auto self=weak.lock())self->queue_refresh(true);});
            if(!timeline_token_.value)timeline_token_=session_.TimelinePropertiesChanged([weak](auto&&,auto&&){if(auto self=weak.lock())self->queue_refresh(false);});
            snapshot_.available=true;
            if(metadata){visibility_.begin_transition();if(metadata_operation_)try{metadata_operation_.Cancel();}catch(...){}metadata_operation_=nullptr;snapshot_.media_revision=++generation_;snapshot_.seek_pending=false;snapshot_.seek_failed=false;}
            PlaybackInfo observation{nullptr};std::optional<PlaybackState> state;for(std::size_t index=0;index<candidates.size();++index)if(candidates[index].id==selection.active){observation=observations[index];state=states[index];break;}
            if(observation && state)update_playback(observation,state);else{snapshot_.playback_valid=false;snapshot_.playing=false;retry(E_PENDING);}
            update_timeline();publish();if(metadata)load_metadata(current,generation_);
            if(browsing_)for(auto& source:sources_)if(source.session!=session_ && source.dirty && !source.loading)load_source_title(source.info.id);
        }catch(...){snapshot_.playback_valid=false;retry(winrt::to_hresult(),metadata);try{publish();}catch(...){}}
    }
public:
    MediaService(winrt::Windows::UI::Core::CoreDispatcher dispatcher,std::shared_ptr<AsyncTasks> tasks,std::function<void(MediaSnapshot)> changed):dispatcher_(dispatcher),tasks_(std::move(tasks)),changed_(std::move(changed)){}
    winrt::fire_and_forget start() {
        if(closed_ || starting_ || manager_)co_return;starting_=true;
        const auto held=shared_from_this();auto work=tasks_->begin();if(!work)co_return;
        try{auto request=Manager::RequestAsync();work.track(request);auto manager=co_await request;if(closed_)co_return;if(!manager)throw winrt::hresult_error(E_PENDING);manager_=manager;refresh(true);
        }catch(...){if(!closed_)retry(winrt::to_hresult(),true);}starting_=false;
    }
    void browse_sources(bool enabled)noexcept{try{if(closed_)return;browsing_=enabled;if(enabled){sync_sources();publish();for(auto& source:sources_)if(source.session!=session_)load_source_title(source.info.id);}}catch(...){}}
    void select_source(std::uint64_t id)noexcept{try{if(closed_)return;if(id && !find_source(id))return;++snapshot_.source_requests;selected_source_=id;refresh(true);}catch(...){}}
    void seek_elapsed(std::int64_t elapsed,std::uint64_t revision,std::uint64_t source) {
        if(closed_ || revision!=generation_ || source!=snapshot_.active_source)return;
        const auto& timeline=snapshot_.timeline;
        const double ratio=timeline.known_duration()?static_cast<double>(static_cast<long double>(elapsed)/(static_cast<long double>(timeline.end)-timeline.start)):0;
        if(const auto target=timeline.seek(ratio))seek(*target,revision);
    }
    winrt::fire_and_forget act(MediaAction action) {
        const auto held=shared_from_this();auto work=tasks_->begin();if(!work)co_return;
        try{const auto session=session_;if(closed_ || !session || !snapshot_.visible || !snapshot_.available || !snapshot_.playback_valid)co_return;if((action==MediaAction::previous && !snapshot_.previous) || (action==MediaAction::next && !snapshot_.next) || (action==MediaAction::toggle && !snapshot_.toggle))co_return;if(action!=MediaAction::toggle)visibility_.begin_transition(MediaTimeline::Clock::now(),true);const auto operation=action==MediaAction::previous?session.TrySkipPreviousAsync():action==MediaAction::next?session.TrySkipNextAsync():session.TryTogglePlayPauseAsync();work.track(operation);co_await operation;if(!closed_ && session==session_)queue_refresh(action!=MediaAction::toggle);}catch(...){}
    }
    winrt::fire_and_forget seek(std::int64_t position,std::uint64_t revision) {
        const auto held=shared_from_this();auto work=tasks_->begin();if(!work)co_return;
        try{
            if(closed_ || !session_ || !snapshot_.visible || !snapshot_.available || revision!=generation_ || snapshot_.seek_pending)co_return;update_playback();update_timeline();const auto target=snapshot_.timeline.seek(snapshot_.timeline.ratio(position));
            if(!target){snapshot_.seek_failed=true;publish();co_return;}
            snapshot_.seek_pending=true;snapshot_.seek_failed=false;publish();const auto operation=session_.TryChangePlaybackPositionAsync(*target);work.track(operation);const auto accepted=co_await operation;
            if(closed_ || revision!=generation_)co_return;snapshot_.seek_pending=false;snapshot_.seek_failed=!accepted;refresh(false);
        }catch(...){if(!closed_ && revision==generation_){snapshot_.seek_pending=false;snapshot_.seek_failed=true;publish();}}
    }
    void close() noexcept {
        closed_=true;++generation_;detach_session();
        try{if(manager_ && current_token_.value)manager_.CurrentSessionChanged(current_token_);}catch(...){}
        try{if(manager_ && sessions_token_.value)manager_.SessionsChanged(sessions_token_);}catch(...){}
        try{if(loss_timer_){loss_timer_.Stop();loss_timer_.Tick(loss_tick_);}}catch(...){}
        try{if(retry_timer_){retry_timer_.Stop();retry_timer_.Tick(retry_tick_);}}catch(...){}retry_timer_=nullptr;
        for(const auto& source:sources_)try{source.session.MediaPropertiesChanged(source.metadata_token);source.session.PlaybackInfoChanged(source.playback_token);}catch(...){}
        sources_.clear();loss_timer_=nullptr;current_token_={};sessions_token_={};manager_=nullptr;metadata_operation_=nullptr;snapshot_=MediaSnapshot{};changed_={};tasks_->close();dispatcher_=nullptr;
    }
};
}
