/*
* Copyright (c) 2014-2022, NVIDIA CORPORATION. All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/

// This file is based on the DeviceManager subsystem of the Donut framework
// distributed by NVIDIA here: https://github.com/NVIDIAGameWorks/donut

#include "VulkanApp.h"
#include "Log.h"

#include <cstdio>
#include <thread>

// Define the Vulkan dynamic dispatcher - this needs to occur in exactly one cpp file in the program.
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

static void ErrorCallback_GLFW(int error, const char *description)
{
    fprintf(stderr, "GLFW error: %s\n", description);
    exit(1);
}

static void KeyCallback_GLFW(GLFWwindow *window, int key, int scancode, int action, int mods)
{
    VulkanApp *manager = static_cast<VulkanApp *>(glfwGetWindowUserPointer(window));
    manager->KeyboardUpdate(key, scancode, action, mods);
}

static void CharModsCallback_GLFW(GLFWwindow *window, unsigned int unicode, int mods)
{
    VulkanApp *manager = static_cast<VulkanApp *>(glfwGetWindowUserPointer(window));
    manager->KeyboardCharInput(unicode, mods);
}

static void MousePosCallback_GLFW(GLFWwindow *window, double xpos, double ypos)
{
    VulkanApp *manager = static_cast<VulkanApp *>(glfwGetWindowUserPointer(window));
    manager->MousePosUpdate(xpos, ypos);
}

static void MouseButtonCallback_GLFW(GLFWwindow *window, int button, int action, int mods)
{
    VulkanApp *manager = static_cast<VulkanApp *>(glfwGetWindowUserPointer(window));
    manager->MouseButtonUpdate(button, action, mods);
}

static void MouseScrollCallback_GLFW(GLFWwindow *window, double xoffset, double yoffset)
{
    VulkanApp *manager = static_cast<VulkanApp *>(glfwGetWindowUserPointer(window));
    manager->MouseScrollUpdate(xoffset, yoffset);
}

bool VulkanApp::InitVulkan(const VulkanAppParameters& params, const char *windowTitle)
{
    if (!glfwInit())
    {
        return false;
    }

    this->m_DeviceParams = params;
    m_RequestedVSync = params.enableVsync;

#ifdef WIN32
    constexpr bool windows = true;
#else
    constexpr bool windows = false;
#endif
    if (m_DeviceParams.enableDebugRuntime || m_DeviceParams.enableVsync && !windows)
        m_DeviceParams.maxFramesInFlight = 0;

    glfwSetErrorCallback(ErrorCallback_GLFW);

    glfwDefaultWindowHints();

    glfwWindowHint(GLFW_REFRESH_RATE, params.refreshRate);

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);   // Ignored for fullscreen

    glfwWindowHint(GLFW_DECORATED, m_DeviceParams.startFullscreen ? GLFW_FALSE : GLFW_TRUE);

    int monitorCount = 0;
    GLFWmonitor** monitors = glfwGetMonitors(&monitorCount);
    if (m_DeviceParams.monitorIndex >= uint32_t(monitorCount))
        m_DeviceParams.monitorIndex = 0;
    GLFWmonitor* monitor = monitors[m_DeviceParams.monitorIndex];

    m_Window = glfwCreateWindow(params.windowWidth, params.windowHeight,
                                windowTitle ? windowTitle : "",
                                params.startFullscreen ? monitor : nullptr,
                                nullptr);

    if (m_Window == nullptr)
    {
        return false;
    }

    if (params.startFullscreen)
    {
        glfwSetWindowMonitor(m_Window, monitor, 0, 0,
            m_DeviceParams.windowWidth, m_DeviceParams.windowHeight, m_DeviceParams.refreshRate);
    }
    else
    {
        int fbWidth = 0, fbHeight = 0;
        glfwGetFramebufferSize(m_Window, &fbWidth, &fbHeight);
        m_DeviceParams.windowWidth = fbWidth;
        m_DeviceParams.windowHeight = fbHeight;
    }

    if (windowTitle)
        m_WindowTitle = windowTitle;

    glfwSetWindowUserPointer(m_Window, this);
    
    glfwSetKeyCallback(m_Window, KeyCallback_GLFW);
    glfwSetCharModsCallback(m_Window, CharModsCallback_GLFW);
    glfwSetCursorPosCallback(m_Window, MousePosCallback_GLFW);
    glfwSetMouseButtonCallback(m_Window, MouseButtonCallback_GLFW);
    glfwSetScrollCallback(m_Window, MouseScrollCallback_GLFW);
	
	
    if (!CreateDeviceAndSwapChain())
        return false;

    glfwShowWindow(m_Window);

    // reset the back buffer size state to enforce a resize event
    m_DeviceParams.windowWidth = 0;
    m_DeviceParams.windowHeight = 0;

    UpdateWindowSize();

    return true;
}

void VulkanApp::RunMessageLoop()
{
    m_PreviousFrameTimestamp = glfwGetTime();

    while(!glfwWindowShouldClose(m_Window))
    {
        glfwPollEvents();

        UpdateWindowSize();

        double curTime = glfwGetTime();
        double elapsedTime = curTime - m_PreviousFrameTimestamp;

        if (m_WindowVisible)
        {
            Animate(elapsedTime);
            BeginFrame();
            Render();
            Present();
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(0));

        m_PreviousFrameTimestamp = curTime;
    }

    GetDevice().waitIdle();
}

void VulkanApp::GetWindowDimensions(uint32_t& width, uint32_t& height)
{
    width = m_DeviceParams.windowWidth;
    height = m_DeviceParams.windowHeight;
}

const VulkanAppParameters& VulkanApp::GetVulkanParams()
{
    return m_DeviceParams;
}

void VulkanApp::UpdateWindowSize()
{
    int width;
    int height;
    glfwGetWindowSize(m_Window, &width, &height);

    if (width == 0 || height == 0)
    {
        // window is minimized
        m_WindowVisible = false;
        return;
    }

    m_WindowVisible = true;

    if (int(m_DeviceParams.windowWidth) != width || 
        int(m_DeviceParams.windowHeight) != height ||
        (m_DeviceParams.enableVsync != m_RequestedVSync))
    {
        // window is not minimized, and the size has changed

        BackBufferResizing();

        m_DeviceParams.windowWidth = width;
        m_DeviceParams.windowHeight = height;
        m_DeviceParams.enableVsync = m_RequestedVSync;

        ResizeSwapChain();
        BackBufferResized();
    }

    m_DeviceParams.enableVsync = m_RequestedVSync;
}

void VulkanApp::Shutdown()
{
    DestroyDeviceAndSwapChain();

    if (m_Window)
    {
        glfwDestroyWindow(m_Window);
        m_Window = nullptr;
    }

    glfwTerminate();
}

void VulkanApp::SetWindowTitle(const char* title)
{
    assert(title);
    if (m_WindowTitle == title)
        return;

    glfwSetWindowTitle(m_Window, title);

    m_WindowTitle = title;
}

void VulkanApp::ResizeSwapChain()
{
    if (m_VulkanDevice)
    {
        destroySwapChain();
        createSwapChain();
    }
}

vk::Image VulkanApp::GetSwapChainImage(uint32_t index)
{
    return m_SwapChainImages[index];
}

vk::ImageView VulkanApp::GetSwapChainImageView(uint32_t index)
{
    return m_SwapChainImageViews[index];
}

uint32_t VulkanApp::GetCurrentSwapChainIndex()
{
    return m_SwapChainIndex;
}

uint32_t VulkanApp::GetSwapChainImageCount()
{
    return uint32_t(m_SwapChainImages.size());
}

vk::CommandBuffer VulkanApp::GetCurrentCmdBuf()
{
    return m_CommandBuffers[m_LoopingFrameIndex];
}

vk::PhysicalDevice VulkanApp::GetPhysicalDevice()
{
    return m_VulkanPhysicalDevice;
}

vk::Device VulkanApp::GetDevice()
{
    return m_VulkanDevice;
}

vk::Queue VulkanApp::GetGraphicsQueue()
{
    return m_GraphicsQueue;
}

static VKAPI_ATTR VkBool32 VKAPI_CALL vulkanDebugCallback(
    VkDebugReportFlagsEXT flags,
    VkDebugReportObjectTypeEXT objType,
    uint64_t obj,
    size_t location,
    int32_t code,
    const char* layerPrefix,
    const char* msg,
    void* userData)
{
    LOG("[Vulkan: location=0x%zx code=%d, layerPrefix='%s'] %s\n", location, code, layerPrefix, msg);

    return VK_FALSE;
}

static std::vector<const char*> stringSetToVector(const std::unordered_set<std::string>& set)
{
    std::vector<const char*> ret;
    for (const auto& s : set)
    {
        ret.push_back(s.c_str());
    }

    return ret;
}

template <typename T>
static std::vector<T> setToVector(const std::unordered_set<T>& set)
{
    std::vector<T> ret;
    for (const auto& s : set)
    {
        ret.push_back(s);
    }

    return ret;
}

bool VulkanApp::createInstance()
{
    if (!glfwVulkanSupported())
    {
        return false;
    }

    // add any extensions required by GLFW
    uint32_t glfwExtCount;
    const char** glfwExt = glfwGetRequiredInstanceExtensions(&glfwExtCount);
    assert(glfwExt);

    for (uint32_t i = 0; i < glfwExtCount; i++)
    {
        enabledExtensions.instance.insert(std::string(glfwExt[i]));
    }

    std::unordered_set<std::string> requiredExtensions = enabledExtensions.instance;

    // figure out which optional extensions are supported
    for (const auto& instanceExt : vk::enumerateInstanceExtensionProperties())
    {
        const std::string name = instanceExt.extensionName;
        if (optionalExtensions.instance.find(name) != optionalExtensions.instance.end())
        {
            enabledExtensions.instance.insert(name);
        }

        requiredExtensions.erase(name);
    }

    if (!requiredExtensions.empty())
    {
        std::stringstream ss;
        ss << "Cannot create a Vulkan instance because the following required extension(s) are not supported:";
        for (const auto& ext : requiredExtensions)
            ss << std::endl << "  - " << ext;

        LOG("%s\n", ss.str().c_str());
        return false;
    }

    LOG("Enabled Vulkan instance extensions:\n");
    for (const auto& ext : enabledExtensions.instance)
    {
        LOG("    %s\n", ext.c_str());
    }

    std::unordered_set<std::string> requiredLayers = enabledExtensions.layers;

    for (const auto& layer : vk::enumerateInstanceLayerProperties())
    {
        const std::string name = layer.layerName;
        if (optionalExtensions.layers.find(name) != optionalExtensions.layers.end())
        {
            enabledExtensions.layers.insert(name);
        }

        requiredLayers.erase(name);
    }

    if (!requiredLayers.empty())
    {
        std::stringstream ss;
        ss << "Cannot create a Vulkan instance because the following required layer(s) are not supported:";
        for (const auto& ext : requiredLayers)
            ss << std::endl << "  - " << ext;

        LOG("%s\n", ss.str().c_str());
        return false;
    }

    LOG("Enabled Vulkan layers:\n");
    for (const auto& layer : enabledExtensions.layers)
    {
        LOG("    %s\n", layer.c_str());
    }

    auto instanceExtVec = stringSetToVector(enabledExtensions.instance);
    auto layerVec = stringSetToVector(enabledExtensions.layers);

    auto applicationInfo = vk::ApplicationInfo()
        .setApiVersion(VK_MAKE_VERSION(1, 2, 0));

    // create the vulkan instance
    vk::InstanceCreateInfo info = vk::InstanceCreateInfo()
        .setEnabledLayerCount(uint32_t(layerVec.size()))
        .setPpEnabledLayerNames(layerVec.data())
        .setEnabledExtensionCount(uint32_t(instanceExtVec.size()))
        .setPpEnabledExtensionNames(instanceExtVec.data())
        .setPApplicationInfo(&applicationInfo);

    const vk::Result res = vk::createInstance(&info, nullptr, &m_VulkanInstance);
    if (res != vk::Result::eSuccess)
    {
        LOG("Failed to create a Vulkan instance, error code = %s\n", VulkanResultToString(res));
        return false;
    }

    VULKAN_HPP_DEFAULT_DISPATCHER.init(m_VulkanInstance);

    return true;
}

void VulkanApp::installDebugCallback()
{
    auto info = vk::DebugReportCallbackCreateInfoEXT()
        .setFlags(vk::DebugReportFlagBitsEXT::eError |
            vk::DebugReportFlagBitsEXT::eWarning |
            //   vk::DebugReportFlagBitsEXT::eInformation |
            vk::DebugReportFlagBitsEXT::ePerformanceWarning)
        .setPfnCallback(vulkanDebugCallback)
        .setPUserData(this);

    vk::Result res = m_VulkanInstance.createDebugReportCallbackEXT(&info, nullptr, &m_DebugReportCallback);
    assert(res == vk::Result::eSuccess);
}

bool VulkanApp::pickPhysicalDevice()
{
    vk::Format requestedFormat = m_DeviceParams.swapChainFormat;
    vk::Extent2D requestedExtent(m_DeviceParams.windowWidth, m_DeviceParams.windowHeight);

    auto devices = m_VulkanInstance.enumeratePhysicalDevices();

    // Start building an error message in case we cannot find a device.
    std::stringstream errorStream;
    errorStream << "Cannot find a Vulkan device that supports all the required extensions and properties.";

    // build a list of GPUs
    std::vector<vk::PhysicalDevice> discreteGPUs;
    std::vector<vk::PhysicalDevice> otherGPUs;
    for (const auto& dev : devices)
    {
        auto prop = dev.getProperties();

        errorStream << std::endl << prop.deviceName.data() << ":";

        // check that all required device extensions are present
        std::unordered_set<std::string> requiredExtensions = enabledExtensions.device;
        auto deviceExtensions = dev.enumerateDeviceExtensionProperties();
        for (const auto& ext : deviceExtensions)
        {
            requiredExtensions.erase(std::string(ext.extensionName.data()));
        }

        bool deviceIsGood = true;

        if (!requiredExtensions.empty())
        {
            // device is missing one or more required extensions
            for (const auto& ext : requiredExtensions)
            {
                errorStream << std::endl << "  - missing " << ext;
            }
            deviceIsGood = false;
        }

        // check that this device supports our intended swap chain creation parameters
        auto surfaceCaps = dev.getSurfaceCapabilitiesKHR(m_WindowSurface);
        auto surfaceFmts = dev.getSurfaceFormatsKHR(m_WindowSurface);

        if (surfaceCaps.minImageCount > m_DeviceParams.swapChainImageCount ||
            (surfaceCaps.maxImageCount < m_DeviceParams.swapChainImageCount && surfaceCaps.maxImageCount > 0))
        {
            errorStream << std::endl << "  - cannot support the requested swap chain image count:";
            errorStream << " requested " << m_DeviceParams.swapChainImageCount << ", available " << surfaceCaps.minImageCount << " - " << surfaceCaps.maxImageCount;
            deviceIsGood = false;
        }

        bool surfaceFormatPresent = false;
        for (const vk::SurfaceFormatKHR& surfaceFmt : surfaceFmts)
        {
            if (surfaceFmt.format == requestedFormat)
            {
                surfaceFormatPresent = true;
                break;
            }
        }

        if (!surfaceFormatPresent)
        {
            // can't create a swap chain using the format requested
            errorStream << std::endl << "  - does not support the requested swap chain format";
            deviceIsGood = false;
        }

        if (!findQueueFamilies(dev))
        {
            // device doesn't have all the queue families we need
            errorStream << std::endl << "  - does not support the necessary queue types";
            deviceIsGood = false;
        }

        // check that we can present from the graphics queue
        uint32_t canPresent = dev.getSurfaceSupportKHR(m_GraphicsQueueFamily, m_WindowSurface);
        if (!canPresent)
        {
            errorStream << std::endl << "  - cannot present";
            deviceIsGood = false;
        }

        if (!deviceIsGood)
            continue;

        if (prop.deviceType == vk::PhysicalDeviceType::eDiscreteGpu)
        {
            discreteGPUs.push_back(dev);
        }
        else {
            otherGPUs.push_back(dev);
        }
    }

    // pick the first discrete GPU if it exists, otherwise the first integrated GPU
    if (!discreteGPUs.empty())
    {
        m_VulkanPhysicalDevice = discreteGPUs[0];
        return true;
    }

    if (!otherGPUs.empty())
    {
        m_VulkanPhysicalDevice = otherGPUs[0];
        return true;
    }

    LOG("%s\n", errorStream.str().c_str());

    return false;
}

bool VulkanApp::findQueueFamilies(vk::PhysicalDevice physicalDevice)
{
    auto props = physicalDevice.getQueueFamilyProperties();

    for (int i = 0; i < int(props.size()); i++)
    {
        const auto& queueFamily = props[i];

        if (m_GraphicsQueueFamily == -1)
        {
            if (queueFamily.queueCount > 0 &&
                (queueFamily.queueFlags & vk::QueueFlagBits::eGraphics))
            {
                m_GraphicsQueueFamily = i;
            }
        }

        if (m_PresentQueueFamily == -1)
        {
            if (queueFamily.queueCount > 0 &&
                glfwGetPhysicalDevicePresentationSupport(m_VulkanInstance, physicalDevice, i))
            {
                m_PresentQueueFamily = i;
            }
        }
    }

    if (m_GraphicsQueueFamily == -1 ||
        m_PresentQueueFamily == -1)
    {
        return false;
    }

    return true;
}

bool VulkanApp::createDevice()
{
    // figure out which optional extensions are supported
    auto deviceExtensions = m_VulkanPhysicalDevice.enumerateDeviceExtensionProperties();
    for (const auto& ext : deviceExtensions)
    {
        const std::string name = ext.extensionName;
        if (optionalExtensions.device.find(name) != optionalExtensions.device.end())
        {
            enabledExtensions.device.insert(name);
        }
    }

    LOG("Enabled Vulkan device extensions:\n");
    for (const auto& ext : enabledExtensions.device)
    {
        LOG("    %s\n", ext.c_str());
    }

    std::unordered_set<int> uniqueQueueFamilies = {
        m_GraphicsQueueFamily,
        m_PresentQueueFamily };

    float priority = 1.f;
    std::vector<vk::DeviceQueueCreateInfo> queueDesc;
    for (int queueFamily : uniqueQueueFamilies)
    {
        queueDesc.push_back(vk::DeviceQueueCreateInfo()
            .setQueueFamilyIndex(queueFamily)
            .setQueueCount(1)
            .setPQueuePriorities(&priority));
    }

    auto deviceFeatures = vk::PhysicalDeviceFeatures();

    auto layerVec = stringSetToVector(enabledExtensions.layers);
    auto extVec = stringSetToVector(enabledExtensions.device);

    auto deviceDesc = vk::DeviceCreateInfo()
        .setPQueueCreateInfos(queueDesc.data())
        .setQueueCreateInfoCount(uint32_t(queueDesc.size()))
        .setPEnabledFeatures(&deviceFeatures)
        .setEnabledExtensionCount(uint32_t(extVec.size()))
        .setPpEnabledExtensionNames(extVec.data())
        .setEnabledLayerCount(uint32_t(layerVec.size()))
        .setPpEnabledLayerNames(layerVec.data());

    const vk::Result res = m_VulkanPhysicalDevice.createDevice(&deviceDesc, nullptr, &m_VulkanDevice);
    if (res != vk::Result::eSuccess)
    {
        LOG("Failed to create a Vulkan physical device, error code = %s\n", VulkanResultToString(res));
        return false;
    }

    m_VulkanDevice.getQueue(m_GraphicsQueueFamily, 0, &m_GraphicsQueue);
    m_VulkanDevice.getQueue(m_PresentQueueFamily, 0, &m_PresentQueue);

    VULKAN_HPP_DEFAULT_DISPATCHER.init(m_VulkanDevice);

    // stash the renderer string
    auto prop = m_VulkanPhysicalDevice.getProperties();
    m_RendererString = std::string(prop.deviceName.data());

    LOG("Created Vulkan device: %s\n", m_RendererString.c_str());

    return true;
}

bool VulkanApp::createWindowSurface()
{
    const VkResult res = glfwCreateWindowSurface(m_VulkanInstance, m_Window, nullptr, (VkSurfaceKHR*)&m_WindowSurface);
    if (res != VK_SUCCESS)
    {
        LOG("Failed to create a GLFW window surface, error code = %s\n", VulkanResultToString(res));
        return false;
    }

    return true;
}

void VulkanApp::destroySwapChain()
{
    if (m_VulkanDevice)
    {
        m_VulkanDevice.waitIdle();
    }

    if (m_SwapChain)
    {
        m_VulkanDevice.destroySwapchainKHR(m_SwapChain);
        m_SwapChain = nullptr;
    }

    for (auto view : m_SwapChainImageViews)
    {
        m_VulkanDevice.destroyImageView(view);
    }
    m_SwapChainImageViews.clear();

    m_SwapChainImages.clear();
}

bool VulkanApp::createSwapChain()
{
    destroySwapChain();

    m_SwapChainFormat = {
        m_DeviceParams.swapChainFormat,
        vk::ColorSpaceKHR::eSrgbNonlinear
    };

    vk::Extent2D extent = vk::Extent2D(m_DeviceParams.windowWidth, m_DeviceParams.windowHeight);

    std::unordered_set<uint32_t> uniqueQueues = {
        uint32_t(m_GraphicsQueueFamily),
        uint32_t(m_PresentQueueFamily) };

    std::vector<uint32_t> queues = setToVector(uniqueQueues);

    const bool enableSwapChainSharing = queues.size() > 1;

    auto desc = vk::SwapchainCreateInfoKHR()
        .setSurface(m_WindowSurface)
        .setMinImageCount(m_DeviceParams.swapChainImageCount)
        .setImageFormat(m_SwapChainFormat.format)
        .setImageColorSpace(m_SwapChainFormat.colorSpace)
        .setImageExtent(extent)
        .setImageArrayLayers(1)
        .setImageUsage(vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled)
        .setImageSharingMode(enableSwapChainSharing ? vk::SharingMode::eConcurrent : vk::SharingMode::eExclusive)
        .setQueueFamilyIndexCount(enableSwapChainSharing ? uint32_t(queues.size()) : 0)
        .setPQueueFamilyIndices(enableSwapChainSharing ? queues.data() : nullptr)
        .setPreTransform(vk::SurfaceTransformFlagBitsKHR::eIdentity)
        .setCompositeAlpha(vk::CompositeAlphaFlagBitsKHR::eOpaque)
        .setPresentMode(m_DeviceParams.enableVsync ? vk::PresentModeKHR::eFifo : vk::PresentModeKHR::eImmediate)
        .setClipped(true)
        .setOldSwapchain(nullptr);

    const vk::Result res = m_VulkanDevice.createSwapchainKHR(&desc, nullptr, &m_SwapChain);
    if (res != vk::Result::eSuccess)
    {
        LOG("Failed to create a Vulkan swap chain, error code = %s\n", VulkanResultToString(res));
        return false;
    }

    m_SwapChainImages = m_VulkanDevice.getSwapchainImagesKHR(m_SwapChain);
    m_SwapChainIndex = 0;

    for (auto image : m_SwapChainImages)
    {
        auto imageView = m_VulkanDevice.createImageView(vk::ImageViewCreateInfo()
            .setImage(image)
            .setFormat(m_SwapChainFormat.format)
            .setViewType(vk::ImageViewType::e2D)
            .setSubresourceRange(vk::ImageSubresourceRange()
                .setLayerCount(1)
                .setLevelCount(1)
                .setAspectMask(vk::ImageAspectFlagBits::eColor)));

        m_SwapChainImageViews.push_back(imageView);
    }

    return true;
}

bool VulkanApp::CreateDeviceAndSwapChain()
{
    if (m_DeviceParams.enableDebugRuntime)
    {
        enabledExtensions.instance.insert("VK_EXT_debug_report");
        enabledExtensions.layers.insert("VK_LAYER_KHRONOS_validation");
    }

    const vk::DynamicLoader dl;
    const PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr =   // NOLINT(misc-misplaced-const)
        dl.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
    VULKAN_HPP_DEFAULT_DISPATCHER.init(vkGetInstanceProcAddr);

#define CHECK(a) if (!(a)) { return false; }

    CHECK(createInstance())

    if (m_DeviceParams.enableDebugRuntime)
    {
        installDebugCallback();
    }

    CHECK(createWindowSurface())
    CHECK(pickPhysicalDevice())
    CHECK(findQueueFamilies(m_VulkanPhysicalDevice))
    CHECK(createDevice())
    CHECK(createSwapChain())

    m_PresentSemaphore = m_VulkanDevice.createSemaphore(vk::SemaphoreCreateInfo());

    for (uint32_t frame = 0; frame < m_DeviceParams.maxFramesInFlight + 1; frame++)
    {
        m_Fences.push_back(m_VulkanDevice.createFence(vk::FenceCreateInfo()));
        m_FencesSignaled.push_back(false);
    }

    // Create the command pool and buffers

    auto cmdPoolInfo = vk::CommandPoolCreateInfo()
        .setQueueFamilyIndex(m_GraphicsQueueFamily)
        .setFlags(vk::CommandPoolCreateFlagBits::eResetCommandBuffer |
            vk::CommandPoolCreateFlagBits::eTransient);

    m_CommandPool = m_VulkanDevice.createCommandPool(cmdPoolInfo);

    auto allocInfo = vk::CommandBufferAllocateInfo()
        .setLevel(vk::CommandBufferLevel::ePrimary)
        .setCommandPool(m_CommandPool)
        .setCommandBufferCount(m_DeviceParams.maxFramesInFlight + 1);

    m_CommandBuffers = m_VulkanDevice.allocateCommandBuffers(allocInfo);

#undef CHECK

    return true;
}

void VulkanApp::DestroyDeviceAndSwapChain()
{
    destroySwapChain();

    m_VulkanDevice.destroySemaphore(m_PresentSemaphore);
    m_PresentSemaphore = nullptr;

    for (auto fence : m_Fences)
    {
        m_VulkanDevice.destroyFence(fence);
    }

    m_VulkanDevice.destroyCommandPool(m_CommandPool);
    m_CommandPool = nullptr;

    m_RendererString.clear();

    if (m_DebugReportCallback)
    {
        m_VulkanInstance.destroyDebugReportCallbackEXT(m_DebugReportCallback);
    }

    if (m_VulkanDevice)
    {
        m_VulkanDevice.destroy();
        m_VulkanDevice = nullptr;
    }

    if (m_WindowSurface)
    {
        assert(m_VulkanInstance);
        m_VulkanInstance.destroySurfaceKHR(m_WindowSurface);
        m_WindowSurface = nullptr;
    }

    if (m_VulkanInstance)
    {
        m_VulkanInstance.destroy();
        m_VulkanInstance = nullptr;
    }
}

void VulkanApp::BeginFrame()
{
    vk::Result res;

    while (true)
    {
        res = m_VulkanDevice.acquireNextImageKHR(m_SwapChain,
            std::numeric_limits<uint64_t>::max(), // timeout
            m_PresentSemaphore,
            vk::Fence(),
            &m_SwapChainIndex);

        if (res == vk::Result::eErrorOutOfDateKHR)
        {
            LOG("Swap chain lost, re-creating.\n");
            BackBufferResizing();
            ResizeSwapChain();
            BackBufferResized();
            continue;
        }

        if (res != vk::Result::eSuccess)
        {
            LOG("vkAcquireNextImageKHR failed: %s\n", VulkanResultToString(res));
        }

        assert(res == vk::Result::eSuccess);

        break;
    }

    auto cmdBuf = GetCurrentCmdBuf();
    cmdBuf.begin(vk::CommandBufferBeginInfo().setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));
}

void VulkanApp::Present()
{
    // Submit the command buffer

    auto cmdBuf = GetCurrentCmdBuf();
    cmdBuf.end();

    vk::PipelineStageFlags waitDstStageMask = vk::PipelineStageFlagBits::eTopOfPipe;

    auto submitInfo = vk::SubmitInfo()
        .setCommandBufferCount(1)
        .setPCommandBuffers(&cmdBuf)
        .setWaitSemaphoreCount(1)
        .setPWaitSemaphores(&m_PresentSemaphore)
        .setPWaitDstStageMask(&waitDstStageMask)
        .setSignalSemaphoreCount(1)
        .setPSignalSemaphores(&m_PresentSemaphore);

    auto res = m_GraphicsQueue.submit(1, &submitInfo, m_Fences[m_LoopingFrameIndex]);
    assert(res == vk::Result::eSuccess);

    m_FencesSignaled[m_LoopingFrameIndex] = true;

    // Present

    vk::PresentInfoKHR info = vk::PresentInfoKHR()
        .setWaitSemaphoreCount(1)
        .setPWaitSemaphores(&m_PresentSemaphore)
        .setSwapchainCount(1)
        .setPSwapchains(&m_SwapChain)
        .setPImageIndices(&m_SwapChainIndex);

    res = m_PresentQueue.presentKHR(&info);
    assert(res == vk::Result::eSuccess || res == vk::Result::eErrorOutOfDateKHR);

    // Advance the frame index

    m_LoopingFrameIndex = (m_LoopingFrameIndex + 1) % (m_DeviceParams.maxFramesInFlight + 1);

    // Wait until some frames have gone through the pipeline

    if (m_FencesSignaled[m_LoopingFrameIndex])
    {
        res = m_VulkanDevice.waitForFences(1, &m_Fences[m_LoopingFrameIndex], true, ~0ull);
        assert(res == vk::Result::eSuccess);

        res = m_VulkanDevice.resetFences(1, &m_Fences[m_LoopingFrameIndex]);
        assert(res == vk::Result::eSuccess);
    }
}

const char* VulkanResultToString(VkResult result)
{
    switch (result)
    {
    case VK_SUCCESS:
        return "VK_SUCCESS";
    case VK_NOT_READY:
        return "VK_NOT_READY";
    case VK_TIMEOUT:
        return "VK_TIMEOUT";
    case VK_EVENT_SET:
        return "VK_EVENT_SET";
    case VK_EVENT_RESET:
        return "VK_EVENT_RESET";
    case VK_INCOMPLETE:
        return "VK_INCOMPLETE";
    case VK_ERROR_OUT_OF_HOST_MEMORY:
        return "VK_ERROR_OUT_OF_HOST_MEMORY";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY:
        return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
    case VK_ERROR_INITIALIZATION_FAILED:
        return "VK_ERROR_INITIALIZATION_FAILED";
    case VK_ERROR_DEVICE_LOST:
        return "VK_ERROR_DEVICE_LOST";
    case VK_ERROR_MEMORY_MAP_FAILED:
        return "VK_ERROR_MEMORY_MAP_FAILED";
    case VK_ERROR_LAYER_NOT_PRESENT:
        return "VK_ERROR_LAYER_NOT_PRESENT";
    case VK_ERROR_EXTENSION_NOT_PRESENT:
        return "VK_ERROR_EXTENSION_NOT_PRESENT";
    case VK_ERROR_FEATURE_NOT_PRESENT:
        return "VK_ERROR_FEATURE_NOT_PRESENT";
    case VK_ERROR_INCOMPATIBLE_DRIVER:
        return "VK_ERROR_INCOMPATIBLE_DRIVER";
    case VK_ERROR_TOO_MANY_OBJECTS:
        return "VK_ERROR_TOO_MANY_OBJECTS";
    case VK_ERROR_FORMAT_NOT_SUPPORTED:
        return "VK_ERROR_FORMAT_NOT_SUPPORTED";
    case VK_ERROR_FRAGMENTED_POOL:
        return "VK_ERROR_FRAGMENTED_POOL";
    case VK_ERROR_UNKNOWN:
        return "VK_ERROR_UNKNOWN";
    case VK_ERROR_OUT_OF_POOL_MEMORY:
        return "VK_ERROR_OUT_OF_POOL_MEMORY";
    case VK_ERROR_INVALID_EXTERNAL_HANDLE:
        return "VK_ERROR_INVALID_EXTERNAL_HANDLE";
    case VK_ERROR_FRAGMENTATION:
        return "VK_ERROR_FRAGMENTATION";
    case VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS:
        return "VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS";
    case VK_ERROR_SURFACE_LOST_KHR:
        return "VK_ERROR_SURFACE_LOST_KHR";
    case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR:
        return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
    case VK_SUBOPTIMAL_KHR:
        return "VK_SUBOPTIMAL_KHR";
    case VK_ERROR_OUT_OF_DATE_KHR:
        return "VK_ERROR_OUT_OF_DATE_KHR";
    case VK_ERROR_INCOMPATIBLE_DISPLAY_KHR:
        return "VK_ERROR_INCOMPATIBLE_DISPLAY_KHR";
    case VK_ERROR_VALIDATION_FAILED_EXT:
        return "VK_ERROR_VALIDATION_FAILED_EXT";
    case VK_ERROR_INVALID_SHADER_NV:
        return "VK_ERROR_INVALID_SHADER_NV";
    case VK_ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT:
        return "VK_ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT";
    case VK_ERROR_NOT_PERMITTED_EXT:
        return "VK_ERROR_NOT_PERMITTED_EXT";
    case VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT:
        return "VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT";
    case VK_THREAD_IDLE_KHR:
        return "VK_THREAD_IDLE_KHR";
    case VK_THREAD_DONE_KHR:
        return "VK_THREAD_DONE_KHR";
    case VK_OPERATION_DEFERRED_KHR:
        return "VK_OPERATION_DEFERRED_KHR";
    case VK_OPERATION_NOT_DEFERRED_KHR:
        return "VK_OPERATION_NOT_DEFERRED_KHR";
    case VK_PIPELINE_COMPILE_REQUIRED_EXT:
        return "VK_PIPELINE_COMPILE_REQUIRED_EXT";

    default: {
        // Print the value into a static buffer - this is not thread safe but that shouldn't matter
        static char buf[24];
        snprintf(buf, sizeof(buf), "Unknown (%d)", result);
        return buf;
    }
    }
}

const char* VulkanResultToString(vk::Result result)
{
    return VulkanResultToString((VkResult)result);
}