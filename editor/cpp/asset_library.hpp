#pragma once
#include "platform.hpp"
namespace starlight::editor {
struct AssetEntry { fs::path path; std::wstring label; };
struct AssetIndex { std::vector<AssetEntry> entries; bool partial = false; size_t errors = 0; };
AssetIndex scan_asset_library(const fs::path&);
std::vector<size_t> filter_asset_library(const AssetIndex&, std::wstring_view, std::wstring_view extension = {});
}
