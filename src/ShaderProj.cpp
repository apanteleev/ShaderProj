/*
* Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.
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

#include "ShaderProj.h"

// Generated source files produced by glslangValidator during build
#include "shader-blit.h"
#include "shader-quad.h"

#include <chrono>
#include <thread>
#include <fstream>
#include <json/reader.h>

using namespace std;


static const char* g_PreambleText = 
    "#version 450\n"
    "#extension GL_ARB_separate_shader_objects : enable\n"
    "layout(location = 0) in vec2 i_uv;\n"
    "layout(location = 0) out vec4 o_color;\n"
    "in vec4 gl_FragCoord;\n"
    "layout(set = 0, binding = 4) uniform UniformBufferObject {\n"
    "  vec3  iResolution;\n"
    "  float iTime;\n"
    "  vec4  iMouse;\n"
    "  vec4  iDate;\n"
    "  float iTimeDelta;\n"
    "  float iSampleRate;\n"
    "  int   iFrame;\n"
    "};\n"
    "layout(push_constant) uniform PushConstants {\n"
    "  vec4  iChannelResolution[4];\n"
    "  float iChannelTime[4];\n"
    "};\n"
    "void mainImage( out vec4 fragColor, in vec2 fragCoord );\n"
    "void main() {\n"
    "  vec2 fragCoord = vec2(gl_FragCoord.x, gl_FragCoord.y);\n"
    "  mainImage(o_color, fragCoord);\n"
    "}\n";


ShaderProj::ShaderProj(const vector<shared_ptr<ShProgram>>& programs)
    : VulkanApp()
    , m_Programs(programs)
{
}

bool ShaderProj::SetScript(const vector<ScriptEntry>& script, double baseInterval)
{
    for (ScriptEntry entry : script)
    {
        entry.duration *= baseInterval;
        entry.programIndex = -1;

        int index = 0;
        for (auto& program : m_Programs)
        {
            if (program->GetName() == entry.programName)
            {
                entry.programIndex = index;
                break;
            }
            ++index;
        }

        if (entry.programIndex < 0)
        {
            LOG("WARNING: program '%s' used in the script was not loaded.\n", entry.programName.c_str());
            continue;
        }

        m_Script.push_back(entry);
    }

    if (m_Script.empty())
        return false;

    m_ActiveProgram = m_Script[0].programIndex;
    m_CurrentDuration = m_Script[0].duration;

    return true;
}

bool ShaderProj::LoadShaders()
{
    blob preamble;
    preamble.assign(g_PreambleText, g_PreambleText + strlen(g_PreambleText));
    
    bool allShadersCompiled = true;
    for (auto& program : m_Programs)
    {
        if (!program->CompileShaders(preamble))
            allShadersCompiled = false;
    }
    
    return allShadersCompiled;
}

bool ShaderProj::CreateShaderObjects()
{
    const auto vkDevice = GetDevice();

    DestroyShaderObjects(vkDevice);

    m_VertexShader = CreateShaderModule(vkDevice, g_QuadVertexShader, sizeof(g_QuadVertexShader));

    if (!m_VertexShader)
        return false;

    m_BlitFragmentShader = CreateShaderModule(vkDevice, g_BlitFragmentShader, sizeof(g_BlitFragmentShader));

    if (!m_BlitFragmentShader)
        return false;

    for (auto& program : m_Programs)
    {
        for (auto& pass : program->GetPasses())
        {
            if (!pass->CreateFragmentShader(vkDevice))
                return false;
        }
    }

    return true;
}

void ShaderProj::DestroyShaderObjects(vk::Device device)
{
    device.destroyShaderModule(m_VertexShader);
    m_VertexShader = nullptr;

    device.destroyShaderModule(m_BlitFragmentShader);
    m_BlitFragmentShader = nullptr;
}

bool ShaderProj::Init()
{
    const auto vkPhysicalDevice = GetPhysicalDevice();
    const auto vkDevice = GetDevice();
    
    auto dummyTextureDesc = vk::ImageCreateInfo()
        .setExtent(vk::Extent3D(1, 1, 1))
        .setMipLevels(1)
        .setArrayLayers(1)
        .setImageType(vk::ImageType::e2D)
        .setFormat(vk::Format::eR8G8B8A8Unorm)
        .setUsage(vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled);

    m_DummyTexture = CreateCommittedImage(vkPhysicalDevice, vkDevice, dummyTextureDesc, vk::ImageViewType::e2D);

    auto dummyCubemapDesc = vk::ImageCreateInfo()
        .setExtent(vk::Extent3D(1, 1, 1))
        .setMipLevels(1)
        .setArrayLayers(6)
        .setImageType(vk::ImageType::e2D)
        .setFormat(vk::Format::eR8G8B8A8Unorm)
        .setFlags(vk::ImageCreateFlagBits::eCubeCompatible)
        .setUsage(vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled);

    m_DummyCubemap = CreateCommittedImage(vkPhysicalDevice, vkDevice, dummyCubemapDesc, vk::ImageViewType::eCube);

    auto dummyVolumeDesc = vk::ImageCreateInfo()
        .setExtent(vk::Extent3D(1, 1, 1))
        .setMipLevels(1)
        .setArrayLayers(1)
        .setImageType(vk::ImageType::e3D)
        .setFormat(vk::Format::eR8G8B8A8Unorm)
        .setUsage(vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled);
    
    m_DummyVolume = CreateCommittedImage(vkPhysicalDevice, vkDevice, dummyVolumeDesc, vk::ImageViewType::e3D);

    auto constantBufferDesc = vk::BufferCreateInfo()
        .setSize(sizeof(ShadertoyUniforms))
        .setUsage(vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eUniformBuffer);
        
    m_ConstantBuffer = CreateCommittedBuffer(vkPhysicalDevice, vkDevice, constantBufferDesc, vk::MemoryPropertyFlagBits::eDeviceLocal);
    
    auto samplerDesc = vk::SamplerCreateInfo()
        .setMinFilter(vk::Filter::eLinear)
        .setMagFilter(vk::Filter::eLinear)
        .setMipmapMode(vk::SamplerMipmapMode::eLinear)
        .setAddressModeU(vk::SamplerAddressMode::eClampToEdge)
        .setAddressModeV(vk::SamplerAddressMode::eClampToEdge)
        .setAddressModeW(vk::SamplerAddressMode::eClampToEdge);

    m_Sampler = vkDevice.createSampler(samplerDesc);

    if (!CreateShaderObjects())
        return false;

    
    // Create the blit pipeline layout
    auto blitInputImageLayoutBinding = vk::DescriptorSetLayoutBinding()
        .setStageFlags(vk::ShaderStageFlagBits::eFragment)
        .setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
        .setDescriptorCount(1)
        .setBinding(0);

    m_BlitDescriptorSetLayout = vkDevice.createDescriptorSetLayout(vk::DescriptorSetLayoutCreateInfo()
        .setBindingCount(1)
        .setPBindings(&blitInputImageLayoutBinding));

    auto pushConstantRange = vk::PushConstantRange()
        .setSize(sizeof(ShadertoyPushConstants))
        .setStageFlags(vk::ShaderStageFlagBits::eFragment);

    m_BlitPipelineLayout = vkDevice.createPipelineLayout(vk::PipelineLayoutCreateInfo()
        .setSetLayoutCount(1)
        .setPSetLayouts(&m_BlitDescriptorSetLayout)
        .setPushConstantRangeCount(1)
        .setPPushConstantRanges(&pushConstantRange));

    // Create the render pass pipeline layout
    const vk::DescriptorSetLayoutBinding passLayoutBindings[] = {
        vk::DescriptorSetLayoutBinding()
            .setStageFlags(vk::ShaderStageFlagBits::eFragment)
            .setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
            .setDescriptorCount(1)
            .setBinding(0),
        vk::DescriptorSetLayoutBinding()
            .setStageFlags(vk::ShaderStageFlagBits::eFragment)
            .setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
            .setDescriptorCount(1)
            .setBinding(1),
        vk::DescriptorSetLayoutBinding()
            .setStageFlags(vk::ShaderStageFlagBits::eFragment)
            .setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
            .setDescriptorCount(1)
            .setBinding(2),
        vk::DescriptorSetLayoutBinding()
            .setStageFlags(vk::ShaderStageFlagBits::eFragment)
            .setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
            .setDescriptorCount(1)
            .setBinding(3),
        vk::DescriptorSetLayoutBinding()
            .setStageFlags(vk::ShaderStageFlagBits::eFragment)
            .setDescriptorType(vk::DescriptorType::eUniformBuffer)
            .setDescriptorCount(1)
            .setBinding(4)
    };

    m_PassDescriptorSetLayout = vkDevice.createDescriptorSetLayout(vk::DescriptorSetLayoutCreateInfo()
        .setBindingCount(uint32_t(std::size(passLayoutBindings)))
        .setPBindings(passLayoutBindings));

    m_PassPipelineLayout = vkDevice.createPipelineLayout(vk::PipelineLayoutCreateInfo()
        .setSetLayoutCount(1)
        .setPSetLayouts(&m_PassDescriptorSetLayout)
        .setPushConstantRangeCount(1)
        .setPPushConstantRanges(&pushConstantRange));

    // Create the render passes

    auto attachment = vk::AttachmentDescription2()
        .setFormat(vk::Format::eR16G16B16A16Sfloat)
        .setInitialLayout(vk::ImageLayout::eColorAttachmentOptimal)
        .setFinalLayout(vk::ImageLayout::eColorAttachmentOptimal);

    auto attachmentRef = vk::AttachmentReference2()
        .setAttachment(0)
        .setLayout(vk::ImageLayout::eColorAttachmentOptimal);

    auto subpass = vk::SubpassDescription2()
        .setPipelineBindPoint(vk::PipelineBindPoint::eGraphics)
        .setColorAttachmentCount(1)
        .setPColorAttachments(&attachmentRef);

    auto renderPassInfo = vk::RenderPassCreateInfo2()
        .setAttachmentCount(1)
        .setPAttachments(&attachment)
        .setSubpassCount(1)
        .setPSubpasses(&subpass);

    m_PassRenderPass = vkDevice.createRenderPass2(renderPassInfo);

    attachment.setFormat(vk::Format::eB8G8R8A8Unorm);

    m_BlitRenderPass = vkDevice.createRenderPass2(renderPassInfo);


    // Create the descriptor pool
    const uint32_t numProgramDescriptorSets = uint32_t(m_Programs.size()) * c_RenderImageCount;
    const uint32_t numBlitDescriptorSets = c_RenderImageCount;

    vk::DescriptorPoolSize poolSizes[] = {
        vk::DescriptorPoolSize().setType(vk::DescriptorType::eCombinedImageSampler).setDescriptorCount(numProgramDescriptorSets * c_MaxPasses + numBlitDescriptorSets),
        vk::DescriptorPoolSize().setType(vk::DescriptorType::eUniformBuffer).setDescriptorCount(numProgramDescriptorSets)
    };

    m_DescriptorPool = vkDevice.createDescriptorPool(vk::DescriptorPoolCreateInfo()
        .setMaxSets(numProgramDescriptorSets + numBlitDescriptorSets)
        .setPoolSizeCount(uint32_t(std::size(poolSizes)))
        .setPPoolSizes(poolSizes));

    if (!m_DescriptorPool)
        return false;

    // Allocate the blit descriptor sets
    auto allocateInfo = vk::DescriptorSetAllocateInfo()
        .setDescriptorPool(m_DescriptorPool)
        .setDescriptorSetCount(1)
        .setPSetLayouts(&m_BlitDescriptorSetLayout);

    for (uint32_t index = 0; index < numBlitDescriptorSets; index++)
    {
        auto res = vkDevice.allocateDescriptorSets(&allocateInfo, &m_BlitDescriptorSets[index]);

        if (res != vk::Result::eSuccess)
            return false;
    }

    // Allocate the program descriptor sets
    for (auto& program : m_Programs)
    {
        for (auto& pass : program->GetPasses())
        {
            if (!pass->AllocateDescriptorSets(vkDevice, m_DescriptorPool, m_PassDescriptorSetLayout))
                return false;
        }
    }

    const auto vkQueue = GetGraphicsQueue();
    const auto cmdBuf = GetCurrentCmdBuf();

    for (auto& program : m_Programs)
    {
        for (auto& pass : program->GetPasses())
        {
            pass->LoadTextures(vkPhysicalDevice, vkDevice, vkQueue, cmdBuf);
        }
    }

    return true;
}

void ShaderProj::Shutdown()
{
    const auto vkDevice = GetDevice();

    for (auto& program : m_Programs)
    {
        for (auto& pass : program->GetPasses())
        {
            pass->Cleanup(vkDevice);
        }
    }

    DestroyShaderObjects(vkDevice);

    DestroyCommittedImage(vkDevice, m_DummyTexture);
    DestroyCommittedImage(vkDevice, m_DummyCubemap);
    DestroyCommittedImage(vkDevice, m_DummyVolume);
    DestroyCommittedBuffer(vkDevice, m_ConstantBuffer);

    vkDevice.destroySampler(m_Sampler);
    m_Sampler = nullptr;

    vkDevice.destroyDescriptorPool(m_DescriptorPool);
    m_DescriptorPool = nullptr;

    vkDevice.destroyPipeline(m_BlitPipeline);
    m_BlitPipeline = nullptr;
    
    vkDevice.destroyPipelineLayout(m_BlitPipelineLayout);
    m_BlitPipelineLayout = nullptr;
    
    vkDevice.destroyDescriptorSetLayout(m_BlitDescriptorSetLayout);
    m_BlitDescriptorSetLayout = nullptr;

    vkDevice.destroyPipelineLayout(m_PassPipelineLayout);
    m_PassPipelineLayout = nullptr;

    vkDevice.destroyDescriptorSetLayout(m_PassDescriptorSetLayout);
    m_PassDescriptorSetLayout = nullptr;

    vkDevice.destroyRenderPass(m_PassRenderPass);
    m_PassRenderPass = nullptr;

    vkDevice.destroyRenderPass(m_BlitRenderPass);
    m_BlitRenderPass = nullptr;

    BackBufferResizing();

    VulkanApp::Shutdown();
}

void ShaderProj::BackBufferResizing()
{
    const auto vkDevice = GetDevice();

    for (auto& image : m_Images)
    {
        DestroyCommittedImage(vkDevice, image);
    }

    for (auto framebuffer : m_SwapChainFramebuffers)
    {
        vkDevice.destroyFramebuffer(framebuffer);
    }
    m_SwapChainFramebuffers.clear();
}

void ShaderProj::KeyboardUpdate(int key, int scancode, int action, int mods)
{
    if (key == GLFW_KEY_Q && action == GLFW_PRESS)
    {
        glfwSetWindowShouldClose(GetWindow(), true);
    }
    else if (key == GLFW_KEY_R && action == GLFW_PRESS)
    {
        if (LoadShaders())
        {
            CreateShaderObjects();
            BackBufferResizing();
        }
        m_ResetRequired = true;
    }
    else if (key == GLFW_KEY_LEFT && action == GLFW_PRESS)
    {
        PreviousProgram();
    }
    else if (key == GLFW_KEY_RIGHT && action == GLFW_PRESS)
    {
        NextProgram();
    }
    else if (key == GLFW_KEY_SPACE && action == GLFW_PRESS)
    {
        m_Paused = !m_Paused;
    }
}

void ShaderProj::MousePosUpdate(double xpos, double ypos)
{
    m_MousePos.x = xpos;
    m_MousePos.y = ypos;

    if (m_MouseDown)
    {
        m_MouseLast.x = xpos;
        m_MouseLast.y = ypos;
    }
}

void ShaderProj::MouseButtonUpdate(int button, int action, int mods)
{
    if (button == GLFW_MOUSE_BUTTON_LEFT)
    {
        if (action == GLFW_PRESS)
        {
            m_MouseDown = true;
            m_MouseLast = m_MousePos;
            m_MouseDragStart = m_MousePos;
        }
        else
        {
            m_MouseDown = false;
        }
        m_MouseChanged = true;
    }
}

void ShaderProj::PreviousProgram()
{
    if (m_Script.empty())
        return;

    --m_ScriptIndex;
    if (m_ScriptIndex < 0)
        m_ScriptIndex = int(m_Script.size()) - 1;
    m_ActiveProgram = m_Script[m_ScriptIndex].programIndex;
    m_CurrentDuration = m_Script[m_ScriptIndex].duration;
    m_ResetRequired = true;
}

void ShaderProj::NextProgram()
{
    if (m_Script.empty())
        return;

    m_ScriptIndex = (m_ScriptIndex + 1) % int(m_Script.size());
    m_ActiveProgram = m_Script[m_ScriptIndex].programIndex;
    m_CurrentDuration = m_Script[m_ScriptIndex].duration;
    m_ResetRequired = true;
}

void ShaderProj::Animate(double fElapsedTimeSeconds)
{
    if (m_Paused)
        return;

    m_CurrentTime += fElapsedTimeSeconds;
    m_CurrentTimeDelta = fElapsedTimeSeconds;

    if (m_CurrentDuration > 0 && m_CurrentTime > m_CurrentDuration)
    {
        NextProgram();
    }
}

void ShaderProj::CreateBuffersAndBindings(int width, int height)
{
    const auto vkPhysicalDevice = GetPhysicalDevice();
    const auto vkDevice = GetDevice();

    for (size_t index = 0; index < m_Images.size(); index++)
    {
        std::stringstream ss;
        ss << "Buffers[" << index << "]";

        auto imageInfo = vk::ImageCreateInfo()
            .setExtent(vk::Extent3D(width, height, 1))
            .setMipLevels(1)
            .setArrayLayers(1)
            .setImageType(vk::ImageType::e2D)
            .setFormat(vk::Format::eR16G16B16A16Sfloat)
            .setUsage(vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eColorAttachment);

        m_Images[index] = CreateCommittedImage(vkPhysicalDevice, vkDevice, imageInfo, vk::ImageViewType::e2D);
        
        // Write the blit descriptor set
        auto descriptorInfo = vk::DescriptorImageInfo()
            .setImageLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
            .setImageView(m_Images[index].imageView)
            .setSampler(m_Sampler);

        auto writeDescriptor = vk::WriteDescriptorSet()
            .setDstSet(m_BlitDescriptorSets[index])
            .setDstBinding(0)
            .setDescriptorCount(1)
            .setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
            .setPImageInfo(&descriptorInfo);

        vkDevice.updateDescriptorSets(1, &writeDescriptor, 0, nullptr);
    }

    m_BufferLayoutInitd = false;

    m_SwapChainLayoutInitd.clear();
    m_SwapChainLayoutInitd.resize(GetSwapChainImageCount());

    CommonResources common;
    common.device = vkDevice;
    common.constantBuffer = m_ConstantBuffer.buffer;
    common.defaultSampler = m_Sampler;
    common.dummyTexture = m_DummyTexture.imageView;
    common.dummyCubemap = m_DummyCubemap.imageView;
    common.dummyVolume = m_DummyVolume.imageView;
    common.images = m_Images;
    common.width = width;
    common.height = height;
    
    for (auto& program : m_Programs)
    {
        int index = 0;
        for (auto& pass : program->GetPasses())
        {
            pass->CreateBindingSets(common, program->GetPasses(), index);
            pass->CreatePipelineAndFramebuffers(vkDevice, m_VertexShader, m_PassPipelineLayout, m_PassRenderPass, width, height);
            ++index;
        }
    }
    
    vkDevice.destroyPipeline(m_BlitPipeline);
    m_BlitPipeline = vk::Pipeline();

    m_BlitPipeline = CreateQuadPipeline(
        vkDevice,
        m_BlitPipelineLayout,
        m_VertexShader,
        m_BlitFragmentShader,
        m_BlitRenderPass,
        width, height);

    // Create the swap chain framebuffers

    assert(m_SwapChainFramebuffers.empty());

    for (uint32_t index = 0; index < GetSwapChainImageCount(); index++)
    {
        auto imageView = GetSwapChainImageView(index);

        auto framebufferInfo = vk::FramebufferCreateInfo()
            .setRenderPass(m_BlitRenderPass)
            .setAttachmentCount(1)
            .setPAttachments(&imageView)
            .setWidth(width)
            .setHeight(height)
            .setLayers(1);

        auto framebuffer = vkDevice.createFramebuffer(framebufferInfo);

        m_SwapChainFramebuffers.push_back(framebuffer);
    }
}

void ShaderProj::Render()
{
    vk::CommandBuffer vkCmdBuf = GetCurrentCmdBuf();

    if (m_Paused)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

    uint32_t width, height;
    GetWindowDimensions(width, height);

    if (!m_Images[0].image)
    {
        CreateBuffersAndBindings(width, height);
    }
            
    if (!m_StaticResourcesInitd)
    {
        ClearImage(vkCmdBuf, m_DummyTexture.image, 1, ImageState::Undefined);
        ClearImage(vkCmdBuf, m_DummyCubemap.image, 6, ImageState::Undefined);
        ClearImage(vkCmdBuf, m_DummyVolume.image, 1, ImageState::Undefined);
        m_StaticResourcesInitd = true;
    }

    if (m_ResetRequired)
    {
        m_FrameIndex = 0;
        m_CurrentTime = 0;
        m_ResetRequired = false;
        LOG("Playing %s for %.1f seconds\n", m_Programs[m_ActiveProgram]->GetName().c_str(), m_CurrentDuration);
    }

    if (!m_BufferLayoutInitd)
    {
        // Clear the buffers and initialize their layouts if they're new
        for (const auto& buffer : m_Images)
        {
            ClearImage(vkCmdBuf, buffer.image, 1, m_BufferLayoutInitd ? ImageState::ShaderResource : ImageState::Undefined);
        }

        m_BufferLayoutInitd = true;
    }

    // Fill the uniform buffer.
    ShadertoyUniforms uniforms = {};
    uniforms.iResolution[0] = float(width);
    uniforms.iResolution[1] = float(height);
    uniforms.iTime = float(m_CurrentTime);
    uniforms.iTimeDelta = float(m_CurrentTimeDelta);
    uniforms.iMouse[0] = float(m_MouseLast.x);
    uniforms.iMouse[1] = float(height - 1.0 - m_MouseLast.y);
    uniforms.iMouse[2] = float(m_MouseDragStart.x) * (m_MouseDown ? 1.f : -1.f);
    uniforms.iMouse[3] = float(height - 1.0 - m_MouseDragStart.y) * (m_MouseDown && (m_MouseDragStart.x == m_MousePos.x) && (m_MouseDragStart.y == m_MousePos.y) ? 1.f : -1.f);
    uniforms.iFrame = m_FrameIndex;
    vkCmdBuf.updateBuffer(m_ConstantBuffer.buffer, 0, sizeof(uniforms), &uniforms);
    
    m_MouseChanged = false;
    
    auto program = m_Programs[m_ActiveProgram];

    uint32_t historyIndex = m_FrameIndex % c_HistoryLength;
    
    // Execute all the passes.
    for (auto& pass : program->GetPasses())
    {
        auto vkDstImage = m_Images[pass->GetRenderTargetIndex(historyIndex)].image;
        auto vkRenderPass = m_PassRenderPass;
        auto vkFramebuffer = pass->GetFramebuffer(historyIndex);
        auto vkDescriptorSet = pass->GetDescriptorSet(historyIndex);
        
        ImageBarrier(vkCmdBuf, vkDstImage, ImageState::ShaderResource, ImageState::RenderTarget);

        vkCmdBuf.beginRenderPass(vk::RenderPassBeginInfo()
            .setRenderPass(vkRenderPass)
            .setFramebuffer(vkFramebuffer)
            .setRenderArea(vk::Rect2D()
                .setExtent(vk::Extent2D(width, height))),
            vk::SubpassContents::eInline);

        vkCmdBuf.bindPipeline(vk::PipelineBindPoint::eGraphics, pass->GetPipeline());

        vkCmdBuf.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, m_PassPipelineLayout, 0, 1, &vkDescriptorSet, 0, nullptr);
        
        auto pushConstants = pass->GetPushConstants();
        vkCmdBuf.pushConstants(m_PassPipelineLayout, vk::ShaderStageFlagBits::eFragment, 0, sizeof(pushConstants), &pushConstants);

        vkCmdBuf.draw(4, 1, 0, 0);

        vkCmdBuf.endRenderPass();
        
        ImageBarrier(vkCmdBuf, vkDstImage, ImageState::RenderTarget, ImageState::ShaderResource);
    }

    // Blit the final image into the swap chain.
    {
        float factor = 1.f;
        if (m_CurrentDuration > 0)
        {
            const double transitionTime = 0.5;
            factor = float(std::min(m_CurrentTime, m_CurrentDuration - m_CurrentTime) / transitionTime);
            factor = std::max(0.f, std::min(1.f, factor));
        }

        int finalBufferIndex = program->GetImagePassIndex() * c_HistoryLength + historyIndex;
        
        int swapChainIndex = GetCurrentSwapChainIndex();
        
        auto vkDstImage = GetSwapChainImage(swapChainIndex);
        auto vkDescriptorSet = m_BlitDescriptorSets[finalBufferIndex];
        
        ImageBarrier(vkCmdBuf, vkDstImage, m_SwapChainLayoutInitd[swapChainIndex] ? ImageState::Present : ImageState::Undefined, ImageState::RenderTarget);
        
        m_SwapChainLayoutInitd[swapChainIndex] = true;

        vkCmdBuf.beginRenderPass(vk::RenderPassBeginInfo()
            .setRenderPass(m_BlitRenderPass)
            .setFramebuffer(m_SwapChainFramebuffers[swapChainIndex])
            .setRenderArea(vk::Rect2D()
                .setExtent(vk::Extent2D(width, height))),
            vk::SubpassContents::eInline);

        vkCmdBuf.bindPipeline(vk::PipelineBindPoint::eGraphics, m_BlitPipeline);

        vkCmdBuf.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, m_BlitPipelineLayout, 0, 1, &vkDescriptorSet, 0, nullptr);
        
        vkCmdBuf.pushConstants(m_BlitPipelineLayout, vk::ShaderStageFlagBits::eFragment, 0, sizeof(float), &factor);

        vkCmdBuf.draw(4, 1, 0, 0);

        vkCmdBuf.endRenderPass();
        
        ImageBarrier(vkCmdBuf, vkDstImage, ImageState::RenderTarget, ImageState::Present);
    }
    
    ++m_FrameIndex;
}


bool LoadScript(const fs::path& scriptFileName, vector<ScriptEntry>& script)
{
    std::ifstream scriptFile(scriptFileName.generic_string());
    if (!scriptFile.is_open())
    {
        LOG("ERROR: Cannot open file '%s'\n", scriptFileName.generic_string().c_str());
        return false;
    }

    Json::Value root;
    try
    {
        scriptFile >> root;
    }
    catch (const std::exception& e)
    {
        LOG("ERROR: Cannot parse '%s': %s\n", scriptFileName.generic_string().c_str(), e.what());
        return false;
    }
    scriptFile.close();

    for (const auto& node : root)
    {
        ScriptEntry entry;

        if (node.isString())
        {
            entry.programName = node.asString();
        }
        else if (node.isObject())
        {
            entry.programName = node["program"].asString();
            if (node["duration"].isNumeric())
                entry.duration = node["duration"].asDouble();
        }
        else
            continue;

        script.push_back(entry);
    }

    if (script.empty())
    {
        LOG("ERROR: Didn't find any valid entries in the script.\n");
        return false;
    }

    return true;
}
