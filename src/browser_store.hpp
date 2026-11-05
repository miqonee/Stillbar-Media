#pragma once
#include <string>
#include <string_view>

namespace islands::browser {
inline constexpr std::wstring_view store_extension_id=L"";
inline constexpr std::wstring_view store_listing_url=L"";
inline bool valid_store_listing(std::wstring_view url){
    constexpr std::wstring_view prefix=L"https://addons.opera.com/en/extensions/details/";
    if(!url.starts_with(prefix) || !url.ends_with(L"/"))return false;
    const auto slug=url.substr(prefix.size(),url.size()-prefix.size()-1);
    return !slug.empty() && slug.find_first_not_of(L"abcdefghijklmnopqrstuvwxyz0123456789-")==std::wstring_view::npos;
}
inline bool store_available(){return store_extension_id.size()==32 && store_extension_id.find_first_not_of(L"abcdefghijklmnop")==std::wstring_view::npos && valid_store_listing(store_listing_url);}
inline std::wstring store_url(){return store_available()?std::wstring(store_listing_url):L"";}
}
