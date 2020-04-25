#include <Windows.h>

#define VK_ENABLE_BETA_EXTENSIONS
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>

#include <pathcch.h>
#include <Shlwapi.h>

#include <fstream>
#include <iostream>
#include <vector>

#define ASSERT_VK_RESULT(r)                                                                    \
    {                                                                                          \
        VkResult result = (r);                                                                 \
        if (result != VK_SUCCESS) {                                                            \
            std::cout << "Vulkan Assertion failed in Line " << __LINE__ << " with: " << result \
                      << std::endl;                                                            \
        }                                                                                      \
    }

#define RESOLVE_VK_INSTANCE_PFN(instance, funcName)                                          \
    {                                                                                        \
        funcName =                                                                           \
            reinterpret_cast<PFN_##funcName>(vkGetInstanceProcAddr(instance, "" #funcName)); \
        if (funcName == nullptr) {                                                           \
            const std::string name = #funcName;                                              \
            std::cout << "Failed to resolve function " << name << std::endl;                 \
        }                                                                                    \
    }

#define RESOLVE_VK_DEVICE_PFN(device, funcName)                                                 \
    {                                                                                           \
        funcName = reinterpret_cast<PFN_##funcName>(vkGetDeviceProcAddr(device, "" #funcName)); \
        if (funcName == nullptr) {                                                              \
            const std::string name = #funcName;                                                 \
            std::cout << "Failed to resolve function " << name << std::endl;                    \
        }                                                                                       \
    }

static std::vector<char> readFile(const std::string& filename) {
    printf("Reading %s\n", filename.c_str());
    std::ifstream file(filename, std::ios::ate | std::ios::binary);

    if (!file.is_open()) {
        throw std::runtime_error("Could not open file");
    }

    size_t fileSize = (size_t)file.tellg();
    std::vector<char> buffer(fileSize);

    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();

    return buffer;
}

struct AccelerationMemory {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    uint64_t memoryAddress = 0;
    void* mappedPointer = nullptr;
};

typedef struct AccelerationMemory MappedBuffer;

VkDevice device = VK_NULL_HANDLE;
VkInstance instance = VK_NULL_HANDLE;
VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;

VkQueue queue = VK_NULL_HANDLE;
VkCommandPool commandPool = VK_NULL_HANDLE;

VkPipeline pipeline = VK_NULL_HANDLE;
VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;

VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;

VkSurfaceKHR surface = VK_NULL_HANDLE;
VkSwapchainKHR swapchain = VK_NULL_HANDLE;

VkSemaphore semaphoreImageAvailable = VK_NULL_HANDLE;
VkSemaphore semaphoreRenderingAvailable = VK_NULL_HANDLE;

VkImage offscreenBuffer;
VkImageView offscreenBufferView;
VkDeviceMemory offscreenBufferMemory;

MappedBuffer shaderBindingTable = {};
uint32_t shaderBindingTableSize = 0;
uint32_t shaderBindingTableGroupCount = 3;

VkAccelerationStructureKHR bottomLevelAS = VK_NULL_HANDLE;
uint64_t bottomLevelASHandle = 0;
VkAccelerationStructureKHR topLevelAS = VK_NULL_HANDLE;
uint64_t topLevelASHandle = 0;

uint32_t desiredWindowWidth = 640;
uint32_t desiredWindowHeight = 480;
VkFormat desiredSurfaceFormat = VK_FORMAT_B8G8R8A8_UNORM;

HWND window = NULL;
HINSTANCE windowInstance;

std::vector<VkCommandBuffer> commandBuffers;

struct MsgInfo {
    HWND hWnd;
    UINT uMsg;
    WPARAM wParam;
    LPARAM lParam;
};

std::wstring appName = L"VK_KHR_ray_tracing triangle";

// clang-format off
std::vector<const char*> instanceExtensions = {
    VK_KHR_SURFACE_EXTENSION_NAME,
    VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
    VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME
};
std::vector<const char*> validationLayers = {
    "VK_LAYER_KHRONOS_validation"
};
std::vector<const char*> deviceExtensions({
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    VK_KHR_RAY_TRACING_EXTENSION_NAME,
    VK_KHR_PIPELINE_LIBRARY_EXTENSION_NAME,
    VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,
    VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
    VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
    VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME
});
// clang-format on

std::string GetExecutablePath() {
    wchar_t path[MAX_PATH + 1];
    DWORD result = GetModuleFileName(NULL, path, sizeof(path) - 1);
    if (result == 0 || result == sizeof(path) - 1)
        return "";
    path[MAX_PATH - 1] = 0;
    PathRemoveFileSpecW(path);
    std::wstring ws(path);
    std::string out(ws.begin(), ws.end());
    return out;
}

VkShaderModule CreateShaderModule(std::vector<char>& code) {
    VkShaderModule shaderModule = VK_NULL_HANDLE;
    VkShaderModuleCreateInfo shaderModuleInfo = {};
    shaderModuleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shaderModuleInfo.pNext = nullptr;
    shaderModuleInfo.codeSize = code.size();
    shaderModuleInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
    ASSERT_VK_RESULT(vkCreateShaderModule(device, &shaderModuleInfo, nullptr, &shaderModule));
    return shaderModule;
}

uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);
    for (uint32_t ii = 0; ii < memProperties.memoryTypeCount; ++ii) {
        if ((typeFilter & (1 << ii)) &&
            (memProperties.memoryTypes[ii].propertyFlags & properties) == properties) {
            return ii;
        }
    };
    throw std::runtime_error("failed to find suitable memory type!");
}

uint64_t GetBufferAddress(VkBuffer buffer) {
    PFN_vkGetBufferDeviceAddressKHR vkGetBufferDeviceAddressKHR = nullptr;
    RESOLVE_VK_DEVICE_PFN(device, vkGetBufferDeviceAddressKHR);

    VkBufferDeviceAddressInfoKHR bufferAddressInfo = {};
    bufferAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    bufferAddressInfo.pNext = nullptr;
    bufferAddressInfo.buffer = buffer;

    return vkGetBufferDeviceAddressKHR(device, &bufferAddressInfo);
}

