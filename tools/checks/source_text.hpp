#pragma once
// For fixes whose last step is a call made from code the check programs cannot run (the editor window's message handlers):
// the check counts how often that call still appears in the source file. Run from the repository root; an unreadable file counts as zero.
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
inline size_t source_occurrences(const std::filesystem::path& file, std::string_view needle) {
    std::ifstream in(file, std::ios::binary);
    const std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    size_t count = 0;
    for (size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + needle.size())) ++count;
    return count;
}
