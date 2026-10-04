#pragma once
#include <algorithm>
#include <cmath>

namespace starlight::audit {
struct VisibleResolution {
  int width, height;
  bool supported, fullscreen;
};
// Bound visible audit workloads without requiring a 4K desktop or upscaling
// a smaller desktop. Tiny renderer unit fixtures use their own resolutions.
inline VisibleResolution visible_resolution(int displayWidth, int displayHeight) {
  if (displayWidth <= 0 || displayHeight <= 0) return {0,0,false,false};
  // 4K is a workload budget, not an aspect-ratio restriction. Preserve native
  // ultrawide dimensions when their total pixel count fits that budget.
  const double scale = std::min({1.0, 16384.0/std::max(displayWidth,displayHeight),
      std::sqrt((3840.0*2160.0)/(double(displayWidth)*displayHeight))});
  const int width = int(std::floor(displayWidth*scale));
  const int height = int(std::floor(displayHeight*scale));
  return {width, height, std::min(width,height) >= 720 && double(width)*height >= 1280.0*720,
          width == displayWidth && height == displayHeight};
}
}
