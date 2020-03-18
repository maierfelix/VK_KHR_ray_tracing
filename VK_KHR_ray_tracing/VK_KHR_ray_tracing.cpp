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

#define ASSERT_VK_RESULT(r)                                                     \
  {                                                                             \
    VkResult result = (r);                                                      \
    if (result != VK_SUCCESS) {                                                 \
      std::cout << "Vulkan Assertion failed in Line " << __LINE__ << std::endl; \
    }                                                                           \
  }

#define RESOLVE_VK_INSTANCE_PFN(instance, funcName)                    \
  {                                                                    \
    funcName = reinterpret_cast<PFN_##funcName>(                       \
        vkGetInstanceProcAddr(instance, "" #funcName));                \
    if (funcName == nullptr) {                                         \
      const std::string name = #funcName;                              \
      std::cout << "Failed to resolve function " << name << std::endl; \
    }                                                                  \
  }

#define RESOLVE_VK_DEVICE_PFN(device, funcName)                        \
  {                                                                    \
    funcName = reinterpret_cast<PFN_##funcName>(                       \
        vkGetDeviceProcAddr(device, "" #funcName));                    \
    if (funcName == nullptr) {                                         \
      const std::string name = #funcName;                              \
      std::cout << "Failed to resolve function " << name << std::endl; \
    }                                                                  \
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

VkShaderModule createShaderModule(std::vector<char>& code) {
  VkShaderModule shaderModule = VK_NULL_HANDLE;
  VkShaderModuleCreateInfo shaderModuleInfo = {};
  shaderModuleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  shaderModuleInfo.pNext = nullptr;
  shaderModuleInfo.codeSize = code.size();
  shaderModuleInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
  ASSERT_VK_RESULT(
      vkCreateShaderModule(device, &shaderModuleInfo, nullptr, &shaderModule));
  return shaderModule;
}

void DrawFrame() {
  uint32_t imageIndex = 0;
  vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, semaphoreImageAvailable,
                        nullptr, &imageIndex);

  VkPipelineStageFlags waitStageMasks[] = {
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};

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
  PFN_vkGetAccelerationStructureMemoryRequirementsKHR vkGetAccelerationStructureMemoryRequirementsKHR;
  PFN_vkCmdBuildAccelerationStructureKHR vkCmdBuildAccelerationStructureKHR;
  PFN_vkDestroyAccelerationStructureKHR vkDestroyAccelerationStructureKHR;
  PFN_vkGetRayTracingShaderGroupHandlesKHR vkGetRayTracingShaderGroupHandlesKHR;
  PFN_vkCmdTraceRaysKHR vkCmdTraceRaysKHR;
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
  const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU |
                      WS_CLIPSIBLINGS | WS_CLIPCHILDREN;

  RECT windowRect;
  windowRect.left = 0;
  windowRect.top = 0;
  windowRect.right = desiredWindowWidth;
  windowRect.bottom = desiredWindowHeight;
  AdjustWindowRectEx(&windowRect, style, FALSE, exStyle);

  window = CreateWindowEx(0, appName.c_str(), appName.c_str(),
                          style | WS_CLIPSIBLINGS | WS_CLIPCHILDREN, 0, 0,
                          windowRect.right - windowRect.left,
                          windowRect.bottom - windowRect.top, nullptr, nullptr,
                          windowInstance, nullptr);

  if (!window) {
    std::cout << "Failed to create window" << std::endl;
    return EXIT_FAILURE;
  }

  const uint32_t x =
      ((uint32_t)GetSystemMetrics(SM_CXSCREEN) - windowRect.right) / 2;
  const uint32_t y =
      ((uint32_t)GetSystemMetrics(SM_CYSCREEN) - windowRect.bottom) / 2;
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

  std::vector<const char*> instanceExtensions = {
      VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME};

  std::vector<const char*> validationLayers = {
      "VK_LAYER_LUNARG_standard_validation"};

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
  ASSERT_VK_RESULT(
      vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data()));
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

  // clang-format off
  std::vector<const char*> deviceExtensions({
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    VK_KHR_RAY_TRACING_EXTENSION_NAME,
    VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,
    VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME
  });
  // clang-format on

  VkDeviceCreateInfo deviceInfo = {};
  deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  deviceInfo.pNext = nullptr;
  deviceInfo.queueCreateInfoCount = 1;
  deviceInfo.pQueueCreateInfos = &deviceQueueInfo;
  deviceInfo.enabledExtensionCount = deviceExtensions.size();
  deviceInfo.ppEnabledExtensionNames = deviceExtensions.data();
  deviceInfo.pEnabledFeatures = new VkPhysicalDeviceFeatures();

  ASSERT_VK_RESULT(
      vkCreateDevice(physicalDevice, &deviceInfo, nullptr, &device));

  vkGetDeviceQueue(device, 0, 0, &queue);

  // clang-format off
  RESOLVE_VK_INSTANCE_PFN(instance, vkGetPhysicalDeviceSurfaceSupportKHR);
  RESOLVE_VK_INSTANCE_PFN(instance, vkGetPhysicalDeviceSurfaceFormatsKHR);

  RESOLVE_VK_DEVICE_PFN(device, vkCreateSwapchainKHR);
  RESOLVE_VK_DEVICE_PFN(device, vkGetSwapchainImagesKHR);

  RESOLVE_VK_DEVICE_PFN(device, vkCreateAccelerationStructureKHR);
  RESOLVE_VK_DEVICE_PFN(device, vkCreateRayTracingPipelinesKHR);
  RESOLVE_VK_DEVICE_PFN(device, vkBindAccelerationStructureMemoryKHR);
  RESOLVE_VK_DEVICE_PFN(device, vkGetAccelerationStructureMemoryRequirementsKHR);
  RESOLVE_VK_DEVICE_PFN(device, vkCmdBuildAccelerationStructureKHR);
  RESOLVE_VK_DEVICE_PFN(device, vkDestroyAccelerationStructureKHR);
  RESOLVE_VK_DEVICE_PFN(device, vkGetRayTracingShaderGroupHandlesKHR);
  RESOLVE_VK_DEVICE_PFN(device, vkCmdTraceRaysKHR);
  // clang-format on

  VkWin32SurfaceCreateInfoKHR surfaceCreateInfo;
  surfaceCreateInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
  surfaceCreateInfo.pNext = nullptr;
  surfaceCreateInfo.flags = 0;
  surfaceCreateInfo.hinstance = windowInstance;
  surfaceCreateInfo.hwnd = window;

  ASSERT_VK_RESULT(
      vkCreateWin32SurfaceKHR(instance, &surfaceCreateInfo, nullptr, &surface));

  uint32_t queueFamilyCount = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount,
                                           nullptr);
  std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
  vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount,
                                           queueFamilies.data());

  uint32_t surfaceFormatCount = 0;
  vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface,
                                       &surfaceFormatCount, nullptr);
  std::vector<VkSurfaceFormatKHR> surfaceFormats(surfaceFormatCount);
  vkGetPhysicalDeviceSurfaceFormatsKHR(
      physicalDevice, surface, &surfaceFormatCount, surfaceFormats.data());

  VkBool32 surfaceSupport = false;
  vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, 0, surface,
                                       &surfaceSupport);
  if (!surfaceSupport) {
    std::cout << "No surface rendering support" << std::endl;
    return EXIT_FAILURE;
  }

  VkCommandPoolCreateInfo cmdPoolInfo = {};
  cmdPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  cmdPoolInfo.pNext = nullptr;
  cmdPoolInfo.flags = 0;
  cmdPoolInfo.queueFamilyIndex = 0;

  ASSERT_VK_RESULT(
      vkCreateCommandPool(device, &cmdPoolInfo, nullptr, &commandPool));

  VkSwapchainCreateInfoKHR swapchainInfo = {};
  swapchainInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  swapchainInfo.pNext = nullptr;
  swapchainInfo.surface = surface;
  swapchainInfo.minImageCount = 3;
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

  ASSERT_VK_RESULT(
      vkCreateSwapchainKHR(device, &swapchainInfo, nullptr, &swapchain));

  uint32_t amountOfImagesInSwapchain = 0;
  vkGetSwapchainImagesKHR(device, swapchain, &amountOfImagesInSwapchain,
                          nullptr);
  std::vector<VkImage> swapchainImages(amountOfImagesInSwapchain);

  ASSERT_VK_RESULT(vkGetSwapchainImagesKHR(
      device, swapchain, &amountOfImagesInSwapchain, swapchainImages.data()));

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

    ASSERT_VK_RESULT(
        vkCreateImageView(device, &imageViewInfo, nullptr, &imageViews[ii]));
  };

  std::vector<char> vertexShaderSrc = readFile("../shaders/triangle_vert.spv");
  std::vector<char> fragmentShaderSrc = readFile("../shaders/triangle_frag.spv");

  VkShaderModule vertexShaderModule = createShaderModule(vertexShaderSrc);
  VkShaderModule fragmentShaderModule = createShaderModule(fragmentShaderSrc);

  VkPipelineShaderStageCreateInfo shaderStageInfoVert = {};
  shaderStageInfoVert.sType =
      VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  shaderStageInfoVert.pNext = nullptr;
  shaderStageInfoVert.stage = VK_SHADER_STAGE_VERTEX_BIT;
  shaderStageInfoVert.module = vertexShaderModule;
  shaderStageInfoVert.pName = "main";
  shaderStageInfoVert.pSpecializationInfo = nullptr;

  VkPipelineShaderStageCreateInfo shaderStageInfoFrag = {};
  shaderStageInfoFrag.sType =
      VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  shaderStageInfoFrag.pNext = nullptr;
  shaderStageInfoFrag.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  shaderStageInfoFrag.module = fragmentShaderModule;
  shaderStageInfoFrag.pName = "main";
  shaderStageInfoFrag.pSpecializationInfo = nullptr;

  std::vector<VkPipelineShaderStageCreateInfo> shaderStages = {
      shaderStageInfoVert, shaderStageInfoFrag};

  // acquire RT properties
  VkPhysicalDeviceRayTracingPropertiesKHR rayTracingProperties = {};
  rayTracingProperties.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PROPERTIES_KHR;
  rayTracingProperties.pNext = nullptr;

  VkPhysicalDeviceProperties2 deviceProperties2 = {};
  deviceProperties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
  deviceProperties2.pNext = &rayTracingProperties;
  vkGetPhysicalDeviceProperties2(physicalDevice, &deviceProperties2);

  // acquire RT features
  VkPhysicalDeviceRayTracingFeaturesKHR rayTracingFeatures = {};
  rayTracingFeatures.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_FEATURES_KHR;
  rayTracingFeatures.pNext = nullptr;

  VkPhysicalDeviceFeatures2 deviceFeatures2 = {};
  deviceFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
  deviceFeatures2.pNext = &rayTracingFeatures;
  vkGetPhysicalDeviceFeatures2(physicalDevice, &deviceFeatures2);

  // graphics pipeline
  VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
  vertexInputInfo.sType =
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vertexInputInfo.pNext = nullptr;
  vertexInputInfo.vertexBindingDescriptionCount = 0;
  vertexInputInfo.pVertexBindingDescriptions = nullptr;
  vertexInputInfo.vertexAttributeDescriptionCount = 0;
  vertexInputInfo.pVertexAttributeDescriptions = nullptr;

  VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateInfo = {};
  inputAssemblyStateInfo.sType =
      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  inputAssemblyStateInfo.pNext = nullptr;
  inputAssemblyStateInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  inputAssemblyStateInfo.primitiveRestartEnable = false;

  VkViewport viewport = {};
  viewport.x = 0;
  viewport.y = 0;
  viewport.width = desiredWindowWidth;
  viewport.height = desiredWindowHeight;
  viewport.minDepth = 0.0;
  viewport.maxDepth = 1.0;

  VkRect2D scissor = {};
  scissor.offset.x = 0;
  scissor.offset.y = 0;
  scissor.extent.width = desiredWindowWidth;
  scissor.extent.height = desiredWindowHeight;

  VkPipelineViewportStateCreateInfo viewportStateInfo = {};
  viewportStateInfo.sType =
      VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewportStateInfo.pNext = nullptr;
  viewportStateInfo.viewportCount = 1;
  viewportStateInfo.pViewports = &viewport;
  viewportStateInfo.scissorCount = 1;
  viewportStateInfo.pScissors = &scissor;

  VkPipelineRasterizationStateCreateInfo rasterizationInfo = {};
  rasterizationInfo.sType =
      VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rasterizationInfo.pNext = nullptr;
  rasterizationInfo.depthClampEnable = false;
  rasterizationInfo.rasterizerDiscardEnable = false;
  rasterizationInfo.polygonMode = VK_POLYGON_MODE_FILL;
  rasterizationInfo.cullMode = VK_CULL_MODE_BACK_BIT;
  rasterizationInfo.frontFace = VK_FRONT_FACE_CLOCKWISE;
  rasterizationInfo.depthBiasEnable = false;
  rasterizationInfo.depthBiasConstantFactor = 0.0;
  rasterizationInfo.depthBiasClamp = 0.0;
  rasterizationInfo.depthBiasSlopeFactor = 0.0;
  rasterizationInfo.lineWidth = 1.0;

  VkPipelineMultisampleStateCreateInfo multisampleInfo = {};
  multisampleInfo.sType =
      VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisampleInfo.pNext = nullptr;
  multisampleInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  multisampleInfo.minSampleShading = 1.0;
  multisampleInfo.pSampleMask = nullptr;
  multisampleInfo.alphaToCoverageEnable = false;
  multisampleInfo.alphaToOneEnable = false;

  VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
  colorBlendAttachment.blendEnable = true;
  colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
  colorBlendAttachment.dstColorBlendFactor =
      VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
  colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
  colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
  colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
  colorBlendAttachment.colorWriteMask =
      (VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
       VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT);

  VkPipelineColorBlendStateCreateInfo colorBlendInfo = {};
  colorBlendInfo.sType =
      VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  colorBlendInfo.pNext = nullptr;
  colorBlendInfo.logicOpEnable = false;
  colorBlendInfo.logicOp = VK_LOGIC_OP_NO_OP;
  colorBlendInfo.attachmentCount = 1;
  colorBlendInfo.pAttachments = &colorBlendAttachment;
  colorBlendInfo.blendConstants[0] = 0.0f;
  colorBlendInfo.blendConstants[1] = 0.0f;
  colorBlendInfo.blendConstants[2] = 0.0f;
  colorBlendInfo.blendConstants[3] = 0.0f;

  VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
  pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.pNext = nullptr;
  pipelineLayoutInfo.setLayoutCount = 0;
  pipelineLayoutInfo.pushConstantRangeCount = 0;

  ASSERT_VK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr,
                                          &pipelineLayout));

  VkAttachmentDescription attachmentDescription = {};
  attachmentDescription.flags = 0;
  attachmentDescription.format = VK_FORMAT_B8G8R8A8_UNORM;
  attachmentDescription.samples = VK_SAMPLE_COUNT_1_BIT;
  attachmentDescription.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  attachmentDescription.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  attachmentDescription.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  attachmentDescription.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  attachmentDescription.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  attachmentDescription.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

  VkAttachmentReference attachmentReference = {};
  attachmentReference.attachment = 0;
  attachmentReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  VkSubpassDescription subpassDescription = {};
  subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpassDescription.inputAttachmentCount = 0;
  subpassDescription.pInputAttachments = nullptr;
  subpassDescription.colorAttachmentCount = 1;
  subpassDescription.pColorAttachments = &attachmentReference;
  subpassDescription.pResolveAttachments = nullptr;
  subpassDescription.pDepthStencilAttachment = nullptr;
  subpassDescription.preserveAttachmentCount = 0;
  subpassDescription.pPreserveAttachments = nullptr;

  VkSubpassDependency subpassDependency = {};
  subpassDependency.srcSubpass = VK_SUBPASS_EXTERNAL;
  subpassDependency.dstSubpass = 0;
  subpassDependency.srcStageMask =
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  subpassDependency.dstStageMask =
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  subpassDependency.srcAccessMask = 0;
  subpassDependency.dstAccessMask = (VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                                     VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
  subpassDependency.dependencyFlags = 0;

  VkRenderPassCreateInfo renderPassInfo = {};
  renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  renderPassInfo.pNext = nullptr;
  renderPassInfo.attachmentCount = 1;
  renderPassInfo.pAttachments = &attachmentDescription;
  renderPassInfo.subpassCount = 1;
  renderPassInfo.pSubpasses = &subpassDescription;
  renderPassInfo.dependencyCount = 1;
  renderPassInfo.pDependencies = &subpassDependency;

  ASSERT_VK_RESULT(
      vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass));

  VkGraphicsPipelineCreateInfo graphicsPipelineInfo = {};
  graphicsPipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  graphicsPipelineInfo.pNext = nullptr;
  graphicsPipelineInfo.stageCount = shaderStages.size();
  graphicsPipelineInfo.pStages = shaderStages.data();
  graphicsPipelineInfo.pVertexInputState = &vertexInputInfo;
  graphicsPipelineInfo.pInputAssemblyState = &inputAssemblyStateInfo;
  graphicsPipelineInfo.pTessellationState = nullptr;
  graphicsPipelineInfo.pViewportState = &viewportStateInfo;
  graphicsPipelineInfo.pRasterizationState = &rasterizationInfo;
  graphicsPipelineInfo.pMultisampleState = &multisampleInfo;
  graphicsPipelineInfo.pDepthStencilState = nullptr;
  graphicsPipelineInfo.pColorBlendState = &colorBlendInfo;
  graphicsPipelineInfo.pDynamicState = nullptr;
  graphicsPipelineInfo.layout = pipelineLayout;
  graphicsPipelineInfo.renderPass = renderPass;
  graphicsPipelineInfo.subpass = 0;
  graphicsPipelineInfo.basePipelineHandle = nullptr;
  graphicsPipelineInfo.basePipelineIndex = -1;

  ASSERT_VK_RESULT(vkCreateGraphicsPipelines(
      device, nullptr, 1, &graphicsPipelineInfo, nullptr, &pipeline));

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
    ASSERT_VK_RESULT(vkCreateFramebuffer(device, &framebufferInfo, nullptr,
                                         &frameBuffers[ii]));
  };

  VkCommandBufferAllocateInfo cmdBufferAllocInfo = {};
  cmdBufferAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  cmdBufferAllocInfo.pNext = nullptr;
  cmdBufferAllocInfo.commandPool = commandPool;
  cmdBufferAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  cmdBufferAllocInfo.commandBufferCount = amountOfImagesInSwapchain;

  commandBuffers = std::vector<VkCommandBuffer>(amountOfImagesInSwapchain);

  ASSERT_VK_RESULT(vkAllocateCommandBuffers(device, &cmdBufferAllocInfo,
                                            commandBuffers.data()));

  VkCommandBufferBeginInfo commandBufferBeginInfo = {};
  commandBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  commandBufferBeginInfo.pNext = nullptr;
  commandBufferBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
  commandBufferBeginInfo.pInheritanceInfo = nullptr;

  for (uint32_t ii = 0; ii < amountOfImagesInSwapchain; ++ii) {
    VkCommandBuffer commandBuffer = commandBuffers[ii];
    ASSERT_VK_RESULT(
        vkBeginCommandBuffer(commandBuffer, &commandBufferBeginInfo));

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
    vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo,
                         VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    vkCmdDraw(commandBuffer, 3, 1, 0, 0);

    vkCmdEndRenderPass(commandBuffer);

    ASSERT_VK_RESULT(vkEndCommandBuffer(commandBuffer));
  };

  VkSemaphoreCreateInfo semaphoreInfo = {};
  semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
  semaphoreInfo.pNext = nullptr;

  ASSERT_VK_RESULT(vkCreateSemaphore(device, &semaphoreInfo, nullptr,
                                     &semaphoreImageAvailable));

  ASSERT_VK_RESULT(vkCreateSemaphore(device, &semaphoreInfo, nullptr,
                                     &semaphoreRenderingAvailable));

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
      DrawFrame();
    }
  }

  return EXIT_SUCCESS;
}
