// facial-auth-verify: does the actual camera capture + face match. Run
// exclusively by pam_facial.so via fork()/execve() with the PAM-resolved
// username as argv[1] — never invoked directly by a user in normal
// operation. Communicates its result ONLY via exit code:
//   0 = confident match          -> pam_facial.so returns PAM_SUCCESS
//   1 = genuine failed match     -> pam_facial.so returns PAM_AUTH_ERR
//   2 = unavailable/error/other  -> pam_facial.so returns PAM_AUTHINFO_UNAVAIL
// See the project plan's "Privilege architecture" section for why this
// logic lives in its own process rather than inside pam_facial.so itself.

#include <exception>
#include <string>

#include <opencv2/core.hpp>

#include "core/camera/V4L2Camera.hpp"
#include "core/config/Config.hpp"
#include "core/face/FaceDetector.hpp"
#include "core/face/FaceEmbedder.hpp"
#include "core/log/Logger.hpp"
#include "core/match/EmbeddingMatcher.hpp"
#include "core/storage/EmbeddingStore.hpp"

namespace {

constexpr int kExitMatch = 0;
constexpr int kExitNoMatch = 1;
constexpr int kExitUnavailable = 2;

int runVerify(const std::string& username) {
    using namespace facial_auth;

    const auto configOpt = Config::load("/etc/facial-auth/config.conf");
    if (!configOpt) {
        Logger::log(LogLevel::Error,
                    "facial-auth-verify: failed to load /etc/facial-auth/config.conf");
        return kExitUnavailable;
    }
    const Config& config = *configOpt;

    EmbeddingStore store;
    const auto enrolledOpt = store.load(username);
    if (!enrolledOpt) {
        Logger::log(LogLevel::Warning,
                     "facial-auth-verify: no enrollment on file for user '" + username + "'");
        return kExitUnavailable;
    }
    const cv::Mat enrolledMat = toMat(*enrolledOpt);

    CameraConfig cameraConfig;
    cameraConfig.devicePath = config.devicePath;
    cameraConfig.pixelFormat = config.pixelFormat;
    cameraConfig.width = config.frameWidth;
    cameraConfig.height = config.frameHeight;
    cameraConfig.timeoutMs = config.captureTimeoutMs;

    V4L2Camera camera(cameraConfig);
    if (!camera.open()) {
        Logger::log(LogLevel::Error,
                    "facial-auth-verify: failed to open camera '" + config.devicePath + "'");
        return kExitUnavailable;
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
            return kExitMatch;
        }
        sawARealFace = true;  // a face was genuinely seen and compared, just didn't match
    }

    return sawARealFace ? kExitNoMatch : kExitUnavailable;
}

}  // namespace

int main(int argc, char** argv) {
    facial_auth::Logger logger("facial-auth-verify");

    if (argc != 2) {
        facial_auth::Logger::log(facial_auth::LogLevel::Error,
                                  "facial-auth-verify: expected exactly one argument (username)");
        return kExitUnavailable;
    }

    // This try/catch is this process's own boundary: its exit code is the
    // only thing pam_facial.so ever observes, so any exception anywhere
    // in the pipeline (bad model file, OpenCV error, etc.) must resolve
    // to a clean "unavailable" exit rather than a crash/nonzero signal
    // death, which pam_facial.so also maps to PAM_AUTHINFO_UNAVAIL but
    // this path logs the actual cause first.
    try {
        return runVerify(argv[1]);
    } catch (const std::exception& e) {
        facial_auth::Logger::log(facial_auth::LogLevel::Error,
                                  std::string("facial-auth-verify: unhandled exception: ") + e.what());
        return kExitUnavailable;
    } catch (...) {
        facial_auth::Logger::log(facial_auth::LogLevel::Error,
                                  "facial-auth-verify: unhandled unknown exception");
        return kExitUnavailable;
    }
}
