#pragma once
#include <string>
#include <vector>

namespace host_config {

struct Entry {
    const char* flag;
    bool takesValue;
    const char* help;
};

inline const std::vector<Entry>& entries() {
    static const std::vector<Entry> list = {
        { "--card", true, "key card (.starscene) with the room, lights, view and cast (default <Host>.starscene beside the host module)" },
    };
    return list;
}

struct Options {
    std::string cardPath;
    std::vector<std::string> given;
    std::vector<std::string> derived;
    void derive(const std::string& text) { derived.push_back(text); }
};

inline bool extract(std::vector<std::string>& tokens, Options& options, std::string& error) {
    std::vector<std::string> rest;
    for (size_t i = 0; i < tokens.size(); ++i) {
        const std::string& token = tokens[i];
        const Entry* entry = nullptr;
        for (const Entry& candidate : entries()) if (token == candidate.flag) { entry = &candidate; break; }
        if (!entry) { rest.push_back(token); continue; }
        std::string value;
        if (entry->takesValue) {
            if (i + 1 >= tokens.size()) { error = "missing value after " + token; return false; }
            value = tokens[++i];
        }
        if (token == "--card") options.cardPath = value;
        options.given.push_back(entry->takesValue ? token + " " + value : token);
    }
    tokens.swap(rest);
    return true;
}

inline std::string usage() {
    std::string text;
    for (const Entry& entry : entries()) text += std::string("  ") + entry.flag + (entry.takesValue ? " <value>" : "") + "  " + entry.help + "\n";
    return text;
}

inline std::string report(const Options& options) {
    std::string text = "flags handled by the maintained host configuration (deep/engine/host_config.h, not the generated flag table):";
    if (options.given.empty()) text += " none given on the command line";
    for (const std::string& flag : options.given) text += " [" + flag + "]";
    for (const std::string& note : options.derived) text += "\n  from the key card: " + note;
    return text;
}

}
