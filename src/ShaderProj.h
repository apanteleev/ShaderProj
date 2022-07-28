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

#pragma once

#include "VulkanApp.h"

#include <functional>
#include <filesystem>
#include <vector>

#include <json/value.h>

#define LOG(...) printf(__VA_ARGS__)

namespace fs = std::filesystem;

typedef std::vector<char> blob;

bool ReadFile(const fs::path& name, std::vector<char>& result);

void InitCompiler();
void ShutdownCompiler();
bool CompileShader(const fs::path& shaderFile, const std::vector<blob*>& preambles, blob& output);


struct Image
{
    vk::DeviceMemory deviceMemory;
    vk::Image image;
    vk::ImageView imageView;
    int width = 0;
    int height = 0;
    int depth = 0;
};

enum class ImageState
{
    Undefined = 0,
    Present,
    ShaderResource,
    RenderTarget,
    TransferSrc,
    TransferDst,

    Count
};

void InitImageCache();
void ShutdownImageCache(vk::Device device);
Image LoadVolume(const fs::path& fileName, vk::PhysicalDevice physicalDevice, vk::Device device, vk::Queue queue, vk::CommandBuffer cmdBuf);
Image LoadTexture(const fs::path& fileName, vk::PhysicalDevice physicalDevice, vk::Device device, vk::Queue queue, vk::CommandBuffer cmdBuf);

Image CreateCommittedImage(vk::PhysicalDevice physicalDevice, vk::Device device, const vk::ImageCreateInfo& info, vk::ImageViewType viewType);
void DestroyCommittedImage(vk::Device device, Image& image);
void ClearImage(vk::CommandBuffer vkCmdBuf, vk::Image vkImage, uint32_t layerCount, ImageState stateBefore);
void ImageBarrier(vk::CommandBuffer cmdBuf, vk::Image image,
    ImageState before,
    ImageState after,
    uint32_t layerCount = 1,
    uint32_t baseMipLevel = 0,
    uint32_t mipLevels = 1);


struct Buffer
{
    vk::DeviceMemory deviceMemory;
    vk::Buffer buffer;
};

enum class BufferState
{
    Undefined = 0,
    TransferSrc,
    TransferDst,

    Count
};

Buffer CreateCommittedBuffer(vk::PhysicalDevice physicalDevice, vk::Device device, const vk::BufferCreateInfo& info, vk::MemoryPropertyFlagBits memoryType);
void DestroyCommittedBuffer(vk::Device device, Buffer& buffer);
void BufferBarrier(vk::CommandBuffer cmdBuf, vk::Buffer buffer,
    BufferState before,
    BufferState after);


vk::ShaderModule CreateShaderModule(vk::Device device, const uint32_t* data, size_t size);
vk::ShaderModule CreateShaderModule(vk::Device device, const blob& data);

vk::Pipeline CreateQuadPipeline(
    vk::Device device,
    vk::PipelineLayout pipelineLayout,
    vk::ShaderModule vertexShader,
    vk::ShaderModule fragmentShader,
    vk::RenderPass renderPass,
    uint32_t width,
    uint32_t height);


struct ShadertoyUniforms
{
    float     iResolution[3]; // viewport resolution (in pixels)
    float     iTime;          // shader playback time (in seconds)
    float     iMouse[4];      // mouse pixel coords. xy: current (if MLB down), zw: click
    float     iDate[4];       // (year, month, day, time in seconds)
    float     iTimeDelta;     // render time (in seconds)
    float     iFrameRate;     // 1/iTimeDelta
    float     iSampleRate;    // sound sample rate (i.e., 44100)
    int       iFrame;         // shader playback frame
};

struct ShadertoyPushConstants
{
    float     iChannelResolution[4][4];
    float     iChannelTime[4];
};

constexpr uint32_t c_MaxPassInputs = 4;
constexpr uint32_t c_MaxPasses = 4;
constexpr uint32_t c_HistoryLength = 2;
constexpr uint32_t c_RenderImageCount = (c_MaxPasses + 1) * c_HistoryLength;

struct CommonResources
{
    uint32_t height = 0;
    uint32_t width = 0;
    vk::Buffer constantBuffer;
    vk::Device device;
    vk::ImageView dummyCubemap;
    vk::ImageView dummyTexture;
    vk::ImageView dummyVolume;
    vk::Sampler defaultSampler;
    std::array<Image, c_RenderImageCount> images;
};

struct ScriptEntry
{
    std::string programName;
    int programIndex = -1;
    double duration = 1.0;
};

bool LoadScript(const fs::path& scriptFileName, std::vector<ScriptEntry>& script);


class ShRenderpass
{
private:
    blob m_InputDeclarations;
    blob m_ShaderData;
    fs::path m_ProjectPath;
    fs::path m_ShaderFile;

    Json::Value m_Declaration;
    ShadertoyPushConstants m_Push{};

    std::array<Image, c_MaxPassInputs> m_StaticInputs;
    std::array<uint32_t, c_HistoryLength> m_RenderTargetIndices;
    std::array<vk::DescriptorSet, c_HistoryLength> m_DescriptorSets;
    std::array<vk::Framebuffer, c_HistoryLength> m_Framebuffers;
    std::array<vk::ImageView, c_HistoryLength> m_RenderTargetViews;
    std::array<vk::Sampler, c_MaxPassInputs> m_Samplers;
    std::string m_OutputId;
    std::string m_ProgramName;
    std::vector<std::string> m_InputIds;

