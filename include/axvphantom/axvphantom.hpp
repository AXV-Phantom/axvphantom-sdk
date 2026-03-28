#ifndef AXVPHANTOM_AXVPHANTOM_HPP
#define AXVPHANTOM_AXVPHANTOM_HPP

#include "axvphantom.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <opencv2/core.hpp>
#include <span>
#include <type_traits>
#include <utility>

inline constexpr axvp_policy_t operator|(axvp_policy_t lhs,
                                         axvp_policy_t rhs) noexcept {
    using Underlying = std::underlying_type_t<axvp_policy_t>;
    return static_cast<axvp_policy_t>(static_cast<Underlying>(lhs) |
                                      static_cast<Underlying>(rhs));
}

inline constexpr axvp_policy_t operator&(axvp_policy_t lhs,
                                         axvp_policy_t rhs) noexcept {
    using Underlying = std::underlying_type_t<axvp_policy_t>;
    return static_cast<axvp_policy_t>(static_cast<Underlying>(lhs) &
                                      static_cast<Underlying>(rhs));
}

inline constexpr axvp_policy_t &operator|=(axvp_policy_t &lhs,
                                           axvp_policy_t rhs) noexcept {
    lhs = lhs | rhs;
    return lhs;
}

inline constexpr axvp_policy_t &operator&=(axvp_policy_t &lhs,
                                           axvp_policy_t rhs) noexcept {
    lhs = lhs & rhs;
    return lhs;
}

namespace axvp {

using Status = ::axvp_status_t;
using Policy = ::axvp_policy_t;
using PixelFormat = ::axvp_pixel_format_t;
using LivenessVerdict = ::axvp_liveness_verdict_t;

class Frame final {
  public:
    Frame() noexcept { reset(); }

    Frame(const void *data, std::uint32_t width, std::uint32_t height,
          std::uint32_t stride, PixelFormat format,
          std::uint64_t timestamp_ns = 0U) noexcept {
        reset();
        frame_.data = data;
        frame_.width = width;
        frame_.height = height;
        frame_.stride = stride;
        frame_.format = format;
        frame_.timestamp_ns = timestamp_ns;
    }

    explicit Frame(const cv::Mat &mat, std::uint64_t timestamp_ns = 0U) noexcept
        : Frame(mat.empty() ? nullptr : mat.data,
                static_cast<std::uint32_t>(mat.cols),
                static_cast<std::uint32_t>(mat.rows),
                static_cast<std::uint32_t>(mat.step), AXVP_FMT_BGR,
                timestamp_ns) {}

    [[nodiscard]] const axvp_frame_t *native() const noexcept {
        return &frame_;
    }

    [[nodiscard]] axvp_frame_t *native() noexcept { return &frame_; }

    [[nodiscard]] const void *data() const noexcept { return frame_.data; }
    [[nodiscard]] std::uint32_t width() const noexcept { return frame_.width; }
    [[nodiscard]] std::uint32_t height() const noexcept {
        return frame_.height;
    }
    [[nodiscard]] std::uint32_t stride() const noexcept {
        return frame_.stride;
    }
    [[nodiscard]] PixelFormat format() const noexcept { return frame_.format; }
    [[nodiscard]] std::uint64_t timestamp_ns() const noexcept {
        return frame_.timestamp_ns;
    }

  private:
    void reset() noexcept {
        frame_ = {};
        frame_.size = sizeof(axvp_frame_t);
    }

    axvp_frame_t frame_{};
};

class Result final {
  public:
    Result() noexcept { reset(); }

    explicit Result(axvp_context_t *ctx) noexcept : ctx_(ctx) { reset(); }

    ~Result() noexcept { reset(); }

    Result(const Result &) = delete;
    Result &operator=(const Result &) = delete;

    Result(Result &&other) noexcept { *this = std::move(other); }

    Result &operator=(Result &&other) noexcept {
        if (this != &other) {
            reset();
            ctx_ = std::exchange(other.ctx_, nullptr);
            result_ = other.result_;
            other.reset();
        }
        return *this;
    }

    [[nodiscard]] axvp_result_t *native() noexcept { return &result_; }
    [[nodiscard]] const axvp_result_t *native() const noexcept {
        return &result_;
    }

    [[nodiscard]] axvp_context_t *context() const noexcept { return ctx_; }

    void reset() noexcept {
        if (ctx_ != nullptr) {
            axvp_release_result(ctx_, &result_);
        }

        result_ = {};
        result_.size = sizeof(axvp_result_t);
    }

  private:
    axvp_context_t *ctx_ = nullptr;
    axvp_result_t result_{};
};

class MetadataView final {
  public:
    MetadataView() noexcept = default;

    explicit MetadataView(std::span<const std::byte> bytes) noexcept
        : bytes_(bytes) {}

    [[nodiscard]] std::span<const std::byte> bytes() const noexcept {
        return bytes_;
    }

    [[nodiscard]] bool empty() const noexcept { return bytes_.empty(); }

  private:
    std::span<const std::byte> bytes_{};
};

class Context final {
  public:
    Context() noexcept = default;

    ~Context() noexcept { reset(); }

    Context(const Context &) = delete;
    Context &operator=(const Context &) = delete;

    Context(Context &&other) noexcept
        : handle_(std::exchange(other.handle_, nullptr)) {}

    Context &operator=(Context &&other) noexcept {
        if (this != &other) {
            reset();
            handle_ = std::exchange(other.handle_, nullptr);
        }

        return *this;
    }

    [[nodiscard]] static std::expected<Context, axvp_status_t>
    create(const axvp_config_t &cfg) noexcept {
        Context context;
        axvp_status_t status = AXVP_STATUS_OK;
        context.handle_ = axvp_create(&cfg, &status);
        if (context.handle_ == nullptr) {
            return std::unexpected(
                status == AXVP_STATUS_OK ? AXVP_STATUS_INTERNAL_ERROR : status);
        }

        return context;
    }

    [[nodiscard]] bool valid() const noexcept { return handle_ != nullptr; }
    [[nodiscard]] axvp_context_t *native() const noexcept { return handle_; }

    [[nodiscard]] axvp_status_t set_policy(Policy policy) noexcept {
        return handle_ == nullptr ? AXVP_STATUS_NOT_INITIALIZED
                                  : axvp_set_policy(handle_, policy);
    }

    [[nodiscard]] axvp_status_t rotate_keys() noexcept {
        return handle_ == nullptr ? AXVP_STATUS_NOT_INITIALIZED
                                  : axvp_rotate_keys(handle_);
    }

    [[nodiscard]] std::expected<Result, axvp_status_t>
    process(const Frame &frame) noexcept {
        if (handle_ == nullptr) {
            return std::unexpected(AXVP_STATUS_NOT_INITIALIZED);
        }

        Result result(handle_);
        const axvp_status_t status =
            axvp_process_frame(handle_, frame.native(), result.native());
        if (status != AXVP_STATUS_OK) {
            return std::unexpected(status);
        }

        return result;
    }

    void reset() noexcept {
        if (handle_ != nullptr) {
            axvp_destroy(handle_);
            handle_ = nullptr;
        }
    }

  private:
    explicit Context(axvp_context_t *handle) noexcept : handle_(handle) {}

    axvp_context_t *handle_ = nullptr;
};

} // namespace axvp

#endif // AXVPHANTOM_AXVPHANTOM_HPP
