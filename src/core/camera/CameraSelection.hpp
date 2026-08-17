#pragma once

#include <string>
#include <vector>

#include "CameraEnumerator.hpp"
#include "CameraMode.hpp"
#include "PixelFormat.hpp"

namespace facial_auth {

// Result of (re)matching a fresh listCameras() scan against a target
// device path — the logic behind SettingsPage's camera-device dropdown,
// pulled out here so it's unit-testable without Qt/real hardware. Mirrors
// exactly what SettingsPage::populateCameraDeviceCombo used to compute
// inline.
struct CameraDeviceSelection {
    // Never empty: a synthetic placeholder/fallback entry (see below) is
    // appended when needed so there's always something to select.
    std::vector<CameraDevice> devices;
    int selectedIndex = 0;
    std::string mode;  // "ir" or "rgb" — effective mode for devices[selectedIndex]
    // True if devicePath was non-empty but matched nothing in `scanned`,
    // so a synthetic "<devicePath> (not detected)" entry (carrying over
    // pixelFormat/frameWidth/frameHeight) was appended as devices.back()
    // and selected — this is what lets a user's already-configured camera
    // stay selected (and Save not silently switch it out) across a scan
    // that doesn't currently see it, e.g. unplugged or GREY-only mid-boot
    // enumeration race.
    bool usedNotDetectedFallback = false;
    // True if `scanned` was empty and there was no devicePath to fall
    // back to either — the "no cameras at all" case — so a synthetic,
    // stream-less placeholder entry was appended as devices.back() and
    // selected purely so the dropdown has something to show.
    bool usedNoCamerasPlaceholder = false;
};

// Matches `scanned` (a fresh facial_auth::listCameras() result) against
// devicePath, exactly like V4L2Camera would open it: a device's ir/rgb
// stream option "is" devicePath if its CameraStreamOption::devicePath
// equals it. If devicePath is empty (no configured camera yet), the first
// scanned device is selected with `cameraMode` as-is (or the "no cameras"
// placeholder if scanned is also empty). If both ir and rgb match the
// same node (same physical device exposing devicePath under both — the
// enrolled-mode is ambiguous), pixelFormat resolves it: YUYV means the
// rgb stream was actually in use, anything else means ir.
CameraDeviceSelection selectCameraDevice(std::vector<CameraDevice> scanned,
                                          const std::string& devicePath, PixelFormat pixelFormat,
                                          CameraMode cameraMode, int frameWidth, int frameHeight,
                                          const std::string& notDetectedLabel,
                                          const std::string& noCamerasLabel);

// What SettingsPage's camera-mode dropdown should offer for one physical
// device — mirrors SettingsPage::populateCameraModeCombo(). selectedMode
// is desiredMode if that device supports it, else whichever mode the
// device does support (ir preferred over rgb), else empty if the device
// supports neither (only reachable via a hand-built CameraDevice — every
// entry listCameras()/selectCameraDevice() produces has at least one of
// ir/rgb).
struct CameraModeSelection {
    bool irAvailable = false;
    bool rgbAvailable = false;
    std::string selectedMode;  // "ir", "rgb", or "" if the device supports neither
    // True exactly when this device has an rgb stream but no ir stream —
    // drives SettingsPage's "no IR sensor" risk callout, independent of
    // which mode ends up selected.
    bool showRgbOnlyDisclaimer = false;
};

CameraModeSelection selectCameraMode(const CameraDevice& device, const std::string& desiredMode);

// What SettingsPage::onCameraDeviceChanged should land on when the user
// switches to `device`: lastConfirmedMode carried over if that device
// still supports it, else the other mode. Doesn't decide whether that
// requires the RGB risk-disclaimer gate — callers still check whether the
// result is "rgb" and gate accordingly, same as before extraction.
std::string resolveModeForDeviceChange(const CameraDevice& device,
                                        const std::string& lastConfirmedMode);

}  // namespace facial_auth
