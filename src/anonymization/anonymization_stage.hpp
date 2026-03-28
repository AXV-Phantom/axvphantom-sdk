#pragma once

#include "anonymization/anonymization_result.hpp"
#include "anonymization/vulkan_context.hpp"
#include "detection/detection_result.hpp"
#include "internal/error.hpp"
#include "internal/unique_frame.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <expected>
#include <memory>
#include <opencv2/imgproc.hpp>
#include <utility>

namespace axvp::internal {

class AnonymizationStage final {
  public:
    AnonymizationStage() = delete;
    AnonymizationStage(const AnonymizationStage &) = delete;
    AnonymizationStage &operator=(const AnonymizationStage &) = delete;
    AnonymizationStage(AnonymizationStage &&) noexcept = default;
    AnonymizationStage &operator=(AnonymizationStage &&) noexcept = default;
    ~AnonymizationStage() = default;

    [[nodiscard]] static std::expected<AnonymizationStage, Error>
    create(std::shared_ptr<const VulkanContext> vulkan_context,
           axvp_policy_t policy) noexcept {
        return AnonymizationStage{std::move(vulkan_context), policy};
    }

    void set_policy(axvp_policy_t policy) noexcept { policy_ = policy; }

    [[nodiscard]] std::expected<AnonymizationResult, Error>
    process(UniqueFrame &frame, const DetectionResult &detection) const {
        if (!frame) {
            return std::unexpected(Error::ConfigInvalidValue);
        }

        cv::Mat &image = frame.mat();
        if (image.empty() || image.type() != CV_8UC3) {
            return std::unexpected(Error::ConfigUnsupportedValue);
        }

        AnonymizationResult result{};
        for (std::size_t face_index = 0U; face_index < detection.size();
             ++face_index) {
            const auto bbox = detection.bbox(face_index);
            const cv::Rect roi = make_roi(image.size(), bbox);
            if (roi.width <= 0 || roi.height <= 0) {
                if (block_on_fail()) {
                    return std::unexpected(Error::PipelineDetectionIncomplete);
                }
                continue;
            }

            render_avatar(image, roi, detection, face_index);
            auto face = result.add_face(face_index, bbox, true);
            if (!face.has_value()) {
                return std::unexpected(face.error());
            }
        }

        return result;
    }

    [[nodiscard]] bool uses_vulkan() const noexcept {
        return vulkan_context_ != nullptr && vulkan_context_->available();
    }

  private:
    AnonymizationStage(std::shared_ptr<const VulkanContext> vulkan_context,
                       axvp_policy_t policy) noexcept
        : vulkan_context_(std::move(vulkan_context)), policy_(policy) {}

    [[nodiscard]] bool block_on_fail() const noexcept {
        return (policy_ & AXVP_POLICY_BLOCK_ON_FAIL) != AXVP_POLICY_NONE;
    }

    [[nodiscard]] static cv::Rect
    make_roi(const cv::Size image_size, const std::array<float, 4U> &bbox)
        noexcept {
        const float pad_x = std::max(4.0f, bbox[2] * 0.15f);
        const float pad_y = std::max(4.0f, bbox[3] * 0.20f);

        const int left = std::max(0, static_cast<int>(std::floor(bbox[0] - pad_x)));
        const int top = std::max(0, static_cast<int>(std::floor(bbox[1] - pad_y)));
        const int right = std::min(
            image_size.width, static_cast<int>(std::ceil(bbox[0] + bbox[2] + pad_x)));
        const int bottom = std::min(
            image_size.height,
            static_cast<int>(std::ceil(bbox[1] + bbox[3] + pad_y)));

        if (right <= left || bottom <= top) {
            return {};
        }

        return cv::Rect{left, top, right - left, bottom - top};
    }

    [[nodiscard]] static cv::Point2f
    landmark_or_default(const DetectionResult &detection, std::size_t face_index,
                        std::size_t landmark_index, const cv::Rect &roi,
                        float x_factor, float y_factor) noexcept {
        const auto &xs = detection.landmark_x_for(face_index);
        const auto &ys = detection.landmark_y_for(face_index);
        if (landmark_index < xs.size() && landmark_index < ys.size()) {
            return {xs[landmark_index] - static_cast<float>(roi.x),
                    ys[landmark_index] - static_cast<float>(roi.y)};
        }

        return {static_cast<float>(roi.width) * x_factor,
                static_cast<float>(roi.height) * y_factor};
    }

    static void render_avatar(cv::Mat &image, const cv::Rect &roi,
                              const DetectionResult &detection,
                              std::size_t face_index) noexcept {
        cv::Mat face = image(roi);
        cv::Mat avatar(roi.size(), CV_8UC3);
        avatar.setTo(cv::Scalar(188, 176, 164));

        const cv::Scalar outline(58, 48, 44);
        const cv::Scalar accent(92, 84, 76);

        const cv::Point2f left_eye = landmark_or_default(
            detection, face_index, 0U, roi, 0.35f, 0.38f);
        const cv::Point2f right_eye = landmark_or_default(
            detection, face_index, 1U, roi, 0.65f, 0.38f);
        const cv::Point2f nose = landmark_or_default(
            detection, face_index, 2U, roi, 0.50f, 0.54f);
        const cv::Point2f mouth_left = landmark_or_default(
            detection, face_index, 3U, roi, 0.38f, 0.73f);
        const cv::Point2f mouth_right = landmark_or_default(
            detection, face_index, 4U, roi, 0.62f, 0.73f);

        const cv::Point center(roi.width / 2, roi.height / 2);
        const cv::Size axes(std::max(1, roi.width / 2 - 2),
                            std::max(1, roi.height / 2 - 2));
        cv::ellipse(avatar, center, axes, 0.0, 0.0, 360.0,
                    cv::Scalar(198, 188, 178), cv::FILLED, cv::LINE_AA);
        cv::ellipse(avatar, center, axes, 0.0, 0.0, 360.0, outline, 2,
                    cv::LINE_AA);

        cv::circle(avatar, {static_cast<int>(left_eye.x),
                            static_cast<int>(left_eye.y)},
                   std::max(1, roi.width / 20), outline, cv::FILLED,
                   cv::LINE_AA);
        cv::circle(avatar, {static_cast<int>(right_eye.x),
                            static_cast<int>(right_eye.y)},
                   std::max(1, roi.width / 20), outline, cv::FILLED,
                   cv::LINE_AA);

        cv::line(avatar, {static_cast<int>(nose.x), static_cast<int>(nose.y)},
                 {static_cast<int>(nose.x), static_cast<int>(nose.y) + 5},
                 accent, 2, cv::LINE_AA);
        cv::ellipse(avatar,
                    {(static_cast<int>(mouth_left.x + mouth_right.x) / 2),
                     (static_cast<int>(mouth_left.y + mouth_right.y) / 2)},
                    {std::max(2, roi.width / 8), std::max(2, roi.height / 12)},
                    0.0, 0.0, 180.0, outline, 2, cv::LINE_AA);

        cv::GaussianBlur(avatar, avatar, cv::Size{3, 3}, 0.6);
        avatar.copyTo(face);
    }

    std::shared_ptr<const VulkanContext> vulkan_context_{};
    axvp_policy_t policy_ = AXVP_POLICY_NONE;
};

} // namespace axvp::internal
