#include "axvphantom/axvphantom.h"
#include "internal/internal.hpp"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <memory>
#include <new>
#include <utility>

struct axvp_context_t {
    axvp_config_t config{};
    axvp_policy_t policy = AXVP_POLICY_NONE;
    std::shared_ptr<const axvp::internal::DetectorModel> detector_model{};
    std::shared_ptr<axvp::internal::DetectionStage> detection_stage{};
    std::shared_ptr<const axvp::internal::VulkanContext> vulkan_context{};
    std::unique_ptr<axvp::internal::AnonymizationStage> anonymization_stage{};
    std::unique_ptr<axvp::internal::LivenessStage> liveness_stage{};
    std::unique_ptr<axvp::internal::ComposerStage> composer_stage{};
    std::shared_ptr<axvp::internal::UniqueFrame> output_frame{};
    std::vector<std::byte> metadata_storage{};
    std::uint64_t frame_sequence = 1U;
};

namespace {

[[maybe_unused]] const auto kInternalAnchor =
    axvp::internal::error_message(axvp::internal::Error::Ok);

[[nodiscard]] constexpr bool has_supported_size(std::uint32_t size,
                                                std::size_t required) noexcept {
    return size >= required;
}

[[nodiscard]] axvp_status_t invalid_result(axvp_result_t *out) noexcept {
    if (out != nullptr) {
        std::memset(out, 0, sizeof(*out));
    }

    return AXVP_STATUS_INVALID_ARGUMENT;
}

[[nodiscard]] axvp_status_t
status_from_error(axvp::internal::Error error) noexcept {
    using axvp::internal::Error;

    switch (error) {
    case Error::Ok:
        return AXVP_STATUS_OK;
    case Error::ConfigError:
    case Error::ConfigMissingValue:
    case Error::ConfigInvalidValue:
    case Error::ResourceError:
    case Error::ResourceNotFound:
        return AXVP_STATUS_INVALID_CONFIG;
    case Error::ConfigUnsupportedValue:
        return AXVP_STATUS_UNSUPPORTED;
    case Error::ResourceAllocationFailed:
    case Error::ResourceExhausted:
    case Error::ResourceLockFailed:
    case Error::ResourceUnlockFailed:
        return AXVP_STATUS_OUT_OF_MEMORY;
    case Error::PipelineError:
    case Error::PipelineNotInitialized:
    case Error::PipelineStageFailed:
    case Error::PipelineDetectionIncomplete:
        return AXVP_STATUS_INTERNAL_ERROR;
    case Error::SecurityError:
    case Error::SecurityModelTampered:
    case Error::SecurityWipeFailed:
    case Error::SecurityIntegrityViolation:
        return AXVP_STATUS_SECURITY_ERROR;
    }

    return AXVP_STATUS_INTERNAL_ERROR;
}

[[nodiscard]] bool is_detectable_frame(const axvp_frame_t &frame) noexcept {
    return frame.data != nullptr && frame.width >= 2U && frame.height >= 2U &&
           frame.stride >= frame.width * 3U;
}

[[nodiscard]] cv::Mat make_input_view(const axvp_frame_t &frame) noexcept {
    return cv::Mat(static_cast<int>(frame.height), static_cast<int>(frame.width),
                   CV_8UC3, const_cast<void *>(frame.data),
                   static_cast<std::size_t>(frame.stride));
}

void blur_entire_frame(cv::Mat &image) noexcept {
    if (image.empty() || image.type() != CV_8UC3) {
        return;
    }

    const cv::Size kernel{
        std::max(3, (image.cols / 32) | 1),
        std::max(3, (image.rows / 32) | 1),
    };
    cv::GaussianBlur(image, image, kernel, 0.0, 0.0);
}

void update_result_frame(axvp_context_t *ctx, axvp_result_t *out,
                         const axvp_frame_t &in) {
    if (ctx->output_frame == nullptr || !ctx->output_frame->has_value()) {
        out->frame = in;
        return;
    }

    out->frame = in;
    out->frame.data = ctx->output_frame->mat().data;
    out->frame.stride = static_cast<std::uint32_t>(ctx->output_frame->mat().step);
}

void clear_result_storage(axvp_context_t *ctx) {
    if (ctx == nullptr) {
        return;
    }

    if (!ctx->metadata_storage.empty()) {
        std::fill(ctx->metadata_storage.begin(), ctx->metadata_storage.end(),
                  std::byte{0});
        std::atomic_signal_fence(std::memory_order_seq_cst);
        ctx->metadata_storage.clear();
    }

    ctx->output_frame.reset();
}

} // namespace

