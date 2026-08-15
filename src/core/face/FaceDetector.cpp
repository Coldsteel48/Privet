#include "FaceDetector.hpp"

#include <algorithm>
#include <stdexcept>

#include <opencv2/imgproc.hpp>

namespace facial_auth {

namespace {

cv::Mat toDetectorInput(const cv::Mat& frame) {
    cv::Mat working = frame;
    if (working.depth() == CV_16U) {
        // Naive linear 16-to-8-bit scaling; revisit once real Y16 IR
        // frames are available to tune against actual sensor dynamic range.
        cv::Mat normalized;
        working.convertTo(normalized, CV_8U, 1.0 / 256.0);
        working = normalized;
    }

    if (working.channels() == 1) {
        cv::Mat bgr;
        cv::cvtColor(working, bgr, cv::COLOR_GRAY2BGR);
        return bgr;
    }
    if (working.channels() == 3) {
        return working;
    }
    throw std::runtime_error("FaceDetector: unsupported frame channel count (" +
                              std::to_string(working.channels()) + ")");
}

}  // namespace

FaceDetector::FaceDetector(const std::string& modelPath, cv::Size inputSize,
                            float scoreThreshold, float nmsThreshold, int topK)
    : inputSize_(inputSize) {
    detector_ = cv::FaceDetectorYN::create(modelPath, "", inputSize_, scoreThreshold, nmsThreshold,
                                            topK);
    if (!detector_) {
        throw std::runtime_error("FaceDetector: failed to load model at '" + modelPath + "'");
    }
}

std::vector<DetectedFace> FaceDetector::detect(const cv::Mat& frame) {
    if (frame.empty()) {
        return {};
    }

    const cv::Mat input = toDetectorInput(frame);
    if (input.size() != inputSize_) {
        detector_->setInputSize(input.size());
        inputSize_ = input.size();
    }

    cv::Mat results;
    detector_->detect(input, results);

    std::vector<DetectedFace> faces;
    faces.reserve(static_cast<std::size_t>(std::max(results.rows, 0)));
    for (int i = 0; i < results.rows; ++i) {
        const float* row = results.ptr<float>(i);
        DetectedFace face;
        face.box = cv::Rect2f(row[0], row[1], row[2], row[3]);
        for (int p = 0; p < 5; ++p) {
            face.landmarks[static_cast<std::size_t>(p)] = cv::Point2f(row[4 + p * 2], row[5 + p * 2]);
        }
        face.score = row[14];
        faces.push_back(face);
    }

    std::sort(faces.begin(), faces.end(),
              [](const DetectedFace& a, const DetectedFace& b) { return a.score > b.score; });
    return faces;
}

}  // namespace facial_auth
