#include "minitest.hpp"

#include "core/camera/CameraSelection.hpp"

using namespace facial_auth;

namespace {

CameraDevice irOnly(const std::string& path) {
    CameraDevice device;
    device.friendlyName = "IR cam";
    device.ir = CameraStreamOption{path, PixelFormat::GREY, 340, 340};
    return device;
}

CameraDevice rgbOnly(const std::string& path) {
    CameraDevice device;
    device.friendlyName = "RGB cam";
    device.rgb = CameraStreamOption{path, PixelFormat::YUYV, 640, 480};
    return device;
}

CameraDevice both(const std::string& irPath, const std::string& rgbPath) {
    CameraDevice device;
    device.friendlyName = "Dual cam";
    device.ir = CameraStreamOption{irPath, PixelFormat::GREY, 340, 340};
    device.rgb = CameraStreamOption{rgbPath, PixelFormat::YUYV, 640, 480};
    return device;
}

}  // namespace

// --- selectCameraDevice: "no cameras present" ------------------------------

TEST(NoCamerasAndNoConfiguredDeviceShowsPlaceholder) {
    const auto selection = selectCameraDevice({}, /*devicePath=*/"", PixelFormat::Unknown,
                                                CameraMode::IR, 640, 480, "not detected",
                                                "No cameras detected");
    ASSERT_EQ(selection.devices.size(), 1u);
    ASSERT_TRUE(selection.usedNoCamerasPlaceholder);
    ASSERT_TRUE(!selection.usedNotDetectedFallback);
    ASSERT_EQ(selection.selectedIndex, 0);
    ASSERT_EQ(selection.devices[0].friendlyName, "No cameras detected");
    ASSERT_TRUE(!selection.devices[0].ir.has_value());
    ASSERT_TRUE(!selection.devices[0].rgb.has_value());
}

TEST(NoCamerasButPreviouslyConfiguredDeviceKeepsItSelectable) {
    // Camera was configured for IR use, then unplugged: scan comes back
    // empty, but the dropdown should still offer the configured device
    // (marked "not detected") rather than falling back to a bare
    // placeholder, so Save doesn't silently switch cameras out from
    // under the user.
    const auto selection =
        selectCameraDevice({}, "/dev/video0", PixelFormat::GREY, CameraMode::IR, 340, 340,
                            "/dev/video0 (not detected)", "No cameras detected");
    ASSERT_EQ(selection.devices.size(), 1u);
    ASSERT_TRUE(selection.usedNotDetectedFallback);
    ASSERT_TRUE(!selection.usedNoCamerasPlaceholder);
    ASSERT_EQ(selection.selectedIndex, 0);
    ASSERT_EQ(selection.mode, "ir");
    ASSERT_TRUE(selection.devices[0].ir.has_value());
    ASSERT_EQ(selection.devices[0].ir->devicePath, "/dev/video0");
}

// --- selectCameraDevice: matching / not-detected fallback -------------------

TEST(MatchesConfiguredIrDevicePath) {
    std::vector<CameraDevice> scanned = {irOnly("/dev/video0"), rgbOnly("/dev/video1")};
    const auto selection = selectCameraDevice(scanned, "/dev/video0", PixelFormat::GREY,
                                                CameraMode::IR, 340, 340, "not detected", "none");
    ASSERT_EQ(selection.selectedIndex, 0);
    ASSERT_EQ(selection.mode, "ir");
    ASSERT_TRUE(!selection.usedNotDetectedFallback);
}

TEST(ConfiguredDeviceNotInFreshScanGetsFallbackEntryAppended) {
    std::vector<CameraDevice> scanned = {irOnly("/dev/video2")};  // different camera now plugged in
    const auto selection =
        selectCameraDevice(scanned, "/dev/video0", PixelFormat::YUYV, CameraMode::RGB, 640, 480,
                            "/dev/video0 (not detected)", "none");
    ASSERT_EQ(selection.devices.size(), 2u);
    ASSERT_TRUE(selection.usedNotDetectedFallback);
    ASSERT_EQ(selection.selectedIndex, 1);
    ASSERT_EQ(selection.mode, "rgb");
    ASSERT_EQ(selection.devices[1].friendlyName, "/dev/video0 (not detected)");
    ASSERT_TRUE(selection.devices[1].rgb.has_value());
    ASSERT_TRUE(!selection.devices[1].ir.has_value());
}

