#pragma once

#include "detection/detection_result.hpp"
#include "internal/error.hpp"

#include <array>
#include <cstddef>
#include <expected>
#include <span>

namespace axvp::internal {

struct AnonymizedFace final {
    std::size_t index = 0U;
    std::array<float, 4U> bbox{};
    bool pixels_wiped = false;
};

struct AnonymizationResult final {
    std::array<AnonymizedFace, AXVP_MAX_FACES> faces{};
    std::size_t face_count = 0U;

    [[nodiscard]] bool empty() const noexcept { return face_count == 0U; }

    [[nodiscard]] bool full() const noexcept {
        return face_count >= AXVP_MAX_FACES;
    }

    [[nodiscard]] std::size_t size() const noexcept { return face_count; }

    [[nodiscard]] std::span<const AnonymizedFace> face_records() const
        noexcept {
        return face_count == 0U
                   ? std::span<const AnonymizedFace>{}
                   : std::span<const AnonymizedFace>{faces.data(), face_count};
    }

    [[nodiscard]] std::expected<AnonymizedFace *, Error>
    add_face(std::size_t index, std::array<float, 4U> bbox,
             bool pixels_wiped = true) noexcept {
        if (full()) {
            return std::unexpected(Error::ResourceExhausted);
        }

        auto &face = faces[face_count];
        face.index = index;
        face.bbox = bbox;
        face.pixels_wiped = pixels_wiped;
        ++face_count;
        return &face;
    }

    [[nodiscard]] const AnonymizedFace *find(std::size_t index) const noexcept {
        for (std::size_t face_index = 0U; face_index < face_count;
             ++face_index) {
            if (faces[face_index].index == index) {
                return &faces[face_index];
            }
        }

        return nullptr;
    }
};

} // namespace axvp::internal
