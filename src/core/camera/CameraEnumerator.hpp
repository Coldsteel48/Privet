#pragma once

#include <optional>
#include <string>
#include <vector>

#include "PixelFormat.hpp"

namespace facial_auth {

// A single usable capture stream on a camera node: a /dev/videoN path (or
// its stable /dev/v4l/by-id/... equivalent, preferred when udev provides
// one — see config/facial-auth.conf.example on why that's preferred over
// a raw device number) plus the pixel format/geometry V4L2Camera should
// request for it. Only formats V4L2Camera::captureFrame() actually
// converts (GREY, YUYV) are ever surfaced here — offering Y16/MJPEG would
// just fail at the first real capture.
struct CameraStreamOption {
    std::string devicePath;
    PixelFormat pixelFormat = PixelFormat::Unknown;
    int width = 0;
    int height = 0;
};

// One physical camera, which may expose an IR stream, an RGB stream, or
// both — common on Windows-Hello-style webcams like the confirmed Logi 4K
// Stream Edition, which exposes separate GREY and YUYV capture nodes on
// the same USB device (plus a couple of metadata-only nodes, which are
// filtered out entirely). At least one of ir/rgb is always populated.
struct CameraDevice {
    std::string friendlyName;
    std::optional<CameraStreamOption> ir;
    std::optional<CameraStreamOption> rgb;
};

// Scans /dev/video* for capture-capable nodes (see the .cpp for the
// V4L2_CAP_DEVICE_CAPS nuance that matters on multi-node webcams), groups
// nodes belonging to the same physical device (by v4l2_capability::bus_info)
// into one CameraDevice each, and classifies each node as an IR (GREY) or
// RGB (YUYV) stream option based on VIDIOC_ENUM_FMT. Nodes offering
// neither implemented format are dropped, and so is any physical device
// left with neither ir nor rgb populated as a result. Never throws; a
// node that fails to open (permissions, races with unplug) is silently
// skipped. Requires read access to the device nodes (the `video` group),
// same as V4L2Camera.
std::vector<CameraDevice> listCameras();

}  // namespace facial_auth
