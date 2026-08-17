#include "minitest.hpp"

#include "core/camera/V4L2Camera.hpp"

// Exercises the exact failure mode VerifyRunner::runVerification() relies
// on at login time: when config.devicePath ("the camera of choice") isn't
// actually present — unplugged, never existed, or a stale/bogus path left
// over in /etc/facial-auth/config.conf — camera.open() must fail cleanly
// (return false, never throw) so runVerification() maps it to
// VerifyOutcome::Unavailable, which pam_facial.so in turn maps to
// PAM_AUTHINFO_UNAVAIL (falls through to the next auth method, never locks
// the user out). No real camera hardware needed: /dev/video-with-a-path-
// that-cannot-exist simply fails at open(2) with ENOENT, same as any other
// missing device node.

using namespace facial_auth;

namespace {
constexpr const char* kNonexistentDevicePath = "/dev/video_facial_auth_test_nonexistent_9876";
}  // namespace

TEST(OpenFailsCleanlyForNonexistentDevicePath) {
    CameraConfig config;
    config.devicePath = kNonexistentDevicePath;
    config.pixelFormat = PixelFormat::GREY;
    config.width = 340;
    config.height = 340;

    V4L2Camera camera(config);
    ASSERT_TRUE(!camera.open());
}

TEST(CaptureFrameFailsCleanlyWhenNeverSuccessfullyOpened) {
    CameraConfig config;
    config.devicePath = kNonexistentDevicePath;
    config.pixelFormat = PixelFormat::GREY;

    V4L2Camera camera(config);
    ASSERT_TRUE(!camera.open());
    // Calling captureFrame() on a camera whose open() failed is exactly
    // what a caller that doesn't check open()'s return value would do —
    // must return nullopt, not crash or block.
    ASSERT_TRUE(!camera.captureFrame().has_value());
}

TEST(CloseIsSafeWithoutASuccessfulOpen) {
    CameraConfig config;
    config.devicePath = kNonexistentDevicePath;

    V4L2Camera camera(config);
    ASSERT_TRUE(!camera.open());
    camera.close();  // must not crash/double-free when nothing was ever opened
}

TEST(OpenFailsCleanlyForEmptyDevicePath) {
    // config.devicePath left blank — e.g. a freshly-installed config.conf
    // before the GUI/CLI has ever written a camera selection.
    CameraConfig config;
    config.devicePath = "";
    config.pixelFormat = PixelFormat::GREY;

    V4L2Camera camera(config);
    ASSERT_TRUE(!camera.open());
}

MINITEST_MAIN()
