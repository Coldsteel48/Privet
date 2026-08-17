// facial-auth-enroll: privileged CLI that captures a user's face and
// writes their enrollment to /var/lib/facial-auth/. Run directly by an
// admin from a root shell, or elevated via `pkexec` by facial-auth-control
// (the GUI) — see the project plan's "Privilege architecture" section.
// Never invoked by pam_facial.so (that only execs facial-auth-verify).

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iostream>
#include <map>
#include <numeric>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <opencv2/core.hpp>

#include "core/camera/V4L2Camera.hpp"
#include "core/config/Config.hpp"
#include "core/face/AngleBucket.hpp"
#include "core/face/FaceDetector.hpp"
#include "core/face/FaceEmbedder.hpp"
#include "core/pam/PamServiceConfig.hpp"
#include "core/storage/EmbeddingStore.hpp"
#include "core/verify/VerifyRunner.hpp"

namespace {

using namespace facial_auth;

constexpr float kMinDetectionScore = 0.85f;

// Below this many usable (detected + landmark-valid) frames, bucketing
// degrades to a single Center template — identical to pre-multi-angle
// behavior. Below kMinFramesForFullGridBucketing but at/above this,
// only the yaw (left/right) axis is bucketed, pitch is ignored. Both
// thresholds assume ~4 frames/bucket as a sane per-template minimum (3
// yaw buckets, 9 full-grid buckets) — self-calibrating to whatever
// range of head motion the user actually produced, not a hardcoded
// angle threshold. See runEnroll's post-process/bucket phases.
constexpr int kMinFramesForYawBucketing = 12;
constexpr int kMinFramesForFullGridBucketing = 36;

// Before ever touching a live PAM config, require most of several fresh
// recognition attempts to actually match — not just one. Guards against
// enabling on a single lucky frame when lighting/positioning is marginal;
// see runPamEnable(). Threshold, not "all 5", so one dropped/blinked
// frame doesn't block an otherwise-working setup.
constexpr int kPamPreEnableAttempts = 5;
constexpr int kPamPreEnableMinPasses = 4;

struct Args {
    std::optional<std::string> user;
    bool reEnroll = false;
    bool remove = false;
    CameraMode cameraMode = CameraMode::IR;
    bool iUnderstandTheRisk = false;
    std::optional<double> illuminationGain;  // --illumination-gain: applied AND persisted, see runEnroll
    bool writeConfig = false;
    std::vector<std::pair<std::string, std::string>> configOverrides;  // --set key=value
    bool status = false;
    bool test = false;
    bool pamEnable = false;
    bool pamDisable = false;
    std::optional<std::string> service;  // --service, for --pam-enable/--pam-disable
    bool showHelp = false;
};

void printUsage() {
    std::cout <<
        "Usage: facial-auth-enroll [--user NAME] [--re-enroll] [--delete]\n"
        "                           [--camera-mode ir|rgb] [--i-understand-the-risk]\n"
        "                           [--illumination-gain VALUE]\n"
        "       facial-auth-enroll --status [--user NAME]\n"
        "       facial-auth-enroll --test [--user NAME]\n"
        "       facial-auth-enroll --write-config --set key=value [--set key=value ...]\n"
        "       facial-auth-enroll --pam-enable --service NAME [--user NAME]\n"
        "       facial-auth-enroll --pam-disable --service NAME\n"
        "\n"
        "Must be run as root (directly, or via pkexec from facial-auth-control) — even\n"
        "--status/--test, since /var/lib/facial-auth/ is root-only and this is the only\n"
        "sanctioned way for the unprivileged GUI to learn a user's enrollment state or\n"
        "exercise a real recognition attempt without going through PAM. --test runs the\n"
        "exact same capture/detect/match logic as facial-auth-verify (via facial_core's\n"
        "VerifyRunner) so a passing test means login will actually work.\n"
        "RGB camera mode is an explicit, at-your-own-risk opt-in: a plain webcam has\n"
        "no depth channel and is far more spoofable (photo/video replay) than IR.\n"
        "\n"
        "--pam-enable/--pam-disable wire pam_facial.so into (or out of) a real\n"
        "/etc/pam.d/NAME, always as \"sufficient\" and never removing the existing\n"
        "password auth line. NAME must be on the fixed allow-list in\n"
        "core/pam/PamServiceConfig.hpp (sudo/gdm-password/sddm/lightdm) — sshd and\n"
        "anything else are always rejected. --pam-enable additionally requires " +
            std::to_string(kPamPreEnableMinPasses) + "/" + std::to_string(kPamPreEnableAttempts) +
            " fresh\n"
            "recognition attempts to actually match first; it never writes to disk otherwise.\n";
}

std::optional<Args> parseArgs(int argc, char** argv) {
    Args args;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            args.showHelp = true;
        } else if (arg == "--user" && i + 1 < argc) {
            args.user = argv[++i];
        } else if (arg == "--re-enroll") {
            args.reEnroll = true;
        } else if (arg == "--delete") {
            args.remove = true;
        } else if (arg == "--camera-mode" && i + 1 < argc) {
            args.cameraMode = cameraModeFromString(argv[++i]);
        } else if (arg == "--i-understand-the-risk") {
            args.iUnderstandTheRisk = true;
        } else if (arg == "--illumination-gain" && i + 1 < argc) {
            args.illuminationGain = std::atof(argv[++i]);
        } else if (arg == "--write-config") {
            args.writeConfig = true;
        } else if (arg == "--status") {
            args.status = true;
        } else if (arg == "--test") {
            args.test = true;
        } else if (arg == "--pam-enable") {
            args.pamEnable = true;
        } else if (arg == "--pam-disable") {
            args.pamDisable = true;
        } else if (arg == "--service" && i + 1 < argc) {
            args.service = argv[++i];
        } else if (arg == "--set" && i + 1 < argc) {
            const std::string kv = argv[++i];
            const auto eq = kv.find('=');
            if (eq == std::string::npos) {
                std::cerr << "facial-auth-enroll: --set expects key=value, got '" << kv << "'\n";
                return std::nullopt;
            }
            args.configOverrides.emplace_back(kv.substr(0, eq), kv.substr(eq + 1));
        } else {
            std::cerr << "facial-auth-enroll: unrecognized argument '" << arg << "'\n";
            return std::nullopt;
        }
    }
    return args;
}

