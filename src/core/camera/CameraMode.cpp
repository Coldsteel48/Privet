#include "CameraMode.hpp"

#include <algorithm>
#include <cctype>

namespace facial_auth {

namespace {
std::string toLower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}
}  // namespace

CameraMode cameraModeFromString(const std::string& text) {
    return toLower(text) == "rgb" ? CameraMode::RGB : CameraMode::IR;
}

std::string toString(CameraMode mode) {
    return mode == CameraMode::RGB ? "rgb" : "ir";
}

}  // namespace facial_auth
