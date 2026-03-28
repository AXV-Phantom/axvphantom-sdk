#pragma once

#include "axvphantom/axvphantom.h"
#include "internal/error.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <memory>
#include <opencv2/objdetect/face.hpp>
#include <openssl/evp.h>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace axvp::internal {

inline constexpr std::string_view kDefaultDetectorModelRelativePath =
    "models/face_detection_yunet/face_detection_yunet_2023mar.onnx";
inline constexpr std::string_view kDefaultDetectorModelSha256 =
    "8f2383e4dd3cfbb4553ea8718107fc0423210dc964f9f4280604804ed2552fa4";
inline constexpr std::uintmax_t kDefaultDetectorModelSize = 232589U;
inline const cv::Size kDefaultDetectorInputSize{320, 320};

class DetectorModel final {
  public:
    DetectorModel() = delete;
    DetectorModel(const DetectorModel &) = delete;
    DetectorModel &operator=(const DetectorModel &) = delete;
    DetectorModel(DetectorModel &&) noexcept = default;
    DetectorModel &operator=(DetectorModel &&) noexcept = default;
    ~DetectorModel() = default;

    [[nodiscard]] static std::expected<DetectorModel, Error>
    create(const axvp_config_t &cfg) noexcept {
        const auto resolved_path = resolve_model_path(cfg);
        if (!resolved_path.has_value()) {
            return std::unexpected(resolved_path.error());
        }

        const bool use_default_validation =
            cfg.detector_model_path == nullptr ||
            cfg.detector_model_path[0] == '\0';
        const auto validation =
            resolve_validation_spec(cfg, use_default_validation);
        if (!validation.has_value()) {
            return std::unexpected(validation.error());
        }

        const auto actual_size = read_file_size(*resolved_path);
        if (!actual_size.has_value()) {
            return std::unexpected(actual_size.error());
        }

        if (validation->size != 0U && *actual_size != validation->size) {
            return std::unexpected(Error::SecurityModelTampered);
        }

        const auto actual_checksum = compute_sha256(*resolved_path);
        if (!actual_checksum.has_value()) {
            return std::unexpected(actual_checksum.error());
        }

        if (!validation->sha256.empty() &&
            normalize_hex(*actual_checksum) !=
                normalize_hex(validation->sha256)) {
            return std::unexpected(Error::SecurityModelTampered);
        }

        const auto detector = create_detector(*resolved_path);
        if (!detector.has_value()) {
            return std::unexpected(detector.error());
        }

        return DetectorModel{*resolved_path, *actual_size,
                             std::move(*actual_checksum), *detector};
    }

    [[nodiscard]] const cv::Ptr<cv::FaceDetectorYN> &detector() const noexcept {
        return detector_;
    }

    [[nodiscard]] const std::filesystem::path &model_path() const noexcept {
        return model_path_;
    }

    [[nodiscard]] std::uintmax_t model_size() const noexcept {
        return model_size_;
    }

    [[nodiscard]] std::string_view checksum() const noexcept {
        return checksum_;
    }

  private:
    struct ValidationSpec final {
        std::string sha256;
        std::uintmax_t size = 0U;
    };

    DetectorModel(std::filesystem::path model_path, std::uintmax_t model_size,
                  std::string checksum,
                  cv::Ptr<cv::FaceDetectorYN> detector) noexcept
        : model_path_(std::move(model_path)), model_size_(model_size),
          checksum_(std::move(checksum)), detector_(std::move(detector)) {}

    [[nodiscard]] static std::expected<std::filesystem::path, Error>
    resolve_model_path(const axvp_config_t &cfg) noexcept {
        const std::filesystem::path model_dir =
            cfg.model_dir == nullptr || cfg.model_dir[0] == '\0'
                ? std::filesystem::path{}
                : std::filesystem::path{cfg.model_dir};

        const std::filesystem::path model_path =
            cfg.detector_model_path == nullptr ||
                    cfg.detector_model_path[0] == '\0'
                ? std::filesystem::path{kDefaultDetectorModelRelativePath}
                : std::filesystem::path{cfg.detector_model_path};

        if (model_path.is_absolute() || model_dir.empty()) {
            return model_path;
        }

        return model_dir / model_path;
    }

