#pragma once

#include "axvphantom/axvphantom.h"
#include "internal/error.hpp"

#include <cstdlib>
#include <cstdint>
#include <expected>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if defined(AXVP_WITH_VULKAN) && AXVP_WITH_VULKAN && __has_include(<vulkan/vulkan.h>)
#include <vulkan/vulkan.h>
#define AXVP_INTERNAL_HAS_VULKAN 1
#else
#define AXVP_INTERNAL_HAS_VULKAN 0
#endif

namespace axvp::internal {

class VulkanContext final {
  public:
    VulkanContext(const VulkanContext &) = delete;
    VulkanContext &operator=(const VulkanContext &) = delete;
    VulkanContext(VulkanContext &&) noexcept = delete;
    VulkanContext &operator=(VulkanContext &&) noexcept = delete;

    ~VulkanContext() noexcept { destroy(); }

    [[nodiscard]] static std::expected<std::shared_ptr<const VulkanContext>,
                                       Error>
    create(const axvp_config_t &cfg) noexcept {
        if (cfg.size < sizeof(axvp_config_t)) {
            return std::unexpected(Error::ConfigInvalidValue);
        }

        auto context =
            std::shared_ptr<VulkanContext>(new (std::nothrow) VulkanContext{});
        if (context == nullptr) {
            return std::unexpected(Error::ResourceAllocationFailed);
        }

        context->selected_device_index_ = cfg.device_index;
        context->initialize();
        return std::static_pointer_cast<const VulkanContext>(context);
    }

    [[nodiscard]] bool available() const noexcept { return available_; }

    [[nodiscard]] std::string_view backend_name() const noexcept {
        return backend_name_;
    }

    [[nodiscard]] std::uint32_t selected_device_index() const noexcept {
        return selected_device_index_;
    }

    [[nodiscard]] std::uint32_t queue_family_index() const noexcept {
        return queue_family_index_;
    }

#if AXVP_INTERNAL_HAS_VULKAN
    [[nodiscard]] VkInstance instance() const noexcept { return instance_; }
    [[nodiscard]] VkDevice device() const noexcept { return device_; }
    [[nodiscard]] VkPhysicalDevice physical_device() const noexcept {
        return physical_device_;
    }
    [[nodiscard]] VkQueue queue() const noexcept { return queue_; }
#endif

  private:
    VulkanContext() noexcept = default;

    void initialize() noexcept {
        backend_name_ = "cpu-fallback";
        available_ = false;

        if (force_cpu_backend()) {
            return;
        }

#if AXVP_INTERNAL_HAS_VULKAN
        if (try_initialize_vulkan()) {
            backend_name_ = "vulkan";
            available_ = true;
            return;
        }

        destroy();
#endif
    }

#if AXVP_INTERNAL_HAS_VULKAN
    [[nodiscard]] static VkPhysicalDevice
    choose_physical_device(VkInstance instance,
                           std::uint32_t device_index) noexcept {
        std::uint32_t count = 0U;
        if (vkEnumeratePhysicalDevices(instance, &count, nullptr) != VK_SUCCESS
            || count == 0U) {
            return VK_NULL_HANDLE;
        }

        std::vector<VkPhysicalDevice> devices(count, VK_NULL_HANDLE);
        if (vkEnumeratePhysicalDevices(instance, &count, devices.data()) !=
            VK_SUCCESS) {
            return VK_NULL_HANDLE;
        }

        if (device_index >= devices.size()) {
            return VK_NULL_HANDLE;
        }

        return devices[device_index];
    }

    [[nodiscard]] static std::uint32_t
    choose_queue_family(VkPhysicalDevice device) noexcept {
        std::uint32_t count = 0U;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
        if (count == 0U) {
            return UINT32_MAX;
        }

        std::vector<VkQueueFamilyProperties> properties(count);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &count,
                                                properties.data());

        for (std::uint32_t index = 0U; index < count; ++index) {
            const auto &family = properties[index];
            if ((family.queueFlags & VK_QUEUE_COMPUTE_BIT) != 0U) {
                return index;
            }
        }

        for (std::uint32_t index = 0U; index < count; ++index) {
            const auto &family = properties[index];
            if ((family.queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0U) {
                return index;
            }
        }

        return UINT32_MAX;
    }

