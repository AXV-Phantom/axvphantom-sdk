#pragma once

#include "composer/face_id_generator.hpp"
#include "anonymization/anonymization_result.hpp"
#include "liveness/liveness_result.hpp"
#include "internal/error.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <flatbuffers/flatbuffers.h>
#include <axvphantom/generated/axvphantom_generated.h>

namespace axvp::internal {

class ComposerStage final {
  public:
    ComposerStage() = delete;
    ComposerStage(const ComposerStage &) = delete;
    ComposerStage &operator=(const ComposerStage &) = delete;
    ComposerStage(ComposerStage &&) noexcept = default;
    ComposerStage &operator=(ComposerStage &&) noexcept = default;
    ~ComposerStage() = default;

    [[nodiscard]] static std::expected<ComposerStage, Error> create() noexcept {
        auto generator = FaceIDGenerator::create();
        if (!generator.has_value()) {
            return std::unexpected(generator.error());
        }

        return ComposerStage{std::move(*generator)};
    }

    [[nodiscard]] std::expected<void, Error> rotate_keys() noexcept {
        return generator_.rotate_key();
    }

    [[nodiscard]] std::expected<std::vector<std::byte>, Error>
    compose(std::uint64_t frame_id, const axvp_frame_t &frame,
            const DetectionResult &detection,
            const AnonymizationResult &anonymization,
            const LivenessResult &liveness, std::uint32_t latency_us) const
        noexcept {
        std::vector<axvp::fb::FaceRecord> face_records;
        face_records.reserve(detection.size());

        std::uint32_t faces_anonymized = 0U;
        bool anonymization_complete = true;

        for (std::size_t face_index = 0U; face_index < detection.size();
             ++face_index) {
            const auto face_id = generator_.generate(detection, face_index);
            if (!face_id.has_value()) {
                return std::unexpected(face_id.error());
            }

            const auto bbox_values = detection.bbox(face_index);
            const axvp::fb::Rect bbox{bbox_values[0], bbox_values[1],
                                      bbox_values[2], bbox_values[3]};

            const auto *anonymized = anonymization.find(face_index);
            const auto *live = liveness.find(face_index);

            const float liveness_score =
                live == nullptr ? 0.0f : live->score;
            const std::uint32_t pulse_bpm =
                live == nullptr ? 0U : live->pulse_bpm;
            const auto verdict = live == nullptr
                                     ? axvp::fb::LivenessVerdict_UNCERTAIN
                                     : static_cast<axvp::fb::LivenessVerdict>(
                                           live->verdict);
            const bool pixels_wiped =
                anonymized != nullptr && anonymized->pixels_wiped;

            if (pixels_wiped) {
                ++faces_anonymized;
            } else {
                anonymization_complete = false;
            }

            const flatbuffers::span<const std::uint8_t, 16U> face_id_span(
                face_id->data(), face_id->size());
            face_records.emplace_back(face_id_span, bbox, liveness_score,
                                      pulse_bpm, verdict, pixels_wiped);
        }

        if (detection.size() == 0U) {
            anonymization_complete = true;
        }

        flatbuffers::FlatBufferBuilder builder(1024U);
        const auto version = builder.CreateString(AXVP_VERSION_STRING);
        const auto faces_offset = builder.CreateVectorOfStructs(face_records);
        const auto root = axvp::fb::CreateFrameMetadata(
            builder, frame_id, frame.timestamp_ns, version, faces_offset,
            static_cast<std::uint32_t>(detection.size()), faces_anonymized,
            anonymization_complete, latency_us);
        axvp::fb::FinishFrameMetadataBuffer(builder, root);

        auto detached = builder.Release();
        std::vector<std::byte> bytes(detached.size());
        if (!bytes.empty()) {
            std::memcpy(bytes.data(), detached.data(), detached.size());
        }
        return bytes;
    }

  private:
    explicit ComposerStage(FaceIDGenerator generator) noexcept
        : generator_(std::move(generator)) {}

    FaceIDGenerator generator_;
};

} // namespace axvp::internal