    vk::Pipeline m_Pipeline;
    vk::ShaderModule m_FragmentShader;

public:
    ShRenderpass(
        const std::string& programName,
        const Json::Value& declaration,
        const fs::path& descriptionFileName,
        const fs::path& projectPath);

    bool AllocateDescriptorSets(vk::Device device, vk::DescriptorPool descriptorPool, vk::DescriptorSetLayout setLayout);
    bool CompilePassShader(blob& preamble, blob& commonSource);

    void CreateBindingSets(
        const CommonResources& common,
        const std::vector<std::shared_ptr<ShRenderpass>>& passes,
        int outputIndex);
    
    bool CreateFragmentShader(vk::Device device);

    bool CreatePipelineAndFramebuffers(
        vk::Device device,
        vk::ShaderModule vertexShader,
        vk::PipelineLayout pipelineLayout,
        vk::RenderPass renderPass,
        uint32_t width,
        uint32_t height);

    void Cleanup(vk::Device device);
    void DestroyFragmentShader(vk::Device device);
    void DestroyPipelineAndFramebuffers(vk::Device device);
    void LoadTextures(vk::PhysicalDevice physicalDevice, vk::Device device, vk::Queue queue, vk::CommandBuffer cmdBuf);

    [[nodiscard]] vk::Pipeline GetPipeline() const { return m_Pipeline; }
    [[nodiscard]] vk::Framebuffer GetFramebuffer(int frame) const { return m_Framebuffers[frame]; }
    [[nodiscard]] uint32_t GetRenderTargetIndex(int frame) const { return m_RenderTargetIndices[frame]; }
    [[nodiscard]] vk::DescriptorSet GetDescriptorSet(int frame) const { return m_DescriptorSets[frame]; }
    [[nodiscard]] ShadertoyPushConstants GetPushConstants() const { return m_Push; }
};


class ShProgram
{
private:
    fs::path m_CommonSourcePath;
    std::vector<std::shared_ptr<ShRenderpass>> m_Passes;
    int m_ImagePassIndex = 0;
    std::string m_Name;

public:
    ShProgram(const std::string& name);
    bool CompileShaders(blob& preamble);
    bool Load(const fs::path& descriptionFileName, const fs::path& projectPath);

    [[nodiscard]] const std::vector<std::shared_ptr<ShRenderpass>>& GetPasses() const { return m_Passes; }
    [[nodiscard]] int GetImagePassIndex() const { return m_ImagePassIndex; }
    [[nodiscard]] const std::string& GetName() const { return m_Name; }
};


struct CommandLineOptions
{
    bool help = false;
    bool debug = false;
    int width = 1024;
    int height = 768;
    int refreshRate = 60;
    bool fullscreen = false;
    int monitor = 0;
    int interval = 0;
    std::string shader;
    std::string projectPath;
    std::string scriptFile;
    
    std::string errorMessage;

    bool parse(int argc, char** argv);
    bool novalue(const char* arg);
};

struct Point2D
{
    double x = 0;
    double y = 0;
};

class ShaderProj : public VulkanApp
{
private:
    bool m_BufferLayoutInitd = false;
    bool m_MouseChanged = false;
    bool m_MouseDown = false;
    bool m_Paused = false;
    bool m_ResetRequired = true;
    bool m_StaticResourcesInitd = false;
    double m_CurrentDuration = 0;
    double m_CurrentTime = 0;
    double m_CurrentTimeDelta = 0;
    Point2D m_MouseDragStart;
    Point2D m_MouseLast;
    Point2D m_MousePos;

    int m_ActiveProgram = 0;
    int m_FrameIndex = 0;
    int m_ScriptIndex = 0;

    Buffer m_ConstantBuffer;
    Image m_DummyCubemap;
    Image m_DummyTexture;
    Image m_DummyVolume;

    std::array<Image, c_RenderImageCount> m_Images;
    std::array<vk::DescriptorSet, c_RenderImageCount> m_BlitDescriptorSets;
    std::vector<bool> m_SwapChainLayoutInitd;
    std::vector<ScriptEntry> m_Script;
    std::vector<std::shared_ptr<ShProgram>> m_Programs;
    std::vector<vk::Framebuffer> m_SwapChainFramebuffers;

    vk::DescriptorPool m_DescriptorPool;
    vk::DescriptorSetLayout m_BlitDescriptorSetLayout;
    vk::DescriptorSetLayout m_PassDescriptorSetLayout;
    vk::Pipeline m_BlitPipeline;
    vk::PipelineLayout m_BlitPipelineLayout;
    vk::PipelineLayout m_PassPipelineLayout;
    vk::RenderPass m_BlitRenderPass;
    vk::RenderPass m_PassRenderPass;
    vk::Sampler m_Sampler;
    vk::ShaderModule m_BlitFragmentShader;
    vk::ShaderModule m_VertexShader;

    bool CreateShaderObjects();
    void CreateBuffersAndBindings(int width, int height);
    void DestroyShaderObjects(vk::Device device);
    void NextProgram();
    void PreviousProgram();

protected:
    void Animate(double fElapsedTimeSeconds) override;
    void BackBufferResizing() override;
    void KeyboardUpdate(int key, int scancode, int action, int mods) override;
    void MouseButtonUpdate(int button, int action, int mods) override;
    void MousePosUpdate(double xpos, double ypos) override;
    void Render() override;

public:
    ShaderProj(const std::vector<std::shared_ptr<ShProgram>>& programs);
    bool Init();
    bool LoadShaders();
    bool SetScript(const std::vector<ScriptEntry>& script, double baseInterval);
    void Shutdown() override;
};