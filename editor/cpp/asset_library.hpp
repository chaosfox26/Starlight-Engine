#pragma once
#include "platform.hpp"
namespace starlight::editor {
struct AssetEntry { fs::path path; std::wstring label; };
struct AssetIndex { std::vector<AssetEntry> entries; bool partial = false; size_t errors = 0; };
// `cancel` (optional) is polled between entries so a scan that is no longer wanted (the editor is closing) stops at once and returns what it has, marked partial.
AssetIndex scan_asset_library(const fs::path&, const std::atomic<bool>* cancel = nullptr);
std::vector<size_t> filter_asset_library(const AssetIndex&, std::wstring_view, std::wstring_view extension = {});
}
