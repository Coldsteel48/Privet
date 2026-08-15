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

    bool save(const std::string& username, const EmbeddingRecord& record,
              const EnrollmentMetadata& metadata);

    // std::nullopt covers: invalid username, no enrollment on file, or a
    // corrupt/wrong-version embedding.bin.
    std::optional<EmbeddingRecord> load(const std::string& username) const;
    std::optional<EnrollmentMetadata> loadMetadata(const std::string& username) const;

    bool remove(const std::string& username);

private:
    std::string baseDir_;
    std::string userDir(const std::string& username) const;
};

}  // namespace facial_auth
