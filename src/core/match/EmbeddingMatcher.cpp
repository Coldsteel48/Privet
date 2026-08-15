#include "EmbeddingMatcher.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace facial_auth {

namespace {
std::string toLower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}
}  // namespace

DistanceMetric distanceMetricFromString(const std::string& text) {
    return toLower(text) == "euclidean" ? DistanceMetric::Euclidean : DistanceMetric::Cosine;
}

std::string toString(DistanceMetric metric) {
    return metric == DistanceMetric::Euclidean ? "euclidean" : "cosine";
}

EmbeddingMatcher::EmbeddingMatcher(DistanceMetric metric, double threshold)
    : metric_(metric), threshold_(threshold) {}

double EmbeddingMatcher::distance(const cv::Mat& a, const cv::Mat& b) const {
    if (a.empty() || b.empty() || a.total() != b.total()) {
        throw std::invalid_argument("EmbeddingMatcher: embeddings must be non-empty and equal length");
    }

    cv::Mat af, bf;
    a.reshape(1, 1).convertTo(af, CV_64F);
    b.reshape(1, 1).convertTo(bf, CV_64F);

    if (metric_ == DistanceMetric::Euclidean) {
        return cv::norm(af, bf, cv::NORM_L2);
    }

    const double dot = af.dot(bf);
    const double normA = cv::norm(af, cv::NORM_L2);
    const double normB = cv::norm(bf, cv::NORM_L2);
    if (normA == 0.0 || normB == 0.0) {
        return 2.0;  // maximally dissimilar by convention for a degenerate (all-zero) vector
    }
    const double cosineSimilarity = dot / (normA * normB);
    return 1.0 - cosineSimilarity;
}

bool EmbeddingMatcher::isMatch(const cv::Mat& probe, const cv::Mat& enrolled) const {
    return distance(probe, enrolled) <= threshold_;
}

}  // namespace facial_auth
