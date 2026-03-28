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

            blur_face_region(image, roi);
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
        const float pad_x = std::max(10.0f, bbox[2] * 0.18f);
        const float pad_y = std::max(14.0f, bbox[3] * 0.26f);

        const int left = std::max(0, static_cast<int>(std::floor(bbox[0] - pad_x)));
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

    static void blur_face_region(cv::Mat &image, const cv::Rect &roi) noexcept {
        cv::Mat face = image(roi);
        cv::Mat blurred;
        const int longest_edge = std::max(roi.width, roi.height);
        const double sigma = std::max(16.0, static_cast<double>(longest_edge) * 0.30);
        cv::GaussianBlur(face, blurred, cv::Size{}, sigma, sigma,
                         cv::BORDER_DEFAULT);
        cv::Mat mask(roi.size(), CV_8UC1, cv::Scalar(0));
        const cv::Point center(roi.width / 2, roi.height / 2);
        const cv::Size axes(std::max(1, roi.width / 2 - 2),
                            std::max(1, roi.height / 2 - 2));
        cv::ellipse(mask, center, axes, 0.0, 0.0, 360.0, cv::Scalar(255),
                    cv::FILLED, cv::LINE_AA);
        blurred.copyTo(face, mask);
    }

    std::shared_ptr<const VulkanContext> vulkan_context_{};
    axvp_policy_t policy_ = AXVP_POLICY_NONE;
};

} // namespace axvp::internal