    [[nodiscard]] bool try_initialize_vulkan() noexcept {
        VkApplicationInfo app_info{};
        app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        app_info.pApplicationName = "AXV Phantom SDK";
        app_info.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
        app_info.pEngineName = "axvphantom";
        app_info.engineVersion = VK_MAKE_VERSION(0, 1, 0);
        app_info.apiVersion = VK_API_VERSION_1_2;

        VkInstanceCreateInfo instance_info{};
        instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        instance_info.pApplicationInfo = &app_info;

        if (vkCreateInstance(&instance_info, nullptr, &instance_) != VK_SUCCESS) {
            return false;
        }

        physical_device_ = choose_physical_device(instance_,
                                                  selected_device_index_);
        if (physical_device_ == VK_NULL_HANDLE) {
            return false;
        }

        queue_family_index_ = choose_queue_family(physical_device_);
        if (queue_family_index_ == UINT32_MAX) {
            return false;
        }

        const float priority = 1.0f;
        VkDeviceQueueCreateInfo queue_info{};
        queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue_info.queueFamilyIndex = queue_family_index_;
        queue_info.queueCount = 1U;
        queue_info.pQueuePriorities = &priority;

        VkDeviceCreateInfo device_info{};
        device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        device_info.queueCreateInfoCount = 1U;
        device_info.pQueueCreateInfos = &queue_info;

        if (vkCreateDevice(physical_device_, &device_info, nullptr, &device_) !=
            VK_SUCCESS) {
            return false;
        }

        vkGetDeviceQueue(device_, queue_family_index_, 0U, &queue_);

        VkCommandPoolCreateInfo command_pool_info{};
        command_pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        command_pool_info.flags =
            VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        command_pool_info.queueFamilyIndex = queue_family_index_;
        if (vkCreateCommandPool(device_, &command_pool_info, nullptr,
                                &command_pool_) != VK_SUCCESS) {
            return false;
        }

        VkDescriptorPoolSize pool_sizes[] = {
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 8U},
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 8U},
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 8U},
        };

        VkDescriptorPoolCreateInfo descriptor_pool_info{};
        descriptor_pool_info.sType =
            VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        descriptor_pool_info.poolSizeCount =
            static_cast<std::uint32_t>(sizeof(pool_sizes) /
                                       sizeof(pool_sizes[0]));
        descriptor_pool_info.pPoolSizes = pool_sizes;
        descriptor_pool_info.maxSets = 8U;

        if (vkCreateDescriptorPool(device_, &descriptor_pool_info, nullptr,
                                   &descriptor_pool_) != VK_SUCCESS) {
            return false;
        }

        return true;
    }

    void destroy() noexcept {
#if AXVP_INTERNAL_HAS_VULKAN
        if (device_ != VK_NULL_HANDLE) {
            if (descriptor_pool_ != VK_NULL_HANDLE) {
                vkDestroyDescriptorPool(device_, descriptor_pool_, nullptr);
                descriptor_pool_ = VK_NULL_HANDLE;
            }
            if (command_pool_ != VK_NULL_HANDLE) {
                vkDestroyCommandPool(device_, command_pool_, nullptr);
                command_pool_ = VK_NULL_HANDLE;
            }
            vkDestroyDevice(device_, nullptr);
            device_ = VK_NULL_HANDLE;
        }

        if (instance_ != VK_NULL_HANDLE) {
            vkDestroyInstance(instance_, nullptr);
            instance_ = VK_NULL_HANDLE;
        }

        physical_device_ = VK_NULL_HANDLE;
        queue_ = VK_NULL_HANDLE;
        queue_family_index_ = 0U;
#endif
        available_ = false;
    }

    [[nodiscard]] static bool force_cpu_backend() noexcept {
        const char *value = std::getenv("AXVP_FORCE_CPU_BACKEND");
        return value != nullptr && value[0] != '\0' && value[0] != '0';
    }

    VkInstance instance_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    VkCommandPool command_pool_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
#endif

    bool available_ = false;
    std::uint32_t selected_device_index_ = 0U;
    std::uint32_t queue_family_index_ = 0U;
    std::string backend_name_{"cpu-fallback"};
};

} // namespace axvp::internal

#undef AXVP_INTERNAL_HAS_VULKAN