TEST(SameNodeMatchingBothStreamsResolvesModeFromPixelFormat) {
    std::vector<CameraDevice> scanned = {both("/dev/video0", "/dev/video0")};
    const auto asIr = selectCameraDevice(scanned, "/dev/video0", PixelFormat::GREY, CameraMode::IR,
                                          340, 340, "not detected", "none");
    ASSERT_EQ(asIr.mode, "ir");
    const auto asRgb = selectCameraDevice(scanned, "/dev/video0", PixelFormat::YUYV,
                                           CameraMode::RGB, 640, 480, "not detected", "none");
    ASSERT_EQ(asRgb.mode, "rgb");
}

TEST(EmptyDevicePathDefaultsToFirstScannedCamera) {
    std::vector<CameraDevice> scanned = {rgbOnly("/dev/video0"), irOnly("/dev/video1")};
    const auto selection = selectCameraDevice(scanned, /*devicePath=*/"", PixelFormat::Unknown,
                                                CameraMode::IR, 640, 480, "not detected", "none");
    ASSERT_EQ(selection.selectedIndex, 0);
    ASSERT_TRUE(!selection.usedNotDetectedFallback);
    ASSERT_TRUE(!selection.usedNoCamerasPlaceholder);
}

// --- selectCameraMode: "no IR camera detected" on the selected device ------

TEST(RgbOnlyDeviceOffersOnlyRgbAndShowsDisclaimer) {
    const auto selection = selectCameraMode(rgbOnly("/dev/video0"), "ir");
    ASSERT_TRUE(!selection.irAvailable);
    ASSERT_TRUE(selection.rgbAvailable);
    ASSERT_EQ(selection.selectedMode, "rgb");  // falls back since "ir" wasn't available
    ASSERT_TRUE(selection.showRgbOnlyDisclaimer);
}

TEST(IrOnlyDeviceOffersOnlyIrAndHidesDisclaimer) {
    const auto selection = selectCameraMode(irOnly("/dev/video0"), "rgb");
    ASSERT_TRUE(selection.irAvailable);
    ASSERT_TRUE(!selection.rgbAvailable);
    ASSERT_EQ(selection.selectedMode, "ir");  // falls back since "rgb" wasn't available
    ASSERT_TRUE(!selection.showRgbOnlyDisclaimer);
}

TEST(DualModeDeviceHonorsDesiredMode) {
    const auto device = both("/dev/video0", "/dev/video1");
    ASSERT_EQ(selectCameraMode(device, "ir").selectedMode, "ir");
    ASSERT_EQ(selectCameraMode(device, "rgb").selectedMode, "rgb");
    ASSERT_TRUE(!selectCameraMode(device, "ir").showRgbOnlyDisclaimer);
}

TEST(DeviceWithNeitherStreamSelectsNoMode) {
    // Not reachable via listCameras() itself (it drops such nodes), but
    // SettingsPage passes an empty CameraDevice{} here whenever the combo
    // has no valid current selection at all — must degrade cleanly rather
    // than crash or pick a mode that doesn't exist.
    const CameraDevice empty{};
    const auto selection = selectCameraMode(empty, "ir");
    ASSERT_TRUE(!selection.irAvailable);
    ASSERT_TRUE(!selection.rgbAvailable);
    ASSERT_EQ(selection.selectedMode, "");
    ASSERT_TRUE(!selection.showRgbOnlyDisclaimer);
}

// --- resolveModeForDeviceChange: switching cameras --------------------------

TEST(SwitchingToRgbOnlyCameraFallsBackFromIr) {
    ASSERT_EQ(resolveModeForDeviceChange(rgbOnly("/dev/video0"), "ir"), "rgb");
}

TEST(SwitchingToIrOnlyCameraFallsBackFromRgb) {
    ASSERT_EQ(resolveModeForDeviceChange(irOnly("/dev/video0"), "rgb"), "ir");
}

TEST(SwitchingToDualModeCameraKeepsLastConfirmedMode) {
    const auto device = both("/dev/video0", "/dev/video1");
    ASSERT_EQ(resolveModeForDeviceChange(device, "ir"), "ir");
    ASSERT_EQ(resolveModeForDeviceChange(device, "rgb"), "rgb");
}

MINITEST_MAIN()
