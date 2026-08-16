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
            continue;  // per-attempt timeout/transient I/O error — retry within budget
        }

        const auto faces = detector.detect(*frameOpt);
        if (faces.size() != 1) {
            // 0 faces: nobody in frame yet, keep trying. >1 faces:
            // ambiguous — Phase 1 policy is to reject rather than guess.
            continue;
        }

        const cv::Mat aligned = embedder.alignAndCrop(*frameOpt, faces.front());
        const cv::Mat probe = embedder.extractEmbedding(aligned);

        if (matcher.isMatch(probe, enrolledMat)) {
            return VerifyOutcome::Match;
        }
        sawARealFace = true;  // a face was genuinely seen and compared, just didn't match
    }

    return sawARealFace ? VerifyOutcome::NoMatch : VerifyOutcome::Unavailable;
}

}  // namespace facial_auth
