#include "EmbeddingStore.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace facial_auth {

namespace {

bool writeFileWithMode(const std::string& path, const std::vector<std::uint8_t>& data, mode_t mode) {
    const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, mode);
    if (fd < 0) return false;

    const std::uint8_t* p = data.data();
    std::size_t remaining = data.size();
    while (remaining > 0) {
        const ssize_t written = ::write(fd, p, remaining);
        if (written < 0) {
            ::close(fd);
            return false;
        }
        p += written;
        remaining -= static_cast<std::size_t>(written);
    }
    ::close(fd);
    return true;
}

bool writeFileWithMode(const std::string& path, const std::string& data, mode_t mode) {
    return writeFileWithMode(path, std::vector<std::uint8_t>(data.begin(), data.end()), mode);
}

std::optional<std::vector<std::uint8_t>> readFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return std::nullopt;
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(file),
                                      std::istreambuf_iterator<char>());
}

std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '"' || c == '\\') out.push_back('\\');
        out.push_back(c);
    }
    return out;
}

// Minimal top-level string/int field extraction for our own flat,
// internally-generated meta.json — not a general-purpose JSON parser.
std::optional<std::string> extractJsonString(const std::string& json, const std::string& key) {
    auto pos = json.find("\"" + key + "\"");
    if (pos == std::string::npos) return std::nullopt;
    pos = json.find(':', pos);
    if (pos == std::string::npos) return std::nullopt;
    pos = json.find('"', pos);
    if (pos == std::string::npos) return std::nullopt;
    const auto end = json.find('"', pos + 1);
    if (end == std::string::npos) return std::nullopt;
    return json.substr(pos + 1, end - pos - 1);
}

std::optional<int> extractJsonInt(const std::string& json, const std::string& key) {
    auto pos = json.find("\"" + key + "\"");
    if (pos == std::string::npos) return std::nullopt;
    pos = json.find(':', pos);
    if (pos == std::string::npos) return std::nullopt;
    ++pos;
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) ++pos;
    auto end = pos;
    while (end < json.size() &&
           (std::isdigit(static_cast<unsigned char>(json[end])) || json[end] == '-')) {
        ++end;
    }
    if (end == pos) return std::nullopt;
    return std::atoi(json.substr(pos, end - pos).c_str());
}

}  // namespace

EmbeddingStore::EmbeddingStore(std::string baseDir) : baseDir_(std::move(baseDir)) {}

bool EmbeddingStore::isValidUsername(const std::string& username) const {
    if (username.empty()) return false;
    if (username.find('/') != std::string::npos) return false;
    if (username.find("..") != std::string::npos) return false;
    return true;
}

std::string EmbeddingStore::userDir(const std::string& username) const {
    return baseDir_ + "/" + username;
}

bool EmbeddingStore::saveAll(const std::string& username, const std::vector<EmbeddingRecord>& records,
                              const EnrollmentMetadata& metadata) {
    if (!isValidUsername(username)) return false;
    if (records.empty()) return false;

    ::mkdir(baseDir_.c_str(), 0700);  // best-effort; may already exist

    const std::string dir = userDir(username);
    if (::mkdir(dir.c_str(), 0700) != 0 && errno != EEXIST) {
        return false;
    }

    if (!writeFileWithMode(dir + "/embedding.bin", serializeEmbeddings(records), 0600)) {
        return false;
    }

    std::ostringstream json;
    json << "{\n"
         << "  \"model_id\": \"" << jsonEscape(metadata.modelId) << "\",\n"
         << "  \"camera_mode\": \"" << toString(metadata.cameraMode) << "\",\n"
         << "  \"sample_count\": " << metadata.sampleCount << ",\n"
         << "  \"angle_bucket_count\": " << metadata.angleBucketCount << ",\n"
         << "  \"enrolled_at\": \"" << jsonEscape(metadata.enrolledAtIso8601) << "\"\n"
         << "}\n";
    return writeFileWithMode(dir + "/meta.json", json.str(), 0600);
}

std::optional<std::vector<EmbeddingRecord>> EmbeddingStore::loadAll(const std::string& username) const {
    if (!isValidUsername(username)) return std::nullopt;
    const auto bytes = readFile(userDir(username) + "/embedding.bin");
    if (!bytes) return std::nullopt;
    try {
        return deserializeEmbeddings(*bytes);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::optional<EnrollmentMetadata> EmbeddingStore::loadMetadata(const std::string& username) const {
    if (!isValidUsername(username)) return std::nullopt;

    std::ifstream file(userDir(username) + "/meta.json");
    if (!file.is_open()) return std::nullopt;
    std::stringstream buffer;
    buffer << file.rdbuf();
    const std::string json = buffer.str();

    EnrollmentMetadata metadata;
    metadata.modelId = extractJsonString(json, "model_id").value_or("");
    metadata.cameraMode = cameraModeFromString(extractJsonString(json, "camera_mode").value_or("ir"));
    metadata.sampleCount = extractJsonInt(json, "sample_count").value_or(0);
    metadata.angleBucketCount = extractJsonInt(json, "angle_bucket_count").value_or(1);
    metadata.enrolledAtIso8601 = extractJsonString(json, "enrolled_at").value_or("");
    return metadata;
}

bool EmbeddingStore::remove(const std::string& username) {
    if (!isValidUsername(username)) return false;
    const std::string dir = userDir(username);
    ::unlink((dir + "/embedding.bin").c_str());
    ::unlink((dir + "/meta.json").c_str());
    return ::rmdir(dir.c_str()) == 0 || errno == ENOENT;
}

}  // namespace facial_auth
