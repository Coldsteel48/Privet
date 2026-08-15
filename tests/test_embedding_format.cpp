#include "minitest.hpp"

#include "core/storage/EmbeddingFormat.hpp"

using namespace facial_auth;

TEST(RoundTripPreservesAllFields) {
    EmbeddingRecord record;
    record.modelId = "sface-test-1.0";
    record.cameraMode = CameraMode::RGB;
    record.vector = {0.1f, -0.2f, 0.3f, 0.0f};

    const auto bytes = serializeEmbedding(record);
    const auto restored = deserializeEmbedding(bytes);

    ASSERT_EQ(restored.modelId, record.modelId);
    ASSERT_TRUE(restored.cameraMode == record.cameraMode);
    ASSERT_EQ(restored.vector.size(), record.vector.size());
    for (std::size_t i = 0; i < record.vector.size(); ++i) {
        ASSERT_NEAR(restored.vector[i], record.vector[i], 1e-6);
    }
}

TEST(RejectsBadMagic) {
    EmbeddingRecord record;
    record.modelId = "x";
    record.vector = {1.f};
    auto bytes = serializeEmbedding(record);
    bytes[0] = 'X';  // corrupt magic
    ASSERT_THROWS(deserializeEmbedding(bytes));
}

TEST(RejectsWrongVersion) {
    EmbeddingRecord record;
    record.modelId = "x";
    record.vector = {1.f};
    auto bytes = serializeEmbedding(record);
    bytes[4] = 0xFF;  // version u32 immediately follows the 4-byte magic
    ASSERT_THROWS(deserializeEmbedding(bytes));
}

TEST(RejectsTruncatedData) {
    EmbeddingRecord record;
    record.modelId = "x";
    record.vector = {1.f, 2.f, 3.f};
    auto bytes = serializeEmbedding(record);
    bytes.resize(bytes.size() - 2);  // chop off part of the last float
    ASSERT_THROWS(deserializeEmbedding(bytes));
}

TEST(ToMatFromMatRoundTrip) {
    EmbeddingRecord record;
    record.modelId = "x";
    record.cameraMode = CameraMode::IR;
    record.vector = {1.f, 2.f, 3.f};

    const cv::Mat mat = toMat(record);
    ASSERT_EQ(mat.cols, 3);
    ASSERT_EQ(mat.rows, 1);

    const auto back = fromMat(mat, "x", CameraMode::IR);
    ASSERT_EQ(back.vector.size(), record.vector.size());
}

MINITEST_MAIN()
