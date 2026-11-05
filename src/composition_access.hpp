#pragma once
#include <winrt/Windows.UI.Composition.h>

namespace islands {
inline winrt::Windows::UI::Composition::ICompositionObject2 animation_object(winrt::Windows::UI::Composition::Visual const& visual) {
    if(!visual)throw winrt::hresult_error(RO_E_CLOSED);
    const auto object=visual.try_as<winrt::Windows::UI::Composition::ICompositionObject2>();
    if(!object)throw winrt::hresult_error(RO_E_CLOSED);
    return object;
}
}
