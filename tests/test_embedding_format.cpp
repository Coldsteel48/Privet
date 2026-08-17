#include "minitest.hpp"

#include <cstring>

#include "core/storage/EmbeddingFormat.hpp"

using namespace facial_auth;

namespace {

void appendU32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(&value);
    out.insert(out.end(), bytes, bytes + sizeof(value));
}

// Hand-builds a legacy v1 embedding.bin byte buffer (magic + version(1) +
// modelId + cameraMode + dim + float32[dim], no recordCount/angleBucket) —
// the exact layout serializeEmbeddings() itself no longer writes, so this
// is the only way to keep the backward-compat path under test.
std::vector<std::uint8_t> buildLegacyV1Bytes(const std::string& modelId, CameraMode mode,
                                              const std::vector<float>& vec) {
    std::vector<std::uint8_t> out;
    out.insert(out.end(), std::begin(kEmbeddingMagic), std::end(kEmbeddingMagic));
    appendU32(out, 1);  // legacy version
    appendU32(out, static_cast<std::uint32_t>(modelId.size()));
    out.insert(out.end(), modelId.begin(), modelId.end());
    out.push_back(mode == CameraMode::RGB ? 1 : 0);
    appendU32(out, static_cast<std::uint32_t>(vec.size()));
    const auto* vecBytes = reinterpret_cast<const std::uint8_t*>(vec.data());
    out.insert(out.end(), vecBytes, vecBytes + vec.size() * sizeof(float));
    return out;
}

}  // namespace

TEST(RoundTripPreservesAllFields) {
    EmbeddingRecord record;
    record.modelId = "sface-test-1.0";
    record.cameraMode = CameraMode::RGB;
    record.angleBucket = AngleBucket::Left;
    record.vector = {0.1f, -0.2f, 0.3f, 0.0f};

    const auto bytes = serializeEmbeddings({record});
    const auto restored = deserializeEmbeddings(bytes);

    ASSERT_EQ(restored.size(), std::size_t{1});
    ASSERT_EQ(restored[0].modelId, record.modelId);
    ASSERT_TRUE(restored[0].cameraMode == record.cameraMode);
    ASSERT_TRUE(restored[0].angleBucket == record.angleBucket);
    ASSERT_EQ(restored[0].vector.size(), record.vector.size());
    for (std::size_t i = 0; i < record.vector.size(); ++i) {
        ASSERT_NEAR(restored[0].vector[i], record.vector[i], 1e-6);
    }
}

TEST(MultiRecordRoundTrip) {
    std::vector<EmbeddingRecord> records;
    for (auto bucket : {AngleBucket::Center, AngleBucket::Left, AngleBucket::UpRight}) {
        EmbeddingRecord record;
        record.modelId = "sface-test-1.0";
        record.cameraMode = CameraMode::IR;
        record.angleBucket = bucket;
        record.vector = {static_cast<float>(bucket), 1.0f, -1.0f};
        records.push_back(record);
    }

    const auto bytes = serializeEmbeddings(records);
    const auto restored = deserializeEmbeddings(bytes);

    ASSERT_EQ(restored.size(), records.size());
    for (std::size_t i = 0; i < records.size(); ++i) {
        ASSERT_TRUE(restored[i].angleBucket == records[i].angleBucket);
        ASSERT_TRUE(restored[i].cameraMode == records[i].cameraMode);
        ASSERT_EQ(restored[i].modelId, records[i].modelId);
        ASSERT_EQ(restored[i].vector.size(), records[i].vector.size());
        for (std::size_t j = 0; j < records[i].vector.size(); ++j) {
            ASSERT_NEAR(restored[i].vector[j], records[i].vector[j], 1e-6);
        }
    }
}

TEST(V1LegacyFileStillLoads) {
    const auto bytes = buildLegacyV1Bytes("sface-legacy", CameraMode::RGB, {1.f, 2.f, 3.f});
    const auto restored = deserializeEmbeddings(bytes);

    ASSERT_EQ(restored.size(), std::size_t{1});
    ASSERT_EQ(restored[0].modelId, "sface-legacy");
    ASSERT_TRUE(restored[0].cameraMode == CameraMode::RGB);
    ASSERT_TRUE(restored[0].angleBucket == AngleBucket::Center);
    ASSERT_EQ(restored[0].vector.size(), std::size_t{3});
    ASSERT_NEAR(restored[0].vector[0], 1.f, 1e-6);
    ASSERT_NEAR(restored[0].vector[1], 2.f, 1e-6);
    ASSERT_NEAR(restored[0].vector[2], 3.f, 1e-6);
}

TEST(RejectsBadMagic) {
    EmbeddingRecord record;
    record.modelId = "x";
    record.vector = {1.f};
    auto bytes = serializeEmbeddings({record});
    bytes[0] = 'X';  // corrupt magic
    ASSERT_THROWS(deserializeEmbeddings(bytes));
}

TEST(RejectsWrongVersion) {
    EmbeddingRecord record;
    record.modelId = "x";
    record.vector = {1.f};
    auto bytes = serializeEmbeddings({record});
    bytes[4] = 0xFF;  // version u32 immediately follows the 4-byte magic
    ASSERT_THROWS(deserializeEmbeddings(bytes));
}

TEST(RejectsTruncatedData) {
    EmbeddingRecord record;
    record.modelId = "x";
    record.vector = {1.f, 2.f, 3.f};
    auto bytes = serializeEmbeddings({record});
    bytes.resize(bytes.size() - 2);  // chop off part of the last float
    ASSERT_THROWS(deserializeEmbeddings(bytes));
}

TEST(SerializeRejectsEmptyRecordSet) {
    ASSERT_THROWS(serializeEmbeddings({}));
}

TEST(ToMatFromMatRoundTrip) {
    EmbeddingRecord record;
    record.modelId = "x";
    record.cameraMode = CameraMode::IR;
    record.vector = {1.f, 2.f, 3.f};

    const cv::Mat mat = toMat(record);
    ASSERT_EQ(mat.cols, 3);
    ASSERT_EQ(mat.rows, 1);

    const auto back = fromMat(mat, "x", CameraMode::IR, AngleBucket::Down);
    ASSERT_EQ(back.vector.size(), record.vector.size());
    ASSERT_TRUE(back.angleBucket == AngleBucket::Down);
}

MINITEST_MAIN()
