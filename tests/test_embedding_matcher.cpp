#include "minitest.hpp"

#include <initializer_list>

#include <opencv2/core.hpp>

#include "core/match/EmbeddingMatcher.hpp"

using namespace facial_auth;

namespace {
cv::Mat vec(std::initializer_list<float> values) {
    cv::Mat m(1, static_cast<int>(values.size()), CV_32F);
    int i = 0;
    for (float v : values) m.at<float>(0, i++) = v;
    return m;
}
}  // namespace

TEST(IdenticalVectorsHaveZeroCosineDistanceAndMatch) {
    EmbeddingMatcher matcher(DistanceMetric::Cosine, 0.01);
    const cv::Mat a = vec({1.f, 0.f, 0.f});
    ASSERT_NEAR(matcher.distance(a, a), 0.0, 1e-6);
    ASSERT_TRUE(matcher.isMatch(a, a));
}

TEST(OrthogonalVectorsHaveCosineDistanceOneAndDoNotMatch) {
    EmbeddingMatcher matcher(DistanceMetric::Cosine, 0.5);
    const cv::Mat a = vec({1.f, 0.f});
    const cv::Mat b = vec({0.f, 1.f});
    ASSERT_NEAR(matcher.distance(a, b), 1.0, 1e-6);
    ASSERT_TRUE(!matcher.isMatch(a, b));
}

TEST(OppositeVectorsHaveCosineDistanceTwo) {
    EmbeddingMatcher matcher(DistanceMetric::Cosine, 0.5);
    const cv::Mat a = vec({1.f, 0.f});
    const cv::Mat b = vec({-1.f, 0.f});
    ASSERT_NEAR(matcher.distance(a, b), 2.0, 1e-6);
}

TEST(EuclideanDistanceMatchesKnownVectors) {
    EmbeddingMatcher matcher(DistanceMetric::Euclidean, 5.5);
    const cv::Mat a = vec({0.f, 0.f});
    const cv::Mat b = vec({3.f, 4.f});  // classic 3-4-5 triangle
    ASSERT_NEAR(matcher.distance(a, b), 5.0, 1e-6);
    ASSERT_TRUE(matcher.isMatch(a, b));  // 5.0 <= 5.5
}

TEST(MismatchedLengthsThrow) {
    EmbeddingMatcher matcher(DistanceMetric::Cosine, 0.5);
    const cv::Mat a = vec({1.f, 0.f});
    const cv::Mat b = vec({1.f, 0.f, 0.f});
    ASSERT_THROWS(matcher.distance(a, b));
}

TEST(DistanceMetricFromStringParsesKnownValuesAndDefaultsToCosine) {
    ASSERT_TRUE(distanceMetricFromString("cosine") == DistanceMetric::Cosine);
    ASSERT_TRUE(distanceMetricFromString("euclidean") == DistanceMetric::Euclidean);
    ASSERT_TRUE(distanceMetricFromString("garbage") == DistanceMetric::Cosine);
}

MINITEST_MAIN()
