#pragma once

#include <cstdint>
#include <string_view>

namespace axvp::internal {

enum class Error : std::uint8_t {
    Ok = 0,
    ConfigMissingValue,
    ConfigInvalidValue,
    ConfigUnsupportedValue,
    ResourceAllocationFailed,
    ResourceExhausted,
    ResourceLockFailed,
    ResourceUnlockFailed,
    PipelineNotInitialized,
    PipelineStageFailed,
    SecurityWipeFailed,
    SecurityIntegrityViolation,
};

[[nodiscard]] inline constexpr std::string_view
error_message(Error error) noexcept {
    switch (error) {
    case Error::Ok:
        return "ok";
    case Error::ConfigMissingValue:
        return "config: missing value";
    case Error::ConfigInvalidValue:
        return "config: invalid value";
    case Error::ConfigUnsupportedValue:
        return "config: unsupported value";
    case Error::ResourceAllocationFailed:
        return "resource: allocation failed";
    case Error::ResourceExhausted:
        return "resource: exhausted";
    case Error::ResourceLockFailed:
        return "resource: lock failed";
    case Error::ResourceUnlockFailed:
        return "resource: unlock failed";
    case Error::PipelineNotInitialized:
        return "pipeline: not initialized";
    case Error::PipelineStageFailed:
        return "pipeline: stage failed";
    case Error::SecurityWipeFailed:
        return "security: wipe failed";
    case Error::SecurityIntegrityViolation:
        return "security: integrity violation";
    }

    return "unknown error";
}

} // namespace axvp::internal