std::string resolveUsername(const Args& args) {
    if (args.user) return *args.user;
    if (const char* sudoUser = std::getenv("SUDO_USER")) return sudoUser;
    std::cerr << "facial-auth-enroll: no --user given and $SUDO_USER is not set\n";
    return "";
}

std::string nowIso8601() {
    const std::time_t t = std::time(nullptr);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&t));
    return buf;
}

// Shared wording with facial-auth-control's RiskDisclaimerDialog (kept in
// sync manually for now — see docs/ for the canonical text both should match).
bool confirmRgbRisk(bool alreadyAcknowledged) {
    if (alreadyAcknowledged) return true;

    if (!isatty(fileno(stdin))) {
        std::cerr << "facial-auth-enroll: --camera-mode rgb requires interactive confirmation "
                     "or --i-understand-the-risk in non-interactive contexts\n";
        return false;
    }

    std::cout <<
        "WARNING: a regular (non-IR) camera has no depth/liveness signal and can be\n"
        "fooled by a photo, video, or printed face. IR is strongly recommended.\n"
        "Type 'yes' to proceed at your own risk with a regular camera: ";
    std::string response;
    std::getline(std::cin, response);
    return response == "yes";
}

std::optional<std::string> readWholeFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) return std::nullopt;
    std::ostringstream buf;
    buf << in.rdbuf();
    return buf.str();
}

