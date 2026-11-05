#pragma once
#include <windows.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

namespace islands {
class AudioMeter {
    struct Endpoint {
        std::wstring id;
        Microsoft::WRL::ComPtr<IAudioMeterInformation> meter;
        Microsoft::WRL::ComPtr<IAudioEndpointVolume> volume;
    };
    Microsoft::WRL::ComPtr<IMMDeviceEnumerator> devices_;
    std::vector<Endpoint> endpoints_;
    ULONGLONG checked_=0;
    HRESULT last_error_=S_OK;
    HRESULT refresh() {
        checked_=GetTickCount64();
        if(!devices_){const auto result=CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&devices_));if(FAILED(result))return result;}
        Microsoft::WRL::ComPtr<IMMDeviceCollection> collection;auto result=devices_->EnumAudioEndpoints(eRender,DEVICE_STATE_ACTIVE,&collection);if(FAILED(result))return result;
        UINT count=0;result=collection->GetCount(&count);if(FAILED(result))return result;
        std::vector<Endpoint> active;active.reserve(std::min(count,32u));
        for(UINT index=0;index<std::min(count,32u);++index){
            Microsoft::WRL::ComPtr<IMMDevice> device;if(FAILED(collection->Item(index,&device)))continue;
            LPWSTR raw=nullptr;if(FAILED(device->GetId(&raw)))continue;const std::wstring id(raw);CoTaskMemFree(raw);
            const auto existing=std::find_if(endpoints_.begin(),endpoints_.end(),[&](const auto& value){return value.id==id;});
            if(existing!=endpoints_.end()){active.push_back(*existing);continue;}
            Endpoint endpoint;endpoint.id=id;
            if(FAILED(device->Activate(__uuidof(IAudioMeterInformation),CLSCTX_INPROC_SERVER,nullptr,reinterpret_cast<void**>(endpoint.meter.GetAddressOf()))))continue;
            device->Activate(__uuidof(IAudioEndpointVolume),CLSCTX_INPROC_SERVER,nullptr,reinterpret_cast<void**>(endpoint.volume.GetAddressOf()));active.push_back(std::move(endpoint));
        }
        endpoints_=std::move(active);return endpoints_.empty()?HRESULT_FROM_WIN32(ERROR_NOT_FOUND):S_OK;
    }
public:
    HRESULT sample(float& level)noexcept {
        level=0;
        try{
            if(endpoints_.empty() && checked_ && GetTickCount64()-checked_<1000)return last_error_;
            if(endpoints_.empty() || GetTickCount64()-checked_>1500){last_error_=refresh();if(FAILED(last_error_))return last_error_;}
            bool measured=false;
            for(auto& endpoint:endpoints_){
                float peak=0;const auto result=endpoint.meter->GetPeakValue(&peak);if(FAILED(result)){last_error_=result;continue;}
                measured=true;if(endpoint.volume){BOOL muted=FALSE;float gain=1;endpoint.volume->GetMute(&muted);endpoint.volume->GetMasterVolumeLevelScalar(&gain);peak=muted?0:peak*gain;}
                if(std::isfinite(peak))level=std::max(level,std::clamp(peak,0.f,1.f));
            }
            if(measured)return S_OK;endpoints_.clear();checked_=GetTickCount64();return last_error_;
        }catch(...){return E_OUTOFMEMORY;}
    }
    void close()noexcept{endpoints_.clear();devices_.Reset();checked_=0;}
};
class AudioHistory {
    std::array<float,9> values_{};
    float envelope_=0;
public:
    const auto& update(float peak) {
        const float level=std::isfinite(peak) && peak>0.001f?std::clamp((20.f*std::log10(peak)+60.f)/60.f,0.f,1.f):0;
        envelope_=std::max(level,envelope_*.82f);
        std::move(values_.begin()+1,values_.end(),values_.begin());values_.back()=envelope_;return values_;
    }
    void reset(){values_.fill(0);envelope_=0;}
};
}
