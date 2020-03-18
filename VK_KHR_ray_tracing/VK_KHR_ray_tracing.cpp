#include <Windows.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_beta.h>
#include <vulkan/vulkan_win32.h>
#pragma comment(lib, "vulkan-1.lib")

#include <pathcch.h>
#pragma comment(lib, "Pathcch.lib")

#include <fstream>
#include <iostream>
#include <vector>

#define ASSERT_VK_RESULT(r)                                                           \
    {                                                                                 \
        VkResult result = (r);                                                        \
        if (result != VK_SUCCESS) {                                                   \
            std::cout << "Vulkan Assertion failed in Line " << __LINE__ << std::endl; \
        }                                                                             \
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

struct MappedBuffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
};

struct AccelerationMemory {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    uint64_t memoryAddress = 0;
};

VkDevice device = VK_NULL_HANDLE;
VkInstance instance = VK_NULL_HANDLE;
VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;

VkQueue queue = VK_NULL_HANDLE;
VkCommandPool commandPool = VK_NULL_HANDLE;

VkPipeline pipeline = VK_NULL_HANDLE;
VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;

VkSurfaceKHR surface = VK_NULL_HANDLE;
VkSwapchainKHR swapchain = VK_NULL_HANDLE;

VkRenderPass renderPass = VK_NULL_HANDLE;

VkSemaphore semaphoreImageAvailable = VK_NULL_HANDLE;
VkSemaphore semaphoreRenderingAvailable = VK_NULL_HANDLE;

VkAccelerationStructureKHR bottomLevelAS = VK_NULL_HANDLE;
uint64_t bottomLevelASHandle = 0;
VkAccelerationStructureKHR topLevelAS = VK_NULL_HANDLE;
uint64_t topLevelASHandle = 0;

HWND window = NULL;
HINSTANCE windowInstance;

std::vector<VkCommandBuffer> commandBuffers;

struct MsgInfo {
    HWND hWnd;
    UINT uMsg;
    WPARAM wParam;
    LPARAM lParam;
};

std::wstring appName = L"VK_KHR_ray_tracing Triangle";

uint32_t desiredWindowWidth = 1280;
uint32_t desiredWindowHeight = 720;

// clang-format off
std::vector<const char*> instanceExtensions = {
    VK_KHR_SURFACE_EXTENSION_NAME,
    VK_KHR_WIN32_SURFACE_EXTENSION_NAME
};
std::vector<const char*> validationLayers = {
    "VK_LAYER_LUNARG_standard_validation"
};
std::vector<const char*> deviceExtensions({
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    VK_KHR_RAY_TRACING_EXTENSION_NAME,
    VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,
    VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME
});
// clang-format on

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

