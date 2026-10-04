#pragma once
#include <cstdint>
namespace starlight {
inline constexpr int maximumRenderDimension = 16384;
inline constexpr int64_t maximumRenderPixels = int64_t(15360)*8640;
inline constexpr bool valid_render_size(int width,int height) {
    return width>0 && height>0 && width<=maximumRenderDimension &&
           height<=maximumRenderDimension && int64_t(width)*height<=maximumRenderPixels;
}
inline constexpr const char* renderSizeError =
    "Resolution exceeds the 16K UHD pixel budget (15360 x 8640) or 16384 pixels per dimension";
}
