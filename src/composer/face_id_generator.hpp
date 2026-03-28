#pragma once

#include "detection/detection_result.hpp"
#include "internal/error.hpp"
#include "internal/secure_buffer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <memory>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <span>
#include <string_view>
#include <vector>

namespace axvp::internal {

class FaceIDGenerator final {
  public:
    FaceIDGenerator() = delete;
    FaceIDGenerator(const FaceIDGenerator &) = delete;
    FaceIDGenerator &operator=(const FaceIDGenerator &) = delete;
    FaceIDGenerator(FaceIDGenerator &&) noexcept = default;
    FaceIDGenerator &operator=(FaceIDGenerator &&) noexcept = default;
    ~FaceIDGenerator() = default;

    [[nodiscard]] static std::expected<FaceIDGenerator, Error> create() noexcept {
        auto key = generate_key();
        if (!key.has_value()) {
            return std::unexpected(key.error());
        }

        return FaceIDGenerator{std::move(*key)};
    }

    [[nodiscard]] std::expected<void, Error> rotate_key() noexcept {
        auto key = generate_key();
        if (!key.has_value()) {
            return std::unexpected(key.error());
        }

        key_ = std::move(*key);
        return {};
    }

    [[nodiscard]] std::expected<std::array<std::uint8_t, 16U>, Error>
    generate(const DetectionResult &detection, std::size_t face_index) const
        noexcept {
        if (face_index >= detection.size()) {
            return std::unexpected(Error::ConfigInvalidValue);
        }

        const auto bbox = detection.bbox(face_index);
        const auto &landmarks_x = detection.landmark_x_for(face_index);
        const auto &landmarks_y = detection.landmark_y_for(face_index);
        const auto &landmarks_z = detection.landmark_z_for(face_index);

        std::vector<float> descriptor;
        descriptor.reserve(8U + (landmarks_x.size() * 3U));

        const float center_x = bbox[0] + (bbox[2] * 0.5f);
        const float center_y = bbox[1] + (bbox[3] * 0.5f);
        const float scale =
            std::max({std::fabs(bbox[2]), std::fabs(bbox[3]), 1.0f});
        descriptor.push_back(center_x / scale);
        descriptor.push_back(center_y / scale);
        descriptor.push_back(bbox[2] / scale);
        descriptor.push_back(bbox[3] / scale);

        for (std::size_t index = 0U; index < landmarks_x.size(); ++index) {
            descriptor.push_back((landmarks_x[index] - center_x) / scale);
            descriptor.push_back((landmarks_y[index] - center_y) / scale);
            descriptor.push_back(landmarks_z.size() > index ? landmarks_z[index]
                                                            : 0.0f);
        }

        std::array<std::uint8_t, 16U> face_id{};
        unsigned int digest_len = 0U;
        std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
        const int ok = HMAC(
                          EVP_sha256(),
                          static_cast<const void *>(key_.data()),
                          static_cast<int>(key_.size()),
                          reinterpret_cast<const unsigned char *>(
                              descriptor.data()),
                          descriptor.size() * sizeof(float), digest.data(),
                          &digest_len)
                      ? 1
                      : 0;
        if (ok != 1 || digest_len < face_id.size()) {
            return std::unexpected(Error::SecurityIntegrityViolation);
        }

        std::memcpy(face_id.data(), digest.data(), face_id.size());
        return face_id;
    }

  private:
    explicit FaceIDGenerator(SecureBuffer key) noexcept : key_(std::move(key)) {}

    [[nodiscard]] static std::expected<SecureBuffer, Error>
    generate_key() noexcept {
        auto key = SecureBuffer::create(32U);
        if (!key.has_value()) {
            return std::unexpected(key.error());
        }

        if (RAND_bytes(reinterpret_cast<unsigned char *>(key->data()),
                       static_cast<int>(key->size())) != 1) {
            return std::unexpected(Error::SecurityIntegrityViolation);
        }

        return key;
    }

    SecureBuffer key_{};
};

} // namespace axvp::internal
