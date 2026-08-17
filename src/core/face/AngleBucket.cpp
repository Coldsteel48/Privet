#include "AngleBucket.hpp"

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

AngleBucket angleBucketFromString(const std::string& text) {
    const std::string lower = toLower(text);
    if (lower == "left") return AngleBucket::Left;
    if (lower == "right") return AngleBucket::Right;
    if (lower == "up") return AngleBucket::Up;
    if (lower == "down") return AngleBucket::Down;
    if (lower == "up_left") return AngleBucket::UpLeft;
    if (lower == "up_right") return AngleBucket::UpRight;
    if (lower == "down_left") return AngleBucket::DownLeft;
    if (lower == "down_right") return AngleBucket::DownRight;
    return AngleBucket::Center;
}

std::string toString(AngleBucket bucket) {
    switch (bucket) {
        case AngleBucket::Left: return "left";
        case AngleBucket::Right: return "right";
        case AngleBucket::Up: return "up";
        case AngleBucket::Down: return "down";
        case AngleBucket::UpLeft: return "up_left";
        case AngleBucket::UpRight: return "up_right";
        case AngleBucket::DownLeft: return "down_left";
        case AngleBucket::DownRight: return "down_right";
        case AngleBucket::Center:
        default:
            return "center";
    }
}

}  // namespace facial_auth
