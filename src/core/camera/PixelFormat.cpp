#include "PixelFormat.hpp"

#include <linux/videodev2.h>

#include <algorithm>
#include <cctype>

namespace facial_auth {

std::uint32_t toV4L2Fourcc(PixelFormat format) {
    switch (format) {
        case PixelFormat::GREY:
            return V4L2_PIX_FMT_GREY;
        case PixelFormat::Y16:
            return V4L2_PIX_FMT_Y16;
        case PixelFormat::YUYV:
            return V4L2_PIX_FMT_YUYV;
        case PixelFormat::MJPEG:
            return V4L2_PIX_FMT_MJPEG;
        case PixelFormat::Unknown:
            return 0;
    }
    return 0;
}

PixelFormat fromV4L2Fourcc(std::uint32_t fourcc) {
    if (fourcc == V4L2_PIX_FMT_GREY) return PixelFormat::GREY;
    if (fourcc == V4L2_PIX_FMT_Y16) return PixelFormat::Y16;
    if (fourcc == V4L2_PIX_FMT_YUYV) return PixelFormat::YUYV;
    if (fourcc == V4L2_PIX_FMT_MJPEG) return PixelFormat::MJPEG;
    return PixelFormat::Unknown;
}

int bytesPerPixel(PixelFormat format) {
    switch (format) {
        case PixelFormat::GREY:
            return 1;
        case PixelFormat::Y16:
            return 2;
        case PixelFormat::YUYV:
            return 2;
        case PixelFormat::MJPEG:
        case PixelFormat::Unknown:
            return 0;
    }
    return 0;
}

namespace {
std::string toLower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}
}  // namespace

PixelFormat pixelFormatFromString(const std::string& text) {
    const std::string lower = toLower(text);
    if (lower == "grey" || lower == "gray") return PixelFormat::GREY;
    if (lower == "y16") return PixelFormat::Y16;
    if (lower == "yuyv") return PixelFormat::YUYV;
    if (lower == "mjpeg") return PixelFormat::MJPEG;
    return PixelFormat::Unknown;
}

std::string toString(PixelFormat format) {
    switch (format) {
        case PixelFormat::GREY:
            return "grey";
        case PixelFormat::Y16:
            return "y16";
        case PixelFormat::YUYV:
            return "yuyv";
        case PixelFormat::MJPEG:
            return "mjpeg";
        case PixelFormat::Unknown:
            return "unknown";
    }
    return "unknown";
}

}  // namespace facial_auth
