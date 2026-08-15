#pragma once

#include <string>

#include <opencv2/core.hpp>
#include <opencv2/objdetect/face.hpp>

#include "FaceDetector.hpp"

namespace facial_auth {

// Thin wrapper over cv::FaceRecognizerSF (SFace, ONNX, OpenCV Zoo).
class FaceEmbedder {
public:
    explicit FaceEmbedder(const std::string& modelPath);

    // Aligns and crops the given detected face out of the full frame
    // (frame should be in the same color format the detector saw it in).
    cv::Mat alignAndCrop(const cv::Mat& frame, const DetectedFace& face);

    // Returns a single 1xN float32 embedding vector for an already
    // aligned/cropped face (see alignAndCrop).
    cv::Mat extractEmbedding(const cv::Mat& alignedFace);

private:
    cv::Ptr<cv::FaceRecognizerSF> recognizer_;
};

}  // namespace facial_auth
