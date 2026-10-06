#pragma once
// Diagnostic for negative checks that accept any exception as a refusal (audit C1): call it inside the catch block and the reason is printed, so a
// check that passes because of an unrelated failure is visible in the log instead of silent.
#include <exception>
#include <iostream>
#include <string>
inline std::string& last_rejection() { static std::string reason; return reason; }
inline void note_rejection(const char* file, int line) {
    std::string why;
    try { throw; } catch (const std::exception& e) { why = e.what(); } catch (...) { why = "non-standard exception"; }
    last_rejection() = why;
    const std::string name = std::string(file).substr(std::string(file).find_last_of("\\/") + 1);
    std::cout << "REJECTION " << name << ":" << line << ": " << why.substr(0, 200) << "\n";
}
