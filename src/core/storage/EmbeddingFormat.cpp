#include "EmbeddingFormat.hpp"

#include <cstring>
#include <stdexcept>

namespace facial_auth {

namespace {

void appendU32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(&value);
    out.insert(out.end(), bytes, bytes + sizeof(value));
}

std::uint32_t readU32(const std::uint8_t* data) {
    std::uint32_t value;
    std::memcpy(&value, data, sizeof(value));
    return value;
}

}  // namespace

cv::Mat toMat(const EmbeddingRecord& record) {
    cv::Mat mat(1, static_cast<int>(record.vector.size()), CV_32F);
    std::memcpy(mat.ptr<float>(0), record.vector.data(), record.vector.size() * sizeof(float));
    return mat;
}

EmbeddingRecord fromMat(const cv::Mat& mat, const std::string& modelId, CameraMode mode) {
    cv::Mat asFloat;
    mat.reshape(1, 1).convertTo(asFloat, CV_32F);

    EmbeddingRecord record;
    record.modelId = modelId;
    record.cameraMode = mode;
    record.vector.assign(asFloat.ptr<float>(0), asFloat.ptr<float>(0) + asFloat.total());
    return record;
}

std::vector<std::uint8_t> serializeEmbedding(const EmbeddingRecord& record) {
    std::vector<std::uint8_t> out;
    out.insert(out.end(), std::begin(kEmbeddingMagic), std::end(kEmbeddingMagic));
    appendU32(out, kEmbeddingFormatVersion);

    appendU32(out, static_cast<std::uint32_t>(record.modelId.size()));
    out.insert(out.end(), record.modelId.begin(), record.modelId.end());

    out.push_back(record.cameraMode == CameraMode::RGB ? 1 : 0);

    appendU32(out, static_cast<std::uint32_t>(record.vector.size()));
    const auto* vecBytes = reinterpret_cast<const std::uint8_t*>(record.vector.data());
    out.insert(out.end(), vecBytes, vecBytes + record.vector.size() * sizeof(float));

    return out;
}

EmbeddingRecord deserializeEmbedding(const std::vector<std::uint8_t>& bytes) {
    std::size_t offset = 0;
    const auto require = [&](std::size_t n) {
        if (offset + n > bytes.size()) {
            throw std::runtime_error("EmbeddingFormat: truncated data");
        }
    };

    require(sizeof(kEmbeddingMagic));
    if (std::memcmp(bytes.data(), kEmbeddingMagic, sizeof(kEmbeddingMagic)) != 0) {
        throw std::runtime_error("EmbeddingFormat: bad magic bytes");
    }
    offset += sizeof(kEmbeddingMagic);

    require(sizeof(std::uint32_t));
    const std::uint32_t version = readU32(bytes.data() + offset);
    offset += sizeof(std::uint32_t);
    if (version != kEmbeddingFormatVersion) {
        throw std::runtime_error("EmbeddingFormat: unsupported format version " +
                                  std::to_string(version));
    }

    require(sizeof(std::uint32_t));
    const std::uint32_t modelIdLen = readU32(bytes.data() + offset);
    offset += sizeof(std::uint32_t);
    require(modelIdLen);
    std::string modelId(reinterpret_cast<const char*>(bytes.data() + offset), modelIdLen);
    offset += modelIdLen;

    require(1);
    const CameraMode mode = bytes[offset] != 0 ? CameraMode::RGB : CameraMode::IR;
    offset += 1;

    require(sizeof(std::uint32_t));
    const std::uint32_t dim = readU32(bytes.data() + offset);
    offset += sizeof(std::uint32_t);
    require(static_cast<std::size_t>(dim) * sizeof(float));

    EmbeddingRecord record;
    record.modelId = std::move(modelId);
    record.cameraMode = mode;
    record.vector.resize(dim);
    std::memcpy(record.vector.data(), bytes.data() + offset, dim * sizeof(float));

    return record;
}

}  // namespace facial_auth
