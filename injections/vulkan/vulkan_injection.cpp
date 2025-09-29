#include <vulkan/vulkan.h>
#include <stdio.h>
#include <dlfcn.h>
#include <string.h>

// Define function pointers for the original Vulkan functions
PFN_vkQueueSubmit original_vkQueueSubmit = nullptr;
PFN_vkQueueWaitIdle original_vkQueueWaitIdle = nullptr;
PFN_vkCreateDevice original_vkCreateDevice = nullptr;
PFN_vkDestroyDevice original_vkDestroyDevice = nullptr;
PFN_vkBeginCommandBuffer original_vkBeginCommandBuffer = nullptr;
PFN_vkEndCommandBuffer original_vkEndCommandBuffer = nullptr;
PFN_vkCmdBeginDebugUtilsLabelEXT original_vkCmdBeginDebugUtilsLabelEXT = nullptr;
PFN_vkCmdEndDebugUtilsLabelEXT original_vkCmdEndDebugUtilsLabelEXT = nullptr;

// Profiler's wrapper for vkCmdBeginDebugUtilsLabelEXT
VKAPI_ATTR void VKAPI_CALL vkCmdBeginDebugUtilsLabelEXT(
    VkCommandBuffer commandBuffer,
    const VkDebugUtilsLabelEXT* pLabelInfo) {
    if (original_vkCmdBeginDebugUtilsLabelEXT) {
        original_vkCmdBeginDebugUtilsLabelEXT(commandBuffer, pLabelInfo);
    }
}

// Profiler's wrapper for vkCmdEndDebugUtilsLabelEXT
VKAPI_ATTR void VKAPI_CALL vkCmdEndDebugUtilsLabelEXT(
    VkCommandBuffer commandBuffer) {
    if (original_vkCmdEndDebugUtilsLabelEXT) {
        original_vkCmdEndDebugUtilsLabelEXT(commandBuffer);
    }
}

PFN_vkGetInstanceProcAddr original_vkGetInstanceProcAddr = nullptr;
PFN_vkGetDeviceProcAddr original_vkGetDeviceProcAddr = nullptr;

// Wrapper for vkGetInstanceProcAddr
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(
    VkInstance instance,
    const char* pName) {

    if (!original_vkGetInstanceProcAddr) {
        original_vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)dlsym(RTLD_NEXT, "vkGetInstanceProcAddr");
        if (!original_vkGetInstanceProcAddr) {
            fprintf(stderr, "[profiler]: Error: Could not find original vkGetInstanceProcAddr\n");
            return nullptr;
        }
    }

    // Intercept functions that create/destroy device or command buffers
    if (strcmp(pName, "vkCreateDevice") == 0) return (PFN_vkVoidFunction)vkCreateDevice;
    if (strcmp(pName, "vkDestroyDevice") == 0) return (PFN_vkVoidFunction)vkDestroyDevice;
    if (strcmp(pName, "vkBeginCommandBuffer") == 0) return (PFN_vkVoidFunction)vkBeginCommandBuffer;
    if (strcmp(pName, "vkEndCommandBuffer") == 0) return (PFN_vkVoidFunction)vkEndCommandBuffer;
    if (strcmp(pName, "vkQueueSubmit") == 0) return (PFN_vkVoidFunction)vkQueueSubmit;
    if (strcmp(pName, "vkQueueWaitIdle") == 0) return (PFN_vkVoidFunction)vkQueueWaitIdle;
    if (strcmp(pName, "vkCmdBeginDebugUtilsLabelEXT") == 0) return (PFN_vkVoidFunction)vkCmdBeginDebugUtilsLabelEXT;
    if (strcmp(pName, "vkCmdEndDebugUtilsLabelEXT") == 0) return (PFN_vkVoidFunction)vkCmdEndDebugUtilsLabelEXT;

    // For all other functions, call the original vkGetInstanceProcAddr
    return original_vkGetInstanceProcAddr(instance, pName);
}

