#pragma once
#include "platform.hpp"
namespace starlight::editor {
json inspect_scene(const fs::path &);
json apply_commands(const fs::path &);
json describe_commands();
json invoke_feature(std::string_view);
int run_commands(int, wchar_t **);
} // namespace starlight::editor
