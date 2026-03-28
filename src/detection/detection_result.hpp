#pragma once

#include "internal/error.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <vector>

namespace axvp::internal {

inline constexpr std::size_t AXVP_MAX_FACES = 32U;

struct FaceROI final {
    std::size_t index = 0U;
    std::array<float, 4U> bbox{};
};

struct DetectionResult final {
    std::array<FaceROI, AXVP_MAX_FACES> faces{};
    std::size_t face_count = 0U;
    std::array<float, AXVP_MAX_FACES> confidences{};
    std::array<float, AXVP_MAX_FACES> bbox_x{};
    std::array<float, AXVP_MAX_FACES> bbox_y{};
    std::array<float, AXVP_MAX_FACES> bbox_w{};
    std::array<float, AXVP_MAX_FACES> bbox_h{};
    std::array<std::size_t, AXVP_MAX_FACES> landmark_offsets{};
    std::array<std::size_t, AXVP_MAX_FACES> landmark_counts{};
    std::vector<float> landmark_x{};
    std::vector<float> landmark_y{};
    std::vector<float> landmark_z{};

    [[nodiscard]] static constexpr std::size_t max_faces() noexcept {
        return AXVP_MAX_FACES;
    }

    [[nodiscard]] bool empty() const noexcept { return face_count == 0U; }

    [[nodiscard]] bool full() const noexcept {
        return face_count >= AXVP_MAX_FACES;
    }

    [[nodiscard]] std::size_t size() const noexcept { return face_count; }

    [[nodiscard]] std::span<const FaceROI> face_rois() const noexcept {
        return face_count == 0U
                   ? std::span<const FaceROI>{}
                   : std::span<const FaceROI>{faces.data(), face_count};
    }

    [[nodiscard]] std::span<FaceROI> face_rois() noexcept {
        return face_count == 0U ? std::span<FaceROI>{}
                                : std::span<FaceROI>{faces.data(), face_count};
    }

    [[nodiscard]] std::expected<FaceROI *, Error>
    add_face(std::array<float, 4U> bbox, float confidence) noexcept {
        if (full()) {
            return std::unexpected(Error::ResourceExhausted);
        }

        const std::size_t index = face_count;
        FaceROI &roi = faces[index];
        roi.index = index;
        roi.bbox = bbox;

        confidences[index] = confidence;
        bbox_x[index] = bbox[0];
        bbox_y[index] = bbox[1];
        bbox_w[index] = bbox[2];
        bbox_h[index] = bbox[3];
        landmark_offsets[index] = landmark_x.size();
        landmark_counts[index] = 0U;
        ++face_count;

        return &roi;
    }

    [[nodiscard]] std::expected<void, Error>
    add_landmark(std::size_t face_index, float x, float y, float z) noexcept {
        if (face_index >= face_count) {
            return std::unexpected(Error::ConfigInvalidValue);
        }

        if (landmark_offsets[face_index] + landmark_counts[face_index] !=
            landmark_x.size()) {
            return std::unexpected(Error::PipelineStageFailed);
        }

        landmark_x.push_back(x);
        landmark_y.push_back(y);
        landmark_z.push_back(z);
        ++landmark_counts[face_index];
        return {};
    }

    [[nodiscard]] std::array<float, 4U>
    bbox(std::size_t face_index) const noexcept {
        if (face_index >= face_count) {
            return {};
        }

        return {bbox_x[face_index], bbox_y[face_index], bbox_w[face_index],
                bbox_h[face_index]};
    }

    [[nodiscard]] float confidence(std::size_t face_index) const noexcept {
        return face_index >= face_count ? 0.0f : confidences[face_index];
    }

    [[nodiscard]] std::size_t
    landmark_count(std::size_t face_index) const noexcept {
        return face_index >= face_count ? 0U : landmark_counts[face_index];
    }

    [[nodiscard]] std::span<const float>
    landmark_x_for(std::size_t face_index) const noexcept {
        return landmark_span(landmark_x, face_index);
    }

    [[nodiscard]] std::span<const float>
    landmark_y_for(std::size_t face_index) const noexcept {
        return landmark_span(landmark_y, face_index);
    }

    [[nodiscard]] std::span<const float>
    landmark_z_for(std::size_t face_index) const noexcept {
        return landmark_span(landmark_z, face_index);
    }

    void clear() noexcept {
        face_count = 0U;
        faces = {};
        confidences = {};
        bbox_x = {};
        bbox_y = {};
        bbox_w = {};
        bbox_h = {};
        landmark_offsets = {};
        landmark_counts = {};
        landmark_x.clear();
        landmark_y.clear();
        landmark_z.clear();
    }

  private:
    [[nodiscard]] std::span<const float>
    landmark_span(const std::vector<float> &values,
                  std::size_t face_index) const noexcept {
        if (face_index >= face_count) {
            return {};
        }

        const std::size_t offset = landmark_offsets[face_index];
        const std::size_t count = landmark_counts[face_index];
        if (count == 0U) {
            return {};
        }

        return std::span<const float>{values.data() + offset, count};
    }
};

} // namespace axvp::internal