// Writes atomically (write-to-temp + fsync + rename, so a crash or power
// loss mid-write can never leave /etc/pam.d/NAME half-written) and
// preserves the original file's mode rather than assuming one, since
// this is the one write path in this codebase that touches a file other
// than this project's own — see runPamEnable/runPamDisable.
bool writeFileAtomic(const std::string& path, const std::string& content) {
    mode_t mode = 0644;
    struct stat st{};
    if (::stat(path.c_str(), &st) == 0) {
        mode = st.st_mode & 07777;
    }

    const std::string tmpPath = path + ".pam_facial.tmp";
    const int fd = ::open(tmpPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, mode);
    if (fd < 0) return false;

    const char* data = content.data();
    std::size_t remaining = content.size();
    while (remaining > 0) {
        const ssize_t written = ::write(fd, data, remaining);
        if (written < 0) {
            ::close(fd);
            ::unlink(tmpPath.c_str());
            return false;
        }
        data += written;
        remaining -= static_cast<std::size_t>(written);
    }
    if (::fsync(fd) != 0) {
        ::close(fd);
        ::unlink(tmpPath.c_str());
        return false;
    }
    ::close(fd);

    if (::rename(tmpPath.c_str(), path.c_str()) != 0) {
        ::unlink(tmpPath.c_str());
        return false;
    }
    return true;
}

// Wires pam_facial.so into (or out of) a real /etc/pam.d/NAME. This is
// the one privileged action in this whole codebase that touches
// something outside facial-auth's own files — see README's "never lock
// out" section and docs/testing-safely.md for why every safeguard here
// (fixed allow-list, "sufficient" only, never auto-touching a hand-edited
// line, a pre-enable recognition threshold, a one-time backup) exists.
//
// The pre-enable recognition check lives HERE rather than in
// facial-auth-control, deliberately: this binary is the actual privilege
// boundary (invoked via pkexec), so the one thing that must never happen
// — writing to a live PAM config on the strength of a single lucky match,
// or because a compromised/buggy GUI skipped its own check — has to be
// enforced on this side of that boundary, not trusted from the caller.
int runPamEnable(const std::string& service, const std::string& username) {
    if (!isAllowedPamService(service)) {
        std::cout << "STATUS=error MESSAGE=\"service '" << service << "' is not on the allow-list\"\n";
        return 1;
    }

    const std::string path = "/etc/pam.d/" + service;
    const auto contentOpt = readWholeFile(path);
    if (!contentOpt) {
        std::cout << "STATUS=error MESSAGE=\"" << path << " does not exist\"\n";
        return 1;
    }

    int passes = 0;
    for (int attempt = 0; attempt < kPamPreEnableAttempts; ++attempt) {
        const auto outcome = runVerification(username);
        std::cerr << "facial-auth-enroll: pre-enable check " << (attempt + 1) << "/"
                  << kPamPreEnableAttempts << ": "
                  << (outcome == VerifyOutcome::Match ? "match" : "no match") << "\n";
        if (outcome == VerifyOutcome::Match) ++passes;
    }
    if (passes < kPamPreEnableMinPasses) {
        std::cout << "STATUS=error MESSAGE=\"pre-enable recognition check only passed " << passes
                   << "/" << kPamPreEnableAttempts << " attempts (need at least "
                   << kPamPreEnableMinPasses << "/" << kPamPreEnableAttempts
                   << "); not touching " << path << "\" TEST_PASSES=" << passes << "\n";
        return 1;
    }

    std::string newContent;
    try {
        newContent = enableInContent(*contentOpt);
    } catch (const std::exception& e) {
        std::cout << "STATUS=error MESSAGE=\"" << e.what() << "\"\n";
        return 1;
    }

    if (newContent == *contentOpt) {
        std::cout << "STATUS=ok MESSAGE=\"already enabled\" TEST_PASSES=" << passes << "\n";
        return 0;
    }

    // One-time backup of the pristine pre-facial-auth file. Never
    // overwritten on subsequent runs, so it always reflects the state
    // before this tool ever touched the file, regardless of how many
    // times enable/disable gets toggled afterward.
    const std::string backupPath = path + ".pam_facial.orig";
    if (!readWholeFile(backupPath) && !writeFileAtomic(backupPath, *contentOpt)) {
        std::cout << "STATUS=error MESSAGE=\"failed to write backup " << backupPath << "\"\n";
        return 1;
    }

    if (!writeFileAtomic(path, newContent)) {
        std::cout << "STATUS=error MESSAGE=\"failed to write " << path << "\"\n";
        return 1;
    }

    std::cout << "STATUS=ok TEST_PASSES=" << passes << "\n";
    return 0;
}

