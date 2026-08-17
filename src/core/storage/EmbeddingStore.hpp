#pragma once

#include <optional>
#include <string>

#include "EmbeddingFormat.hpp"

namespace facial_auth {

struct EnrollmentMetadata {
    std::string modelId;
    CameraMode cameraMode = CameraMode::IR;
    int sampleCount = 0;
    std::string enrolledAtIso8601;
    // Number of angle-bucket templates stored (see AngleBucket.hpp).
    // Defaults to 1 for meta.json files written before this field existed
    // — a v1-format enrollment is exactly one Center-tagged template.
    int angleBucketCount = 1;
};

// Reads/writes /var/lib/facial-auth/<username>/{embedding.bin,meta.json}.
// Directory is created 0700, files 0600, all root-owned in normal
// operation (facial-auth-enroll runs as root; facial-auth-verify inherits
// root from its PAM-module parent). Deliberately has no dependency on
// Logger so it stays trivially unit-testable — callers log on failure.
class EmbeddingStore {
public:
    explicit EmbeddingStore(std::string baseDir = "/var/lib/facial-auth");

    // Rejects empty usernames and anything containing '/' or "..", so a
    // malicious or malformed username can never escape baseDir.
    bool isValidUsername(const std::string& username) const;

    // `records` must be non-empty (see EmbeddingFormat::serializeEmbeddings).
    bool saveAll(const std::string& username, const std::vector<EmbeddingRecord>& records,
                 const EnrollmentMetadata& metadata);

    // std::nullopt covers: invalid username, no enrollment on file, or a
    // corrupt/wrong-version embedding.bin. A v1-format file loads as a
    // single Center-tagged record — see EmbeddingFormat::deserializeEmbeddings.
    std::optional<std::vector<EmbeddingRecord>> loadAll(const std::string& username) const;
    std::optional<EnrollmentMetadata> loadMetadata(const std::string& username) const;

    bool remove(const std::string& username);

private:
    std::string baseDir_;
    std::string userDir(const std::string& username) const;
};

}  // namespace facial_auth