AccelerationMemory CreateAccelerationMemory(VkAccelerationStructureKHR acceleration) {
    AccelerationMemory out = {};

    PFN_vkGetAccelerationStructureMemoryRequirementsKHR
        vkGetAccelerationStructureMemoryRequirementsKHR;
    RESOLVE_VK_DEVICE_PFN(device, vkGetAccelerationStructureMemoryRequirementsKHR);

    PFN_vkGetBufferDeviceAddressKHR vkGetBufferDeviceAddressKHR;
    RESOLVE_VK_DEVICE_PFN(device, vkGetBufferDeviceAddressKHR);

    VkMemoryRequirements2 memoryRequirements2 = {};
    memoryRequirements2.memoryRequirements = {};

    VkAccelerationStructureMemoryRequirementsInfoKHR accelerationMemoryRequirements = {};
    accelerationMemoryRequirements.sType =
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_INFO_KHR;
    accelerationMemoryRequirements.pNext = nullptr;
    accelerationMemoryRequirements.type =
        VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_BUILD_SCRATCH_KHR;
    accelerationMemoryRequirements.buildType = VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR;
    accelerationMemoryRequirements.accelerationStructure = acceleration;
    vkGetAccelerationStructureMemoryRequirementsKHR(device, &accelerationMemoryRequirements,
                                                    &memoryRequirements2);

    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.pNext = nullptr;
    bufferInfo.size = memoryRequirements2.memoryRequirements.size;
    bufferInfo.usage =
        VK_BUFFER_USAGE_RAY_TRACING_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
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
        FindMemoryType(memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    ASSERT_VK_RESULT(vkAllocateMemory(device, &memAllocInfo, nullptr, &out.memory));

    ASSERT_VK_RESULT(vkBindBufferMemory(device, out.buffer, out.memory, 0));

    VkBufferDeviceAddressInfoKHR bufferAddressInfo = {};
    bufferAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    bufferAddressInfo.pNext = nullptr;
    bufferAddressInfo.buffer = out.buffer;
    out.memoryAddress = vkGetBufferDeviceAddressKHR(device, &bufferAddressInfo);

    return out;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    MsgInfo info{hWnd, uMsg, wParam, lParam};
    switch (info.uMsg) {
        case WM_CLOSE:
            DestroyWindow(info.hWnd);
            PostQuitMessage(0);
            break;

        case WM_KEYDOWN:
            switch (info.wParam) {
                case VK_ESCAPE:
                    PostQuitMessage(0);
                    break;
            }
            break;
    }
    return (DefWindowProc(hWnd, uMsg, wParam, lParam));
}

void DrawFrame() {
    uint32_t imageIndex = 0;
    vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, semaphoreImageAvailable, nullptr,
                          &imageIndex);

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
}

