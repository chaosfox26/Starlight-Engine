#pragma once
#include "named_colors.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <vector>
namespace starlight::editor {
inline std::array<double, 3> color_rgb(std::string text) {
    auto begin = text.find_first_not_of(" \t\r\n"), end = text.find_last_not_of(" \t\r\n");
    if (begin == std::string::npos)
        throw std::runtime_error("Empty color");
    text = text.substr(begin, end - begin + 1);
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    if (auto it = named_colors.find(text); it != named_colors.end())
        text = it->second;
    if (text.starts_with("sc#")) {
        std::replace(text.begin(), text.end(), ',', ' ');
        std::istringstream stream(text.substr(3));
        std::vector<double> values;
        double n;
        while (stream >> n)
            values.push_back(n);
        if (!stream.eof() || (values.size() != 3 && values.size() != 4))
            throw std::runtime_error("Invalid scRGB color");
        std::array<double, 3> out{};
        for (size_t i = 0; i < 3; i++) {
            double v = values[values.size() - 3 + i];
            if (!std::isfinite(v))
                throw std::runtime_error("Invalid color component");
            v = std::clamp(v, 0., 1.);
            out[i] =
                std::round((v <= .0031308 ? v * 12.92 : 1.055 * std::pow(v, 1 / 2.4) - .055) * 255) / 255;
        }
        return out;
    }
    if (text[0] != '#')
        throw std::runtime_error("Unknown color");
    text.erase(0, 1);
    if (text.size() == 3 || text.size() == 4) {
        std::string expanded;
        for (char c : text) {
            expanded += c;
            expanded += c;
        }
        text = expanded;
    }
    if ((text.size() != 6 && text.size() != 8) ||
        text.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos)
        throw std::runtime_error("Invalid hex color");
    if (text.size() == 8)
        text.erase(0, 2);
    auto n = std::stoul(text, nullptr, 16);
    return {double((n >> 16) & 255) / 255, double((n >> 8) & 255) / 255, double(n & 255) / 255};
}
} // namespace starlight::editor
