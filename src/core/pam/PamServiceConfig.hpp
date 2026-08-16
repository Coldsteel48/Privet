#pragma once

#include <array>
#include <string>

namespace facial_auth {

// Fixed, non-extensible list of PAM services facial-auth-control /
// facial-auth-enroll are ever allowed to modify. Deliberately excludes
// sshd and anything not on this list: a mistake in a local greeter or
// sudo is recoverable from the console; a mistake in sshd on a
// remote/headless box may not be. See README's "never lock out" section
// and docs/testing-safely.md — this list is the code-level enforcement
// of that same policy, not just documentation.
inline constexpr std::array<const char*, 4> kAllowedPamServices = {
    "sudo",
    "gdm-password",
    "sddm",
    "lightdm",
};

bool isAllowedPamService(const std::string& service);

enum class PamFacialState {
    Absent,         // no pam_facial.so reference in the file at all
    EnabledSafe,    // present as "auth  sufficient  pam_facial.so"
    EnabledUnsafe,  // present with any other control flag (hand-edited) — never touched automatically
};

PamFacialState detectPamFacialState(const std::string& fileContent);

// Returns fileContent with "auth    sufficient   pam_facial.so" prepended
// as the very first line, so it runs before every existing auth line
// (including anything pulled in via @include/substack further down).
// Idempotent: returns fileContent unchanged if already EnabledSafe.
// Throws std::runtime_error if the current state is EnabledUnsafe — a
// hand-edited line is never auto-modified, only ever reported.
std::string enableInContent(const std::string& fileContent);

// Returns fileContent with every line referencing pam_facial.so removed,
// regardless of control flag. Unconditionally safe to call: removing an
// auth line only ever narrows what a login accepts, never what it
// rejects, and this is also the sanctioned recovery path out of
// EnabledUnsafe. Idempotent: returns fileContent unchanged if nothing
// referenced pam_facial.so.
std::string disableInContent(const std::string& fileContent);

}  // namespace facial_auth
