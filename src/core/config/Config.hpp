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
    int maxCaptureAttempts = 3;

    std::string detectorModelPath = "/etc/facial-auth/models/face_detection_yunet.onnx";
    std::string embedderModelPath = "/etc/facial-auth/models/face_recognition_sface.onnx";

    // "cosine" | "euclidean" — parsed into match::DistanceMetric by
    // whoever constructs an EmbeddingMatcher; kept as a raw string here so
    // this header doesn't need to pull in OpenCV types.
    std::string distanceMetric = "cosine";
    double matchThreshold = 0.36;

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
