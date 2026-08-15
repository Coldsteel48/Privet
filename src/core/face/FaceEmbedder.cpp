#include "FaceEmbedder.hpp"

#include <stdexcept>

namespace facial_auth {

namespace {

// cv::FaceRecognizerSF::alignCrop expects the same 1x15 row format
// FaceDetectorYN::detect() produces (bbox + 5 landmark pairs + score);
// DetectedFace splits that out into named fields for readability
// elsewhere, so it's reassembled here for the one API that needs the raw
// layout.
cv::Mat toRawDetectionRow(const DetectedFace& face) {
    cv::Mat row(1, 15, CV_32F);
    float* p = row.ptr<float>(0);
    p[0] = face.box.x;
    p[1] = face.box.y;
    p[2] = face.box.width;
    p[3] = face.box.height;
    for (int i = 0; i < 5; ++i) {
        p[4 + i * 2] = face.landmarks[static_cast<std::size_t>(i)].x;
        p[5 + i * 2] = face.landmarks[static_cast<std::size_t>(i)].y;
    }
    p[14] = face.score;
    return row;
}

}  // namespace

FaceEmbedder::FaceEmbedder(const std::string& modelPath) {
    recognizer_ = cv::FaceRecognizerSF::create(modelPath, "");
    if (!recognizer_) {
        throw std::runtime_error("FaceEmbedder: failed to load model at '" + modelPath + "'");
    }
}

cv::Mat FaceEmbedder::alignAndCrop(const cv::Mat& frame, const DetectedFace& face) {
    cv::Mat aligned;
    recognizer_->alignCrop(frame, toRawDetectionRow(face), aligned);
    return aligned;
}

cv::Mat FaceEmbedder::extractEmbedding(const cv::Mat& alignedFace) {
    cv::Mat embedding;
    recognizer_->feature(alignedFace, embedding);
    return embedding.clone();
}

}  // namespace facial_auth
