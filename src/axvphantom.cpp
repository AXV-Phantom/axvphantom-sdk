#include "axvphantom/axvphantom.h"
#include "internal/internal.hpp"

#include <cstring>
#include <new>
#include <utility>

struct axvp_context_t {
    axvp_config_t config{};
    axvp_policy_t policy = AXVP_POLICY_NONE;
    std::shared_ptr<const axvp::internal::DetectorModel> detector_model{};
    std::shared_ptr<axvp::internal::DetectionStage> detection_stage{};
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

    out->size = sizeof(axvp_result_t);
    out->frame = *in;
    out->metadata = nullptr;
    out->metadata_size = 0U;
    out->status = AXVP_STATUS_OK;
    out->faces_detected = 0U;
    out->faces_anonymized = 0U;
    out->anonymization_complete = 0U;
    std::memset(out->reserved, 0, sizeof(out->reserved));

    if (ctx->detection_stage != nullptr && in->data != nullptr &&
        in->format == AXVP_FMT_BGR && in->width > 0U && in->height > 0U &&
        in->width >= 2U && in->height >= 2U && in->stride > 0U) {
        try {
            const auto image = cv::Mat(
                static_cast<int>(in->height), static_cast<int>(in->width),
                CV_8UC3, const_cast<void *>(in->data),
                static_cast<std::size_t>(in->stride));
            axvp::internal::UniqueFrame frame{image};
            const auto detected = ctx->detection_stage->process(frame);
            if (!detected.has_value()) {
                return status_from_error(detected.error());
            }

            out->faces_detected = static_cast<std::uint32_t>(detected->size());
        } catch (const cv::Exception &) {
            return AXVP_STATUS_INTERNAL_ERROR;
        }
    }

    return AXVP_STATUS_OK;
}

void axvp_release_result(axvp_context_t *ctx, axvp_result_t *result) noexcept {
    (void)ctx;

    if (result == nullptr) {
        return;
    }

    std::memset(result, 0, sizeof(*result));
}

axvp_status_t axvp_set_policy(axvp_context_t *ctx,
                              axvp_policy_t policy) noexcept {
    if (ctx == nullptr) {
        return AXVP_STATUS_NOT_INITIALIZED;
    }

    ctx->policy = policy;
    return AXVP_STATUS_OK;
}

axvp_status_t axvp_rotate_keys(axvp_context_t *ctx) noexcept {
    return ctx == nullptr ? AXVP_STATUS_NOT_INITIALIZED : AXVP_STATUS_OK;
}

} // extern "C"
