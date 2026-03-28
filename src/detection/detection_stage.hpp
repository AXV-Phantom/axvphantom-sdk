#pragma once

#include "detector_model.hpp"
#include "detection_result.hpp"
#include "internal/error.hpp"
#include "internal/unique_frame.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <memory>
#include <new>
#include <opencv2/face.hpp>
#include <opencv2/core.hpp>
#include <string_view>
#include <utility>
#include <vector>

namespace axvp::internal {

inline constexpr std::string_view kDefaultLandmarkModelRelativePath =
    "models/face_landmark/lbfmodel.yaml";
inline constexpr std::string_view kLegacyLandmarkModelRelativePath =
    "models/face_landmark/face_landmark_model.dat";

class LandmarkBackend {
  public:
    LandmarkBackend() = default;
    LandmarkBackend(const LandmarkBackend &) = delete;
    LandmarkBackend &operator=(const LandmarkBackend &) = delete;
    LandmarkBackend(LandmarkBackend &&) = delete;
    LandmarkBackend &operator=(LandmarkBackend &&) = delete;
    virtual ~LandmarkBackend() = default;

    virtual bool fit(const cv::Mat &image, const std::vector<cv::Rect> &faces,
                     std::vector<std::vector<cv::Point2f>> &landmarks) = 0;
};

class OpenCvFacemarkBackend final : public LandmarkBackend {
  public:
    OpenCvFacemarkBackend(const OpenCvFacemarkBackend &) = delete;
    OpenCvFacemarkBackend &operator=(const OpenCvFacemarkBackend &) = delete;
    OpenCvFacemarkBackend(OpenCvFacemarkBackend &&) noexcept = delete;
    OpenCvFacemarkBackend &operator=(OpenCvFacemarkBackend &&) noexcept =
        delete;
    ~OpenCvFacemarkBackend() override = default;

    [[nodiscard]] static std::expected<std::shared_ptr<OpenCvFacemarkBackend>,
                                       Error>
    create(const std::filesystem::path &model_path) noexcept {
        std::error_code ec;
        if (!std::filesystem::exists(model_path, ec) || ec) {
            return std::unexpected(Error::ResourceNotFound);
        }

        try {
            const cv::Ptr<cv::face::Facemark> facemark =
                cv::face::createFacemarkLBF();
            if (facemark == nullptr) {
                return std::unexpected(Error::ResourceAllocationFailed);
            }

            facemark->loadModel(model_path.string());
            auto backend = std::shared_ptr<OpenCvFacemarkBackend>(
                new (std::nothrow) OpenCvFacemarkBackend(facemark));
            if (backend == nullptr) {
                return std::unexpected(Error::ResourceAllocationFailed);
            }

            return backend;
        } catch (const cv::Exception &) {
            return std::unexpected(Error::ConfigUnsupportedValue);
        }
    }

    bool fit(const cv::Mat &image, const std::vector<cv::Rect> &faces,
             std::vector<std::vector<cv::Point2f>> &landmarks) override {
        return facemark_ != nullptr &&
               facemark_->fit(image, faces, landmarks);
    }

  private:
    explicit OpenCvFacemarkBackend(cv::Ptr<cv::face::Facemark> facemark) noexcept
        : facemark_(std::move(facemark)) {}

