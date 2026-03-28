#pragma once

#include "axvphantom/axvphantom.h"
#include "detection/detection_result.hpp"
#include "internal/error.hpp"

#include <array>
#include <cstddef>
#include <expected>
#include <span>

namespace axvp::internal {

struct LivenessFace final {
    std::size_t index = 0U;
    float score = 0.0f;
    std::uint32_t pulse_bpm = 0U;
    axvp_liveness_verdict_t verdict = AXVP_LIVENESS_UNCERTAIN;
};

struct LivenessResult final {
    std::array<LivenessFace, AXVP_MAX_FACES> faces{};
    std::size_t face_count = 0U;

    [[nodiscard]] bool empty() const noexcept { return face_count == 0U; }

    [[nodiscard]] bool full() const noexcept {
        return face_count >= AXVP_MAX_FACES;
    }

    [[nodiscard]] std::size_t size() const noexcept { return face_count; }

    [[nodiscard]] std::span<const LivenessFace> face_records() const noexcept {
        return face_count == 0U
                   ? std::span<const LivenessFace>{}
                   : std::span<const LivenessFace>{faces.data(), face_count};
    }

    [[nodiscard]] std::expected<LivenessFace *, Error>
    add_face(std::size_t index, float score, std::uint32_t pulse_bpm,
             axvp_liveness_verdict_t verdict) noexcept {
        if (full()) {
            return std::unexpected(Error::ResourceExhausted);
        }

        auto &face = faces[face_count];
        face.index = index;
        face.score = score;
        face.pulse_bpm = pulse_bpm;
        face.verdict = verdict;
        ++face_count;
        return &face;
    }

    [[nodiscard]] const LivenessFace *find(std::size_t index) const noexcept {
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
