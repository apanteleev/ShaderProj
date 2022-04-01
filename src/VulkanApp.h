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

#pragma once

#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>

#define GLFW_INCLUDE_NONE // Do not include any OpenGL headers
#include <GLFW/glfw3.h>

#include <unordered_set>

struct VulkanAppParameters
{
    bool startFullscreen = false;
    uint32_t windowWidth = 1280;
    uint32_t windowHeight = 720;
    uint32_t refreshRate = 60;
    uint32_t monitorIndex = 0;
    uint32_t swapChainImageCount = 3;
    vk::Format swapChainFormat = vk::Format::eB8G8R8A8Unorm;
    uint32_t maxFramesInFlight = 2;
    bool enableDebugRuntime = false;
    bool enableVsync = false;
};

class VulkanApp
{
public:
    bool InitVulkan(const VulkanAppParameters& params, const char *windowTitle);
    
    void RunMessageLoop();

    void GetWindowDimensions(uint32_t& width, uint32_t& height);

    // App-specific events
    virtual void Animate(double elapsedTime) { }
    virtual void Render() { }
    virtual void BackBufferResizing() { }
    virtual void BackBufferResized() { }

    virtual void KeyboardUpdate(int key, int scancode, int action, int mods) { }
    virtual void KeyboardCharInput(unsigned int unicode, int mods) { }
    virtual void MousePosUpdate(double xpos, double ypos) { }
    virtual void MouseButtonUpdate(int button, int action, int mods) { }
    virtual void MouseScrollUpdate(double xoffset, double yoffset) { }

protected:

    VulkanApp() = default;

    void UpdateWindowSize();

    bool CreateDeviceAndSwapChain();
    void DestroyDeviceAndSwapChain();
    void ResizeSwapChain();
    void BeginFrame();
    void Present();

public:
    vk::PhysicalDevice GetPhysicalDevice();
    vk::Device GetDevice();
    vk::Queue GetGraphicsQueue();
    vk::Image GetSwapChainImage(uint32_t index);
    vk::ImageView GetSwapChainImageView(uint32_t index);
    uint32_t GetCurrentSwapChainIndex();
    uint32_t GetSwapChainImageCount();
    vk::CommandBuffer GetCurrentCmdBuf();

    const VulkanAppParameters& GetVulkanParams();
    [[nodiscard]] bool IsVsyncEnabled() const { return m_DeviceParams.enableVsync; }
    virtual void SetVsync(bool enabled) { m_RequestedVSync = enabled; /* will be processed later */ }
    
    [[nodiscard]] GLFWwindow* GetWindow() const { return m_Window; }
    
    virtual void Shutdown();
    virtual ~VulkanApp() = default;

    void SetWindowTitle(const char* title);

private:
    VulkanAppParameters m_DeviceParams;
    GLFWwindow *m_Window = nullptr;

    std::string m_WindowTitle;
    std::string m_RendererString;

    bool m_WindowVisible = false;
    bool m_RequestedVSync = false;
    
    vk::Instance m_VulkanInstance;
    vk::DebugReportCallbackEXT m_DebugReportCallback;

    vk::PhysicalDevice m_VulkanPhysicalDevice;
    int m_GraphicsQueueFamily = -1;
    int m_PresentQueueFamily = -1;

    vk::Device m_VulkanDevice;
    vk::Queue m_GraphicsQueue;
    vk::Queue m_PresentQueue;

    vk::SurfaceKHR m_WindowSurface;

    vk::SurfaceFormatKHR m_SwapChainFormat;
    vk::SwapchainKHR m_SwapChain;
    std::vector<vk::Image> m_SwapChainImages;
    std::vector<vk::ImageView> m_SwapChainImageViews;
    uint32_t m_SwapChainIndex = uint32_t(-1);

    vk::Semaphore m_PresentSemaphore;

    vk::CommandPool m_CommandPool;
    std::vector<vk::CommandBuffer> m_CommandBuffers;
    std::vector<vk::Fence> m_Fences;
    std::vector<bool> m_FencesSignaled;

    uint32_t m_LoopingFrameIndex = 0;
    double m_PreviousFrameTimestamp = 0.0;

    struct VulkanExtensionSet
    {
        std::unordered_set<std::string> instance;
        std::unordered_set<std::string> layers;
        std::unordered_set<std::string> device;
    };

    VulkanExtensionSet enabledExtensions = {
        // instance
        {
            VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME
        },
        // layers
        { },
        // device
        {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME,
            VK_KHR_MAINTENANCE1_EXTENSION_NAME
        },
    };

    VulkanExtensionSet optionalExtensions = {
        // instance
        {
            VK_EXT_DEBUG_UTILS_EXTENSION_NAME
        },
        // layers
        { },
        // device
        {
            VK_EXT_DEBUG_MARKER_EXTENSION_NAME
        },
    };

    bool createInstance();
    void installDebugCallback();
    bool pickPhysicalDevice();
    bool findQueueFamilies(vk::PhysicalDevice physicalDevice);
    bool createDevice();
    bool createWindowSurface();
    void destroySwapChain();
    bool createSwapChain();

};

const char* VulkanResultToString(VkResult result);
const char* VulkanResultToString(vk::Result result);
