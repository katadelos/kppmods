#pragma once

namespace km::reactNative {
struct BundleApi {
    const char *load;
    const char *evaluate;
};
// Each entry is a complete load/evaluate contract, indexed by C++ string ABI.
inline constexpr BundleApi bridge[] = {
    {"_ZN11reactNative6Bridge10loadBundleERKSs",
     "_ZN11reactNative6Bridge20loadScriptFromStringEOSs"},
    {"_ZN11reactNative6Bridge10loadBundleERKNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEE",
     "_ZN11reactNative6Bridge20loadScriptFromStringEONSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEE"},
};
inline constexpr BundleApi application[] = {
    {"_ZN11reactNative11ReactNative15loadApplicationERKSs",
     "_ZN11reactNative11ReactNative21loadApplicationBundleEOSs"},
    {"_ZN11reactNative11ReactNative15loadApplicationERKNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEE",
     "_ZN11reactNative11ReactNative21loadApplicationBundleEONSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEE"},
};
} // namespace km::reactNative
