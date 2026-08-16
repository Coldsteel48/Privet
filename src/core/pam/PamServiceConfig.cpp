#include "PamServiceConfig.hpp"

#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace facial_auth {

namespace {

constexpr const char* kInsertedLine = "auth    sufficient   pam_facial.so\n";

std::string trim(const std::string& s) {
    const auto begin = s.find_first_not_of(" \t");
    if (begin == std::string::npos) return "";
    const auto end = s.find_last_not_of(" \t\r");
    return s.substr(begin, end - begin + 1);
}

std::vector<std::string> splitLines(const std::string& content) {
    std::vector<std::string> lines;
    std::istringstream stream(content);
    std::string line;
    while (std::getline(stream, line)) lines.push_back(line);
    return lines;
}

bool lineReferencesPamFacial(const std::string& line) {
    const std::string trimmed = trim(line);
    if (trimmed.empty() || trimmed[0] == '#') return false;
    return trimmed.find("pam_facial.so") != std::string::npos;
}

// Second whitespace-separated field of an "auth ..." line — the control
// flag ("sufficient", "required", "requisite", "optional", or a bracketed
// [key=value ...] form, which >> reads as just its opening token). Only
// the exact plain word "sufficient" is ever treated as safe; anything
// else (including a bracketed form we didn't write ourselves) is unsafe.
std::string controlFlagOf(const std::string& line) {
    std::istringstream stream(trim(line));
    std::string authWord, control;
    stream >> authWord >> control;
    return control;
}

}  // namespace

bool isAllowedPamService(const std::string& service) {
    return std::find(kAllowedPamServices.begin(), kAllowedPamServices.end(), service) !=
           kAllowedPamServices.end();
}

PamFacialState detectPamFacialState(const std::string& fileContent) {
    for (const auto& line : splitLines(fileContent)) {
        if (!lineReferencesPamFacial(line)) continue;
        return controlFlagOf(line) == "sufficient" ? PamFacialState::EnabledSafe
                                                     : PamFacialState::EnabledUnsafe;
    }
    return PamFacialState::Absent;
}

std::string enableInContent(const std::string& fileContent) {
    switch (detectPamFacialState(fileContent)) {
        case PamFacialState::EnabledSafe:
            return fileContent;  // idempotent no-op
        case PamFacialState::EnabledUnsafe:
            throw std::runtime_error(
                "an existing pam_facial.so line uses a control flag other than 'sufficient' -- "
                "not touching it automatically; fix or remove it by hand, or use Disable to "
                "strip it, then try again");
        case PamFacialState::Absent:
        default:
            return std::string(kInsertedLine) + fileContent;
    }
}

std::string disableInContent(const std::string& fileContent) {
    std::ostringstream out;
    bool changed = false;
    for (const auto& line : splitLines(fileContent)) {
        if (lineReferencesPamFacial(line)) {
            changed = true;
            continue;
        }
        out << line << "\n";
    }
    return changed ? out.str() : fileContent;
}

}  // namespace facial_auth
