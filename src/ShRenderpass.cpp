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


ShRenderpass::ShRenderpass(
	const std::string& programName,
	const Json::Value& declaration,
	const fs::path& descriptionFileName,
	const fs::path& projectPath)
    : m_ProgramName(programName)
    , m_Declaration(declaration)
    , m_ProjectPath(projectPath)
{
    m_OutputId = m_Declaration["outputs"][0]["id"].asString();
    
    std::stringstream inputDecls;
    for (const auto& node : m_Declaration["inputs"])
    {
        if (node["type"] == "buffer")
        {
            std::string inputId = node["id"].asString();
            m_InputIds.push_back(inputId);
        }

        int samplerChannel = node["channel"].asInt();

        std::string samplerType = "sampler2D";
        if (node["type"] == "cubemap")
            samplerType = "samplerCube";
        else if (node["type"] == "volume")
            samplerType = "sampler3D";
        
        inputDecls << "uniform layout(set = 0, binding = " << samplerChannel << ") " << samplerType << " iChannel" << samplerChannel << ";\n";
    }
    auto inputDeclsStr = inputDecls.str();
    m_InputDeclarations.assign(inputDeclsStr.begin(), inputDeclsStr.end());

    m_ShaderFile = descriptionFileName.parent_path() / declaration["code"].asString();

    memset(&m_Push, 0, sizeof(m_Push));

    m_RenderTargetIndices.fill(0);
}

bool ShRenderpass::CompilePassShader(blob& preamble, blob& commonSource)
{
    std::vector<blob*> preambles;
    preambles.push_back(&preamble);
    preambles.push_back(&m_InputDeclarations);
    preambles.push_back(&commonSource);

    return CompileShader(m_ShaderFile, preambles, m_ShaderData);
}

bool ShRenderpass::CreateFragmentShader(vk::Device device)
{
    DestroyFragmentShader(device);

    m_FragmentShader = CreateShaderModule(device, m_ShaderData);

    return !!m_FragmentShader;
}

void ShRenderpass::DestroyFragmentShader(vk::Device device)
{
    device.destroyShaderModule(m_FragmentShader);
    m_FragmentShader = nullptr;
}

void ShRenderpass::LoadTextures(vk::PhysicalDevice physicalDevice, vk::Device device, vk::Queue queue, vk::CommandBuffer cmdBuf)
{
    for (const auto& node : m_Declaration["inputs"])
    {
        int samplerChannel = node["channel"].asInt();

        auto samplerDesc = vk::SamplerCreateInfo()
            .setMaxLod(std::numeric_limits<float>::max());

        auto samplerNode = node["sampler"];

        if (samplerNode["filter"] == "linear")
            samplerDesc.setMinFilter(vk::Filter::eLinear)
                       .setMagFilter(vk::Filter::eLinear)
                       .setMipmapMode(vk::SamplerMipmapMode::eNearest);
        else if (samplerNode["filter"] == "mipmap")
            samplerDesc.setMinFilter(vk::Filter::eLinear)
                       .setMagFilter(vk::Filter::eLinear)
                       .setMipmapMode(vk::SamplerMipmapMode::eLinear);
        else if (samplerNode["filter"] == "nearest")
            samplerDesc.setMinFilter(vk::Filter::eNearest)
                       .setMagFilter(vk::Filter::eNearest)
                       .setMipmapMode(vk::SamplerMipmapMode::eNearest);
        else
            LOG("WARNING: unknown filter mode '%s'\n", samplerNode["filter"].asCString());

        const auto addressMode = samplerNode["wrap"] == "clamp"
            ? vk::SamplerAddressMode::eClampToEdge
            : vk::SamplerAddressMode::eRepeat;

        samplerDesc.setAddressModeU(addressMode)
                   .setAddressModeV(addressMode)
                   .setAddressModeW(addressMode);
        m_Samplers[samplerChannel] = device.createSampler(samplerDesc);


        std::string fileName = node["filepath"].asString();
        if (fileName.size() <= 1)
            continue;

        if (fileName[0] == '/')
            fileName.erase(fileName.begin(), fileName.begin() + 1);

        auto textureFileName = m_ProjectPath / fileName;
        if (node["type"] == "texture")
        {
            m_StaticInputs[samplerChannel] = LoadTexture(textureFileName, physicalDevice, device, queue, cmdBuf);
        }
        else if (node["type"] == "volume")
        {
            m_StaticInputs[samplerChannel] = LoadVolume(textureFileName, physicalDevice, device, queue, cmdBuf);
        }
    }
}

bool ShRenderpass::CreatePipelineAndFramebuffers(
    vk::Device device,
    vk::ShaderModule vertexShader,
    vk::PipelineLayout pipelineLayout,
    vk::RenderPass renderPass,
    uint32_t width,
    uint32_t height)
{
    DestroyPipelineAndFramebuffers(device);

    for (uint32_t frame = 0; frame < 2; frame++)
    {
        auto framebufferInfo = vk::FramebufferCreateInfo()
            .setRenderPass(renderPass)
            .setAttachmentCount(1)
            .setPAttachments(&m_RenderTargetViews[frame])
            .setWidth(width)
            .setHeight(height)
            .setLayers(1);

        m_Framebuffers[frame] = device.createFramebuffer(framebufferInfo);
    }

    m_Pipeline = CreateQuadPipeline(
        device,
        pipelineLayout,
        vertexShader,
        m_FragmentShader,
        renderPass,
        width, height);
    
    return !!m_Pipeline;
}

