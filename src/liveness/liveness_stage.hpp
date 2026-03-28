#pragma once

#include "detection/detection_result.hpp"
#include "internal/error.hpp"
#include "liveness/liveness_result.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <limits>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <span>
#include <utility>

namespace axvp::internal {

class LivenessStage final {
  public:
    LivenessStage() = delete;
    LivenessStage(const LivenessStage &) = delete;
    LivenessStage &operator=(const LivenessStage &) = delete;
    LivenessStage(LivenessStage &&) noexcept = default;
    LivenessStage &operator=(LivenessStage &&) noexcept = default;
    ~LivenessStage() = default;

    [[nodiscard]] static std::expected<LivenessStage, Error>
    create(const axvp_config_t &cfg) noexcept {
        if (cfg.rppg_window_frames == 0U) {
            return std::unexpected(Error::ConfigInvalidValue);
        }

        return LivenessStage{cfg.rppg_window_frames};
    }

    [[nodiscard]] std::expected<LivenessResult, Error>
    process(const cv::Mat &image, const DetectionResult &detection,
            std::uint64_t timestamp_ns) noexcept {
        if (image.empty() || image.type() != CV_8UC3) {
            return std::unexpected(Error::ConfigUnsupportedValue);
        }

        LivenessResult result{};
        for (std::size_t face_index = 0U; face_index < detection.size();
             ++face_index) {
            const auto bbox = detection.bbox(face_index);
            const cv::Rect roi = make_roi(image.size(), bbox);
            if (roi.width <= 0 || roi.height <= 0) {
                continue;
            }

            auto &track = tracks_[face_index];
            const auto sample = sample_signal(image, roi);
            track.push(sample.signal, sample.brightness, timestamp_ns);

            const auto analysis = analyze_track(track, window_frames_);
            if (!analysis.has_value()) {
                return std::unexpected(analysis.error());
            }

            auto face = result.add_face(face_index, analysis->score,
                                        analysis->pulse_bpm, analysis->verdict);
            if (!face.has_value()) {
                return std::unexpected(face.error());
            }
        }

        return result;
    }

    void reset() noexcept {
        for (auto &track : tracks_) {
            track.reset();
        }
    }

  private:
    struct RppgSample final {
        float signal = 0.0f;
        float brightness = 0.0f;
    };

    struct TrackState final {
        std::array<float, 256U> signal{};
        std::array<float, 256U> brightness{};
        std::array<std::uint64_t, 256U> timestamps{};
        std::size_t head = 0U;
        std::size_t count = 0U;

        void push(float signal_value, float brightness_value,
                  std::uint64_t timestamp_ns) noexcept {
            signal[head] = signal_value;
            brightness[head] = brightness_value;
            timestamps[head] = timestamp_ns;
            head = (head + 1U) % signal.size();
            if (count < signal.size()) {
                ++count;
            }
        }

        void reset() noexcept {
            signal = {};
            brightness = {};
            timestamps = {};
            head = 0U;
            count = 0U;
        }

        [[nodiscard]] std::array<float, 256U> ordered_signal() const noexcept {
            std::array<float, 256U> ordered{};
            for (std::size_t index = 0U; index < count; ++index) {
                ordered[index] =
                    signal[(head + signal.size() - count + index) % signal.size()];
            }
            return ordered;
        }

        [[nodiscard]] std::array<float, 256U> ordered_brightness() const
            noexcept {
            std::array<float, 256U> ordered{};
            for (std::size_t index = 0U; index < count; ++index) {
                ordered[index] = brightness
                    [(head + brightness.size() - count + index) % brightness.size()];
            }
            return ordered;
        }

        [[nodiscard]] std::array<std::uint64_t, 256U> ordered_timestamps()
            const noexcept {
            std::array<std::uint64_t, 256U> ordered{};
            for (std::size_t index = 0U; index < count; ++index) {
                ordered[index] = timestamps
                    [(head + timestamps.size() - count + index) % timestamps.size()];
            }
            return ordered;
        }
    };

    struct Analysis final {
        float score = 0.0f;
        std::uint32_t pulse_bpm = 0U;
        axvp_liveness_verdict_t verdict = AXVP_LIVENESS_UNCERTAIN;
    };

