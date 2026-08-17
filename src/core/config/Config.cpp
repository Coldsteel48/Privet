#include "Config.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace facial_auth {

namespace {

std::string trim(const std::string& s) {
    const auto begin = s.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return "";
    const auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(begin, end - begin + 1);
}

bool toBool(const std::string& value, bool fallback) {
    std::string v = trim(value);
    std::transform(v.begin(), v.end(), v.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (v == "true" || v == "1" || v == "yes" || v == "on") return true;
    if (v == "false" || v == "0" || v == "no" || v == "off") return false;
    return fallback;
}

}  // namespace

void applyConfigOverride(Config& config, const std::string& key, const std::string& value) {
    if (key == "device_path") {
        config.devicePath = value;
    } else if (key == "pixel_format") {
        config.pixelFormat = pixelFormatFromString(value);
    } else if (key == "camera_mode") {
        config.cameraMode = cameraModeFromString(value);
    } else if (key == "require_ir") {
        config.requireIr = toBool(value, config.requireIr);
    } else if (key == "frame_width") {
        config.frameWidth = std::atoi(value.c_str());
    } else if (key == "frame_height") {
        config.frameHeight = std::atoi(value.c_str());
    } else if (key == "capture_timeout_ms") {
        config.captureTimeoutMs = std::atoi(value.c_str());
    } else if (key == "max_capture_attempts") {
        config.maxCaptureAttempts = std::atoi(value.c_str());
    } else if (key == "detector_model_path") {
        config.detectorModelPath = value;
    } else if (key == "embedder_model_path") {
        config.embedderModelPath = value;
    } else if (key == "distance_metric") {
        config.distanceMetric = value;
    } else if (key == "match_threshold") {
        config.matchThreshold = std::atof(value.c_str());
    } else if (key == "illumination_gain") {
        config.illuminationGain = std::atof(value.c_str());
    } else if (key == "enroll_video_duration_sec") {
        config.enrollVideoDurationSec = std::atoi(value.c_str());
    }
    // Unknown keys are intentionally ignored for forward compatibility.
}

Config Config::defaults() {
    return Config{};
}

std::optional<Config> Config::load(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return std::nullopt;
    }

    Config config = Config::defaults();
    std::string line;
    while (std::getline(file, line)) {
        const std::string trimmed = trim(line);
        if (trimmed.empty() || trimmed[0] == '#' || trimmed[0] == ';') continue;

        const auto eq = trimmed.find('=');
        if (eq == std::string::npos) continue;  // malformed line — skip, don't fail the whole load

        const std::string key = trim(trimmed.substr(0, eq));
        const std::string value = trim(trimmed.substr(eq + 1));
        if (key.empty()) continue;

        applyConfigOverride(config, key, value);
    }

    return config;
}

std::string serialize(const Config& config) {
    std::ostringstream out;
    out << "# facial-auth config — see docs/build-dependencies.md and config/facial-auth.conf.example\n"
        << "device_path = " << config.devicePath << "\n"
        << "pixel_format = " << toString(config.pixelFormat) << "\n"
        << "camera_mode = " << toString(config.cameraMode) << "\n"
        << "require_ir = " << (config.requireIr ? "true" : "false") << "\n"
        << "frame_width = " << config.frameWidth << "\n"
        << "frame_height = " << config.frameHeight << "\n"
        << "capture_timeout_ms = " << config.captureTimeoutMs << "\n"
        << "max_capture_attempts = " << config.maxCaptureAttempts << "\n"
        << "detector_model_path = " << config.detectorModelPath << "\n"
        << "embedder_model_path = " << config.embedderModelPath << "\n"
        << "distance_metric = " << config.distanceMetric << "\n"
        << "match_threshold = " << config.matchThreshold << "\n"
        << "illumination_gain = " << config.illuminationGain << "\n"
        << "enroll_video_duration_sec = " << config.enrollVideoDurationSec << "\n";
    return out.str();
}

}  // namespace facial_auth
