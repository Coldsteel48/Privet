#pragma once

#include <optional>
#include <string>

#include "core/camera/CameraMode.hpp"
#include "core/camera/PixelFormat.hpp"

namespace facial_auth {

// Parsed contents of /etc/facial-auth/config.conf, shared by
// facial-auth-verify, facial-auth-enroll, and facial-auth-control so all
// three agree on device/model/threshold settings. See
// config/facial-auth.conf.example for the on-disk format.
struct Config {
    std::string devicePath;
    PixelFormat pixelFormat = PixelFormat::Unknown;
    CameraMode cameraMode = CameraMode::IR;
    bool requireIr = true;

    int frameWidth = 640;
    int frameHeight = 480;
    int captureTimeoutMs = 2000;
    // On the confirmed IR hardware the illuminator strobes on alternating
    // frames, so per-attempt detection succeeds roughly 25% of the time
    // even under good conditions (empirically, runEnroll needs ~32
    // attempts for 8 successful detections). 20 attempts gives
    // 0.75^20 ≈ 0.3% chance of total failure, vs 0.75^3 = 42% at the old
    // default. Each captureFrame() returns near-instantly under normal
    // streaming (bounded by the ~33ms frame period, not
    // captureTimeoutMs), so 20 attempts costs well under a second in the
    // normal case; the outer 8s PAM timeout (pam_facial.cpp's
    // kOuterTimeoutMs) independently bounds a genuinely stalled camera.
    int maxCaptureAttempts = 20;

    std::string detectorModelPath = "/etc/facial-auth/models/face_detection_yunet.onnx";
    std::string embedderModelPath = "/etc/facial-auth/models/face_recognition_sface.onnx";

    // "cosine" | "euclidean" — parsed into match::DistanceMetric by
    // whoever constructs an EmbeddingMatcher; kept as a raw string here so
    // this header doesn't need to pull in OpenCV types.
    std::string distanceMetric = "cosine";
    double matchThreshold = 0.36;

    // Software brightness multiplier applied uniformly to every captured
    // frame in V4L2Camera::captureFrame() (see CameraConfig::illuminationGain)
    // — not real IR-illuminator control (no such control was found on the
    // confirmed hardware; the illuminator strobes autonomously). 1.0 = no
    // change. Tuned live via facial-auth-control's Enrollment tab slider,
    // which only persists here on an explicit Save so real captures (enroll
    // and verify alike) see the same boost the user tuned by eye.
    double illuminationGain = 1.0;

    // Duration (wall-clock seconds) of the raw-frame recording window
    // facial-auth-enroll buffers before post-processing it into
    // angle-bucketed templates (see AngleBucket.hpp): record everything
    // first while the user turns their head, then run detection/embedding
    // over the whole buffer at once, rather than trying to steer capture
    // live. 8s gives enough usable (illuminated + successfully-detected)
    // frames to populate the full 3x3 yaw/pitch grid (needs >=36 usable
    // frames; fewer degrades gracefully to a coarser split, see runEnroll).
    int enrollVideoDurationSec = 8;

    // Parses a `key = value` config file (# or ; starts a comment line,
    // blank lines ignored, unrecognized keys ignored for forward
    // compatibility). Returns std::nullopt only if the file cannot be
    // opened — a config that exists but has a stray bad line still loads
    // with defaults for the affected field, since a single typo
    // shouldn't be indistinguishable from "no config at all" to a caller
    // deciding whether facial auth is available.
    static std::optional<Config> load(const std::string& path);

    // Defaults only, no file I/O — used by unit tests and as the baseline
    // Config::load() starts from before overlaying the file's contents.
    static Config defaults();
};

// Applies a single `key=value` pair the same way Config::load() applies
// one line of the config file. Used by facial-auth-enroll's
// --write-config mode (see the GUI SettingsPage design in the project
// plan) to update individual settings without a full file round-trip in
// the caller. Unknown keys are ignored.
void applyConfigOverride(Config& config, const std::string& key, const std::string& value);

// Serializes back to the same `key = value` text format Config::load()
// parses — the counterpart --write-config uses to persist updates.
std::string serialize(const Config& config);

}  // namespace facial_auth
