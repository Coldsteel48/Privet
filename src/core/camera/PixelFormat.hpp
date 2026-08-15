#pragma once

#include <cstdint>
#include <string>

namespace facial_auth {

// V4L2 pixel formats pam_facial knows how to convert to cv::Mat.
// GREY/Y16 are the likely candidates for a Brio-style IR sensor node but
// are NOT yet confirmed against real v4l2-ctl output — see
// V4L2Camera::captureFrame() and the "deferred until v4l2-ctl" notes in
// the project plan. YUYV is implemented now since it's the common format
// for an ordinary UVC RGB stream, letting the capture pipeline be
// exercised end-to-end before the IR format is confirmed.
enum class PixelFormat {
    Unknown,
    GREY,   // V4L2_PIX_FMT_GREY - 8-bit grayscale
    Y16,    // V4L2_PIX_FMT_Y16  - 16-bit grayscale (possible IR depth stream)
    YUYV,   // V4L2_PIX_FMT_YUYV - common UVC packed YUV 4:2:2
    MJPEG,  // V4L2_PIX_FMT_MJPEG
};

// Maps to/from the V4L2 FourCC values used by VIDIOC_S_FMT etc.
std::uint32_t toV4L2Fourcc(PixelFormat format);
PixelFormat fromV4L2Fourcc(std::uint32_t fourcc);

// Bytes per pixel, for raw-buffer size calculations. YUYV is 2 bytes per
// pixel (averaged over the 2-pixel macropixel), MJPEG has no fixed value
// (variable-length compressed) and returns 0 — callers must not use this
// for MJPEG buffer sizing.
int bytesPerPixel(PixelFormat format);

// Parses the config file's pixel_format string ("grey"/"gray", "y16",
// "yuyv", "mjpeg", case-insensitive). Returns PixelFormat::Unknown for
// anything else, including an empty string.
PixelFormat pixelFormatFromString(const std::string& text);
std::string toString(PixelFormat format);

}  // namespace facial_auth