    cv::Ptr<cv::face::Facemark> facemark_{};
};

class SyntheticLandmarkBackend final : public LandmarkBackend {
  public:
    bool fit(const cv::Mat &image, const std::vector<cv::Rect> &faces,
             std::vector<std::vector<cv::Point2f>> &landmarks) override {
        landmarks.clear();
        if (image.empty() || faces.empty()) {
            return false;
        }

        landmarks.reserve(faces.size());
        for (const cv::Rect &face : faces) {
            if (face.width <= 0 || face.height <= 0) {
                return false;
            }

            const float width = static_cast<float>(face.width);
            const float height = static_cast<float>(face.height);
            const float x = static_cast<float>(face.x);
            const float y = static_cast<float>(face.y);

            landmarks.emplace_back(std::initializer_list<cv::Point2f>{
                {x + 0.32f * width, y + 0.35f * height},
                {x + 0.68f * width, y + 0.35f * height},
                {x + 0.50f * width, y + 0.55f * height},
                {x + 0.38f * width, y + 0.75f * height},
                {x + 0.62f * width, y + 0.75f * height},
            });
        }

        return true;
    }
};

class DetectionStage final {
  public:
    DetectionStage() = delete;
    DetectionStage(const DetectionStage &) = delete;
    DetectionStage &operator=(const DetectionStage &) = delete;
    DetectionStage(DetectionStage &&) noexcept = default;
    DetectionStage &operator=(DetectionStage &&) noexcept = default;
    ~DetectionStage() = default;

    DetectionStage(std::shared_ptr<const DetectorModel> detector_model,
                   std::shared_ptr<LandmarkBackend> landmark_backend,
                   axvp_policy_t policy,
                   std::filesystem::path landmark_model_path) noexcept
        : detector_model_(std::move(detector_model)),
          landmark_backend_(std::move(landmark_backend)), policy_(policy),
          landmark_model_path_(std::move(landmark_model_path)) {}

    [[nodiscard]] static std::expected<DetectionStage, Error>
    create(const axvp_config_t &cfg,
           std::shared_ptr<const DetectorModel> detector_model) noexcept {
        if (detector_model == nullptr || detector_model->detector() == nullptr) {
            return std::unexpected(Error::PipelineNotInitialized);
        }

        std::shared_ptr<LandmarkBackend> landmark_backend =
            std::make_shared<SyntheticLandmarkBackend>();
        std::filesystem::path resolved_landmark_model_path{};

        const auto landmark_model_path = resolve_landmark_model_path(cfg);
        if (landmark_model_path.has_value()) {
            resolved_landmark_model_path = *landmark_model_path;
            const auto real_backend =
                OpenCvFacemarkBackend::create(*landmark_model_path);
            if (real_backend.has_value()) {
                landmark_backend = std::move(*real_backend);
            } else {
                resolved_landmark_model_path.clear();
            }
        }

        return DetectionStage{std::move(detector_model),
                              std::move(landmark_backend), cfg.policy,
                              resolved_landmark_model_path};
    }

    [[nodiscard]] std::expected<DetectionResult, Error>
    process(UniqueFrame &frame) {
        if (!frame) {
            return std::unexpected(Error::ConfigInvalidValue);
        }

        if (detector_model_ == nullptr || detector_model_->detector() == nullptr
            || landmark_backend_ == nullptr) {
            return std::unexpected(Error::PipelineNotInitialized);
        }

        const cv::Mat &image = frame.mat();
        if (image.empty() || image.rows <= 0 || image.cols <= 0) {
            return std::unexpected(Error::ConfigInvalidValue);
        }

        try {
            detector_model_->detector()->setInputSize(image.size());
        } catch (const cv::Exception &) {
            return std::unexpected(Error::PipelineStageFailed);
        }

        cv::Mat detected_faces;
        try {
            const int face_count =
                detector_model_->detector()->detect(image, detected_faces);
            if (face_count < 0) {
                return std::unexpected(Error::PipelineStageFailed);
            }
        } catch (const cv::Exception &) {
            return std::unexpected(Error::PipelineStageFailed);
        }

        DetectionResult result{};
        if (detected_faces.empty()) {
            return result;
        }

        if (detected_faces.type() != CV_32F || detected_faces.cols < 15) {
            return std::unexpected(Error::PipelineStageFailed);
        }

        for (int row = 0; row < detected_faces.rows; ++row) {
            const std::array<float, 4U> bbox{
                detected_faces.at<float>(row, 0),
                detected_faces.at<float>(row, 1),
                detected_faces.at<float>(row, 2),
                detected_faces.at<float>(row, 3),
            };
            const float score = detected_faces.at<float>(row, 14);

            const cv::Rect roi = make_roi(image.size(), bbox);
            if (roi.width <= 0 || roi.height <= 0) {
                if (block_on_fail()) {
                    return std::unexpected(Error::PipelineDetectionIncomplete);
                }
                continue;
            }

            const cv::Mat face_crop = image(roi);
            const std::vector<cv::Rect> faces{cv::Rect{0, 0, roi.width,
                                                       roi.height}};
            std::vector<std::vector<cv::Point2f>> landmarks;
            if (!landmark_backend_->fit(face_crop, faces, landmarks) ||
                landmarks.empty() || landmarks.front().empty()) {
                if (block_on_fail()) {
                    return std::unexpected(Error::PipelineDetectionIncomplete);
                }
                continue;
            }

            auto face = result.add_face(bbox, score);
            if (!face.has_value()) {
                return std::unexpected(face.error());
            }

            const std::size_t face_index = (*face)->index;
            for (const cv::Point2f &point : landmarks.front()) {
                auto landmark = result.add_landmark(
                    face_index, point.x + static_cast<float>(roi.x),
                    point.y + static_cast<float>(roi.y), 0.0f);
                if (!landmark.has_value()) {
                    return std::unexpected(landmark.error());
                }
            }
        }

        return result;
    }