    [[nodiscard]] static std::expected<ValidationSpec, Error>
    resolve_validation_spec(const axvp_config_t &cfg,
                            bool use_default_validation) noexcept {
        ValidationSpec spec{};

        if (cfg.detector_model_sha256 != nullptr &&
            cfg.detector_model_sha256[0] != '\0') {
            spec.sha256 = cfg.detector_model_sha256;
        } else if (use_default_validation) {
            spec.sha256 = std::string{kDefaultDetectorModelSha256};
        } else {
            return std::unexpected(Error::ConfigMissingValue);
        }

        if (cfg.detector_model_size != 0U) {
            spec.size = static_cast<std::uintmax_t>(cfg.detector_model_size);
        } else if (use_default_validation) {
            spec.size = kDefaultDetectorModelSize;
        } else {
            return std::unexpected(Error::ConfigMissingValue);
        }

        return spec;
    }

    [[nodiscard]] static std::expected<std::uintmax_t, Error>
    read_file_size(const std::filesystem::path &path) noexcept {
        std::error_code ec;
        const auto size = std::filesystem::file_size(path, ec);
        if (ec) {
            return std::unexpected(Error::ResourceNotFound);
        }

        return size;
    }

    [[nodiscard]] static std::expected<std::string, Error>
    compute_sha256(const std::filesystem::path &path) noexcept {
        std::ifstream input(path, std::ios::binary);
        if (!input.is_open()) {
            return std::unexpected(Error::ResourceNotFound);
        }

        EVP_MD_CTX *ctx = EVP_MD_CTX_new();
        if (ctx == nullptr) {
            return std::unexpected(Error::ResourceAllocationFailed);
        }

        std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> guard(
            ctx, &EVP_MD_CTX_free);

        if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1) {
            return std::unexpected(Error::SecurityIntegrityViolation);
        }

        constexpr std::size_t kChunkSize =
            static_cast<std::size_t>(64U) * 1024U;
        std::array<char, kChunkSize> buffer{};
        while (input) {
            input.read(buffer.data(),
                       static_cast<std::streamsize>(buffer.size()));
            const std::streamsize count = input.gcount();
            if (count > 0 &&
                EVP_DigestUpdate(ctx, buffer.data(),
                                 static_cast<std::size_t>(count)) != 1) {
                return std::unexpected(Error::SecurityIntegrityViolation);
            }
        }

        if (!input.eof() && input.fail()) {
            return std::unexpected(Error::ResourceNotFound);
        }

        std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
        unsigned int digest_len = 0U;
        if (EVP_DigestFinal_ex(ctx, digest.data(), &digest_len) != 1) {
            return std::unexpected(Error::SecurityIntegrityViolation);
        }

        static constexpr char kHexDigits[] = "0123456789abcdef";
        std::string hex;
        hex.reserve(static_cast<std::size_t>(digest_len) * 2U);
        for (unsigned int index = 0U; index < digest_len; ++index) {
            const unsigned char byte = digest[index];
            hex.push_back(kHexDigits[(byte >> 4U) & 0x0FU]);
            hex.push_back(kHexDigits[byte & 0x0FU]);
        }

        return hex;
    }

    [[nodiscard]] static std::expected<cv::Ptr<cv::FaceDetectorYN>, Error>
    create_detector(const std::filesystem::path &path) noexcept {
        try {
            const auto detector = cv::FaceDetectorYN::create(
                path.string(), std::string{}, kDefaultDetectorInputSize);
            if (detector == nullptr) {
                return std::unexpected(Error::ConfigUnsupportedValue);
            }

            return detector;
        } catch (const cv::Exception &) {
            return std::unexpected(Error::ConfigUnsupportedValue);
        }
    }

    [[nodiscard]] static std::string normalize_hex(std::string_view value) {
        std::string normalized(value);
        std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                       [](unsigned char ch) {
                           return static_cast<char>(std::tolower(ch));
                       });
        return normalized;
    }

    std::filesystem::path model_path_{};
    std::uintmax_t model_size_ = 0U;
    std::string checksum_{};
    cv::Ptr<cv::FaceDetectorYN> detector_{};
};

} // namespace axvp::internal
