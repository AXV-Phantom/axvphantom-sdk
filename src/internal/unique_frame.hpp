#pragma once

#include "support.hpp"

#include <cstddef>
#include <memory>
#include <opencv2/core/mat.hpp>
#include <span>
#include <utility>

namespace axvp::internal::detail {

inline void wipe_mat(cv::Mat &mat) noexcept {
    if (mat.data == nullptr || mat.total() == 0U || mat.elemSize() == 0U) {
        return;
    }

    secure_zero(mat.data, mat.total() * mat.elemSize());
}

} // namespace axvp::internal::detail

namespace axvp::internal {

class UniqueFrame final {
  public:
    struct MatDeleter final {
        void operator()(cv::Mat *mat) const noexcept {
            if (mat == nullptr) {
                return;
            }

            detail::wipe_mat(*mat);
            delete mat;
        }
    };

    UniqueFrame() noexcept = default;

    explicit UniqueFrame(const cv::Mat &frame) {
        if (!frame.empty()) {
            frame_ = std::unique_ptr<cv::Mat, MatDeleter>(
                new cv::Mat(frame.clone()));
        }
    }

    ~UniqueFrame() noexcept = default;

    UniqueFrame(const UniqueFrame &) = delete;
    UniqueFrame &operator=(const UniqueFrame &) = delete;
    UniqueFrame(UniqueFrame &&) noexcept = default;
    UniqueFrame &operator=(UniqueFrame &&) noexcept = default;

    [[nodiscard]] bool has_value() const noexcept { return frame_ != nullptr; }

    [[nodiscard]] explicit operator bool() const noexcept {
        return has_value();
    }

    [[nodiscard]] cv::Mat &mat() noexcept { return *frame_; }

    [[nodiscard]] const cv::Mat &mat() const noexcept { return *frame_; }

    [[nodiscard]] cv::Mat *get() noexcept { return frame_.get(); }

    [[nodiscard]] const cv::Mat *get() const noexcept { return frame_.get(); }

    [[nodiscard]] std::size_t byte_size() const noexcept {
        return frame_ == nullptr ? 0U : (frame_->total() * frame_->elemSize());
    }

    [[nodiscard]] std::span<std::byte> bytes() noexcept {
        return frame_ == nullptr
                   ? std::span<std::byte>{}
                   : std::span<std::byte>{
                         reinterpret_cast<std::byte *>(frame_->data),
                         byte_size()};
    }

    [[nodiscard]] std::span<const std::byte> bytes() const noexcept {
        return frame_ == nullptr
                   ? std::span<const std::byte>{}
                   : std::span<const std::byte>{
                         reinterpret_cast<const std::byte *>(frame_->data),
                         byte_size()};
    }

    void wipe() noexcept {
        if (frame_ != nullptr) {
            detail::wipe_mat(*frame_);
        }
    }

  private:
    std::unique_ptr<cv::Mat, MatDeleter> frame_{};
};

} // namespace axvp::internal