    [[nodiscard]] bool block_on_fail() const noexcept {
        return (policy_ & AXVP_POLICY_BLOCK_ON_FAIL) != AXVP_POLICY_NONE;
    }

    [[nodiscard]] const std::filesystem::path &landmark_model_path() const
        noexcept {
        return landmark_model_path_;
    }

  private:
    [[nodiscard]] static std::expected<std::filesystem::path, Error>
    resolve_landmark_model_path(const axvp_config_t &cfg) noexcept {
        if (cfg.model_dir == nullptr || cfg.model_dir[0] == '\0') {
            return std::unexpected(Error::ConfigMissingValue);
        }

        const std::filesystem::path model_dir{cfg.model_dir};
        std::error_code ec;
        const std::filesystem::path yaml_model_path =
            model_dir / kDefaultLandmarkModelRelativePath;
        if (std::filesystem::exists(yaml_model_path, ec) && !ec) {
            return yaml_model_path;
        }

        ec.clear();
        const std::filesystem::path legacy_model_path =
            model_dir / kLegacyLandmarkModelRelativePath;
        if (std::filesystem::exists(legacy_model_path, ec) && !ec) {
            return legacy_model_path;
        }

        return std::unexpected(Error::ResourceNotFound);
    }

    [[nodiscard]] static cv::Rect make_roi(const cv::Size image_size,
                                           const std::array<float, 4U> &bbox)
        noexcept {
        const float pad_x = std::max(2.0f, bbox[2] * 0.12f);
        const float pad_y = std::max(2.0f, bbox[3] * 0.12f);

        const int left = std::max(
            0, static_cast<int>(std::floor(bbox[0] - pad_x)));
        const int top = std::max(0, static_cast<int>(std::floor(bbox[1] - pad_y)));
        const int right = std::min(
            image_size.width,
            static_cast<int>(std::ceil(bbox[0] + bbox[2] + pad_x)));
        const int bottom = std::min(
            image_size.height,
            static_cast<int>(std::ceil(bbox[1] + bbox[3] + pad_y)));

        if (right <= left || bottom <= top) {
            return {};
        }

        return cv::Rect{left, top, right - left, bottom - top};
    }

    std::shared_ptr<const DetectorModel> detector_model_{};
    std::shared_ptr<LandmarkBackend> landmark_backend_{};
    axvp_policy_t policy_ = AXVP_POLICY_NONE;
    std::filesystem::path landmark_model_path_{};
};

} // namespace axvp::internal