void ShRenderpass::DestroyPipelineAndFramebuffers(vk::Device device)
{
    for (auto& framebuffer : m_Framebuffers)
    {
        device.destroyFramebuffer(framebuffer);
        framebuffer = nullptr;
    }

    device.destroyPipeline(m_Pipeline);
    m_Pipeline = nullptr;
}

void ShRenderpass::Cleanup(vk::Device device)
{
    DestroyPipelineAndFramebuffers(device);
    DestroyFragmentShader(device);

    for (auto& sampler : m_Samplers)
    {
        device.destroySampler(sampler);
        sampler = nullptr;
    }
}

bool ShRenderpass::AllocateDescriptorSets(vk::Device device, vk::DescriptorPool descriptorPool, vk::DescriptorSetLayout setLayout)
{
    auto allocateInfo = vk::DescriptorSetAllocateInfo()
        .setDescriptorPool(descriptorPool)
        .setDescriptorSetCount(1)
        .setPSetLayouts(&setLayout);

    for (int frame = 0; frame < 2; frame++)
    {
        auto res = device.allocateDescriptorSets(&allocateInfo, &m_DescriptorSets[frame]);

        if (res != vk::Result::eSuccess)
            return false;
    }

    return true;
}

void ShRenderpass::CreateBindingSets(
	const CommonResources& common,
	const std::vector<std::shared_ptr<ShRenderpass>>& passes,
	int outputIndex)
{
    for (int frame = 0; frame < 2; frame++)
    {
        vk::DescriptorImageInfo imageInfos[c_MaxPassInputs];
        for (int channel = 0; channel < c_MaxPassInputs; channel++)
        {
            auto sampler = m_Samplers[channel] ? m_Samplers[channel] : common.defaultSampler;

            imageInfos[channel] = vk::DescriptorImageInfo()
                .setImageLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
                .setImageView(common.dummyTexture)
                .setSampler(sampler);
        }

        vk::DescriptorBufferInfo uniformBufferInfo = vk::DescriptorBufferInfo()
            .setBuffer(common.constantBuffer)
            .setRange(sizeof(ShadertoyUniforms));

        vk::WriteDescriptorSet descriptors[c_MaxPassInputs + 1];
        for (int channel = 0; channel < c_MaxPassInputs; channel++)
        {
            descriptors[channel] = vk::WriteDescriptorSet()
                .setDstSet(m_DescriptorSets[frame])
                .setDstBinding(channel)
                .setDescriptorCount(1)
                .setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
                .setPImageInfo(&imageInfos[channel]);
        }

        descriptors[c_MaxPassInputs] = vk::WriteDescriptorSet()
            .setDstSet(m_DescriptorSets[frame])
            .setDstBinding(4)
            .setDescriptorCount(1)
            .setDescriptorType(vk::DescriptorType::eUniformBuffer)
            .setPBufferInfo(&uniformBufferInfo);
        
        
        for (const auto& node : m_Declaration["inputs"])
        {
            int samplerChannel = node["channel"].asInt();

            vk::ImageView imageView;
            int inputSize[3] = { 0 };

            if (m_StaticInputs[samplerChannel].imageView)
            {
                auto& image = m_StaticInputs[samplerChannel];
                imageView = image.imageView;
                inputSize[0] = image.width;
                inputSize[1] = image.height;
                inputSize[2] = image.depth;
            }
            else if (node["type"] == "cubemap")
            {
                imageView = common.dummyCubemap;
            }
            else if (node["type"] == "volume")
            {
                imageView = common.dummyVolume;
            }
            else if (node["type"] == "buffer")
            {
                std::string bufferId = node["id"].asString();
                int passIndex = 0;
                for (auto& pass : passes)
                {
                    if (pass->m_OutputId == bufferId)
                    {
                        int sourceFrame = (pass.get() == this) ? !frame : frame;
                        int sourceBufferIndex = passIndex * 2 + sourceFrame;
                        imageView = common.images[sourceBufferIndex].imageView;
                        
                        inputSize[0] = common.width;
                        inputSize[1] = common.height;
                        inputSize[2] = 1;
                        break;
                    }
                    ++passIndex;
                }

                if (!imageView)
                {
                    LOG("ERROR: program '%s' cannot find input buffer with id = %s\n",
                        m_ProgramName.c_str(), bufferId.c_str());
                }
            }

            if (imageView)
            {
                imageInfos[samplerChannel].setImageView(imageView);
            }
            
            m_Push.iChannelResolution[samplerChannel][0] = float(inputSize[0]);
            m_Push.iChannelResolution[samplerChannel][1] = float(inputSize[1]);
            m_Push.iChannelResolution[samplerChannel][2] = float(inputSize[2]);
        }
        
        common.device.updateDescriptorSets(uint32_t(std::size(descriptors)), descriptors, 0, nullptr);

        m_RenderTargetIndices[frame] = outputIndex * 2 + frame;
        m_RenderTargetViews[frame] = common.images[m_RenderTargetIndices[frame]].imageView;
    }
}
