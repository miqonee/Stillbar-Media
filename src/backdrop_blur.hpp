#pragma once
#include <algorithm>
#include <d2d1effects.h>
#include <dwmapi.h>
#include <windows.graphics.effects.interop.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Graphics.Effects.h>
#include <winrt/Windows.UI.Composition.h>
#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Hosting.h>
#include <winrt/Windows.UI.Xaml.Media.h>
#include <winrt/Windows.UI.ViewManagement.h>

namespace islands {
inline float blur_strength(float amount){const float value=std::clamp(amount/48.f,0.f,1.f);return value*value*(3-2*value);}
class BlurEffect : public winrt::implements<BlurEffect,winrt::Windows::Graphics::Effects::IGraphicsEffect,winrt::Windows::Graphics::Effects::IGraphicsEffectSource,ABI::Windows::Graphics::Effects::IGraphicsEffectD2D1Interop> {
    winrt::hstring name_=L"Blur";
    winrt::Windows::Graphics::Effects::IGraphicsEffectSource source_;
    float amount_;
public:
    BlurEffect(float amount,winrt::Windows::Graphics::Effects::IGraphicsEffectSource source):source_(source),amount_(amount){}
    winrt::hstring Name()const{return name_;}void Name(winrt::hstring const& value){name_=value;}
    HRESULT __stdcall GetEffectId(GUID* id)noexcept final{if(!id)return E_POINTER;*id=CLSID_D2D1GaussianBlur;return S_OK;}
    HRESULT __stdcall GetNamedPropertyMapping(LPCWSTR name,UINT* index,ABI::Windows::Graphics::Effects::GRAPHICS_EFFECT_PROPERTY_MAPPING* mapping)noexcept final {
        if(!name || !index || !mapping)return E_POINTER;if(wcscmp(name,L"StandardDeviation"))return E_INVALIDARG;
        *index=0;*mapping=ABI::Windows::Graphics::Effects::GRAPHICS_EFFECT_PROPERTY_MAPPING_DIRECT;return S_OK;
    }
    HRESULT __stdcall GetPropertyCount(UINT* count)noexcept final{if(!count)return E_POINTER;*count=3;return S_OK;}
    HRESULT __stdcall GetProperty(UINT index,ABI::Windows::Foundation::IPropertyValue** value)noexcept final {
        if(!value)return E_POINTER;*value=nullptr;
        try{
            winrt::Windows::Foundation::IInspectable property{nullptr};
            if(index==0)property=winrt::Windows::Foundation::PropertyValue::CreateSingle(amount_);
            else if(index==1)property=winrt::Windows::Foundation::PropertyValue::CreateUInt32(D2D1_GAUSSIANBLUR_OPTIMIZATION_BALANCED);
            else if(index==2)property=winrt::Windows::Foundation::PropertyValue::CreateUInt32(D2D1_BORDER_MODE_HARD);
            else return E_INVALIDARG;
            property.as<ABI::Windows::Foundation::IPropertyValue>().copy_to(value);return S_OK;
        }catch(...){return winrt::to_hresult();}
    }
    HRESULT __stdcall GetSourceCount(UINT* count)noexcept final{if(!count)return E_POINTER;*count=1;return S_OK;}
    HRESULT __stdcall GetSource(UINT index,ABI::Windows::Graphics::Effects::IGraphicsEffectSource** source)noexcept final {
        if(!source)return E_POINTER;*source=nullptr;if(index)return E_INVALIDARG;source_.as<ABI::Windows::Graphics::Effects::IGraphicsEffectSource>().copy_to(source);return S_OK;
    }
};
inline winrt::Windows::UI::Composition::CompositionEffectBrush make_backdrop_blur(winrt::Windows::UI::Composition::Compositor compositor,float amount,bool host=false) {
    const auto effect=winrt::make<BlurEffect>(amount,winrt::Windows::UI::Composition::CompositionEffectSourceParameter{L"Backdrop"});
    auto brush=compositor.CreateEffectFactory(effect,{L"Blur.StandardDeviation"}).CreateBrush();
    brush.SetSourceParameter(L"Backdrop",host?compositor.CreateHostBackdropBrush():compositor.CreateBackdropBrush());return brush;
}
class HostBackdropWindow {
    HWND window_=nullptr;BOOL original_=FALSE;bool known_=false;
    HRESULT error_=S_OK;
public:
    bool enable(HWND window)noexcept {
        if(window==window_)return window_!=nullptr;
        close();if(!window)return false;
        original_=FALSE;
        known_=SUCCEEDED(DwmGetWindowAttribute(window,DWMWA_USE_HOSTBACKDROPBRUSH,&original_,sizeof(original_)));
        if(!known_)original_=FALSE;
        const BOOL enabled=TRUE;error_=DwmSetWindowAttribute(window,DWMWA_USE_HOSTBACKDROPBRUSH,&enabled,sizeof(enabled));
        if(FAILED(error_))return false;window_=window;return true;
    }
    bool enabled()const{return window_!=nullptr;}
    HRESULT error()const{return error_;}
    void close()noexcept{if(window_ && IsWindow(window_)){DWORD process=0;GetWindowThreadProcessId(window_,&process);if(process==GetCurrentProcessId())DwmSetWindowAttribute(window_,DWMWA_USE_HOSTBACKDROPBRUSH,&original_,sizeof(original_));}window_=nullptr;known_=false;}
};
struct BackdropPaint : winrt::Windows::UI::Xaml::Media::XamlCompositionBrushBaseT<BackdropPaint> {
    explicit BackdropPaint(winrt::Windows::UI::Composition::CompositionBrush brush){CompositionBrush(brush);}
    void OnConnected(){}
    void OnDisconnected(){}
};
class BackdropBlur {
    winrt::Windows::UI::Xaml::Controls::Border host_{nullptr};
    winrt::Windows::UI::Composition::SpriteVisual visual_{nullptr};
    winrt::Windows::UI::Composition::CompositionEffectBrush brush_{nullptr};
    winrt::Windows::UI::Xaml::Media::Brush paint_{nullptr};
    HRESULT error_=S_OK;
    bool host_backdrop_=false;
public:
    void update(winrt::Windows::UI::Xaml::Controls::Border host,float amount,double radius,float opacity=1,bool host_backdrop=false)noexcept {
        using Preview=winrt::Windows::UI::Xaml::Hosting::ElementCompositionPreview;
        try{
            if(host_backdrop!=host_backdrop_){close();host_backdrop_=host_backdrop;}host_=host;
            if(!winrt::Windows::UI::ViewManagement::UISettings{}.AdvancedEffectsEnabled()){close();return;}
            if(amount<=0 || opacity<=0){close();return;}
            if(!brush_){
                const auto compositor=Preview::GetElementVisual(host).Compositor();
                brush_=make_backdrop_blur(compositor,amount,host_backdrop);
                if(host_backdrop){paint_=winrt::make<BackdropPaint>(brush_);host.Background(paint_);}
                else{visual_=compositor.CreateSpriteVisual();visual_.Brush(brush_);Preview::SetElementChildVisual(host,visual_);}
            }
            brush_.Properties().InsertScalar(L"Blur.StandardDeviation",amount);
            host.CornerRadius({radius,radius,radius,radius});
            if(paint_){paint_.Opacity(blur_strength(amount)*opacity);return;}
            visual_.Opacity(blur_strength(amount)*opacity);
            const auto size=host.ActualSize();visual_.Size(size);
            auto clip=visual_.Compositor().CreateRectangleClip();clip.Right(size.x);clip.Bottom(size.y);
            const float corner=static_cast<float>(radius);clip.TopLeftRadius({corner,corner});clip.TopRightRadius({corner,corner});clip.BottomLeftRadius({corner,corner});clip.BottomRightRadius({corner,corner});visual_.Clip(clip);
        }catch(...){error_=winrt::to_hresult();close();}
    }
    HRESULT error()const{return error_;}
    float width()const{return paint_ && host_?static_cast<float>(host_.ActualWidth()):visual_?visual_.Size().x:0;}
    float height()const{return paint_ && host_?static_cast<float>(host_.ActualHeight()):visual_?visual_.Size().y:0;}
    void close()noexcept{try{if(host_){if(paint_)host_.Background(nullptr);if(visual_)winrt::Windows::UI::Xaml::Hosting::ElementCompositionPreview::SetElementChildVisual(host_,nullptr);}}catch(...){}paint_=nullptr;visual_=nullptr;brush_=nullptr;host_=nullptr;}
};
}