    [[nodiscard]] static cv::Rect
    make_roi(const cv::Size image_size, const std::array<float, 4U> &bbox)
        noexcept {
        const float pad_x = std::max(2.0f, bbox[2] * 0.10f);
        const float pad_y = std::max(2.0f, bbox[3] * 0.10f);

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

    [[nodiscard]] static cv::Rect
    clamp_rect(const cv::Rect &rect, const cv::Size size) noexcept {
        const int left = std::clamp(rect.x, 0, std::max(0, size.width - 1));
        const int top = std::clamp(rect.y, 0, std::max(0, size.height - 1));
        const int right = std::clamp(rect.x + rect.width, 0, size.width);
        const int bottom = std::clamp(rect.y + rect.height, 0, size.height);
        if (right <= left || bottom <= top) {
            return {};
        }
        return {left, top, right - left, bottom - top};
    }

    [[nodiscard]] static RppgSample
    sample_signal(const cv::Mat &image, const cv::Rect &roi) noexcept {
        const int w = std::max(1, roi.width);
        const int h = std::max(1, roi.height);

        const cv::Rect forehead = clamp_rect(
            {roi.x + (w / 4), roi.y + (h / 10), std::max(1, w / 2),
             std::max(1, h / 5)},
            image.size());
        const cv::Rect left_cheek = clamp_rect(
            {roi.x + (w / 10), roi.y + (h / 2), std::max(1, w / 3),
             std::max(1, h / 3)},
            image.size());
        const cv::Rect right_cheek = clamp_rect(
            {roi.x + (w * 6 / 10), roi.y + (h / 2), std::max(1, w / 3),
             std::max(1, h / 3)},
            image.size());

        const auto sample_roi = [&](const cv::Rect &rect) {
            const cv::Mat region = image(rect);
            const cv::Scalar mean = cv::mean(region);
            const float signal =
                static_cast<float>(mean[1]) -
                0.5f * static_cast<float>(mean[0] + mean[2]);
            const float brightness = static_cast<float>(
                (mean[0] + mean[1] + mean[2]) / 3.0);
            return std::pair<float, float>{signal, brightness};
        };

        const auto [signal_a, brightness_a] = sample_roi(forehead);
        const auto [signal_b, brightness_b] = sample_roi(left_cheek);
        const auto [signal_c, brightness_c] = sample_roi(right_cheek);

        return {.signal = (signal_a + signal_b + signal_c) / 3.0f,
                .brightness = (brightness_a + brightness_b + brightness_c) /
                              3.0f};
    }

    [[nodiscard]] static float
    estimate_fps(const TrackState &track) noexcept {
        if (track.count < 2U) {
            return 30.0f;
        }

        const auto ordered = track.ordered_timestamps();
        const std::uint64_t first = ordered[0];
        const std::uint64_t last = ordered[track.count - 1U];
        if (last <= first) {
            return 30.0f;
        }

        const double delta_ns = static_cast<double>(last - first);
        const double frames = static_cast<double>(track.count - 1U);
        const double fps = (frames * 1'000'000'000.0) / delta_ns;
        if (!std::isfinite(fps) || fps <= 0.0) {
            return 30.0f;
        }

        return static_cast<float>(std::clamp(fps, 15.0, 120.0));
    }

    [[nodiscard]] static std::expected<Analysis, Error>
    analyze_track(const TrackState &track, std::size_t window_frames) noexcept {
        constexpr float kLowLightThreshold = 35.0f;
        constexpr float kMinStdDev = 1.2f;
        constexpr float kLiveSNRThreshold = 1.8f;
        constexpr float kSpoofSNRThreshold = 1.2f;
        constexpr float kMinHz = 0.7f;
        constexpr float kMaxHz = 3.5f;

        if (track.count == 0U) {
            return Analysis{};
        }

        const auto signal = track.ordered_signal();
        const auto brightness = track.ordered_brightness();
        const std::span<const float> samples(signal.data(), track.count);
        const std::span<const float> brightness_samples(brightness.data(),
                                                        track.count);

        double signal_mean = 0.0;
        double brightness_mean = 0.0;
        for (std::size_t index = 0U; index < track.count; ++index) {
            signal_mean += samples[index];
            brightness_mean += brightness_samples[index];
        }
        signal_mean /= static_cast<double>(track.count);
        brightness_mean /= static_cast<double>(track.count);

        double signal_variance = 0.0;
        for (std::size_t index = 0U; index < track.count; ++index) {
            const double delta = samples[index] - signal_mean;
            signal_variance += delta * delta;
        }
        signal_variance /= static_cast<double>(track.count);
        const double signal_stddev = std::sqrt(signal_variance);

        const std::size_t minimum_samples =
            std::max<std::size_t>(6U, std::min<std::size_t>(window_frames / 2U, 16U));
        if (track.count < minimum_samples) {
            return Analysis{0.0f, 0U, AXVP_LIVENESS_UNCERTAIN};
        }

        cv::Mat signal_mat(1, static_cast<int>(track.count), CV_32F,
                           const_cast<float *>(samples.data()));
        signal_mat = signal_mat - static_cast<float>(signal_mean);

        cv::Mat spectrum;
        cv::dft(signal_mat, spectrum, cv::DFT_COMPLEX_OUTPUT);

        const float fps = estimate_fps(track);
        const int low_bin = std::max(
            1, static_cast<int>(std::ceil(kMinHz *
                                          static_cast<float>(track.count) /
                                          fps)));
        const int high_bin = std::min(
            static_cast<int>(track.count / 2U),
            static_cast<int>(std::floor(kMaxHz *
                                        static_cast<float>(track.count) /
                                        fps)));
        if (high_bin < low_bin) {
            return Analysis{0.0f, 0U, AXVP_LIVENESS_UNCERTAIN};
        }

        float peak_power = 0.0f;
        int peak_bin = low_bin;
        float total_power = 0.0f;
        int bins = 0;
        for (int bin = low_bin; bin <= high_bin; ++bin) {
            const cv::Vec2f value = spectrum.at<cv::Vec2f>(0, bin);
            const float power = (value[0] * value[0]) + (value[1] * value[1]);
            total_power += power;
            ++bins;
            if (power > peak_power) {
                peak_power = power;
                peak_bin = bin;
            }
        }

        const float average_power =
            bins > 0 ? total_power / static_cast<float>(bins) : 0.0f;
        const float snr =
            average_power > std::numeric_limits<float>::epsilon()
                ? peak_power / average_power
                : 0.0f;
        const float dominant_hz =
            static_cast<float>(peak_bin) * fps /
            static_cast<float>(track.count);
        const std::uint32_t pulse_bpm = static_cast<std::uint32_t>(
            std::round(std::clamp(dominant_hz * 60.0f, 0.0f, 220.0f)));

        axvp_liveness_verdict_t verdict = AXVP_LIVENESS_UNCERTAIN;
        if (brightness_mean < kLowLightThreshold &&
            signal_stddev < kMinStdDev) {
            verdict = AXVP_LIVENESS_UNCERTAIN;
        } else if (signal_stddev < kMinStdDev || snr < kSpoofSNRThreshold) {
            verdict = brightness_mean < kLowLightThreshold
                          ? AXVP_LIVENESS_UNCERTAIN
                          : AXVP_LIVENESS_SPOOF;
        } else if (snr >= kLiveSNRThreshold &&
                   dominant_hz >= kMinHz && dominant_hz <= kMaxHz) {
            verdict = AXVP_LIVENESS_LIVE;
        } else {
            verdict = AXVP_LIVENESS_SPOOF;
        }

        float score = static_cast<float>(std::clamp(
            (snr - 1.0f) / 3.0f + static_cast<float>(signal_stddev) / 20.0f,
            0.0f, 1.0f));
        if (verdict == AXVP_LIVENESS_UNCERTAIN) {
            score *= 0.5f;
        } else if (verdict == AXVP_LIVENESS_SPOOF) {
            score *= 0.35f;
        }

        if (verdict != AXVP_LIVENESS_LIVE) {
            return Analysis{score, 0U, verdict};
        }

        return Analysis{score, pulse_bpm, verdict};
    }

    std::array<TrackState, AXVP_MAX_FACES> tracks_{};
    std::size_t window_frames_ = 0U;

    explicit LivenessStage(std::uint32_t window_frames) noexcept
        : window_frames_(window_frames) {}
};

} // namespace axvp::internal