extern "C" {

axvp_context_t *axvp_create(const axvp_config_t *cfg,
                            axvp_status_t *status) noexcept {
    if (status != nullptr) {
        *status = AXVP_STATUS_OK;
    }

    if (cfg == nullptr) {
        if (status != nullptr) {
            *status = AXVP_STATUS_INVALID_ARGUMENT;
        }
        return nullptr;
    }

    if (!has_supported_size(cfg->size, sizeof(axvp_config_t))) {
        if (status != nullptr) {
            *status = AXVP_STATUS_INVALID_CONFIG;
        }
        return nullptr;
    }

    auto *context = new (std::nothrow) axvp_context_t{};
    if (context == nullptr) {
        if (status != nullptr) {
            *status = AXVP_STATUS_OUT_OF_MEMORY;
        }
        return nullptr;
    }

    context->config = *cfg;
    context->policy = cfg->policy;

    auto detector_model = axvp::internal::DetectorModel::create(*cfg);
    if (!detector_model.has_value()) {
        if (status != nullptr) {
            *status = status_from_error(detector_model.error());
        }
        delete context;
        return nullptr;
    }

    context->detector_model = std::make_shared<axvp::internal::DetectorModel>(
        std::move(detector_model.value()));

    auto detection_stage = axvp::internal::DetectionStage::create(
        *cfg, context->detector_model);
    if (!detection_stage.has_value()) {
        if (status != nullptr) {
            *status = status_from_error(detection_stage.error());
        }
        delete context;
        return nullptr;
    }

    context->detection_stage = std::make_shared<axvp::internal::DetectionStage>(
        std::move(detection_stage.value()));

    auto vulkan_context = axvp::internal::VulkanContext::create(*cfg);
    if (!vulkan_context.has_value()) {
        if (status != nullptr) {
            *status = status_from_error(vulkan_context.error());
        }
        delete context;
        return nullptr;
    }

    context->vulkan_context = std::move(*vulkan_context);

    auto anonymization_stage = axvp::internal::AnonymizationStage::create(
        context->vulkan_context, context->policy);
    if (!anonymization_stage.has_value()) {
        if (status != nullptr) {
            *status = status_from_error(anonymization_stage.error());
        }
        delete context;
        return nullptr;
    }
    context->anonymization_stage =
        std::make_unique<axvp::internal::AnonymizationStage>(
            std::move(anonymization_stage.value()));

    auto liveness_stage = axvp::internal::LivenessStage::create(*cfg);
    if (!liveness_stage.has_value()) {
        if (status != nullptr) {
            *status = status_from_error(liveness_stage.error());
        }
        delete context;
        return nullptr;
    }
    context->liveness_stage = std::make_unique<axvp::internal::LivenessStage>(
        std::move(liveness_stage.value()));

    auto composer_stage = axvp::internal::ComposerStage::create();
    if (!composer_stage.has_value()) {
        if (status != nullptr) {
            *status = status_from_error(composer_stage.error());
        }
        delete context;
        return nullptr;
    }
    context->composer_stage = std::make_unique<axvp::internal::ComposerStage>(
        std::move(composer_stage.value()));

    return context;
}

void axvp_destroy(axvp_context_t *ctx) noexcept { delete ctx; }

axvp_status_t axvp_process_frame(axvp_context_t *ctx, const axvp_frame_t *in,
                                 axvp_result_t *out) noexcept {
    if (ctx == nullptr) {
        return AXVP_STATUS_NOT_INITIALIZED;
    }

    if (in == nullptr || out == nullptr) {
        return invalid_result(out);
    }

    if (!has_supported_size(in->size, sizeof(axvp_frame_t)) ||
        !has_supported_size(out->size, sizeof(axvp_result_t))) {
        return invalid_result(out);
    }

    if (in->format != AXVP_FMT_BGR || in->data == nullptr || in->width == 0U ||
        in->height == 0U || in->stride < (in->width * 3U)) {
        return invalid_result(out);
    }

    out->size = sizeof(axvp_result_t);
    out->frame = *in;
    out->metadata = nullptr;
    out->metadata_size = 0U;
    out->status = AXVP_STATUS_OK;
    out->faces_detected = 0U;
    out->faces_anonymized = 0U;
    out->anonymization_complete = 1U;
    std::memset(out->reserved, 0, sizeof(out->reserved));

    std::atomic<std::uint32_t> latency_us{0U};
    axvp::internal::ScopedTimer timer(latency_us);

    cv::Mat input_view = make_input_view(*in);
    axvp::internal::UniqueFrame source(input_view);
    if (!source) {
        return invalid_result(out);
    }

    axvp::internal::UniqueFrame anonymized(source.mat());
    if (!anonymized) {
        return invalid_result(out);
    }

    axvp::internal::DetectionResult detection{};
    axvp::internal::AnonymizationResult anonymization{};
    axvp::internal::LivenessResult liveness{};

    if (ctx->detection_stage != nullptr && is_detectable_frame(*in)) {
        const auto detected = ctx->detection_stage->process(source);
        if (!detected.has_value()) {
            if ((ctx->policy & AXVP_POLICY_BLUR_FALLBACK) != AXVP_POLICY_NONE) {
                blur_entire_frame(anonymized.mat());
                source.wipe();
                timer.stop();
                auto metadata = ctx->composer_stage->compose(
                    ctx->frame_sequence++, *in, detection, anonymization,
                    liveness, latency_us.load(std::memory_order_relaxed));
                if (!metadata.has_value()) {
                    return status_from_error(metadata.error());
                }

                ctx->metadata_storage = std::move(*metadata);
                ctx->output_frame =
                    std::make_shared<axvp::internal::UniqueFrame>(
                        std::move(anonymized));
                out->faces_detected = 0U;
                out->faces_anonymized = 0U;
                out->anonymization_complete = 1U;
                update_result_frame(ctx, out, *in);
                out->metadata = ctx->metadata_storage.empty()
                                    ? nullptr
                                    : reinterpret_cast<const std::uint8_t *>(
                                          ctx->metadata_storage.data());
                out->metadata_size = ctx->metadata_storage.size();
                out->status = AXVP_STATUS_OK;
                return AXVP_STATUS_OK;
            }

            return status_from_error(detected.error());
        }

        detection = std::move(*detected);
    }

    if (ctx->anonymization_stage != nullptr) {
        const auto anonymized_result =
            ctx->anonymization_stage->process(anonymized, detection);
        if (!anonymized_result.has_value()) {
            return status_from_error(anonymized_result.error());
        }

        anonymization = std::move(*anonymized_result);
    }

    if (ctx->liveness_stage != nullptr && detection.size() > 0U) {
        const auto liveness_result =
            ctx->liveness_stage->process(source.mat(), detection,
                                         in->timestamp_ns);
        if (!liveness_result.has_value()) {
            return status_from_error(liveness_result.error());
        }

        liveness = std::move(*liveness_result);
    }

    source.wipe();
    timer.stop();

    if (ctx->composer_stage == nullptr) {
        return AXVP_STATUS_INTERNAL_ERROR;
    }

    const auto metadata = ctx->composer_stage->compose(
        ctx->frame_sequence++, *in, detection, anonymization, liveness,
        latency_us.load(std::memory_order_relaxed));
    if (!metadata.has_value()) {
        return status_from_error(metadata.error());
    }

    ctx->metadata_storage = std::move(*metadata);
    ctx->output_frame =
        std::make_shared<axvp::internal::UniqueFrame>(std::move(anonymized));

    out->faces_detected = static_cast<std::uint32_t>(detection.size());
    out->faces_anonymized = static_cast<std::uint32_t>(anonymization.size());
    out->anonymization_complete =
        detection.size() == 0U
            ? 1U
            : static_cast<std::uint8_t>(
                  anonymization.size() == detection.size() &&
                  std::all_of(anonymization.face_records().begin(),
                              anonymization.face_records().end(),
                              [](const auto &face) { return face.pixels_wiped; }));
    update_result_frame(ctx, out, *in);
    out->metadata = ctx->metadata_storage.empty()
                        ? nullptr
                        : reinterpret_cast<const std::uint8_t *>(
                              ctx->metadata_storage.data());
    out->metadata_size = ctx->metadata_storage.size();
    out->status = AXVP_STATUS_OK;

    return AXVP_STATUS_OK;
}

void axvp_release_result(axvp_context_t *ctx, axvp_result_t *result) noexcept {
    if (result == nullptr) {
        return;
    }

    clear_result_storage(ctx);
    std::memset(result, 0, sizeof(*result));
}

axvp_status_t axvp_set_policy(axvp_context_t *ctx,
                              axvp_policy_t policy) noexcept {
    if (ctx == nullptr) {
        return AXVP_STATUS_NOT_INITIALIZED;
    }

    ctx->policy = policy;
    if (ctx->detection_stage != nullptr) {
        ctx->detection_stage->set_policy(policy);
    }
    if (ctx->anonymization_stage != nullptr) {
        ctx->anonymization_stage->set_policy(policy);
    }

    return AXVP_STATUS_OK;
}

axvp_status_t axvp_rotate_keys(axvp_context_t *ctx) noexcept {
    if (ctx == nullptr) {
        return AXVP_STATUS_NOT_INITIALIZED;
    }

    if (ctx->composer_stage == nullptr) {
        return AXVP_STATUS_INTERNAL_ERROR;
    }

    const auto rotated = ctx->composer_stage->rotate_keys();
    if (!rotated.has_value()) {
        return status_from_error(rotated.error());
    }

    return AXVP_STATUS_OK;
}

} // extern "C"
