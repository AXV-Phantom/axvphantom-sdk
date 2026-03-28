#pragma once

#include <cstdint>
#include <string_view>

namespace axvp::internal {

enum class Error : std::uint8_t {
    Ok = 0,
    ConfigError = 0x10,
    ConfigMissingValue,
    ConfigInvalidValue,
    ConfigUnsupportedValue,
    ResourceError = 0x20,
    ResourceAllocationFailed,
    ResourceExhausted,
    ResourceLockFailed,
    ResourceUnlockFailed,
    PipelineError = 0x30,
    PipelineNotInitialized,
    PipelineStageFailed,
    SecurityError = 0x40,
    SecurityWipeFailed,
    SecurityIntegrityViolation,
};

[[nodiscard]] inline constexpr std::string_view
error_message(Error error) noexcept {
    switch (error) {
    case Error::Ok:
        return "ok";
    case Error::ConfigError:
        return "config error";
    case Error::ConfigMissingValue:
        return "config: missing value";
    case Error::ConfigInvalidValue:
        return "config: invalid value";
    case Error::ConfigUnsupportedValue:
        return "config: unsupported value";
    case Error::ResourceError:
        return "resource error";
    case Error::ResourceAllocationFailed:
        return "resource: allocation failed";
    case Error::ResourceExhausted:
        return "resource: exhausted";
    case Error::ResourceLockFailed:
        return "resource: lock failed";
    case Error::ResourceUnlockFailed:
        return "resource: unlock failed";
    case Error::PipelineError:
        return "pipeline error";
    case Error::PipelineNotInitialized:
        return "pipeline: not initialized";
    case Error::PipelineStageFailed:
        return "pipeline: stage failed";
    case Error::SecurityError:
        return "security error";
    case Error::SecurityWipeFailed:
        return "security: wipe failed";
    case Error::SecurityIntegrityViolation:
        return "security: integrity violation";
    }

    return "unknown error";
}

} // namespace axvp::internal
