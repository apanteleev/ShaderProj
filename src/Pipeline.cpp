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
#include "Log.h"

vk::ShaderModule CreateShaderModule(vk::Device device, const uint32_t* data, size_t size)
{
    auto shaderInfo = vk::ShaderModuleCreateInfo()
        .setCodeSize(size)
        .setPCode(data);

    vk::ShaderModule shaderModule;
    const vk::Result res = device.createShaderModule(&shaderInfo, nullptr, &shaderModule);

    if (res != vk::Result::eSuccess)
    {
        LOG("ERROR: Failed to create a shader module, result = %s\n", VulkanResultToString(res));
        return vk::ShaderModule();
    }

    return shaderModule;
}

vk::ShaderModule CreateShaderModule(vk::Device device, const blob& data)
{
    return CreateShaderModule(device, (const uint32_t*)data.data(), data.size());
}

vk::Pipeline CreateQuadPipeline(
    vk::Device device,
    vk::PipelineLayout pipelineLayout,
    vk::ShaderModule vertexShader,
    vk::ShaderModule fragmentShader,
    vk::RenderPass renderPass,
    uint32_t width,
    uint32_t height)
{
    vk::PipelineShaderStageCreateInfo shaderStages[] = {
        vk::PipelineShaderStageCreateInfo()
            .setStage(vk::ShaderStageFlagBits::eVertex)
            .setPName("main")
            .setModule(vertexShader),
        vk::PipelineShaderStageCreateInfo()
            .setStage(vk::ShaderStageFlagBits::eFragment)
            .setPName("main")
            .setModule(fragmentShader)
    };

    auto inputAssembly = vk::PipelineInputAssemblyStateCreateInfo()
        .setTopology(vk::PrimitiveTopology::eTriangleStrip);

    auto vertexInput = vk::PipelineVertexInputStateCreateInfo();

    auto viewport = vk::Viewport().setWidth(float(width)).setHeight(-float(height)).setY(float(height));
    auto scissor = vk::Rect2D().setExtent(vk::Extent2D().setWidth(width).setHeight(height));
    auto viewportState = vk::PipelineViewportStateCreateInfo()
        .setViewportCount(1)
        .setPViewports(&viewport)
        .setScissorCount(1)
        .setPScissors(&scissor);

    auto rasterizer = vk::PipelineRasterizationStateCreateInfo()
        .setCullMode(vk::CullModeFlagBits::eNone)
        .setLineWidth(1.f);

    auto multisample = vk::PipelineMultisampleStateCreateInfo();

    auto depthStencil = vk::PipelineDepthStencilStateCreateInfo();

    auto colorAttachment = vk::PipelineColorBlendAttachmentState()
        .setColorWriteMask(vk::ColorComponentFlags(0xf));

    auto colorBlend = vk::PipelineColorBlendStateCreateInfo()
        .setAttachmentCount(1)
        .setPAttachments(&colorAttachment);

    return device.createGraphicsPipeline(nullptr, vk::GraphicsPipelineCreateInfo()
        .setLayout(pipelineLayout)
        .setStageCount(uint32_t(std::size(shaderStages)))
        .setPStages(shaderStages)
        .setPInputAssemblyState(&inputAssembly)
        .setPVertexInputState(&vertexInput)
        .setPViewportState(&viewportState)
        .setPRasterizationState(&rasterizer)
        .setPMultisampleState(&multisample)
        .setPDepthStencilState(&depthStencil)
        .setPColorBlendState(&colorBlend)
        .setRenderPass(renderPass)
    ).value;
}
