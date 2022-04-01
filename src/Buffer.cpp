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

using namespace std;

Buffer CreateCommittedBuffer(vk::PhysicalDevice physicalDevice, vk::Device device, const vk::BufferCreateInfo& info, vk::MemoryPropertyFlagBits memoryType)
{
    Buffer buffer;
    buffer.buffer = device.createBuffer(info);

    if (!buffer.buffer)
        return buffer;

    const auto memRequirements = device.getBufferMemoryRequirements(buffer.buffer);

    vk::PhysicalDeviceMemoryProperties memProperties;
    physicalDevice.getMemoryProperties(&memProperties);

    uint32_t memTypeIndex;
    for (memTypeIndex = 0; memTypeIndex < memProperties.memoryTypeCount; memTypeIndex++)
    {
        if ((memRequirements.memoryTypeBits & (1 << memTypeIndex)) &&
            ((memProperties.memoryTypes[memTypeIndex].propertyFlags & memoryType) == memoryType))
        {
            break;
        }
    }
    assert(memTypeIndex < memProperties.memoryTypeCount);

    auto allocInfo = vk::MemoryAllocateInfo()
        .setAllocationSize(memRequirements.size)
        .setMemoryTypeIndex(memTypeIndex);

    buffer.deviceMemory = device.allocateMemory(allocInfo);

    if (!buffer.deviceMemory)
        return buffer;

    device.bindBufferMemory(buffer.buffer, buffer.deviceMemory, 0);
    
    return buffer;
}

void DestroyCommittedBuffer(vk::Device device, Buffer& buffer)
{
    device.destroyBuffer(buffer.buffer);
    buffer.buffer = nullptr;

    device.freeMemory(buffer.deviceMemory);
    buffer.deviceMemory = nullptr;
}

struct BufferStateMapping
{
    vk::PipelineStageFlags stageMask;
    vk::AccessFlags accessMask;
};

static const BufferStateMapping g_BufferStates[uint32_t(BufferState::Count)] = {
    { vk::PipelineStageFlagBits::eTopOfPipe, vk::AccessFlagBits::eNoneKHR },
    { vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferRead },
    { vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferWrite }
};

void BufferBarrier(vk::CommandBuffer cmdBuf, vk::Buffer buffer,
    BufferState before,
    BufferState after)
{
    assert(before < BufferState::Count);
    assert(after < BufferState::Count);

    const BufferStateMapping& mappingBefore = g_BufferStates[uint32_t(before)];
    const BufferStateMapping& mappingAfter = g_BufferStates[uint32_t(after)];

    cmdBuf.pipelineBarrier(mappingBefore.stageMask, mappingAfter.stageMask, vk::DependencyFlags(), {}, {
        vk::BufferMemoryBarrier()
            .setSrcAccessMask(mappingBefore.accessMask)
            .setDstAccessMask(mappingAfter.accessMask)
            .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setBuffer(buffer)
        }, {});
}