#pragma once

#include <cstdint>
#include <string>

namespace facial_auth {

// Which head-pose region an enrollment template was captured from, along
// two independent axes (yaw = left/right, pitch = up/down). Combined into
// a single 3x3 grid rather than two separate tags so EmbeddingFormat only
// needs one tag byte per record. Which physical direction Left/Right/Up/
// Down actually maps to isn't verified against real hardware and doesn't
// need to be — verification only needs "compare against all templates,
// take the best," so a consistent-but-unlabeled split is functionally
// correct. See enroll/main.cpp's bucketing logic for how frames are
// assigned.
enum class AngleBucket : std::uint8_t {
    Center,
    Left,
    Right,
    Up,
    Down,
    UpLeft,
    UpRight,
    DownLeft,
    DownRight,
};

// "center"/"left"/.../"down_right", defaults to Center on garbage.
AngleBucket angleBucketFromString(const std::string& text);
std::string toString(AngleBucket bucket);

}  // namespace facial_auth
