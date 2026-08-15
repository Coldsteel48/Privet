#pragma once

#include <string>

#include <opencv2/core.hpp>

namespace facial_auth {

enum class DistanceMetric { Cosine, Euclidean };

DistanceMetric distanceMetricFromString(const std::string& text);  // defaults to Cosine
std::string toString(DistanceMetric metric);

// Pure math over embedding vectors — no I/O, no model dependency, fully
// unit-testable with hand-constructed cv::Mat vectors.
class EmbeddingMatcher {
public:
    EmbeddingMatcher(DistanceMetric metric, double threshold);

    // For both metrics, smaller distance() means more similar, and
    // isMatch() is distance() <= threshold — this keeps "match" semantics
    // consistent regardless of which metric is configured. Cosine
    // distance is defined as (1 - cosine similarity), so it ranges [0, 2]
    // with 0 meaning identical direction.
    double distance(const cv::Mat& a, const cv::Mat& b) const;
    bool isMatch(const cv::Mat& probe, const cv::Mat& enrolled) const;

private:
    DistanceMetric metric_;
    double threshold_;
};

}  // namespace facial_auth
