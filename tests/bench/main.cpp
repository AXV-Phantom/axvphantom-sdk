#include "axvphantom/axvphantom.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <numeric>
#include <string_view>
#include <vector>

#include <opencv2/imgcodecs.hpp>

#ifndef AXVP_TEST_DATA_DIR
#define AXVP_TEST_DATA_DIR "."
#endif

namespace {

[[nodiscard]] std::filesystem::path test_data_path(std::string_view relative) {
    return std::filesystem::path{AXVP_TEST_DATA_DIR} / relative;
}

[[nodiscard]] cv::Mat load_test_image(std::string_view relative) {
    return cv::imread(test_data_path(relative).string(), cv::IMREAD_COLOR);
}

[[nodiscard]] axvp_config_t make_config() {
    axvp_config_t config{};
    config.size = static_cast<std::uint32_t>(sizeof(config));
    config.width = 1U;
    config.height = 1U;
    config.format = AXVP_FMT_BGR;
    config.policy = AXVP_POLICY_NONE;
    config.device_index = 0U;
    config.rppg_window_frames = 16U;
    config.model_dir = AXVP_TEST_DATA_DIR;
    return config;
}

[[nodiscard]] double percentile(std::vector<double> values, double p) {
    if (values.empty()) {
        return 0.0;
    }

    const std::size_t index = static_cast<std::size_t>(
        std::clamp(p, 0.0, 1.0) * static_cast<double>(values.size() - 1U));
    std::nth_element(values.begin(), values.begin() + index, values.end());
    return values[index];
}

} // namespace

int main() {
    const cv::Mat image =
        load_test_image("test-images/face_detection/opencv_extra/gray_face.png");
    if (image.empty()) {
        std::cerr << "benchmark image missing\n";
        return 1;
    }

    auto context = axvp::Context::create(make_config());
    if (!context.has_value()) {
        std::cerr << "failed to create axvphantom context\n";
        return 2;
    }

    constexpr std::size_t kWarmup = 4U;
    constexpr std::size_t kSamples = 24U;
    std::vector<double> latencies_us;
    latencies_us.reserve(kSamples);

    for (std::size_t index = 0U; index < (kWarmup + kSamples); ++index) {
        axvp::Frame frame(image, static_cast<std::uint64_t>(index));
        const auto start = std::chrono::steady_clock::now();
        auto processed = context->process(frame);
        const auto stop = std::chrono::steady_clock::now();
        if (!processed.has_value()) {
            std::cerr << "benchmark processing failed\n";
            return 3;
        }

        if (index >= kWarmup) {
            const auto elapsed = std::chrono::duration_cast<
                std::chrono::duration<double, std::micro>>(stop - start);
            latencies_us.push_back(elapsed.count());
        }
    }

    const double mean = std::accumulate(latencies_us.begin(),
                                        latencies_us.end(), 0.0) /
                        static_cast<double>(latencies_us.size());
    const double p50 = percentile(latencies_us, 0.50);
    const double p95 = percentile(latencies_us, 0.95);
    const double p99 = percentile(latencies_us, 0.99);

    std::cout << "axvp_process_frame latency_us "
              << "mean=" << mean << ' '
              << "p50=" << p50 << ' '
              << "p95=" << p95 << ' '
              << "p99=" << p99 << '\n';
    return 0;
}
