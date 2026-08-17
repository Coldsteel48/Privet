#pragma once

// Header-only (no .cpp / no facial_core linkage needed) so pam_facial.so —
// which deliberately links only libpam + libc, see src/pam/pam_facial.cpp —
// can use this without pulling in facial_core/OpenCV.

#include <cctype>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

namespace facial_auth {

// Fallback and bounds for the "Authenticate using face recognition?"
// answer timeout.
// Falls back to the default rather than an unparsable/absent/zero/negative
// configured value — a config typo must never turn into an unbounded wait
// (no cap) or an instant auto-decline that looks like a bug (0s). The
// upper bound keeps a fat-fingered value (e.g. an extra zero) from
// effectively reintroducing an unbounded hang.
inline constexpr int kDefaultConfirmationTimeoutMs = 20000;
inline constexpr int kMinConfirmationTimeoutMs = 1000;
inline constexpr int kMaxConfirmationTimeoutMs = 300000;

// Same file facial-auth-verify/-enroll already read via Config::load()
// (see src/core/config/Config.cpp) — reused here rather than a second
// config file, even though pam_facial.so reads only this one key from it
// directly instead of going through the full Config class (which would
// pull facial_core into the module's link, see pam_facial.cpp's header
// comment).
inline constexpr const char* kFacialAuthConfigPath = "/etc/facial-auth/config.conf";

// Pure parser over `confirmation_timeout_sec = N` (same `key = value`,
// `#`/`;`-comment grammar as Config::load()). Returns N*1000ms, clamped to
// [kMinConfirmationTimeoutMs, kMaxConfirmationTimeoutMs]; falls back to
// kDefaultConfirmationTimeoutMs if the key is absent, commented out, or
// its value doesn't parse as a positive integer. Factored out from the
// file read below so it's unit-testable without touching the filesystem.
inline int parseConfirmationTimeoutMs(const std::string& fileContent) {
    std::istringstream stream(fileContent);
    std::string line;
    while (std::getline(stream, line)) {
        const size_t begin = line.find_first_not_of(" \t\r\n");
        if (begin == std::string::npos || line[begin] == '#' || line[begin] == ';') {
            continue;
        }
        const size_t eq = line.find('=', begin);
        if (eq == std::string::npos) {
            continue;
        }
        size_t keyEnd = eq;
        while (keyEnd > begin && std::isspace(static_cast<unsigned char>(line[keyEnd - 1]))) {
            --keyEnd;
        }
        if (line.compare(begin, keyEnd - begin, "confirmation_timeout_sec") != 0) {
            continue;
        }
        size_t valBegin = eq + 1;
        while (valBegin < line.size() && std::isspace(static_cast<unsigned char>(line[valBegin]))) {
            ++valBegin;
        }
        const size_t valEnd = line.find_first_of(" \t\r\n#;", valBegin);
        const std::string value =
            line.substr(valBegin, valEnd == std::string::npos ? std::string::npos : valEnd - valBegin);
        try {
            size_t consumed = 0;
            const long seconds = std::stol(value, &consumed);
            if (consumed == 0 || consumed != value.size() || seconds <= 0) {
                return kDefaultConfirmationTimeoutMs;
            }
            const long ms = seconds * 1000;
            if (ms < kMinConfirmationTimeoutMs) {
                return kMinConfirmationTimeoutMs;
            }
            if (ms > kMaxConfirmationTimeoutMs) {
                return kMaxConfirmationTimeoutMs;
            }
            return static_cast<int>(ms);
        } catch (...) {
            return kDefaultConfirmationTimeoutMs;
        }
    }
    return kDefaultConfirmationTimeoutMs;
}

// Best-effort read of kFacialAuthConfigPath — a missing file (e.g. before
// the admin has ever configured this machine) is the normal, safe case,
// not an error, so it's treated identically to an absent key.
inline int readConfirmationTimeoutMs() {
    std::ifstream file(kFacialAuthConfigPath);
    if (!file.is_open()) {
        return kDefaultConfirmationTimeoutMs;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return parseConfirmationTimeoutMs(buffer.str());
}

// Only "y"/"yes" (case-insensitive, surrounding whitespace ignored) counts
// as consent to open the camera. Empty input (bare Enter), "n", garbage,
// or a null response all decline — an ambiguous answer must never be
// treated as permission.
inline bool isAffirmativeResponse(const char* response) {
    if (response == nullptr) {
        return false;
    }
    size_t begin = 0;
    size_t end = std::strlen(response);
    while (begin < end && std::isspace(static_cast<unsigned char>(response[begin]))) {
        ++begin;
    }
    while (end > begin && std::isspace(static_cast<unsigned char>(response[end - 1]))) {
        --end;
    }
    const size_t len = end - begin;
    if (len == 0) {
        return false;
    }
    auto matches = [&](const char* word) {
        const size_t wordLen = std::strlen(word);
        if (len != wordLen) {
            return false;
        }
        for (size_t i = 0; i < len; ++i) {
            if (std::tolower(static_cast<unsigned char>(response[begin + i])) != word[i]) {
                return false;
            }
        }
        return true;
    };
    return matches("y") || matches("yes");
}

// Allow-list of environment variables passed through to the
// facial-auth-confirm GUI helper (see tryGuiConfirmation() in
// src/pam/pam_facial.cpp). At PAM-authenticate time this process is
// generally still running as root, so only these display-connection
// variables are copied individually — never the full environment — to
// keep a root-launched GUI toolkit from ever honoring an
// attacker-controlled LD_PRELOAD/LD_LIBRARY_PATH/QT_PLUGIN_PATH/etc.
inline constexpr const char* kGuiEnvVarNames[] = {"DISPLAY", "WAYLAND_DISPLAY", "XAUTHORITY",
                                                   "XDG_RUNTIME_DIR"};

// True if either value looks like a real display connection (non-null,
// non-empty) — decides whether it's worth even attempting the
// facial-auth-confirm GUI helper before falling back to the PAM text
// prompt. Takes the two values directly rather than reading getenv()
// itself so the decision is unit-testable without touching process
// environment.
inline bool hasDisplayEnv(const char* display, const char* waylandDisplay) {
    return (display != nullptr && display[0] != '\0') ||
           (waylandDisplay != nullptr && waylandDisplay[0] != '\0');
}

}  // namespace facial_auth