// Wrapper for vkGetDeviceProcAddr
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(
    VkDevice device,
    const char* pName) {

    if (!original_vkGetDeviceProcAddr) {
        original_vkGetDeviceProcAddr = (PFN_vkGetDeviceProcAddr)dlsym(RTLD_NEXT, "vkGetDeviceProcAddr");
        if (!original_vkGetDeviceProcAddr) {
            fprintf(stderr, "[profiler]: Error: Could not find original vkGetDeviceProcAddr\n");
            return nullptr;
        }
    }

    // Intercept functions that submit jobs to GPU or tag shaders
    if (strcmp(pName, "vkQueueSubmit") == 0) return (PFN_vkVoidFunction)vkQueueSubmit;
    if (strcmp(pName, "vkQueueWaitIdle") == 0) return (PFN_vkVoidFunction)vkQueueWaitIdle;
    if (strcmp(pName, "vkCmdBeginDebugUtilsLabelEXT") == 0) return (PFN_vkVoidFunction)vkCmdBeginDebugUtilsLabelEXT;
    if (strcmp(pName, "vkCmdEndDebugUtilsLabelEXT") == 0) return (PFN_vkVoidFunction)vkCmdEndDebugUtilsLabelEXT;
    if (strcmp(pName, "vkBeginCommandBuffer") == 0) return (PFN_vkVoidFunction)vkBeginCommandBuffer;
    if (strcmp(pName, "vkEndCommandBuffer") == 0) return (PFN_vkVoidFunction)vkEndCommandBuffer;

    // For all other functions, call the original vkGetDeviceProcAddr
    return original_vkGetDeviceProcAddr(device, pName);
}

static VkDevice g_device = VK_NULL_HANDLE;

// Wrapper for vkCreateDevice
VKAPI_ATTR VkResult VKAPI_CALL vkCreateDevice(
    VkPhysicalDevice physicalDevice,
    const VkDeviceCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkDevice* pDevice) {

    if (!original_vkCreateDevice) {
        original_vkCreateDevice = (PFN_vkCreateDevice)dlsym(RTLD_NEXT, "vkCreateDevice");
        if (!original_vkCreateDevice) {
            fprintf(stderr, "[profiler]: Error: Could not find original vkCreateDevice\n");
            return VK_ERROR_UNKNOWN;
        }
    }

    VkResult result = original_vkCreateDevice(physicalDevice, pCreateInfo, pAllocator, pDevice);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "[profiler]: Error: Original vkCreateDevice failed with result %d\n", result);
        return result;
    }

    g_device = *pDevice;

    // Get function pointers for debug utils from the next layer/driver
    PFN_vkGetDeviceProcAddr next_vkGetDeviceProcAddr = (PFN_vkGetDeviceProcAddr)dlsym(RTLD_NEXT, "vkGetDeviceProcAddr");
    if (next_vkGetDeviceProcAddr) {
        original_vkCmdBeginDebugUtilsLabelEXT = (PFN_vkCmdBeginDebugUtilsLabelEXT)next_vkGetDeviceProcAddr(*pDevice, "vkCmdBeginDebugUtilsLabelEXT");
        original_vkCmdEndDebugUtilsLabelEXT = (PFN_vkCmdEndDebugUtilsLabelEXT)next_vkGetDeviceProcAddr(*pDevice, "vkCmdEndDebugUtilsLabelEXT");
    } else {
        fprintf(stderr, "[profiler]: Warning: Could not find original vkGetDeviceProcAddr for debug utils.\n");
    }


    return result;
}

// Wrapper for vkDestroyDevice
VKAPI_ATTR void VKAPI_CALL vkDestroyDevice(
    VkDevice device,
    const VkAllocationCallbacks* pAllocator) {

    if (!original_vkDestroyDevice) {
        original_vkDestroyDevice = (PFN_vkDestroyDevice)dlsym(RTLD_NEXT, "vkDestroyDevice");
        if (!original_vkDestroyDevice) {
            fprintf(stderr, "[profiler]: Error: Could not find original vkDestroyDevice\n");
            return;
        }
    }

    original_vkDestroyDevice(device, pAllocator);
    g_device = VK_NULL_HANDLE;
}

