#pragma once
// For fixes whose last step is a call made from code the check programs cannot run (the editor window's message handlers):
// the check counts how often that call still appears in the source file. Run from the repository root; an unreadable file counts as zero.
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
inline std::string source_text(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    return std::string{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
// The text from the first `from` to the next `to` after it; empty when either is missing.
inline std::string source_span(const std::filesystem::path& file, std::string_view from, std::string_view to) {
    const auto text = source_text(file);
    const auto begin = text.find(from);
    const auto end = begin == std::string::npos ? begin : text.find(to, begin + from.size());
    return end == std::string::npos ? std::string() : text.substr(begin, end - begin);
}
inline size_t source_occurrences(const std::filesystem::path& file, std::string_view needle) {
    const auto text = source_text(file);
    size_t count = 0;
    for (size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + needle.size())) ++count;
    return count;
}