// Unconditionally safe (see PamServiceConfig.hpp's disableInContent):
// strips any pam_facial.so line regardless of control flag, so this also
// doubles as the recovery path out of a hand-edited EnabledUnsafe state.
// No pre-check needed — this only ever narrows what a login accepts.
int runPamDisable(const std::string& service) {
    if (!isAllowedPamService(service)) {
        std::cout << "STATUS=error MESSAGE=\"service '" << service << "' is not on the allow-list\"\n";
        return 1;
    }

    const std::string path = "/etc/pam.d/" + service;
    const auto contentOpt = readWholeFile(path);
    if (!contentOpt) {
        std::cout << "STATUS=error MESSAGE=\"" << path << " does not exist\"\n";
        return 1;
    }

    const std::string newContent = disableInContent(*contentOpt);
    if (newContent == *contentOpt) {
        std::cout << "STATUS=ok MESSAGE=\"already disabled\"\n";
        return 0;
    }

    if (!writeFileAtomic(path, newContent)) {
        std::cout << "STATUS=error MESSAGE=\"failed to write " << path << "\"\n";
        return 1;
    }

    std::cout << "STATUS=ok\n";
    return 0;
}

int runWriteConfig(const Args& args) {
    const std::string path = "/etc/facial-auth/config.conf";
    Config config = Config::load(path).value_or(Config::defaults());
    for (const auto& [key, value] : args.configOverrides) {
        applyConfigOverride(config, key, value);
    }

    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open()) {
        std::cerr << "STATUS=error MESSAGE=\"failed to open " << path << " for writing\"\n";
        return 1;
    }
    out << serialize(config);
    std::cout << "STATUS=ok\n";
    return 0;
}

// Prints machine-parseable enrollment state for the given user. This is
// the only sanctioned way facial-auth-control (unprivileged) learns
// whether/how a user is enrolled, since /var/lib/facial-auth/ is
// root-only — see EnrollHelperRunner in the GUI, which pkexec's this.
int runStatus(const std::string& username) {
    EmbeddingStore store;
    if (!store.isValidUsername(username)) {
        std::cout << "STATUS=error MESSAGE=\"invalid username\"\n";
        return 1;
    }

    const auto metadataOpt = store.loadMetadata(username);
    if (!metadataOpt) {
        std::cout << "STATUS=ok ENROLLED=false\n";
        return 0;
    }

    std::cout << "STATUS=ok ENROLLED=true CAMERA_MODE=" << toString(metadataOpt->cameraMode)
              << " SAMPLES=" << metadataOpt->sampleCount
              << " ENROLLED_AT=\"" << metadataOpt->enrolledAtIso8601 << "\"\n";
    return 0;
}

// Drives facial-auth-control's "Test Recognition" button: runs the exact
// same VerifyRunner logic facial-auth-verify uses at real login time, so a
// user can confirm recognition actually works (and tune illumination_gain
// against it) without needing to trigger a real PAM authentication. Always
// exits 0/STATUS=ok — MATCH conveys the actual outcome, since "the test
// ran" and "you matched" are different questions and only the former
// should ever produce STATUS=error.
int runTest(const std::string& username) {
    switch (runVerification(username)) {
        case VerifyOutcome::Match:
            std::cout << "STATUS=ok MATCH=true\n";
            return 0;
        case VerifyOutcome::NoMatch:
            std::cout << "STATUS=ok MATCH=false\n";
            return 0;
        case VerifyOutcome::Unavailable:
        default:
            std::cout << "STATUS=ok MATCH=unavailable\n";
            return 0;
    }
}

int runDelete(const std::string& username) {
    EmbeddingStore store;
    if (!store.remove(username)) {
        std::cout << "STATUS=error MESSAGE=\"failed to remove enrollment for " << username << "\"\n";
        return 1;
    }
    std::cout << "STATUS=ok\n";
    return 0;
}