int main() {
    // clang-format off
    PFN_vkGetPhysicalDeviceSurfaceSupportKHR vkGetPhysicalDeviceSurfaceSupportKHR;
    PFN_vkGetPhysicalDeviceSurfaceFormatsKHR vkGetPhysicalDeviceSurfaceFormatsKHR;

    PFN_vkCreateSwapchainKHR vkCreateSwapchainKHR;
    PFN_vkGetSwapchainImagesKHR vkGetSwapchainImagesKHR;

    PFN_vkCreateAccelerationStructureKHR vkCreateAccelerationStructureKHR;
    PFN_vkCreateRayTracingPipelinesKHR vkCreateRayTracingPipelinesKHR;
    PFN_vkBindAccelerationStructureMemoryKHR vkBindAccelerationStructureMemoryKHR;
    PFN_vkCmdBuildAccelerationStructureKHR vkCmdBuildAccelerationStructureKHR;
    PFN_vkDestroyAccelerationStructureKHR vkDestroyAccelerationStructureKHR;
    PFN_vkGetRayTracingShaderGroupHandlesKHR vkGetRayTracingShaderGroupHandlesKHR;
    PFN_vkCmdTraceRaysKHR vkCmdTraceRaysKHR;
    PFN_vkGetAccelerationStructureDeviceAddressKHR vkGetAccelerationStructureDeviceAddressKHR;
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
    createInfo.enabledLayerCount = validationLayers.size();
    createInfo.ppEnabledLayerNames = validationLayers.data();

    ASSERT_VK_RESULT(vkCreateInstance(&createInfo, nullptr, &instance));

    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
    if (deviceCount <= 0) {
        std::cout << "No physical devices available" << std::endl;
        return EXIT_FAILURE;
    }
    std::vector<VkPhysicalDevice> devices(deviceCount);
    ASSERT_VK_RESULT(vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data()));
    // TODO: prefer discrete and extension compatible device
    physicalDevice = devices[0];

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

    VkPhysicalDeviceBufferDeviceAddressFeatures bufferDeviceAddressFeatures = {};
    bufferDeviceAddressFeatures.sType =
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
    bufferDeviceAddressFeatures.pNext = nullptr;
    bufferDeviceAddressFeatures.bufferDeviceAddress = VK_TRUE;

    VkDeviceCreateInfo deviceInfo = {};
    deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceInfo.pNext = &bufferDeviceAddressFeatures;
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &deviceQueueInfo;
    deviceInfo.enabledExtensionCount = deviceExtensions.size();
    deviceInfo.ppEnabledExtensionNames = deviceExtensions.data();
    deviceInfo.pEnabledFeatures = new VkPhysicalDeviceFeatures();

    ASSERT_VK_RESULT(vkCreateDevice(physicalDevice, &deviceInfo, nullptr, &device));

    vkGetDeviceQueue(device, 0, 0, &queue);

    // clang-format off
    RESOLVE_VK_INSTANCE_PFN(instance, vkGetPhysicalDeviceSurfaceSupportKHR);
    RESOLVE_VK_INSTANCE_PFN(instance, vkGetPhysicalDeviceSurfaceFormatsKHR);

    RESOLVE_VK_DEVICE_PFN(device, vkCreateSwapchainKHR);
    RESOLVE_VK_DEVICE_PFN(device, vkGetSwapchainImagesKHR);

    RESOLVE_VK_DEVICE_PFN(device, vkCreateAccelerationStructureKHR);
    RESOLVE_VK_DEVICE_PFN(device, vkCreateRayTracingPipelinesKHR);
    RESOLVE_VK_DEVICE_PFN(device, vkBindAccelerationStructureMemoryKHR);
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

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount,
                                             queueFamilies.data());

    uint32_t surfaceFormatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &surfaceFormatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> surfaceFormats(surfaceFormatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &surfaceFormatCount,
                                         surfaceFormats.data());

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

    // acquire RT features
    VkPhysicalDeviceRayTracingFeaturesKHR rayTracingFeatures = {};
    rayTracingFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_FEATURES_KHR;
    rayTracingFeatures.pNext = nullptr;

    VkPhysicalDeviceFeatures2 deviceFeatures2 = {};
    deviceFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    deviceFeatures2.pNext = &rayTracingFeatures;
    vkGetPhysicalDeviceFeatures2(physicalDevice, &deviceFeatures2);

    // create bottom-level container
    {
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
        accelerationGeometry.geometry.triangles.vertexData.hostAddress =
            reinterpret_cast<void*>(vertices.data());
        accelerationGeometry.geometry.triangles.vertexStride = 3 * sizeof(float);
        accelerationGeometry.geometry.triangles.indexType = VK_INDEX_TYPE_UINT32;
        accelerationGeometry.geometry.triangles.indexData.hostAddress =
            reinterpret_cast<void*>(indices.data());
        accelerationGeometry.geometry.triangles.transformData.deviceAddress = VK_NULL_HANDLE;

        std::vector<VkAccelerationStructureGeometryKHR*> accelerationGeometries(
            {&accelerationGeometry});

        AccelerationMemory scratchMemory = CreateAccelerationMemory(bottomLevelAS);

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
        accelerationBuildGeometryInfo.geometryArrayOfPointers = VK_TRUE;
        accelerationBuildGeometryInfo.geometryCount = 1;
        accelerationBuildGeometryInfo.ppGeometries = accelerationGeometries.data();
        accelerationBuildGeometryInfo.scratchData.deviceAddress = scratchMemory.memoryAddress;

        VkAccelerationStructureDeviceAddressInfoKHR accelerationDeviceAddressInfo = {};
        accelerationDeviceAddressInfo.sType =
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
        accelerationDeviceAddressInfo.pNext = nullptr;
        accelerationDeviceAddressInfo.accelerationStructure = bottomLevelAS;

        bottomLevelASHandle =
            vkGetAccelerationStructureDeviceAddressKHR(device, &accelerationDeviceAddressInfo);

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

        VkAccelerationStructureBuildOffsetInfoKHR accelerationBuildOffsetInfo = {};
        accelerationBuildOffsetInfo.primitiveCount = 3;
        accelerationBuildOffsetInfo.primitiveOffset = 0x0;
        accelerationBuildOffsetInfo.firstVertex = 0;
        accelerationBuildOffsetInfo.transformOffset = 0x0;

        std::vector<VkAccelerationStructureBuildOffsetInfoKHR*> accelerationBuildOffsets = {
            &accelerationBuildOffsetInfo};

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

    // create top-level container
    {
        VkAccelerationStructureCreateGeometryTypeInfoKHR accelerationCreateGeometryInfo = {};
        accelerationCreateGeometryInfo.sType =
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_GEOMETRY_TYPE_INFO_KHR;
        accelerationCreateGeometryInfo.pNext = nullptr;
        accelerationCreateGeometryInfo.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
        accelerationCreateGeometryInfo.maxPrimitiveCount = 128;
        accelerationCreateGeometryInfo.indexType = VK_INDEX_TYPE_UINT32;
        accelerationCreateGeometryInfo.maxVertexCount = 8;
        accelerationCreateGeometryInfo.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
        accelerationCreateGeometryInfo.allowsTransforms = VK_TRUE;

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

        // clang-format off
        std::vector<VkAccelerationStructureInstanceKHR> instances = {
            {
                {1.0f, 0.0f, 0.0, 0.0f, 0.0f, 1.0f, 0.0, 0.0f, 0.0f, 0.0f, 1.0, 0.0f},
                 0,
                 0xFF,
                 0x0,
                 VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR,
                 bottomLevelASHandle
            }
        };
        // clang-format on

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
        accelerationGeometry.geometry.instances.data.hostAddress =
            reinterpret_cast<void*>(instances.data());

        std::vector<VkAccelerationStructureGeometryKHR*> accelerationGeometries(
            {&accelerationGeometry});

        AccelerationMemory scratchMemory = CreateAccelerationMemory(topLevelAS);

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
        accelerationBuildGeometryInfo.geometryArrayOfPointers = VK_TRUE;
        accelerationBuildGeometryInfo.geometryCount = 1;
        accelerationBuildGeometryInfo.ppGeometries = accelerationGeometries.data();
        accelerationBuildGeometryInfo.scratchData.deviceAddress = scratchMemory.memoryAddress;

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

        VkAccelerationStructureBuildOffsetInfoKHR accelerationBuildOffsetInfo = {};
        accelerationBuildOffsetInfo.primitiveCount = 3;
        accelerationBuildOffsetInfo.primitiveOffset = 0x0;
        accelerationBuildOffsetInfo.firstVertex = 0;
        accelerationBuildOffsetInfo.transformOffset = 0x0;

        std::vector<VkAccelerationStructureBuildOffsetInfoKHR*> accelerationBuildOffsets = {
            &accelerationBuildOffsetInfo};

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

    VkSwapchainCreateInfoKHR swapchainInfo = {};
    swapchainInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchainInfo.pNext = nullptr;
    swapchainInfo.surface = surface;
    swapchainInfo.imageFormat = VK_FORMAT_B8G8R8A8_UNORM;
    swapchainInfo.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    swapchainInfo.imageExtent.width = desiredWindowWidth;
    swapchainInfo.imageExtent.height = desiredWindowHeight;
    swapchainInfo.imageArrayLayers = 1;
    swapchainInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    swapchainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapchainInfo.queueFamilyIndexCount = 0;
    swapchainInfo.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    swapchainInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapchainInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
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
        imageViewInfo.format = VK_FORMAT_B8G8R8A8_UNORM;
        imageViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        imageViewInfo.subresourceRange.baseMipLevel = 0;
        imageViewInfo.subresourceRange.levelCount = 1;
        imageViewInfo.subresourceRange.baseArrayLayer = 0;
        imageViewInfo.subresourceRange.layerCount = 1;

        ASSERT_VK_RESULT(vkCreateImageView(device, &imageViewInfo, nullptr, &imageViews[ii]));
    };

    std::vector<char> vertexShaderSrc = readFile("../shaders/triangle_vert.spv");
    std::vector<char> fragmentShaderSrc = readFile("../shaders/triangle_frag.spv");

    VkShaderModule vertexShaderModule = CreateShaderModule(vertexShaderSrc);
    VkShaderModule fragmentShaderModule = CreateShaderModule(fragmentShaderSrc);

    VkPipelineShaderStageCreateInfo shaderStageInfoVert = {};
    shaderStageInfoVert.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStageInfoVert.pNext = nullptr;
    shaderStageInfoVert.stage = VK_SHADER_STAGE_VERTEX_BIT;
    shaderStageInfoVert.module = vertexShaderModule;
    shaderStageInfoVert.pName = "main";
    shaderStageInfoVert.pSpecializationInfo = nullptr;

    VkPipelineShaderStageCreateInfo shaderStageInfoFrag = {};
    shaderStageInfoFrag.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStageInfoFrag.pNext = nullptr;
    shaderStageInfoFrag.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shaderStageInfoFrag.module = fragmentShaderModule;
    shaderStageInfoFrag.pName = "main";
    shaderStageInfoFrag.pSpecializationInfo = nullptr;

    std::vector<VkPipelineShaderStageCreateInfo> shaderStages = {shaderStageInfoVert,
                                                                 shaderStageInfoFrag};

    std::vector<VkFramebuffer> frameBuffers(amountOfImagesInSwapchain);

    for (uint32_t ii = 0; ii < amountOfImagesInSwapchain; ++ii) {
        VkFramebufferCreateInfo framebufferInfo = {};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.pNext = nullptr;
        framebufferInfo.renderPass = renderPass;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = &imageViews[ii];
        framebufferInfo.width = desiredWindowWidth;
        framebufferInfo.height = desiredWindowHeight;
        framebufferInfo.layers = 1;
        ASSERT_VK_RESULT(vkCreateFramebuffer(device, &framebufferInfo, nullptr, &frameBuffers[ii]));
    };

    VkCommandBufferAllocateInfo cmdBufferAllocInfo = {};
    cmdBufferAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdBufferAllocInfo.pNext = nullptr;
    cmdBufferAllocInfo.commandPool = commandPool;
    cmdBufferAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdBufferAllocInfo.commandBufferCount = amountOfImagesInSwapchain;

    commandBuffers = std::vector<VkCommandBuffer>(amountOfImagesInSwapchain);

    ASSERT_VK_RESULT(vkAllocateCommandBuffers(device, &cmdBufferAllocInfo, commandBuffers.data()));

    VkCommandBufferBeginInfo commandBufferBeginInfo = {};
    commandBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    commandBufferBeginInfo.pNext = nullptr;
    commandBufferBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
    commandBufferBeginInfo.pInheritanceInfo = nullptr;

    /*for (uint32_t ii = 0; ii < amountOfImagesInSwapchain; ++ii) {
        VkCommandBuffer commandBuffer = commandBuffers[ii];
        ASSERT_VK_RESULT(vkBeginCommandBuffer(commandBuffer, &commandBufferBeginInfo));

        VkClearValue clearValue = {};

        VkRenderPassBeginInfo renderPassBeginInfo = {};
        renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassBeginInfo.pNext = nullptr;
        renderPassBeginInfo.renderPass = renderPass;
        renderPassBeginInfo.framebuffer = frameBuffers[ii];
        renderPassBeginInfo.renderArea.offset.x = 0;
        renderPassBeginInfo.renderArea.offset.y = 0;
        renderPassBeginInfo.renderArea.extent.width = desiredWindowWidth;
        renderPassBeginInfo.renderArea.extent.height = desiredWindowHeight;
        renderPassBeginInfo.clearValueCount = 1;
        renderPassBeginInfo.pClearValues = &clearValue;
        vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

        vkCmdDraw(commandBuffer, 3, 1, 0, 0);

        vkCmdEndRenderPass(commandBuffer);

        ASSERT_VK_RESULT(vkEndCommandBuffer(commandBuffer));
    };

    VkSemaphoreCreateInfo semaphoreInfo = {};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    semaphoreInfo.pNext = nullptr;

    ASSERT_VK_RESULT(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &semaphoreImageAvailable));

    ASSERT_VK_RESULT(
        vkCreateSemaphore(device, &semaphoreInfo, nullptr, &semaphoreRenderingAvailable));
        */
    std::cout << "Running.." << std::endl;

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
            //DrawFrame();
        }
    }

    return EXIT_SUCCESS;
}
