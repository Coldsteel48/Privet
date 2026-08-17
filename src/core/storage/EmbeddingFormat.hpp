#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <opencv2/core.hpp>

#include "core/camera/CameraMode.hpp"
#include "core/face/AngleBucket.hpp"

namespace facial_auth {

inline constexpr char kEmbeddingMagic[4] = {'F', 'A', 'E', '1'};
inline constexpr std::uint32_t kEmbeddingFormatVersion = 2;

// In-memory form of one template stored in embedding.bin. A single
// enrollment now stores one EmbeddingRecord per angle bucket (see
// AngleBucket.hpp), all sharing the same modelId/cameraMode (tagged so a
// future embedding-model swap or an IR<->RGB mode change can never be
// silently compared against mismatched old data).
struct EmbeddingRecord {
    std::string modelId;  // e.g. "sface-1.0" — identifies which embedder produced `vector`
    CameraMode cameraMode = CameraMode::IR;
    AngleBucket angleBucket = AngleBucket::Center;
    std::vector<float> vector;
};

cv::Mat toMat(const EmbeddingRecord& record);
EmbeddingRecord fromMat(const cv::Mat& mat, const std::string& modelId, CameraMode mode,
                         AngleBucket angleBucket = AngleBucket::Center);

// Binary layout (v2): magic(4) + version(u32) + modelId length(u32) +
// modelId bytes + cameraMode(u8: 0=IR,1=RGB) + recordCount(u32), then per
// record: angleBucket(u8) + dim(u32) + float32[dim]. modelId/cameraMode
// are hoisted to this file-level header since every record in one
// enrollment session shares them. All native byte order (single-machine,
// root-owned local storage — not intended for cross-machine transport).
//
// `records` must be non-empty; all records must share modelId/cameraMode
// (only the first record's values are written).
std::vector<std::uint8_t> serializeEmbeddings(const std::vector<EmbeddingRecord>& records);

// Parses either the current v2 layout or the legacy v1 single-record
// layout (magic + version(1) + modelId + cameraMode + dim + float32[dim],
// no angle bucket) — a v1 file is returned as a 1-element vector tagged
// AngleBucket::Center, so old enrollments keep working read-only without
// forced migration. Throws std::runtime_error on bad magic, unsupported
// version, or truncated/malformed data.
std::vector<EmbeddingRecord> deserializeEmbeddings(const std::vector<std::uint8_t>& bytes);

}  // namespace facial_auth