int runEnroll(const std::string& username, const Args& args) {
    EmbeddingStore store;
    if (!store.isValidUsername(username)) {
        std::cout << "STATUS=error MESSAGE=\"invalid username\"\n";
        return 1;
    }
    if (store.loadMetadata(username) && !args.reEnroll) {
        std::cout << "STATUS=error MESSAGE=\"user already enrolled, pass --re-enroll to overwrite\"\n";
        return 1;
    }

    if (args.cameraMode == CameraMode::RGB && !confirmRgbRisk(args.iUnderstandTheRisk)) {
        std::cout << "STATUS=error MESSAGE=\"RGB camera mode not confirmed\"\n";
        return 1;
    }

    const auto configOpt = Config::load("/etc/facial-auth/config.conf");
    if (!configOpt) {
        std::cout << "STATUS=error MESSAGE=\"failed to load /etc/facial-auth/config.conf\"\n";
        return 1;
    }
    Config config = *configOpt;
    config.cameraMode = args.cameraMode;

    // Whatever illumination_gain facial-auth-control's slider was set to
    // when the user pressed Enroll/Re-enroll is what actually worked for
    // this capture session, so persist it as the new default — the same
    // config real facial-auth-verify reads at login — rather than making
    // the user separately press "Save Illumination" first.
    if (args.illuminationGain) {
        config.illuminationGain = *args.illuminationGain;
        std::ofstream out("/etc/facial-auth/config.conf", std::ios::trunc);
        if (!out.is_open()) {
            std::cout << "STATUS=error MESSAGE=\"failed to persist illumination_gain to "
                         "/etc/facial-auth/config.conf\"\n";
            return 1;
        }
        out << serialize(config);
    }

    CameraConfig cameraConfig;
    cameraConfig.devicePath = config.devicePath;
    cameraConfig.pixelFormat = config.pixelFormat;
    cameraConfig.width = config.frameWidth;
    cameraConfig.height = config.frameHeight;
    cameraConfig.timeoutMs = config.captureTimeoutMs;
    cameraConfig.illuminationGain = config.illuminationGain;

    V4L2Camera camera(cameraConfig);
    if (!camera.open()) {
        std::cout << "STATUS=error MESSAGE=\"failed to open camera '" << config.devicePath << "'\"\n";
        return 1;
    }

    FaceDetector detector(config.detectorModelPath);
    FaceEmbedder embedder(config.embedderModelPath);

    // Phase 1 — Record: buffer raw frames for a fixed wall-clock window
    // while the user turns/tilts their head, rather than trying to steer
    // capture live. No cv::VideoWriter/file involved — frames just live in
    // memory for the (short-lived, root) duration of this process; see the
    // project plan for why (keeps the deliberately-trimmed OpenCV link
    // list, avoids an unverified FFmpeg backend dependency).
    std::cout << "Recording for " << config.enrollVideoDurationSec
              << "s — follow the prompts, keeping your face in frame:\n";
    struct Checkpoint {
        double atFraction;
        const char* message;
    };
    const std::vector<Checkpoint> checkpoints = {
        {0.0, "  Look straight at the camera..."},
        {0.20, "  Slowly turn your head to the left..."},
        {0.40, "  ...now to the right..."},
        {0.60, "  ...now tilt your head up..."},
        {0.75, "  ...now tilt your head down..."},
        {0.90, "  ...and return to center."},
    };
    std::vector<cv::Mat> rawFrames;
    std::size_t nextCheckpoint = 0;
    const auto recordStart = std::chrono::steady_clock::now();
    while (true) {
        const double elapsedSec =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - recordStart).count();
        if (elapsedSec >= config.enrollVideoDurationSec) break;
        while (nextCheckpoint < checkpoints.size() &&
               elapsedSec >= checkpoints[nextCheckpoint].atFraction * config.enrollVideoDurationSec) {
            std::cout << checkpoints[nextCheckpoint].message << "\n";
            ++nextCheckpoint;
        }
        if (const auto frameOpt = camera.captureFrame()) {
            rawFrames.push_back(*frameOpt);
        }
    }

    if (rawFrames.empty()) {
        std::cout << "STATUS=error MESSAGE=\"no frames captured\"\n";
        return 1;
    }

    // Phase 2 — Post-process: run detection/embedding over the whole
    // buffer now that recording is done. Also computes a yaw/pitch ratio
    // per usable frame from the detector's 5-point landmarks (right eye,
    // left eye, nose tip, right mouth corner, left mouth corner), used for
    // bucketing in phase 3. Which physical direction these ratios
    // correspond to isn't verified against real hardware and doesn't need
    // to be — see AngleBucket.hpp.
    struct FrameSample {
        cv::Mat embedding;
        float yawRatio = 0.0f;
        float pitchRatio = 0.0f;
    };
    std::vector<FrameSample> usable;
    for (const cv::Mat& frame : rawFrames) {
        const auto faces = detector.detect(frame);
        if (faces.size() != 1 || faces.front().score < kMinDetectionScore) {
            continue;  // 0/>1 faces, or low-confidence detection — skip this frame
        }
        const DetectedFace& face = faces.front();
        const cv::Point2f& rightEye = face.landmarks[0];
        const cv::Point2f& leftEye = face.landmarks[1];
        const cv::Point2f& nose = face.landmarks[2];
        const cv::Point2f& rightMouth = face.landmarks[3];
        const cv::Point2f& leftMouth = face.landmarks[4];

        const float interocular = std::fabs(leftEye.x - rightEye.x);
        const float eyeMidX = (rightEye.x + leftEye.x) / 2.0f;
        const float eyeMidY = (rightEye.y + leftEye.y) / 2.0f;
        const float mouthMidY = (rightMouth.y + leftMouth.y) / 2.0f;
        const float pitchSpan = mouthMidY - eyeMidY;
        if (interocular < 1.0f || std::fabs(pitchSpan) < 1.0f) {
            continue;  // degenerate landmarks (near-zero denominator) — skip rather than divide
        }

        FrameSample sample;
        const cv::Mat aligned = embedder.alignAndCrop(frame, face);
        sample.embedding = embedder.extractEmbedding(aligned);
        sample.yawRatio = (nose.x - eyeMidX) / interocular;
        sample.pitchRatio = (nose.y - eyeMidY) / pitchSpan;
        usable.push_back(std::move(sample));
        std::cout << "  processed sample " << usable.size() << " (of " << rawFrames.size()
                  << " frames captured)\n";
    }

    if (usable.empty()) {
        std::cout << "STATUS=error MESSAGE=\"no usable face samples captured\"\n";
        return 1;
    }

    // Phase 3 — Bucket: split usable frames by yaw/pitch tercile into a
    // self-calibrating grid (no hardcoded angle threshold — it adapts to
    // whatever range of motion the user actually produced), gracefully
    // degrading to fewer buckets when there isn't enough data to support
    // finer ones. A user who doesn't move their head still enrolls
    // successfully with a single Center template, exactly as before this
    // feature existed.
    std::map<AngleBucket, std::vector<cv::Mat>> bucketed;
    if (static_cast<int>(usable.size()) < kMinFramesForYawBucketing) {
        for (const auto& sample : usable) bucketed[AngleBucket::Center].push_back(sample.embedding);
    } else {
        std::vector<std::size_t> byYaw(usable.size());
        std::iota(byYaw.begin(), byYaw.end(), 0);
        std::sort(byYaw.begin(), byYaw.end(), [&](std::size_t a, std::size_t b) {
            return usable[a].yawRatio < usable[b].yawRatio;
        });
        std::vector<int> yawThird(usable.size());
        for (std::size_t rank = 0; rank < byYaw.size(); ++rank) {
            yawThird[byYaw[rank]] = static_cast<int>(rank * 3 / byYaw.size());  // 0, 1, or 2
        }

        if (static_cast<int>(usable.size()) < kMinFramesForFullGridBucketing) {
            // Not enough data for the full grid — yaw only, pitch ignored.
            static constexpr AngleBucket kYawOnly[3] = {AngleBucket::Left, AngleBucket::Center,
                                                          AngleBucket::Right};
            for (std::size_t i = 0; i < usable.size(); ++i) {
                bucketed[kYawOnly[yawThird[i]]].push_back(usable[i].embedding);
            }
        } else {
            std::vector<std::size_t> byPitch(usable.size());
            std::iota(byPitch.begin(), byPitch.end(), 0);
            std::sort(byPitch.begin(), byPitch.end(), [&](std::size_t a, std::size_t b) {
                return usable[a].pitchRatio < usable[b].pitchRatio;
            });
            std::vector<int> pitchThird(usable.size());
            for (std::size_t rank = 0; rank < byPitch.size(); ++rank) {
                pitchThird[byPitch[rank]] = static_cast<int>(rank * 3 / byPitch.size());  // 0, 1, or 2
            }

            // [pitchThird][yawThird] — full 3x3 yaw x pitch grid.
            static constexpr AngleBucket kGrid[3][3] = {
                {AngleBucket::UpLeft, AngleBucket::Up, AngleBucket::UpRight},
                {AngleBucket::Left, AngleBucket::Center, AngleBucket::Right},
                {AngleBucket::DownLeft, AngleBucket::Down, AngleBucket::DownRight},
            };
            for (std::size_t i = 0; i < usable.size(); ++i) {
                bucketed[kGrid[pitchThird[i]][yawThird[i]]].push_back(usable[i].embedding);
            }
        }
    }

    // Phase 4 — Average + store: one template per non-empty bucket.
    // Averaging (already L2-normalized, per SFace's own output convention)
    // embeddings then re-normalizing reduces sensitivity to any single bad
    // frame/pose/lighting condition within a bucket.
    const std::string modelId = "sface:" + config.embedderModelPath;
    std::vector<EmbeddingRecord> records;
    for (const auto& [bucket, embeddingsInBucket] : bucketed) {
        cv::Mat sum = cv::Mat::zeros(embeddingsInBucket.front().size(), CV_32F);
        for (const auto& e : embeddingsInBucket) sum += e;
        sum /= static_cast<float>(embeddingsInBucket.size());
        const double norm = cv::norm(sum, cv::NORM_L2);
        if (norm > 0.0) sum /= static_cast<float>(norm);
        records.push_back(fromMat(sum, modelId, config.cameraMode, bucket));
    }

    EnrollmentMetadata metadata;
    metadata.modelId = modelId;
    metadata.cameraMode = config.cameraMode;
    metadata.sampleCount = static_cast<int>(usable.size());
    metadata.angleBucketCount = static_cast<int>(records.size());
    metadata.enrolledAtIso8601 = nowIso8601();

    if (!store.saveAll(username, records, metadata)) {
        std::cout << "STATUS=error MESSAGE=\"failed to write enrollment to disk\"\n";
        return 1;
    }

    std::cout << "STATUS=ok ENROLLED=true SAMPLES=" << usable.size() << " ANGLE_BUCKETS="
              << records.size() << " CAMERA_MODE=" << toString(config.cameraMode)
              << " ENROLLED_AT=\"" << metadata.enrolledAtIso8601 << "\"\n";
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    const auto argsOpt = parseArgs(argc, argv);
    if (!argsOpt) {
        printUsage();
        return 1;
    }
    const Args& args = *argsOpt;

    if (args.showHelp) {
        printUsage();
        return 0;
    }

    try {
        if (args.writeConfig) {
            if (geteuid() != 0) {
                std::cerr << "facial-auth-enroll: --write-config must be run as root\n";
                return 1;
            }
            return runWriteConfig(args);
        }

        if (geteuid() != 0) {
            std::cerr << "facial-auth-enroll: must be run as root (directly, or via pkexec)\n";
            return 1;
        }

        if (args.pamEnable || args.pamDisable) {
            if (!args.service) {
                std::cout << "STATUS=error MESSAGE=\"--service is required\"\n";
                return 1;
            }
            if (args.pamDisable) {
                return runPamDisable(*args.service);
            }
            const std::string username = resolveUsername(args);
            if (username.empty()) return 1;
            return runPamEnable(*args.service, username);
        }

        const std::string username = resolveUsername(args);
        if (username.empty()) return 1;

        if (args.status) {
            return runStatus(username);
        }
        if (args.test) {
            return runTest(username);
        }
        if (args.remove) {
            return runDelete(username);
        }
        return runEnroll(username, args);
    } catch (const std::exception& e) {
        std::cerr << "facial-auth-enroll: error: " << e.what() << "\n";
        return 1;
    }
}
