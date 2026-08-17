#include "VerifyRunner.hpp"

#include <opencv2/core.hpp>

#include "core/camera/V4L2Camera.hpp"
#include "core/config/Config.hpp"
#include "core/face/FaceDetector.hpp"
#include "core/face/FaceEmbedder.hpp"
#include "core/log/Logger.hpp"
#include "core/match/EmbeddingMatcher.hpp"
#include "core/storage/EmbeddingStore.hpp"

namespace facial_auth {

VerifyOutcome runVerification(const std::string& username) {
    const auto configOpt = Config::load("/etc/facial-auth/config.conf");
    if (!configOpt) {
        Logger::log(LogLevel::Error, "runVerification: failed to load /etc/facial-auth/config.conf");
        return VerifyOutcome::Unavailable;
    }
    const Config& config = *configOpt;

    EmbeddingStore store;
    const auto enrolledOpt = store.load(username);
    if (!enrolledOpt) {
        Logger::log(LogLevel::Warning,
                    "runVerification: no enrollment on file for user '" + username + "'");
        return VerifyOutcome::Unavailable;
    }
    const cv::Mat enrolledMat = toMat(*enrolledOpt);

    CameraConfig cameraConfig;
    cameraConfig.devicePath = config.devicePath;
    cameraConfig.pixelFormat = config.pixelFormat;
    cameraConfig.width = config.frameWidth;
    cameraConfig.height = config.frameHeight;
    cameraConfig.timeoutMs = config.captureTimeoutMs;
    cameraConfig.illuminationGain = config.illuminationGain;

    V4L2Camera camera(cameraConfig);
    if (!camera.open()) {
        Logger::log(LogLevel::Error, "runVerification: failed to open camera '" + config.devicePath + "'");
        return VerifyOutcome::Unavailable;
    }

    FaceDetector detector(config.detectorModelPath);
    FaceEmbedder embedder(config.embedderModelPath);
    EmbeddingMatcher matcher(distanceMetricFromString(config.distanceMetric), config.matchThreshold);

    bool sawARealFace = false;
    for (int attempt = 0; attempt < config.maxCaptureAttempts; ++attempt) {
        const auto frameOpt = camera.captureFrame();
        if (!frameOpt) {
            Logger::log(LogLevel::Debug, "runVerification: attempt " + std::to_string(attempt) +
                                              ": capture timeout/transient I/O error, retrying");
            continue;  // per-attempt timeout/transient I/O error — retry within budget
        }

        const auto faces = detector.detect(*frameOpt);
        if (faces.empty()) {
            Logger::log(LogLevel::Debug,
                        "runVerification: attempt " + std::to_string(attempt) + ": 0 faces detected");
            continue;
        }
        if (faces.size() > 1) {
            Logger::log(LogLevel::Warning, "runVerification: attempt " + std::to_string(attempt) +
                                                ": " + std::to_string(faces.size()) +
                                                " faces detected, ambiguous — rejecting");
            continue;
        }

        const cv::Mat aligned = embedder.alignAndCrop(*frameOpt, faces.front());
        const cv::Mat probe = embedder.extractEmbedding(aligned);
        sawARealFace = true;  // a face was genuinely seen and compared, just didn't match (yet)

        const double dist = matcher.distance(probe, enrolledMat);
        if (dist <= config.matchThreshold) {
            Logger::log(LogLevel::Info, "runVerification: attempt " + std::to_string(attempt) +
                                             ": match, distance=" + std::to_string(dist) +
                                             " threshold=" + std::to_string(config.matchThreshold));
            return VerifyOutcome::Match;
        }
        Logger::log(LogLevel::Info, "runVerification: attempt " + std::to_string(attempt) +
                                         ": no match, distance=" + std::to_string(dist) +
                                         " threshold=" + std::to_string(config.matchThreshold));
    }

    Logger::log(LogLevel::Info, "runVerification: exhausted " +
                                     std::to_string(config.maxCaptureAttempts) +
                                     " attempts, sawARealFace=" + (sawARealFace ? "true" : "false"));
    return sawARealFace ? VerifyOutcome::NoMatch : VerifyOutcome::Unavailable;
}

}  // namespace facial_auth
