#pragma once

#include <optional>
#include <string>
#include <vector>

#include <opencv2/core.hpp>

#include "PixelFormat.hpp"

namespace facial_auth {

struct CameraConfig {
    std::string devicePath;
    PixelFormat pixelFormat = PixelFormat::Unknown;
    int width = 640;
    int height = 480;
    // Per-attempt capture timeout. This bounds a single captureFrame()
    // call; callers implementing a retry loop (facial-auth-verify,
    // facial-auth-enroll) additionally enforce their own overall
    // wall-clock budget across attempts. pam_facial.so adds a third,
    // outer timeout around the whole helper process — see the plan's
    // "never lock out" design for why there are three independent layers.
    int timeoutMs = 2000;
    // Software brightness multiplier applied to every captured frame right
    // before it's returned (cv::convertScaleAbs, alpha=illuminationGain,
    // beta=0), uniformly regardless of pixel format. 1.0 = no change. This
    // is a display/detection aid, not real IR-illuminator control — see
    // Config::illuminationGain, which is where this value actually comes
    // from in facial-auth-verify/-enroll/-control.
    double illuminationGain = 1.0;
};

// Thin V4L2 mmap-streaming capture wrapper. The generic buffer-management
// plumbing here (REQBUFS/QBUF/DQBUF, poll()-based timeout) is
// format-agnostic and works today; only the raw-buffer-to-cv::Mat
// conversion in captureFrame() depends on the configured PixelFormat, and
// only YUYV is implemented so far (GREY/Y16 — the likely IR formats — are
// stubbed pending the author's v4l2-ctl confirmation).
class V4L2Camera {
public:
    explicit V4L2Camera(CameraConfig config);
    ~V4L2Camera();

    V4L2Camera(const V4L2Camera&) = delete;
    V4L2Camera& operator=(const V4L2Camera&) = delete;

    // Opens the device, negotiates format, allocates and queues mmap
    // buffers, starts streaming. Returns false on any failure (device
    // missing, permission denied, busy, format rejected by the driver) —
    // never throws, so a missing/misconfigured camera is always a clean,
    // reportable failure rather than an exception a caller must know to
    // catch.
    bool open();

    // Blocks for at most config.timeoutMs waiting for a frame, then
    // dequeues and converts it. Returns std::nullopt on timeout or a
    // transient I/O error (camera unplugged mid-stream, etc.) — these are
    // expected, retryable conditions. Throws std::runtime_error if
    // config.pixelFormat has no conversion implemented yet (currently
    // GREY/Y16) — this is a configuration error, not a transient one, and
    // is caught by the top-level try/catch in facial-auth-verify's/
    // facial-auth-enroll's main() like any other core-library exception.
    std::optional<cv::Mat> captureFrame();

    void close();

private:
    struct MappedBuffer {
        void* start = nullptr;
        std::size_t length = 0;
    };

    CameraConfig config_;
    int fd_ = -1;
    bool streaming_ = false;
    std::vector<MappedBuffer> buffers_;
};

}  // namespace facial_auth
