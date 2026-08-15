#pragma once

#include <string>

namespace facial_auth {

// IR is the recommended mode (real depth/liveness signal, like Windows
// Hello). RGB is an explicit, at-your-own-risk opt-in: a plain webcam has
// no depth channel and is far more spoofable (photo/video replay). See
// Config::requireIr and the RiskDisclaimer flow in facial-auth-enroll /
// facial-auth-control — selecting RGB must always go through one of those,
// never silently.
enum class CameraMode { IR, RGB };

CameraMode cameraModeFromString(const std::string& text);  // "ir"/"rgb", defaults to IR on garbage
std::string toString(CameraMode mode);

}  // namespace facial_auth
