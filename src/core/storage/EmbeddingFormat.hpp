#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <opencv2/core.hpp>

#include "core/camera/CameraMode.hpp"

namespace facial_auth {

inline constexpr char kEmbeddingMagic[4] = {'F', 'A', 'E', '1'};
inline constexpr std::uint32_t kEmbeddingFormatVersion = 1;

// In-memory form of what's stored in embedding.bin. Tagged with modelId
// and cameraMode so a future embedding-model swap or an IR<->RGB mode
// change can never be silently compared against mismatched old data.
struct EmbeddingRecord {
    std::string modelId;  // e.g. "sface-1.0" — identifies which embedder produced `vector`
    CameraMode cameraMode = CameraMode::IR;
    std::vector<float> vector;
};

cv::Mat toMat(const EmbeddingRecord& record);
EmbeddingRecord fromMat(const cv::Mat& mat, const std::string& modelId, CameraMode mode);

// Binary layout: magic(4) + version(u32) + modelId length(u32) + modelId
// bytes + cameraMode(u8: 0=IR,1=RGB) + dim(u32) + float32[dim], all native
// byte order (single-machine, root-owned local storage — not intended for
// cross-machine transport).
std::vector<std::uint8_t> serializeEmbedding(const EmbeddingRecord& record);

// Throws std::runtime_error on bad magic, unsupported version, or
// truncated/malformed data.
EmbeddingRecord deserializeEmbedding(const std::vector<std::uint8_t>& bytes);

}  // namespace facial_auth
