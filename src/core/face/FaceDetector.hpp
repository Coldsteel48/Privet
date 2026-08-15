#pragma once

#include <array>
#include <string>
#include <vector>

#include <opencv2/core.hpp>
#include <opencv2/objdetect/face.hpp>

namespace facial_auth {

struct DetectedFace {
    cv::Rect2f box;
    std::array<cv::Point2f, 5> landmarks;  // right eye, left eye, nose tip, right/left mouth corner
    float score = 0.0f;
};

// Thin wrapper over cv::FaceDetectorYN (YuNet, ONNX, OpenCV Zoo). Accuracy
// on genuine IR frames is unverified until tested on real hardware — see
// the project plan's flagged risk; dlib is the documented fallback if
// YuNet proves inadequate on IR input.
class FaceDetector {
public:
    explicit FaceDetector(const std::string& modelPath, cv::Size inputSize = cv::Size(320, 320),
                           float scoreThreshold = 0.9f, float nmsThreshold = 0.3f, int topK = 5000);

    // Accepts BGR or single-channel grayscale input (grayscale is
    // converted to BGR internally, since FaceDetectorYN expects 3
    // channels — this is what makes the same wrapper usable for both the
    // YUYV/RGB path today and the GREY/Y16 IR path once implemented).
    // Returns faces sorted by descending score. May throw on an
    // unexpected frame format or an inference failure.
    std::vector<DetectedFace> detect(const cv::Mat& frame);

private:
    cv::Ptr<cv::FaceDetectorYN> detector_;
    cv::Size inputSize_;
};

}  // namespace facial_auth
