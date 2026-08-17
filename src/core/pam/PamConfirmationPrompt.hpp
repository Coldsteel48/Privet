#pragma once

// Header-only (no .cpp / no facial_core linkage needed) so pam_facial.so —
// which deliberately links only libpam + libc, see src/pam/pam_facial.cpp —
// can use this without pulling in facial_core/OpenCV.

#include <cctype>
#include <cstring>
#include <fstream>
#include <optional>
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

// Finds the value of `key = value` (same `#`/`;`-comment grammar as
// Config::load()) in raw config-file text, or std::nullopt if the key is
// absent or commented out. Shared by every `key = value` this header
// reads directly out of kFacialAuthConfigPath — pam_facial.so can't link
// Config/facial_core (see this header's top comment), so it can't reuse
// Config::load() itself.
inline std::optional<std::string> findConfigValue(const std::string& fileContent, const std::string& key) {
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
        if (line.compare(begin, keyEnd - begin, key) != 0) {
            continue;
        }
        size_t valBegin = eq + 1;
        while (valBegin < line.size() && std::isspace(static_cast<unsigned char>(line[valBegin]))) {
            ++valBegin;
        }
        const size_t valEnd = line.find_first_of(" \t\r\n#;", valBegin);
        return line.substr(valBegin, valEnd == std::string::npos ? std::string::npos : valEnd - valBegin);
    }
    return std::nullopt;
}

// Best-effort read of the whole config file's raw text, or std::nullopt if
// it doesn't exist yet (e.g. before the admin has ever configured this
// machine) — the normal, safe case, not an error.
inline std::optional<std::string> readConfigFileContent() {
    std::ifstream file(kFacialAuthConfigPath);
    if (!file.is_open()) {
        return std::nullopt;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

// Pure parser over `confirmation_timeout_sec = N`. Returns N*1000ms,
// clamped to [kMinConfirmationTimeoutMs, kMaxConfirmationTimeoutMs]; falls
// back to kDefaultConfirmationTimeoutMs if the key is absent, commented
// out, or its value doesn't parse as a positive integer. Shared by
// parseConfirmationTimeoutMs below (whole-file parsing) and
// Config::applyConfigOverride (an already-split key/value pair) so
// pam_facial.so and the GUI/Config layer can never disagree on this
// clamping — also unit-testable on its own without touching the
// filesystem.
inline int confirmationTimeoutMsFromString(const std::string& value) {
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

inline int parseConfirmationTimeoutMs(const std::string& fileContent) {
    const std::optional<std::string> value = findConfigValue(fileContent, "confirmation_timeout_sec");
    return value ? confirmationTimeoutMsFromString(*value) : kDefaultConfirmationTimeoutMs;
}

inline int readConfirmationTimeoutMs() {
    const std::optional<std::string> content = readConfigFileContent();
    return content ? parseConfirmationTimeoutMs(*content) : kDefaultConfirmationTimeoutMs;
}

namespace detail {
inline std::string toLowerAscii(std::string text) {
    for (char& c : text) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return text;
}
}  // namespace detail

// How pam_facial.so asks "OK to open the camera?" before authenticating —
// configured via `confirmation_mode` in kFacialAuthConfigPath, settable
// from facial-auth-control's Settings page (see Config::confirmationMode,
// which mirrors this enum so the GUI/Config layer and pam_facial.so agree
// on the same three choices without pam_facial.so linking Config itself).
//   Gui:  try the clickable facial-auth-confirm Yes/No box first
//         (tryGuiConfirmation() in pam_facial.cpp); when no display is
//         reachable, defer to greeterConfirmationMode below. This is the
//         default — unchanged from pre-existing behavior.
//   Text: always use the plain-text PAM conversation "(y/n)" prompt,
//         never attempt the GUI box even if a display is reachable.
//   None: never ask at all — proceed straight to face recognition.
enum class ConfirmationMode { Gui, Text, None };

inline std::string toString(ConfirmationMode mode) {
    switch (mode) {
        case ConfirmationMode::Text:
            return "text";
        case ConfirmationMode::None:
            return "none";
        case ConfirmationMode::Gui:
        default:
            return "gui";
    }
}

// Defaults to Gui on an absent, commented-out, or unrecognized value —
// same "unparsable config must never change behavior in a surprising way"
// principle as parseConfirmationTimeoutMs above.
inline ConfirmationMode confirmationModeFromString(const std::string& value) {
    const std::string v = detail::toLowerAscii(value);
    if (v == "text") return ConfirmationMode::Text;
    if (v == "none") return ConfirmationMode::None;
    return ConfirmationMode::Gui;
}

inline ConfirmationMode parseConfirmationMode(const std::string& fileContent) {
    const std::optional<std::string> value = findConfigValue(fileContent, "confirmation_mode");
    return value ? confirmationModeFromString(*value) : ConfirmationMode::Gui;
}

inline ConfirmationMode readConfirmationMode() {
    const std::optional<std::string> content = readConfigFileContent();
    return content ? parseConfirmationMode(*content) : ConfirmationMode::Gui;
}

// Only consulted when confirmationMode == Gui *and* no display is
// reachable in pam_facial.so's own process (hasDisplayEnv() below) — i.e.
// exactly the console `login` prompt / graphical-greeter case (including
// COSMIC via greetd), where the GUI box is structurally unreachable
// regardless of this setting. Configured via `greeter_confirmation_mode`,
// independently of `confirmation_mode`, so e.g. `sudo` (where the GUI box
// can actually appear) can keep asking via the clickable box while the
// login screen either still asks via text or skips asking altogether.
//   Text: fall back to the plain-text PAM conversation prompt (default,
//         unchanged pre-existing behavior).
//   None: skip confirmation and proceed straight to face recognition.
enum class GreeterConfirmationMode { Text, None };

inline std::string toString(GreeterConfirmationMode mode) {
    return mode == GreeterConfirmationMode::None ? "none" : "text";
}

inline GreeterConfirmationMode greeterConfirmationModeFromString(const std::string& value) {
    return detail::toLowerAscii(value) == "none" ? GreeterConfirmationMode::None
                                                  : GreeterConfirmationMode::Text;
}

inline GreeterConfirmationMode parseGreeterConfirmationMode(const std::string& fileContent) {
    const std::optional<std::string> value = findConfigValue(fileContent, "greeter_confirmation_mode");
    return value ? greeterConfirmationModeFromString(*value) : GreeterConfirmationMode::Text;
}

inline GreeterConfirmationMode readGreeterConfirmationMode() {
    const std::optional<std::string> content = readConfigFileContent();
    return content ? parseGreeterConfirmationMode(*content) : GreeterConfirmationMode::Text;
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