MappedBuffer CreateMappedBuffer(void* srcData, uint32_t byteLength) {
    MappedBuffer out = {};

    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.pNext = nullptr;
    bufferInfo.size = byteLength;
    bufferInfo.usage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    bufferInfo.queueFamilyIndexCount = 0;
    bufferInfo.pQueueFamilyIndices = nullptr;
    ASSERT_VK_RESULT(vkCreateBuffer(device, &bufferInfo, nullptr, &out.buffer));

    VkMemoryRequirements memoryRequirements = {};
    vkGetBufferMemoryRequirements(device, out.buffer, &memoryRequirements);

    VkMemoryAllocateFlagsInfo memAllocFlagsInfo = {};
    memAllocFlagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    memAllocFlagsInfo.pNext = nullptr;
    memAllocFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR;
    memAllocFlagsInfo.deviceMask = 0;

    VkMemoryAllocateInfo memAllocInfo = {};
    memAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    memAllocInfo.pNext = &memAllocFlagsInfo;
    memAllocInfo.allocationSize = memoryRequirements.size;
    memAllocInfo.memoryTypeIndex =
        FindMemoryType(memoryRequirements.memoryTypeBits,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    ASSERT_VK_RESULT(vkAllocateMemory(device, &memAllocInfo, nullptr, &out.memory));

    ASSERT_VK_RESULT(vkBindBufferMemory(device, out.buffer, out.memory, 0));

    out.memoryAddress = GetBufferAddress(out.buffer);

    void* dstData;
    ASSERT_VK_RESULT(vkMapMemory(device, out.memory, 0, byteLength, 0, &dstData));
    if (srcData != nullptr) {
        memcpy(dstData, srcData, byteLength);
    }
    vkUnmapMemory(device, out.memory);
    out.mappedPointer = dstData;

    return out;
}

VkMemoryRequirements GetAccelerationStructureMemoryRequirements(
    VkAccelerationStructureKHR acceleration,
    VkAccelerationStructureMemoryRequirementsTypeKHR type) {
    VkMemoryRequirements2 memoryRequirements2 = {};
    memoryRequirements2.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2;

    PFN_vkGetAccelerationStructureMemoryRequirementsKHR
        vkGetAccelerationStructureMemoryRequirementsKHR = nullptr;
    RESOLVE_VK_DEVICE_PFN(device, vkGetAccelerationStructureMemoryRequirementsKHR);

    VkAccelerationStructureMemoryRequirementsInfoKHR accelerationMemoryRequirements = {};
    accelerationMemoryRequirements.sType =
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_INFO_KHR;
    accelerationMemoryRequirements.pNext = nullptr;
    accelerationMemoryRequirements.type = type;
    accelerationMemoryRequirements.buildType = VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR;
    accelerationMemoryRequirements.accelerationStructure = acceleration;
    vkGetAccelerationStructureMemoryRequirementsKHR(device, &accelerationMemoryRequirements,
                                                    &memoryRequirements2);

    return memoryRequirements2.memoryRequirements;
}

void BindAccelerationMemory(VkAccelerationStructureKHR acceleration, VkDeviceMemory memory) {
    PFN_vkBindAccelerationStructureMemoryKHR vkBindAccelerationStructureMemoryKHR = nullptr;
    RESOLVE_VK_DEVICE_PFN(device, vkBindAccelerationStructureMemoryKHR);

    VkBindAccelerationStructureMemoryInfoKHR accelerationMemoryBindInfo = {};
    accelerationMemoryBindInfo.sType =
        VK_STRUCTURE_TYPE_BIND_ACCELERATION_STRUCTURE_MEMORY_INFO_KHR;
    accelerationMemoryBindInfo.pNext = nullptr;
    accelerationMemoryBindInfo.accelerationStructure = acceleration;
    accelerationMemoryBindInfo.memory = memory;
    accelerationMemoryBindInfo.memoryOffset = 0;
    accelerationMemoryBindInfo.deviceIndexCount = 0;
    accelerationMemoryBindInfo.pDeviceIndices = nullptr;

    ASSERT_VK_RESULT(vkBindAccelerationStructureMemoryKHR(device, 1, &accelerationMemoryBindInfo));
}

AccelerationMemory CreateAccelerationScratchBuffer(
    VkAccelerationStructureKHR acceleration,
    VkAccelerationStructureMemoryRequirementsTypeKHR type) {
    AccelerationMemory out = {};

    VkMemoryRequirements asRequirements =
        GetAccelerationStructureMemoryRequirements(acceleration, type);

    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.pNext = nullptr;
    bufferInfo.size = asRequirements.size;
    bufferInfo.usage =
        VK_BUFFER_USAGE_RAY_TRACING_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    bufferInfo.queueFamilyIndexCount = 0;
    bufferInfo.pQueueFamilyIndices = nullptr;
    ASSERT_VK_RESULT(vkCreateBuffer(device, &bufferInfo, nullptr, &out.buffer));

    VkMemoryRequirements bufRequirements = {};
    vkGetBufferMemoryRequirements(device, out.buffer, &bufRequirements);

    // buffer requirements can differ to AS requirements, so we max them
    uint64_t alloctionSize =
        asRequirements.size > bufRequirements.size ? asRequirements.size : bufRequirements.size;
    // combine AS and buf mem types
    uint32_t allocationMemoryBits = bufRequirements.memoryTypeBits | asRequirements.memoryTypeBits;

    VkMemoryAllocateFlagsInfo memAllocFlagsInfo = {};
    memAllocFlagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    memAllocFlagsInfo.pNext = nullptr;
    memAllocFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR;
    memAllocFlagsInfo.deviceMask = 0;

    VkMemoryAllocateInfo memAllocInfo = {};
    memAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    memAllocInfo.pNext = &memAllocFlagsInfo;
    memAllocInfo.allocationSize = alloctionSize;
    memAllocInfo.memoryTypeIndex =
        FindMemoryType(allocationMemoryBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    ASSERT_VK_RESULT(vkAllocateMemory(device, &memAllocInfo, nullptr, &out.memory));

    ASSERT_VK_RESULT(vkBindBufferMemory(device, out.buffer, out.memory, 0));

    out.memoryAddress = GetBufferAddress(out.buffer);

    return out;
}

void InsertCommandImageBarrier(VkCommandBuffer commandBuffer,
                               VkImage image,
                               VkAccessFlags srcAccessMask,
                               VkAccessFlags dstAccessMask,
                               VkImageLayout oldLayout,
                               VkImageLayout newLayout,
                               const VkImageSubresourceRange& subresourceRange) {
    VkImageMemoryBarrier imageMemoryBarrier = {};
    imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    imageMemoryBarrier.pNext = nullptr;
    imageMemoryBarrier.srcAccessMask = srcAccessMask;
    imageMemoryBarrier.dstAccessMask = dstAccessMask;
    imageMemoryBarrier.oldLayout = oldLayout;
    imageMemoryBarrier.newLayout = newLayout;
    imageMemoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imageMemoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imageMemoryBarrier.image = image;
    imageMemoryBarrier.subresourceRange = subresourceRange;

    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1,
                         &imageMemoryBarrier);
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    MsgInfo info{hWnd, uMsg, wParam, lParam};
    switch (info.uMsg) {
        case WM_CLOSE:
            DestroyWindow(info.hWnd);
            PostQuitMessage(0);
            break;
    }
    return (DefWindowProc(hWnd, uMsg, wParam, lParam));
}

bool IsValidationLayerAvailable(const char* layerName) {
    uint32_t propertyCount = 0;
    ASSERT_VK_RESULT(vkEnumerateInstanceLayerProperties(&propertyCount, nullptr));
    std::vector<VkLayerProperties> properties(propertyCount);
    ASSERT_VK_RESULT(vkEnumerateInstanceLayerProperties(&propertyCount, properties.data()));
    // loop through all toggled layers and check if we can enable each
    for (unsigned int ii = 0; ii < properties.size(); ++ii) {
        if (strcmp(layerName, properties[ii].layerName) == 0) {
            return true;
        }
    };
    return false;
}

int main() {
    // clang-format off
    PFN_vkGetPhysicalDeviceSurfaceSupportKHR vkGetPhysicalDeviceSurfaceSupportKHR = nullptr;

    PFN_vkCreateSwapchainKHR vkCreateSwapchainKHR = nullptr;
    PFN_vkGetSwapchainImagesKHR vkGetSwapchainImagesKHR = nullptr;

    PFN_vkCreateAccelerationStructureKHR vkCreateAccelerationStructureKHR = nullptr;
    PFN_vkCreateRayTracingPipelinesKHR vkCreateRayTracingPipelinesKHR = nullptr;
    PFN_vkCmdBuildAccelerationStructureKHR vkCmdBuildAccelerationStructureKHR = nullptr;
    PFN_vkDestroyAccelerationStructureKHR vkDestroyAccelerationStructureKHR = nullptr;
    PFN_vkGetRayTracingShaderGroupHandlesKHR vkGetRayTracingShaderGroupHandlesKHR = nullptr;
    PFN_vkCmdTraceRaysKHR vkCmdTraceRaysKHR = nullptr;

    PFN_vkGetAccelerationStructureDeviceAddressKHR vkGetAccelerationStructureDeviceAddressKHR = nullptr;
    // clang-format on

    TCHAR dest[MAX_PATH];
    const DWORD length = GetModuleFileName(nullptr, dest, MAX_PATH);
    PathCchRemoveFileSpec(dest, MAX_PATH);

    std::wstring basePath = std::wstring(dest);

    windowInstance = GetModuleHandle(0);

    WNDCLASSEX wndClass;
    wndClass.cbSize = sizeof(WNDCLASSEX);
    wndClass.style = CS_HREDRAW | CS_VREDRAW;
    wndClass.lpfnWndProc = WndProc;
    wndClass.cbClsExtra = 0;
    wndClass.cbWndExtra = 0;
    wndClass.hInstance = windowInstance;
    wndClass.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wndClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wndClass.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wndClass.lpszMenuName = nullptr;
    wndClass.lpszClassName = appName.c_str();
    wndClass.hIconSm = LoadIcon(nullptr, IDI_WINLOGO);

    if (!RegisterClassEx(&wndClass)) {
        std::cout << "Failed to create window" << std::endl;
        return EXIT_FAILURE;
    }

    const DWORD exStyle = WS_EX_APPWINDOW | WS_EX_WINDOWEDGE;
    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;

    RECT windowRect;
    windowRect.left = 0;
    windowRect.top = 0;
    windowRect.right = desiredWindowWidth;
    windowRect.bottom = desiredWindowHeight;
    AdjustWindowRectEx(&windowRect, style, FALSE, exStyle);

    window = CreateWindowEx(0, appName.c_str(), appName.c_str(),
                            style | WS_CLIPSIBLINGS | WS_CLIPCHILDREN, 0, 0,
                            windowRect.right - windowRect.left, windowRect.bottom - windowRect.top,
                            nullptr, nullptr, windowInstance, nullptr);

    if (!window) {
        std::cout << "Failed to create window" << std::endl;
        return EXIT_FAILURE;
    }

    const uint32_t x = ((uint32_t)GetSystemMetrics(SM_CXSCREEN) - windowRect.right) / 2;
    const uint32_t y = ((uint32_t)GetSystemMetrics(SM_CYSCREEN) - windowRect.bottom) / 2;
    SetWindowPos(window, 0, x, y, 0, 0, SWP_NOZORDER | SWP_NOSIZE);

    ShowWindow(window, SW_SHOW);
    SetForegroundWindow(window);
    SetFocus(window);

    // check which validation layers are available
    std::vector<const char*> availableValidationLayers = {};
    for (unsigned int ii = 0; ii < validationLayers.size(); ++ii) {
        if (IsValidationLayerAvailable(validationLayers[ii])) {
            availableValidationLayers.push_back(validationLayers[ii]);
        } else {
            std::cout << "Ignoring layer '" << std::string(validationLayers[ii]) << "' as it is unavailable" << std::endl;
        }
    };

    VkApplicationInfo appInfo = {};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pNext = nullptr;
    appInfo.pApplicationName = "Hello Triangle";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "No Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_2;

    VkInstanceCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pNext = nullptr;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = (uint32_t)instanceExtensions.size();
    createInfo.ppEnabledExtensionNames = instanceExtensions.data();
    createInfo.enabledLayerCount = availableValidationLayers.size();
    createInfo.ppEnabledLayerNames = availableValidationLayers.data();

    ASSERT_VK_RESULT(vkCreateInstance(&createInfo, nullptr, &instance));

    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
    if (deviceCount <= 0) {
        std::cout << "No physical devices available" << std::endl;
        return EXIT_FAILURE;
    }
    std::vector<VkPhysicalDevice> devices(deviceCount);
    ASSERT_VK_RESULT(vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data()));

    // find RT compatible device
    for (unsigned int ii = 0; ii < devices.size(); ++ii) {
        // acquire RT features
        VkPhysicalDeviceRayTracingFeaturesKHR rayTracingFeatures = {};
        rayTracingFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_FEATURES_KHR;
        rayTracingFeatures.pNext = nullptr;

        VkPhysicalDeviceFeatures2 deviceFeatures2 = {};
        deviceFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        deviceFeatures2.pNext = &rayTracingFeatures;
        vkGetPhysicalDeviceFeatures2(devices[ii], &deviceFeatures2);

        if (rayTracingFeatures.rayTracing == VK_TRUE) {
            physicalDevice = devices[ii];
            break;
        }
    };

    if (physicalDevice == VK_NULL_HANDLE) {
        std::cout << "'No ray tracing compatible GPU found" << std::endl;
    }

    VkPhysicalDeviceProperties deviceProperties = {};
    vkGetPhysicalDeviceProperties(physicalDevice, &deviceProperties);
    std::cout << "GPU: " << deviceProperties.deviceName << std::endl;

    const float queuePriority = 0.0f;

    VkDeviceQueueCreateInfo deviceQueueInfo = {};
    deviceQueueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    deviceQueueInfo.pNext = nullptr;
    deviceQueueInfo.queueFamilyIndex = 0;
    deviceQueueInfo.queueCount = 1;
    deviceQueueInfo.pQueuePriorities = &queuePriority;

    VkPhysicalDeviceRayTracingFeaturesKHR deviceRayTracingFeatures = {};
    deviceRayTracingFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_FEATURES_KHR;
    deviceRayTracingFeatures.pNext = nullptr;
    deviceRayTracingFeatures.rayTracing = VK_TRUE;

    VkPhysicalDeviceVulkan12Features deviceVulkan12Features = {};
    deviceVulkan12Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    deviceVulkan12Features.pNext = &deviceRayTracingFeatures;
    deviceVulkan12Features.bufferDeviceAddress = VK_TRUE;

    VkDeviceCreateInfo deviceInfo = {};
    deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceInfo.pNext = &deviceVulkan12Features;
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &deviceQueueInfo;
    deviceInfo.enabledExtensionCount = deviceExtensions.size();
    deviceInfo.ppEnabledExtensionNames = deviceExtensions.data();
    deviceInfo.pEnabledFeatures = nullptr;

    ASSERT_VK_RESULT(vkCreateDevice(physicalDevice, &deviceInfo, nullptr, &device));

    vkGetDeviceQueue(device, 0, 0, &queue);

    // clang-format off
    RESOLVE_VK_INSTANCE_PFN(instance, vkGetPhysicalDeviceSurfaceSupportKHR);

    RESOLVE_VK_DEVICE_PFN(device, vkCreateSwapchainKHR);
    RESOLVE_VK_DEVICE_PFN(device, vkGetSwapchainImagesKHR);

    RESOLVE_VK_DEVICE_PFN(device, vkCreateAccelerationStructureKHR);
    RESOLVE_VK_DEVICE_PFN(device, vkCreateRayTracingPipelinesKHR);
    RESOLVE_VK_DEVICE_PFN(device, vkCmdBuildAccelerationStructureKHR);
    RESOLVE_VK_DEVICE_PFN(device, vkDestroyAccelerationStructureKHR);
    RESOLVE_VK_DEVICE_PFN(device, vkGetRayTracingShaderGroupHandlesKHR);
    RESOLVE_VK_DEVICE_PFN(device, vkCmdTraceRaysKHR);
    RESOLVE_VK_DEVICE_PFN(device, vkGetAccelerationStructureDeviceAddressKHR);
    // clang-format on

    VkWin32SurfaceCreateInfoKHR surfaceCreateInfo;
    surfaceCreateInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    surfaceCreateInfo.pNext = nullptr;
    surfaceCreateInfo.flags = 0;
    surfaceCreateInfo.hinstance = windowInstance;
    surfaceCreateInfo.hwnd = window;

    ASSERT_VK_RESULT(vkCreateWin32SurfaceKHR(instance, &surfaceCreateInfo, nullptr, &surface));

    VkBool32 surfaceSupport = false;
    vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, 0, surface, &surfaceSupport);
    if (!surfaceSupport) {
        std::cout << "No surface rendering support" << std::endl;
        return EXIT_FAILURE;
    }

    VkCommandPoolCreateInfo cmdPoolInfo = {};
    cmdPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cmdPoolInfo.pNext = nullptr;
    cmdPoolInfo.flags = 0;
    cmdPoolInfo.queueFamilyIndex = 0;

    ASSERT_VK_RESULT(vkCreateCommandPool(device, &cmdPoolInfo, nullptr, &commandPool));

    // acquire RT properties
    VkPhysicalDeviceRayTracingPropertiesKHR rayTracingProperties = {};
    rayTracingProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PROPERTIES_KHR;
    rayTracingProperties.pNext = nullptr;

    VkPhysicalDeviceProperties2 deviceProperties2 = {};
    deviceProperties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    deviceProperties2.pNext = &rayTracingProperties;
    vkGetPhysicalDeviceProperties2(physicalDevice, &deviceProperties2);

    // create bottom-level container
    {
        std::cout << "Creating Bottom-Level Acceleration Structure.." << std::endl;

        VkAccelerationStructureCreateGeometryTypeInfoKHR accelerationCreateGeometryInfo = {};
        accelerationCreateGeometryInfo.sType =
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_GEOMETRY_TYPE_INFO_KHR;
        accelerationCreateGeometryInfo.pNext = nullptr;
        accelerationCreateGeometryInfo.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
        accelerationCreateGeometryInfo.maxPrimitiveCount = 128;
        accelerationCreateGeometryInfo.indexType = VK_INDEX_TYPE_UINT32;
        accelerationCreateGeometryInfo.maxVertexCount = 8;
        accelerationCreateGeometryInfo.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
        accelerationCreateGeometryInfo.allowsTransforms = VK_FALSE;

        VkAccelerationStructureCreateInfoKHR accelerationInfo = {};
        accelerationInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
        accelerationInfo.pNext = nullptr;
        accelerationInfo.compactedSize = 0;
        accelerationInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        accelerationInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
        accelerationInfo.maxGeometryCount = 1;
        accelerationInfo.pGeometryInfos = &accelerationCreateGeometryInfo;
        accelerationInfo.deviceAddress = VK_NULL_HANDLE;

        ASSERT_VK_RESULT(
            vkCreateAccelerationStructureKHR(device, &accelerationInfo, nullptr, &bottomLevelAS));

        AccelerationMemory objectMemory = CreateAccelerationScratchBuffer(
            bottomLevelAS, VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_OBJECT_KHR);

        BindAccelerationMemory(bottomLevelAS, objectMemory.memory);

        AccelerationMemory buildScratchMemory = CreateAccelerationScratchBuffer(
            bottomLevelAS, VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_BUILD_SCRATCH_KHR);

        // Get bottom level acceleration structure handle for use in top level instances
        VkAccelerationStructureDeviceAddressInfoKHR devAddrInfo{};
        devAddrInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
        devAddrInfo.accelerationStructure = bottomLevelAS;
        bottomLevelASHandle = vkGetAccelerationStructureDeviceAddressKHR(device, &devAddrInfo);

        // clang-format off
        std::vector<float> vertices = {
            +1.0f, +1.0f, +0.0f,
            -1.0f, +1.0f, +0.0f,
            +0.0f, -1.0f, +0.0f
        };
        std::vector<uint32_t> indices = {
            0, 1, 2
        };
        // clang-format on

        MappedBuffer vertexBuffer =
            CreateMappedBuffer(vertices.data(), sizeof(float) * vertices.size());

        MappedBuffer indexBuffer =
            CreateMappedBuffer(indices.data(), sizeof(uint32_t) * indices.size());

        VkAccelerationStructureGeometryKHR accelerationGeometry = {};
        accelerationGeometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
        accelerationGeometry.pNext = nullptr;
        accelerationGeometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
        accelerationGeometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
        accelerationGeometry.geometry = {};
        accelerationGeometry.geometry.triangles = {};
        accelerationGeometry.geometry.triangles.sType =
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
        accelerationGeometry.geometry.triangles.pNext = nullptr;
        accelerationGeometry.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
        accelerationGeometry.geometry.triangles.vertexData.deviceAddress =
            vertexBuffer.memoryAddress;
        accelerationGeometry.geometry.triangles.vertexStride = 3 * sizeof(float);
        accelerationGeometry.geometry.triangles.indexType = VK_INDEX_TYPE_UINT32;
        accelerationGeometry.geometry.triangles.indexData.deviceAddress = indexBuffer.memoryAddress;
        accelerationGeometry.geometry.triangles.transformData.deviceAddress = 0;

        std::vector<VkAccelerationStructureGeometryKHR> accelerationGeometries(
            {accelerationGeometry});
        const VkAccelerationStructureGeometryKHR* ppGeometries = accelerationGeometries.data();

        VkAccelerationStructureBuildGeometryInfoKHR accelerationBuildGeometryInfo = {};
        accelerationBuildGeometryInfo.sType =
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
        accelerationBuildGeometryInfo.pNext = nullptr;
        accelerationBuildGeometryInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        accelerationBuildGeometryInfo.flags =
            VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
        accelerationBuildGeometryInfo.update = VK_FALSE;
        accelerationBuildGeometryInfo.srcAccelerationStructure = VK_NULL_HANDLE;
        accelerationBuildGeometryInfo.dstAccelerationStructure = bottomLevelAS;
        accelerationBuildGeometryInfo.geometryArrayOfPointers = VK_FALSE;
        accelerationBuildGeometryInfo.geometryCount = 1;
        accelerationBuildGeometryInfo.ppGeometries = &ppGeometries;
        accelerationBuildGeometryInfo.scratchData.deviceAddress = buildScratchMemory.memoryAddress;

        VkAccelerationStructureBuildOffsetInfoKHR accelerationBuildOffsetInfo = {};
        accelerationBuildOffsetInfo.primitiveCount = 1;
        accelerationBuildOffsetInfo.primitiveOffset = 0x0;
        accelerationBuildOffsetInfo.firstVertex = 0;
        accelerationBuildOffsetInfo.transformOffset = 0x0;

        std::vector<VkAccelerationStructureBuildOffsetInfoKHR*> accelerationBuildOffsets = {
            &accelerationBuildOffsetInfo};

        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;

        VkCommandBufferAllocateInfo commandBufferAllocateInfo = {};
        commandBufferAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        commandBufferAllocateInfo.pNext = nullptr;
        commandBufferAllocateInfo.commandPool = commandPool;
        commandBufferAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        commandBufferAllocateInfo.commandBufferCount = 1;
        ASSERT_VK_RESULT(
            vkAllocateCommandBuffers(device, &commandBufferAllocateInfo, &commandBuffer));

        VkCommandBufferBeginInfo commandBufferBeginInfo = {};
        commandBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        commandBufferBeginInfo.pNext = nullptr;
        commandBufferBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(commandBuffer, &commandBufferBeginInfo);

        vkCmdBuildAccelerationStructureKHR(commandBuffer, 1, &accelerationBuildGeometryInfo,
                                           accelerationBuildOffsets.data());

        ASSERT_VK_RESULT(vkEndCommandBuffer(commandBuffer));

        VkSubmitInfo submitInfo = {};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.pNext = nullptr;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;

        VkFence fence = VK_NULL_HANDLE;
        VkFenceCreateInfo fenceInfo = {};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.pNext = nullptr;

        ASSERT_VK_RESULT(vkCreateFence(device, &fenceInfo, nullptr, &fence));
        ASSERT_VK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, fence));
        ASSERT_VK_RESULT(vkWaitForFences(device, 1, &fence, true, UINT64_MAX));

        vkDestroyFence(device, fence, nullptr);
        vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);

        // make sure bottom AS handle is valid
        if (bottomLevelASHandle == 0) {
            std::cout << "Invalid Handle to BLAS" << std::endl;
            return EXIT_FAILURE;
        }
    }

    // create top-level container
    {
        std::cout << "Creating Top-Level Acceleration Structure.." << std::endl;

        VkAccelerationStructureCreateGeometryTypeInfoKHR accelerationCreateGeometryInfo = {};
        accelerationCreateGeometryInfo.sType =
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_GEOMETRY_TYPE_INFO_KHR;
        accelerationCreateGeometryInfo.pNext = nullptr;
        accelerationCreateGeometryInfo.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
        accelerationCreateGeometryInfo.maxPrimitiveCount = 1;

        VkAccelerationStructureCreateInfoKHR accelerationInfo = {};
        accelerationInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
        accelerationInfo.pNext = nullptr;
        accelerationInfo.compactedSize = 0;
        accelerationInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        accelerationInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
        accelerationInfo.maxGeometryCount = 1;
        accelerationInfo.pGeometryInfos = &accelerationCreateGeometryInfo;
        accelerationInfo.deviceAddress = VK_NULL_HANDLE;

        ASSERT_VK_RESULT(
            vkCreateAccelerationStructureKHR(device, &accelerationInfo, nullptr, &topLevelAS));

        AccelerationMemory objectMemory = CreateAccelerationScratchBuffer(
            topLevelAS, VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_OBJECT_KHR);

        BindAccelerationMemory(topLevelAS, objectMemory.memory);

        AccelerationMemory buildScratchMemory = CreateAccelerationScratchBuffer(
            topLevelAS, VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_BUILD_SCRATCH_KHR);

        std::vector<VkAccelerationStructureInstanceKHR> instances(
            {{{1.0f, 0.0f, 0.0, 0.0f, 0.0f, 1.0f, 0.0, 0.0f, 0.0f, 0.0f, 1.0, 0.0f},
              0,
              0xFF,
              0x0,
              VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR,
              bottomLevelASHandle}});

        MappedBuffer instanceBuffer = CreateMappedBuffer(
            instances.data(), sizeof(VkAccelerationStructureInstanceKHR) * instances.size());

        VkAccelerationStructureGeometryKHR accelerationGeometry = {};
        accelerationGeometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
        accelerationGeometry.pNext = nullptr;
        accelerationGeometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
        accelerationGeometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
        accelerationGeometry.geometry = {};
        accelerationGeometry.geometry.instances = {};
        accelerationGeometry.geometry.instances.sType =
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
        accelerationGeometry.geometry.instances.pNext = nullptr;
        accelerationGeometry.geometry.instances.arrayOfPointers = VK_FALSE;
        accelerationGeometry.geometry.instances.data.deviceAddress = instanceBuffer.memoryAddress;

        std::vector<VkAccelerationStructureGeometryKHR> accelerationGeometries(
            {accelerationGeometry});
        const VkAccelerationStructureGeometryKHR* ppGeometries = accelerationGeometries.data();

        VkAccelerationStructureBuildGeometryInfoKHR accelerationBuildGeometryInfo = {};
        accelerationBuildGeometryInfo.sType =
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
        accelerationBuildGeometryInfo.pNext = nullptr;
        accelerationBuildGeometryInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        accelerationBuildGeometryInfo.flags =
            VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
        accelerationBuildGeometryInfo.update = VK_FALSE;
        accelerationBuildGeometryInfo.srcAccelerationStructure = VK_NULL_HANDLE;
        accelerationBuildGeometryInfo.dstAccelerationStructure = topLevelAS;
        accelerationBuildGeometryInfo.geometryArrayOfPointers = VK_FALSE;
        accelerationBuildGeometryInfo.geometryCount = 1;
        accelerationBuildGeometryInfo.ppGeometries = &ppGeometries;
        accelerationBuildGeometryInfo.scratchData.deviceAddress = buildScratchMemory.memoryAddress;

        VkAccelerationStructureBuildOffsetInfoKHR accelerationBuildOffsetInfo = {};
        accelerationBuildOffsetInfo.primitiveCount = 1;
        accelerationBuildOffsetInfo.primitiveOffset = 0x0;
        accelerationBuildOffsetInfo.firstVertex = 0;
        accelerationBuildOffsetInfo.transformOffset = 0x0;

        std::vector<VkAccelerationStructureBuildOffsetInfoKHR*> accelerationBuildOffsets = {
            &accelerationBuildOffsetInfo};

        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;

        VkCommandBufferAllocateInfo commandBufferAllocateInfo = {};
        commandBufferAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        commandBufferAllocateInfo.pNext = nullptr;
        commandBufferAllocateInfo.commandPool = commandPool;
        commandBufferAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        commandBufferAllocateInfo.commandBufferCount = 1;
        ASSERT_VK_RESULT(
            vkAllocateCommandBuffers(device, &commandBufferAllocateInfo, &commandBuffer));

        VkCommandBufferBeginInfo commandBufferBeginInfo = {};
        commandBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        commandBufferBeginInfo.pNext = nullptr;
        commandBufferBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(commandBuffer, &commandBufferBeginInfo);

        vkCmdBuildAccelerationStructureKHR(commandBuffer, 1, &accelerationBuildGeometryInfo,
                                           accelerationBuildOffsets.data());

        ASSERT_VK_RESULT(vkEndCommandBuffer(commandBuffer));

        VkSubmitInfo submitInfo = {};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.pNext = nullptr;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;

        VkFence fence = VK_NULL_HANDLE;
        VkFenceCreateInfo fenceInfo = {};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.pNext = nullptr;

        ASSERT_VK_RESULT(vkCreateFence(device, &fenceInfo, nullptr, &fence));
        ASSERT_VK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, fence));
        ASSERT_VK_RESULT(vkWaitForFences(device, 1, &fence, true, UINT64_MAX));

        vkDestroyFence(device, fence, nullptr);
        vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
    }

    // offscreen buffer
    {
        std::cout << "Creating Offsceen Buffer.." << std::endl;

        VkImageCreateInfo imageInfo = {};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.pNext = nullptr;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = desiredSurfaceFormat;
        imageInfo.extent = {desiredWindowWidth, desiredWindowHeight, 1};
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.queueFamilyIndexCount = 0;
        imageInfo.pQueueFamilyIndices = nullptr;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        ASSERT_VK_RESULT(vkCreateImage(device, &imageInfo, nullptr, &offscreenBuffer));

        VkMemoryRequirements memoryRequirements = {};
        vkGetImageMemoryRequirements(device, offscreenBuffer, &memoryRequirements);

        VkMemoryAllocateInfo memoryAllocateInfo = {};
        memoryAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        memoryAllocateInfo.pNext = nullptr;
        memoryAllocateInfo.allocationSize = memoryRequirements.size;
        memoryAllocateInfo.memoryTypeIndex =
            FindMemoryType(memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        ASSERT_VK_RESULT(
            vkAllocateMemory(device, &memoryAllocateInfo, nullptr, &offscreenBufferMemory));

        ASSERT_VK_RESULT(vkBindImageMemory(device, offscreenBuffer, offscreenBufferMemory, 0));

        VkImageViewCreateInfo imageViewInfo = {};
        imageViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        imageViewInfo.pNext = nullptr;
        imageViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        imageViewInfo.format = desiredSurfaceFormat;
        imageViewInfo.subresourceRange = {};
        imageViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        imageViewInfo.subresourceRange.baseMipLevel = 0;
        imageViewInfo.subresourceRange.levelCount = 1;
        imageViewInfo.subresourceRange.baseArrayLayer = 0;
        imageViewInfo.subresourceRange.layerCount = 1;
        imageViewInfo.image = offscreenBuffer;
        imageViewInfo.flags = 0;
        imageViewInfo.components.r = VK_COMPONENT_SWIZZLE_R;
        imageViewInfo.components.g = VK_COMPONENT_SWIZZLE_G;
        imageViewInfo.components.b = VK_COMPONENT_SWIZZLE_B;
        imageViewInfo.components.a = VK_COMPONENT_SWIZZLE_A;

        ASSERT_VK_RESULT(vkCreateImageView(device, &imageViewInfo, nullptr, &offscreenBufferView));
    }

    // rt descriptor set layout
    {
        std::cout << "Creating RT Descriptor Set Layout.." << std::endl;

        VkDescriptorSetLayoutBinding accelerationStructureLayoutBinding = {};
        accelerationStructureLayoutBinding.binding = 0;
        accelerationStructureLayoutBinding.descriptorType =
            VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
        accelerationStructureLayoutBinding.descriptorCount = 1;
        accelerationStructureLayoutBinding.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
        accelerationStructureLayoutBinding.pImmutableSamplers = nullptr;

        VkDescriptorSetLayoutBinding storageImageLayoutBinding = {};
        storageImageLayoutBinding.binding = 1;
        storageImageLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        storageImageLayoutBinding.descriptorCount = 1;
        storageImageLayoutBinding.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

        std::vector<VkDescriptorSetLayoutBinding> bindings(
            {accelerationStructureLayoutBinding, storageImageLayoutBinding});

        VkDescriptorSetLayoutCreateInfo layoutInfo = {};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.pNext = nullptr;
        layoutInfo.flags = 0;
        layoutInfo.bindingCount = bindings.size();
        layoutInfo.pBindings = bindings.data();

        ASSERT_VK_RESULT(
            vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout));
    }

    // rt descriptor set
    {
        std::cout << "Creating RT Descriptor Set.." << std::endl;

        std::vector<VkDescriptorPoolSize> poolSizes(
            {{VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1},
             {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1}});

        VkDescriptorPoolCreateInfo descriptorPoolInfo = {};
        descriptorPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        descriptorPoolInfo.pNext = nullptr;
        descriptorPoolInfo.flags = 0;
        descriptorPoolInfo.maxSets = 1;
        descriptorPoolInfo.poolSizeCount = poolSizes.size();
        descriptorPoolInfo.pPoolSizes = poolSizes.data();

        ASSERT_VK_RESULT(
            vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));

        VkDescriptorSetAllocateInfo descriptorSetAllocateInfo = {};
        descriptorSetAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        descriptorSetAllocateInfo.pNext = nullptr;
        descriptorSetAllocateInfo.descriptorPool = descriptorPool;
        descriptorSetAllocateInfo.descriptorSetCount = 1;
        descriptorSetAllocateInfo.pSetLayouts = &descriptorSetLayout;

        ASSERT_VK_RESULT(
            vkAllocateDescriptorSets(device, &descriptorSetAllocateInfo, &descriptorSet));

        VkWriteDescriptorSetAccelerationStructureKHR descriptorAccelerationStructureInfo;
        descriptorAccelerationStructureInfo.sType =
            VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
        descriptorAccelerationStructureInfo.pNext = nullptr;
        descriptorAccelerationStructureInfo.accelerationStructureCount = 1;
        descriptorAccelerationStructureInfo.pAccelerationStructures = &topLevelAS;

        VkWriteDescriptorSet accelerationStructureWrite = {};
        accelerationStructureWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        accelerationStructureWrite.pNext = &descriptorAccelerationStructureInfo;
        accelerationStructureWrite.dstSet = descriptorSet;
        accelerationStructureWrite.dstBinding = 0;
        accelerationStructureWrite.descriptorCount = 1;
        accelerationStructureWrite.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;

        VkDescriptorImageInfo storageImageInfo = {};
        storageImageInfo.sampler = VK_NULL_HANDLE;
        storageImageInfo.imageView = offscreenBufferView;
        storageImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        VkWriteDescriptorSet outputImageWrite = {};
        outputImageWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        outputImageWrite.pNext = nullptr;
        outputImageWrite.dstSet = descriptorSet;
        outputImageWrite.dstBinding = 1;
        outputImageWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        outputImageWrite.descriptorCount = 1;
        outputImageWrite.pImageInfo = &storageImageInfo;

        std::vector<VkWriteDescriptorSet> descriptorWrites(
            {accelerationStructureWrite, outputImageWrite});

        vkUpdateDescriptorSets(device, descriptorWrites.size(), descriptorWrites.data(), 0,
                               nullptr);
    }

    // rt pipeline layout
    {
        std::cout << "Creating RT Pipeline Layout.." << std::endl;

        VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.pNext = nullptr;
        pipelineLayoutInfo.flags = 0;
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
        pipelineLayoutInfo.pushConstantRangeCount = 0;
        pipelineLayoutInfo.pPushConstantRanges = nullptr;

        ASSERT_VK_RESULT(
            vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout));
    }

    // rt pipeline
    {
        std::cout << "Creating RT Pipeline.." << std::endl;

        std::string basePath = GetExecutablePath() + "/../../shaders";

        std::vector<char> rgenShaderSrc = readFile(basePath + "/ray-generation.spv");
        std::vector<char> rchitShaderSrc = readFile(basePath + "/ray-closest-hit.spv");
        std::vector<char> rmissShaderSrc = readFile(basePath + "/ray-miss.spv");

        VkPipelineShaderStageCreateInfo rayGenShaderStageInfo = {};
        rayGenShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        rayGenShaderStageInfo.pNext = nullptr;
        rayGenShaderStageInfo.stage = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
        rayGenShaderStageInfo.module = CreateShaderModule(rgenShaderSrc);
        rayGenShaderStageInfo.pName = "main";

        VkPipelineShaderStageCreateInfo rayChitShaderStageInfo = {};
        rayChitShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        rayChitShaderStageInfo.pNext = nullptr;
        rayChitShaderStageInfo.stage = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
        rayChitShaderStageInfo.module = CreateShaderModule(rchitShaderSrc);
        rayChitShaderStageInfo.pName = "main";

        VkPipelineShaderStageCreateInfo rayMissShaderStageInfo = {};
        rayMissShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        rayMissShaderStageInfo.pNext = nullptr;
        rayMissShaderStageInfo.stage = VK_SHADER_STAGE_MISS_BIT_KHR;
        rayMissShaderStageInfo.module = CreateShaderModule(rmissShaderSrc);
        rayMissShaderStageInfo.pName = "main";

        std::vector<VkPipelineShaderStageCreateInfo> shaderStages = {
            rayGenShaderStageInfo, rayChitShaderStageInfo, rayMissShaderStageInfo};

        VkRayTracingShaderGroupCreateInfoKHR rayGenGroup = {};
        rayGenGroup.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
        rayGenGroup.pNext = nullptr;
        rayGenGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
        rayGenGroup.generalShader = 0;
        rayGenGroup.closestHitShader = VK_SHADER_UNUSED_KHR;
        rayGenGroup.anyHitShader = VK_SHADER_UNUSED_KHR;
        rayGenGroup.intersectionShader = VK_SHADER_UNUSED_KHR;

        VkRayTracingShaderGroupCreateInfoKHR rayHitGroup = {};
        rayHitGroup.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
        rayHitGroup.pNext = nullptr;
        rayHitGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
        rayHitGroup.generalShader = VK_SHADER_UNUSED_KHR;
        rayHitGroup.closestHitShader = 1;
        rayHitGroup.anyHitShader = VK_SHADER_UNUSED_KHR;
        rayHitGroup.intersectionShader = VK_SHADER_UNUSED_KHR;

        VkRayTracingShaderGroupCreateInfoKHR rayMissGroup = {};
        rayMissGroup.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
        rayMissGroup.pNext = nullptr;
        rayMissGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
        rayMissGroup.generalShader = 2;
        rayMissGroup.closestHitShader = VK_SHADER_UNUSED_KHR;
        rayMissGroup.anyHitShader = VK_SHADER_UNUSED_KHR;
        rayMissGroup.intersectionShader = VK_SHADER_UNUSED_KHR;

        std::vector<VkRayTracingShaderGroupCreateInfoKHR> shaderGroups = {rayGenGroup, rayHitGroup,
                                                                          rayMissGroup};

        VkRayTracingPipelineCreateInfoKHR pipelineInfo = {};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;
        pipelineInfo.pNext = nullptr;
        pipelineInfo.flags = 0;
        pipelineInfo.stageCount = shaderStages.size();
        pipelineInfo.pStages = shaderStages.data();
        pipelineInfo.groupCount = shaderGroups.size();
        pipelineInfo.pGroups = shaderGroups.data();
        pipelineInfo.maxRecursionDepth = 1;
        pipelineInfo.libraries = {};
        pipelineInfo.libraries.sType = VK_STRUCTURE_TYPE_PIPELINE_LIBRARY_CREATE_INFO_KHR;
        pipelineInfo.libraries.pNext = nullptr;
        pipelineInfo.libraries.libraryCount = 0;
        pipelineInfo.libraries.pLibraries = nullptr;
        pipelineInfo.pLibraryInterface = nullptr;
        pipelineInfo.layout = pipelineLayout;
        pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
        pipelineInfo.basePipelineIndex = 0;

        ASSERT_VK_RESULT(
            vkCreateRayTracingPipelinesKHR(device, nullptr, 1, &pipelineInfo, nullptr, &pipeline));
    }

    // shader binding table
    {
        std::cout << "Creating Shader Binding Table.." << std::endl;

        shaderBindingTable = {};
        shaderBindingTableSize =
            shaderBindingTableGroupCount * rayTracingProperties.shaderGroupHandleSize;

        VkBufferCreateInfo bufferInfo = {};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.pNext = nullptr;
        bufferInfo.size = shaderBindingTableSize;
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        bufferInfo.queueFamilyIndexCount = 0;
        bufferInfo.pQueueFamilyIndices = nullptr;
        ASSERT_VK_RESULT(vkCreateBuffer(device, &bufferInfo, nullptr, &shaderBindingTable.buffer));

        VkMemoryRequirements memoryRequirements = {};
        vkGetBufferMemoryRequirements(device, shaderBindingTable.buffer, &memoryRequirements);

        VkMemoryAllocateInfo memAllocInfo = {};
        memAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        memAllocInfo.pNext = nullptr;
        memAllocInfo.allocationSize = memoryRequirements.size;
        memAllocInfo.memoryTypeIndex =
            FindMemoryType(memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

        ASSERT_VK_RESULT(
            vkAllocateMemory(device, &memAllocInfo, nullptr, &shaderBindingTable.memory));

        ASSERT_VK_RESULT(
            vkBindBufferMemory(device, shaderBindingTable.buffer, shaderBindingTable.memory, 0));

        void* dstData;
        ASSERT_VK_RESULT(
            vkMapMemory(device, shaderBindingTable.memory, 0, shaderBindingTableSize, 0, &dstData));

        vkGetRayTracingShaderGroupHandlesKHR(device, pipeline, 0, shaderBindingTableGroupCount,
                                             shaderBindingTableSize, dstData);
        vkUnmapMemory(device, shaderBindingTable.memory);
    }

    std::cout << "Initializing Swapchain.." << std::endl;

    uint32_t presentModeCount = 0;
    ASSERT_VK_RESULT(vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface,
                                                               &presentModeCount, nullptr));

    std::vector<VkPresentModeKHR> presentModes(presentModeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount,
                                              presentModes.data());

    bool isMailboxModeSupported = std::find(presentModes.begin(), presentModes.end(),
                                            VK_PRESENT_MODE_MAILBOX_KHR) != presentModes.end();

    VkSwapchainCreateInfoKHR swapchainInfo = {};
    swapchainInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchainInfo.pNext = nullptr;
    swapchainInfo.surface = surface;
    swapchainInfo.minImageCount = 3;
    swapchainInfo.imageFormat = desiredSurfaceFormat;
    swapchainInfo.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    swapchainInfo.imageExtent.width = desiredWindowWidth;
    swapchainInfo.imageExtent.height = desiredWindowHeight;
    swapchainInfo.imageArrayLayers = 1;
    swapchainInfo.imageUsage =
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    swapchainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapchainInfo.queueFamilyIndexCount = 0;
    swapchainInfo.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    swapchainInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapchainInfo.presentMode =
        isMailboxModeSupported ? VK_PRESENT_MODE_MAILBOX_KHR : VK_PRESENT_MODE_FIFO_KHR;
    swapchainInfo.clipped = VK_TRUE;
    swapchainInfo.oldSwapchain = nullptr;

    ASSERT_VK_RESULT(vkCreateSwapchainKHR(device, &swapchainInfo, nullptr, &swapchain));

    uint32_t amountOfImagesInSwapchain = 0;
    vkGetSwapchainImagesKHR(device, swapchain, &amountOfImagesInSwapchain, nullptr);
    std::vector<VkImage> swapchainImages(amountOfImagesInSwapchain);

    ASSERT_VK_RESULT(vkGetSwapchainImagesKHR(device, swapchain, &amountOfImagesInSwapchain,
                                             swapchainImages.data()));

    std::vector<VkImageView> imageViews(amountOfImagesInSwapchain);

    for (uint32_t ii = 0; ii < amountOfImagesInSwapchain; ++ii) {
        VkImageViewCreateInfo imageViewInfo = {};
        imageViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        imageViewInfo.pNext = nullptr;
        imageViewInfo.image = swapchainImages[ii];
        imageViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        imageViewInfo.format = desiredSurfaceFormat;
        imageViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        imageViewInfo.subresourceRange.baseMipLevel = 0;
        imageViewInfo.subresourceRange.levelCount = 1;
        imageViewInfo.subresourceRange.baseArrayLayer = 0;
        imageViewInfo.subresourceRange.layerCount = 1;

        ASSERT_VK_RESULT(vkCreateImageView(device, &imageViewInfo, nullptr, &imageViews[ii]));
    };

    std::cout << "Recording frame commands.." << std::endl;

    VkImageCopy copyRegion = {};
    copyRegion.srcSubresource = {};
    copyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copyRegion.srcSubresource.mipLevel = 0;
    copyRegion.srcSubresource.baseArrayLayer = 0;
    copyRegion.srcSubresource.layerCount = 1;
    copyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copyRegion.dstSubresource.mipLevel = 0;
    copyRegion.dstSubresource.baseArrayLayer = 0;
    copyRegion.dstSubresource.layerCount = 1;
    copyRegion.extent = {};
    copyRegion.extent.depth = 1;
    copyRegion.extent.width = desiredWindowWidth;
    copyRegion.extent.height = desiredWindowHeight;

    VkImageSubresourceRange subresourceRange = {};
    subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    subresourceRange.baseMipLevel = 0;
    subresourceRange.levelCount = 1;
    subresourceRange.baseArrayLayer = 0;
    subresourceRange.layerCount = 1;

    // clang-format off
    VkStridedBufferRegionKHR rayGenSBT = {
        shaderBindingTable.buffer, 0, 0, shaderBindingTableSize
    };
    VkStridedBufferRegionKHR rayMissSBT = {
        shaderBindingTable.buffer, 2 * rayTracingProperties.shaderGroupHandleSize, 0, shaderBindingTableSize
    };
    VkStridedBufferRegionKHR rayHitSBT = {
        shaderBindingTable.buffer, 1 * rayTracingProperties.shaderGroupHandleSize, 0, shaderBindingTableSize
    };
    VkStridedBufferRegionKHR rayCallSBT = {
        VK_NULL_HANDLE, 0, 0, 0
    };
    // clang-format on

    VkCommandBufferAllocateInfo commandBufferAllocateInfo = {};
    commandBufferAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    commandBufferAllocateInfo.pNext = nullptr;
    commandBufferAllocateInfo.commandPool = commandPool;
    commandBufferAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandBufferAllocateInfo.commandBufferCount = amountOfImagesInSwapchain;

    commandBuffers = std::vector<VkCommandBuffer>(amountOfImagesInSwapchain);

    ASSERT_VK_RESULT(
        vkAllocateCommandBuffers(device, &commandBufferAllocateInfo, commandBuffers.data()));

    VkCommandBufferBeginInfo commandBufferBeginInfo = {};
    commandBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    commandBufferBeginInfo.pNext = nullptr;
    commandBufferBeginInfo.flags = 0;
    commandBufferBeginInfo.pInheritanceInfo = nullptr;

    for (uint32_t ii = 0; ii < amountOfImagesInSwapchain; ++ii) {
        VkCommandBuffer commandBuffer = commandBuffers[ii];
        VkImage swapchainImage = swapchainImages[ii];

        ASSERT_VK_RESULT(vkBeginCommandBuffer(commandBuffer, &commandBufferBeginInfo));

        // transition offscreen buffer into shader writeable state
        InsertCommandImageBarrier(commandBuffer, offscreenBuffer, 0, VK_ACCESS_SHADER_WRITE_BIT,
                                  VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                                  subresourceRange);

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipeline);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
                                pipelineLayout, 0, 1, &descriptorSet, 0, 0);
        vkCmdTraceRaysKHR(commandBuffer, &rayGenSBT, &rayMissSBT, &rayHitSBT, &rayCallSBT,
                          desiredWindowWidth, desiredWindowHeight, 1);

        // transition swapchain image into copy destination state
        InsertCommandImageBarrier(commandBuffer, swapchainImage, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
                                  VK_IMAGE_LAYOUT_UNDEFINED,
                                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, subresourceRange);

        // transition offscreen buffer into copy source state
        InsertCommandImageBarrier(commandBuffer, offscreenBuffer, VK_ACCESS_SHADER_WRITE_BIT,
                                  VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL,
                                  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, subresourceRange);

        // copy offscreen buffer into swapchain image
        vkCmdCopyImage(commandBuffer, offscreenBuffer, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       swapchainImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

        // transition swapchain image into presentable state
        InsertCommandImageBarrier(commandBuffer, swapchainImage, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
                                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                  VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, subresourceRange);

        ASSERT_VK_RESULT(vkEndCommandBuffer(commandBuffer));
    };

    VkSemaphoreCreateInfo semaphoreInfo = {};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    semaphoreInfo.pNext = nullptr;

    ASSERT_VK_RESULT(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &semaphoreImageAvailable));

    ASSERT_VK_RESULT(
        vkCreateSemaphore(device, &semaphoreInfo, nullptr, &semaphoreRenderingAvailable));

    std::cout << "Done!" << std::endl;
    std::cout << "Drawing.." << std::endl;

    MSG msg;
    bool quitMessageReceived = false;
    while (!quitMessageReceived) {
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT) {
                quitMessageReceived = true;
                break;
            }
        }
        if (!quitMessageReceived) {
            uint32_t imageIndex = 0;
            ASSERT_VK_RESULT(vkAcquireNextImageKHR(device, swapchain, UINT64_MAX,
                                                   semaphoreImageAvailable, nullptr, &imageIndex));

            VkPipelineStageFlags waitStageMasks[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};

            VkSubmitInfo submitInfo = {};
            submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submitInfo.pNext = nullptr;
            submitInfo.waitSemaphoreCount = 1;
            submitInfo.pWaitSemaphores = &semaphoreImageAvailable;
            submitInfo.pWaitDstStageMask = waitStageMasks;
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers = &commandBuffers[imageIndex];
            submitInfo.signalSemaphoreCount = 1;
            submitInfo.pSignalSemaphores = &semaphoreRenderingAvailable;

            ASSERT_VK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, nullptr));

            VkPresentInfoKHR presentInfo = {};
            presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
            presentInfo.pNext = nullptr;
            presentInfo.waitSemaphoreCount = 1;
            presentInfo.pWaitSemaphores = &semaphoreRenderingAvailable;
            presentInfo.swapchainCount = 1;
            presentInfo.pSwapchains = &swapchain;
            presentInfo.pImageIndices = &imageIndex;
            presentInfo.pResults = nullptr;

            ASSERT_VK_RESULT(vkQueuePresentKHR(queue, &presentInfo));

            ASSERT_VK_RESULT(vkQueueWaitIdle(queue));
        }
    }

    return EXIT_SUCCESS;
}
