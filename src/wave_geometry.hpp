#pragma once
#include <d2d1.h>
#include <windows.graphics.interop.h>
#include <winrt/Windows.Graphics.h>
#include <winrt/Windows.UI.Composition.h>
#include <cmath>

namespace islands {
class WaveSource : public winrt::implements<WaveSource,winrt::Windows::Graphics::IGeometrySource2D,ABI::Windows::Graphics::IGeometrySource2DInterop> {
    winrt::com_ptr<ID2D1Geometry> geometry_;
public:
    explicit WaveSource(winrt::com_ptr<ID2D1Geometry> geometry):geometry_(std::move(geometry)){}
    HRESULT __stdcall GetGeometry(ID2D1Geometry** value)noexcept final{if(!value)return E_POINTER;geometry_.copy_to(value);return S_OK;}
    HRESULT __stdcall TryGetGeometryUsingFactory(ID2D1Factory* factory,ID2D1Geometry** value)noexcept final {
        if(!value)return E_POINTER;*value=nullptr;if(!factory)return E_INVALIDARG;
        winrt::com_ptr<ID2D1Factory> owner;geometry_->GetFactory(owner.put());if(owner.get()==factory)geometry_.copy_to(value);return S_OK;
    }
};
inline winrt::com_ptr<ID2D1PathGeometry> wave_geometry(ID2D1Factory* factory,float baseline,float amplitude,float phase) {
    constexpr float tau=6.2831853071795864769f,step=250;
    winrt::com_ptr<ID2D1PathGeometry> path;winrt::check_hresult(factory->CreatePathGeometry(path.put()));
    winrt::com_ptr<ID2D1GeometrySink> sink;winrt::check_hresult(path->Open(sink.put()));
    const auto height=[&](float x){return baseline+amplitude*std::sin(phase+x*tau/1000);};
    const auto slope=[&](float x){return amplitude*tau/1000*std::cos(phase+x*tau/1000);};
    sink->BeginFigure({0,height(0)},D2D1_FIGURE_BEGIN_FILLED);
    for(unsigned index=0;index<8;++index){const float start=index*step,end=start+step;sink->AddBezier({{start+step/3,height(start)+slope(start)*step/3},{end-step/3,height(end)-slope(end)*step/3},{end,height(end)}});}
    sink->AddLine({2000,1000});sink->AddLine({0,1000});sink->EndFigure(D2D1_FIGURE_END_CLOSED);winrt::check_hresult(sink->Close());return path;
}
}
