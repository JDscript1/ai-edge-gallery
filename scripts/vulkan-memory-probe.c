// SPDX-License-Identifier: Apache-2.0
// Read-only Vulkan capability query through Android's system loader.
#include <vulkan/vulkan.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void json_string(const char *value) {
    putchar('"');
    for (const unsigned char *p = (const unsigned char *)value; *p; ++p) {
        if (*p == '"' || *p == '\\') putchar('\\');
        if (*p < 32) printf("\\u%04x", *p);
        else putchar(*p);
    }
    putchar('"');
}

int main(void) {
    VkApplicationInfo app = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "Local Edge Gallery memory probe", .apiVersion = VK_API_VERSION_1_1 };
    VkInstanceCreateInfo create = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app };
    VkInstance instance;
    VkResult result = vkCreateInstance(&create, NULL, &instance);
    if (result != VK_SUCCESS) { fprintf(stderr, "vkCreateInstance: %d\n", result); return 1; }
    uint32_t count = 0;
    result = vkEnumeratePhysicalDevices(instance, &count, NULL);
    if (result != VK_SUCCESS || !count) { vkDestroyInstance(instance, NULL); return 2; }
    VkPhysicalDevice *devices = calloc(count, sizeof(*devices));
    if (!devices) { vkDestroyInstance(instance, NULL); return 3; }
    result = vkEnumeratePhysicalDevices(instance, &count, devices);
    if (result != VK_SUCCESS) { free(devices); vkDestroyInstance(instance, NULL); return 4; }
    puts("{\"loader\":\"Android system libvulkan.so\",\"devices\":[");
    for (uint32_t i = 0; i < count; ++i) {
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(devices[i], &properties);
        VkPhysicalDeviceMaintenance3Properties maintenance = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_3_PROPERTIES };
        VkPhysicalDeviceProperties2 properties2 = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, .pNext = &maintenance };
        vkGetPhysicalDeviceProperties2(devices[i], &properties2);
        uint32_t extension_count = 0;
        vkEnumerateDeviceExtensionProperties(devices[i], NULL, &extension_count, NULL);
        VkExtensionProperties *extensions = calloc(extension_count, sizeof(*extensions));
        int has_budget = 0, import_host = 0;
        if (extensions) {
            vkEnumerateDeviceExtensionProperties(devices[i], NULL, &extension_count, extensions);
            for (uint32_t e = 0; e < extension_count; ++e) {
                if (!strcmp(extensions[e].extensionName, VK_EXT_MEMORY_BUDGET_EXTENSION_NAME)) has_budget = 1;
                if (!strcmp(extensions[e].extensionName, "VK_EXT_external_memory_host")) import_host = 1;
            }
            free(extensions);
        }
        VkPhysicalDeviceMemoryBudgetPropertiesEXT budget = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT };
        VkPhysicalDeviceMemoryProperties2 memory = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2,
            .pNext = has_budget ? &budget : NULL };
        vkGetPhysicalDeviceMemoryProperties2(devices[i], &memory);
        if (i) puts(",");
        printf("{\"name\":"); json_string(properties.deviceName);
        printf(",\"vendor_id\":%u,\"device_type\":%u,\"api_version\":\"%u.%u.%u\",",
            properties.vendorID, properties.deviceType, VK_VERSION_MAJOR(properties.apiVersion),
            VK_VERSION_MINOR(properties.apiVersion), VK_VERSION_PATCH(properties.apiVersion));
        printf("\"max_compute_shared_memory_bytes\":%u,\"max_memory_allocations\":%u,"
            "\"max_storage_buffer_range\":%llu,\"max_memory_allocation_size\":%llu,"
            "\"max_compute_workgroup_invocations\":%u,"
            "\"memory_budget_supported\":%s,\"external_memory_host_supported\":%s,\"heaps\":[",
            properties.limits.maxComputeSharedMemorySize, properties.limits.maxMemoryAllocationCount,
            (unsigned long long) properties.limits.maxStorageBufferRange,
            (unsigned long long) maintenance.maxMemoryAllocationSize,
            properties.limits.maxComputeWorkGroupInvocations,
            has_budget ? "true" : "false", import_host ? "true" : "false");
        for (uint32_t h = 0; h < memory.memoryProperties.memoryHeapCount; ++h) {
            if (h) putchar(',');
            printf("{\"index\":%u,\"size_bytes\":%llu,\"flags\":%u", h,
                (unsigned long long) memory.memoryProperties.memoryHeaps[h].size,
                memory.memoryProperties.memoryHeaps[h].flags);
            if (has_budget) printf(",\"budget_bytes\":%llu,\"usage_bytes\":%llu",
                (unsigned long long) budget.heapBudget[h], (unsigned long long) budget.heapUsage[h]);
            putchar('}');
        }
        printf("],\"memory_types\":[");
        for (uint32_t t = 0; t < memory.memoryProperties.memoryTypeCount; ++t) {
            if (t) putchar(',');
            VkMemoryPropertyFlags flags = memory.memoryProperties.memoryTypes[t].propertyFlags;
            printf("{\"heap\":%u,\"raw_flags\":%u,\"device_local\":%s,\"host_visible\":%s,\"host_coherent\":%s,\"host_cached\":%s,\"lazily_allocated\":%s,\"protected\":%s}",
                memory.memoryProperties.memoryTypes[t].heapIndex,
                flags,
                flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT ? "true" : "false",
                flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT ? "true" : "false",
                flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT ? "true" : "false",
                flags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT ? "true" : "false",
                flags & VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT ? "true" : "false",
                flags & VK_MEMORY_PROPERTY_PROTECTED_BIT ? "true" : "false");
        }
        printf("]}");
    }
    puts("]}");
    free(devices);
    vkDestroyInstance(instance, NULL);
    return 0;
}