// Wrapper for vkBeginCommandBuffer
VKAPI_ATTR VkResult VKAPI_CALL vkBeginCommandBuffer(
    VkCommandBuffer commandBuffer,
    const VkCommandBufferBeginInfo* pBeginInfo) {

    if (!original_vkBeginCommandBuffer) {
        original_vkBeginCommandBuffer = (PFN_vkBeginCommandBuffer)dlsym(RTLD_NEXT, "vkBeginCommandBuffer");
        if (!original_vkBeginCommandBuffer) {
            fprintf(stderr, "[profiler]: Error: Could not find original vkBeginCommandBuffer\n");
            return VK_ERROR_UNKNOWN;
        }
    }

    VkResult result = original_vkBeginCommandBuffer(commandBuffer, pBeginInfo);

    if (result == VK_SUCCESS && original_vkCmdBeginDebugUtilsLabelEXT) {
        VkDebugUtilsLabelEXT labelInfo{};
        labelInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
        char labelName[256];
        snprintf(labelName, sizeof(labelName), "[profiler]: Command Buffer Start: %p", (void*)commandBuffer);
        labelInfo.pLabelName = labelName;
        labelInfo.color[0] = 0.0f; labelInfo.color[1] = 1.0f; labelInfo.color[2] = 0.0f; labelInfo.color[3] = 1.0f; // Green
        original_vkCmdBeginDebugUtilsLabelEXT(commandBuffer, &labelInfo);
    }
    return result;
}

// Wrapper for vkEndCommandBuffer
VKAPI_ATTR VkResult VKAPI_CALL vkEndCommandBuffer(
    VkCommandBuffer commandBuffer) {

    if (!original_vkEndCommandBuffer) {
        original_vkEndCommandBuffer = (PFN_vkEndCommandBuffer)dlsym(RTLD_NEXT, "vkEndCommandBuffer");
        if (!original_vkEndCommandBuffer) {
            fprintf(stderr, "[profiler]: Error: Could not find original vkEndCommandBuffer\n");
            return VK_ERROR_UNKNOWN;
        }
    }

    if (original_vkCmdEndDebugUtilsLabelEXT) {
        original_vkCmdEndDebugUtilsLabelEXT(commandBuffer);
    }

    VkResult result = original_vkEndCommandBuffer(commandBuffer);

    return result;
}

// Wrapper for vkQueueSubmit
VKAPI_ATTR VkResult VKAPI_CALL vkQueueSubmit(
    VkQueue                                     queue,
    uint32_t                                    submitCount,
    const VkSubmitInfo*                         pSubmits,
    VkFence                                     fence) {
    
    printf("[profiler]: vkQueueSubmit called! Queue: %p, Submit Count: %u\n", (void*)queue, submitCount);

    for (uint32_t i = 0; i < submitCount; ++i) {
        for (uint32_t j = 0; j < pSubmits[i].commandBufferCount; ++j) {
            printf("[profiler]:   Command Buffer[%u][%u]: %p\n", i, j, (void*)pSubmits[i].pCommandBuffers[j]);
        }
    }

    if (!original_vkQueueSubmit) {
        original_vkQueueSubmit = (PFN_vkQueueSubmit)dlsym(RTLD_NEXT, "vkQueueSubmit");
        if (!original_vkQueueSubmit) {
            fprintf(stderr, "[profiler]: Error: Could not find original vkQueueSubmit\n");
            return VK_ERROR_UNKNOWN;
        }
    }
    return original_vkQueueSubmit(queue, submitCount, pSubmits, fence);
}

// Wrapper for vkQueueWaitIdle
VKAPI_ATTR VkResult VKAPI_CALL vkQueueWaitIdle(
    VkQueue                                     queue) {

    printf("[profiler]: vkQueueWaitIdle called! Queue: %p\n", (void*)queue);

    if (!original_vkQueueWaitIdle) {
        original_vkQueueWaitIdle = (PFN_vkQueueWaitIdle)dlsym(RTLD_NEXT, "vkQueueWaitIdle");
        if (!original_vkQueueWaitIdle) {
            fprintf(stderr, "[profiler]: Error: Could not find original vkQueueWaitIdle\n");
            return VK_ERROR_UNKNOWN;
        }
    }
    return original_vkQueueWaitIdle(queue);
}
