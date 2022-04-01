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
#include "stb_image.h"

#include <cinttypes>

using namespace std;

static std::unordered_map<string, Image>* g_ImageCache = nullptr;

void InitImageCache()
{
    g_ImageCache = new unordered_map<string, Image>();
}

void ShutdownImageCache(vk::Device device)
{
    for (auto& [name, image] : *g_ImageCache)
    {
        DestroyCommittedImage(device, image);
    }

    delete g_ImageCache;
}

struct VolumeHeader
{
    char magic[4];
    uint32_t width;
    uint32_t height;
    uint32_t depth;
    uint32_t channels;
};

static Buffer UploadImage(vk::PhysicalDevice physicalDevice, vk::Device device, vk::Image image, vk::CommandBuffer cmdBuf,
        const void* data, int width, int height, int depth, int channels)
{
    const auto memRequirements = device.getImageMemoryRequirements(image);

    auto bufferDesc = vk::BufferCreateInfo()
        .setSize(memRequirements.size)
        .setUsage(vk::BufferUsageFlagBits::eTransferSrc);

    auto buffer = CreateCommittedBuffer(physicalDevice, device, bufferDesc,
        vk::MemoryPropertyFlagBits::eHostVisible);

    if (!buffer.buffer)
    {
        LOG("ERROR: failed to create an upload buffer with %" PRIu64 " byte capacity.\n", bufferDesc.size);
        return Buffer();
    }


    void* deviceMemory = nullptr;
    auto res = device.mapMemory(buffer.deviceMemory, 0, bufferDesc.size, vk::MemoryMapFlags(), &deviceMemory);
    if (!deviceMemory || res != vk::Result::eSuccess)
    {
        LOG("ERROR: failed to map the upload buffer.\n");
        DestroyCommittedBuffer(device, buffer);
        return Buffer();
    }

    memcpy(deviceMemory, data, width * height * depth * channels);

    device.unmapMemory(buffer.deviceMemory);

    auto imageCopy = vk::BufferImageCopy()
        .setBufferOffset(0)
        .setBufferRowLength(width)
        .setBufferImageHeight(height)
        .setImageSubresource(vk::ImageSubresourceLayers()
            .setAspectMask(vk::ImageAspectFlagBits::eColor)
            .setMipLevel(0)
            .setBaseArrayLayer(0)
            .setLayerCount(1))
        .setImageOffset(vk::Offset3D())
        .setImageExtent(vk::Extent3D(width, height, depth));

    cmdBuf.copyBufferToImage(buffer.buffer,
          image, vk::ImageLayout::eTransferDstOptimal,
          1, &imageCopy);

    return buffer;
}

Image LoadVolume(const fs::path& fileName, vk::PhysicalDevice physicalDevice, vk::Device device, vk::Queue queue, vk::CommandBuffer cmdBuf)
{
    string fileNameStr = fileName.generic_string();

    assert(g_ImageCache);

    auto found = g_ImageCache->find(fileNameStr);
    if (found != g_ImageCache->end())
        return found->second;

    blob data;
    ReadFile(fileName, data);
    if (data.size() < sizeof(VolumeHeader))
        return Image();

    const VolumeHeader* header = (const VolumeHeader*)data.data();
    if (header->magic[0] != 'B' || header->magic[1] != 'I' || header->magic[2] != 'N' || header->magic[3] != 0)
        return Image();

    if (data.size() != sizeof(VolumeHeader) + header->width * header->height * header->depth * header->channels)
        return Image();

    if (header->width == 0 || header->height == 0 || header->depth == 0 || header->channels == 0 || header->channels > 4)
        return Image();

    const vk::Format formats[] = {
        vk::Format::eR8Unorm,
        vk::Format::eR8G8Unorm,
        vk::Format::eR8G8B8Unorm,
        vk::Format::eR8G8B8A8Unorm,
    };

    auto imageInfo = vk::ImageCreateInfo()
        .setExtent(vk::Extent3D(header->width, header->height, header->depth))
        .setMipLevels(1)
        .setArrayLayers(1)
        .setImageType(vk::ImageType::e3D)
        .setFormat(formats[header->channels - 1])
        .setUsage(vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled);

    auto image = CreateCommittedImage(physicalDevice, device, imageInfo, vk::ImageViewType::e3D);

    if (!image.image)
    {
        LOG("ERROR: failed to create a %dx%dx%d 3D image with %d channels.\n", header->width, header->height, header->depth, header->channels);
        return Image();
    }

    auto beginInfo = vk::CommandBufferBeginInfo()
        .setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
    cmdBuf.begin(beginInfo);

    ImageBarrier(cmdBuf, image.image, ImageState::Undefined, ImageState::TransferDst, 1, 0, 1);

    Buffer buffer = UploadImage(physicalDevice, device, image.image, cmdBuf, data.data() + sizeof(VolumeHeader),
        header->width, header->height, header->depth, header->channels);

    ImageBarrier(cmdBuf, image.image, ImageState::TransferDst, ImageState::ShaderResource, 1, 0, 1);

    cmdBuf.end();

    if (!buffer.buffer)
    {
        return Image();
    }

    auto submitInfo = vk::SubmitInfo()
        .setCommandBufferCount(1)
        .setPCommandBuffers(&cmdBuf);

    auto res = queue.submit(1, &submitInfo, nullptr);
    assert(res == vk::Result::eSuccess);

    queue.waitIdle();

    DestroyCommittedBuffer(device, buffer);


    LOG("INFO: loaded %dx%dx%d: %s\n", header->width, header->height, header->depth, fileNameStr.c_str());

    (*g_ImageCache)[fileNameStr] = image;

    return image;
}

Image LoadTexture(const fs::path& fileName, vk::PhysicalDevice physicalDevice, vk::Device device, vk::Queue queue, vk::CommandBuffer cmdBuf)
{
    string fileNameStr = fileName.generic_string();

    assert(g_ImageCache);

    auto found = g_ImageCache->find(fileNameStr);
    if (found != g_ImageCache->end())
        return found->second;

    stbi_set_flip_vertically_on_load(true);

    int width = 0;
    int height = 0;
    unsigned char* data = stbi_load(fileNameStr.c_str(), &width, &height, nullptr, 4);

    if (!data)
    {
        LOG("ERROR: failed to load image '%s'\n", fileNameStr.c_str());
        return Image();
    }

    int mipWidth = width;
    int mipHeight = height;
    int mipLevels = 1;
    while (mipWidth > 1 && mipHeight > 1)
    {
        mipWidth = std::max(mipWidth >> 1, 1);
        mipHeight = std::max(mipHeight >> 1, 1);
        ++mipLevels;
    }

    auto imageInfo = vk::ImageCreateInfo()
        .setExtent(vk::Extent3D(width, height, 1))
        .setMipLevels(mipLevels)
        .setArrayLayers(1)
        .setImageType(vk::ImageType::e2D)
        .setFormat(vk::Format::eR8G8B8A8Srgb)
        .setUsage(vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled);

    auto image = CreateCommittedImage(physicalDevice, device, imageInfo, vk::ImageViewType::e2D);

    if (!image.image)
    {
        free(data);
        LOG("ERROR: failed to create a %dx%d image with %d mips.\n", width, height, mipLevels);
        return Image();
    }


    auto beginInfo = vk::CommandBufferBeginInfo()
        .setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
    cmdBuf.begin(beginInfo);

    ImageBarrier(cmdBuf, image.image, ImageState::Undefined, ImageState::TransferDst, 1, 0, mipLevels);

    Buffer buffer = UploadImage(physicalDevice, device, image.image, cmdBuf, data, width, height, 1, 4);

    free(data);

    if (!buffer.buffer)
    {
        cmdBuf.end();
        DestroyCommittedImage(device, image);
        return Image();
    }

    mipWidth = width;
    mipHeight = height;
    for (int mipLevel = 1; mipLevel < mipLevels; ++mipLevel)
    {
        int dstMipWidth = std::max(mipWidth >> 1, 1);
        int dstMipHeight = std::max(mipHeight >> 1, 1);

        ImageBarrier(cmdBuf, image.image, ImageState::TransferDst, ImageState::TransferSrc, 1, mipLevel - 1);

        auto imageBlit = vk::ImageBlit()
            .setSrcSubresource(vk::ImageSubresourceLayers()
                .setAspectMask(vk::ImageAspectFlagBits::eColor)
                .setMipLevel(mipLevel - 1)
                .setBaseArrayLayer(0)
                .setLayerCount(1))
            .setDstSubresource(vk::ImageSubresourceLayers()
                .setAspectMask(vk::ImageAspectFlagBits::eColor)
                .setMipLevel(mipLevel)
                .setBaseArrayLayer(0)
                .setLayerCount(1))
            .setSrcOffsets({ vk::Offset3D(0, 0, 0),
                             vk::Offset3D(mipWidth, mipHeight, 1) })
            .setDstOffsets({ vk::Offset3D(0, 0, 0),
                             vk::Offset3D(dstMipWidth, dstMipHeight, 1) });

        cmdBuf.blitImage(image.image, vk::ImageLayout::eTransferSrcOptimal,
            image.image, vk::ImageLayout::eTransferDstOptimal,
            { imageBlit }, vk::Filter::eLinear);

        ImageBarrier(cmdBuf, image.image, ImageState::TransferSrc, ImageState::ShaderResource, 1, mipLevel - 1);

        mipWidth = dstMipWidth;
        mipHeight = dstMipHeight;
    }

    ImageBarrier(cmdBuf, image.image, ImageState::TransferDst, ImageState::ShaderResource, 1, mipLevels - 1);


    cmdBuf.end();

    auto submitInfo = vk::SubmitInfo()
        .setCommandBufferCount(1)
        .setPCommandBuffers(&cmdBuf);

    auto res = queue.submit(1, &submitInfo, nullptr);
    assert(res == vk::Result::eSuccess);

    queue.waitIdle();

    DestroyCommittedBuffer(device, buffer);


    LOG("INFO: loaded %dx%d: %s\n", width, height, fileNameStr.c_str());

    (*g_ImageCache)[fileNameStr] = image;

    return image;
}

Image CreateCommittedImage(vk::PhysicalDevice physicalDevice, vk::Device device, const vk::ImageCreateInfo& info, vk::ImageViewType viewType)
{
    Image image;
    image.image = device.createImage(info);

    if (!image.image)
        return image;

    const auto memRequirements = device.getImageMemoryRequirements(image.image);
    const auto memPropertyFlags = vk::MemoryPropertyFlagBits::eDeviceLocal;

    vk::PhysicalDeviceMemoryProperties memProperties;
    physicalDevice.getMemoryProperties(&memProperties);

    uint32_t memTypeIndex;
    for (memTypeIndex = 0; memTypeIndex < memProperties.memoryTypeCount; memTypeIndex++)
    {
        if ((memRequirements.memoryTypeBits & (1 << memTypeIndex)) &&
            ((memProperties.memoryTypes[memTypeIndex].propertyFlags & memPropertyFlags) == memPropertyFlags))
        {
            break;
        }
    }
    assert(memTypeIndex < memProperties.memoryTypeCount);

    auto allocInfo = vk::MemoryAllocateInfo()
        .setAllocationSize(memRequirements.size)
        .setMemoryTypeIndex(memTypeIndex);

    image.deviceMemory = device.allocateMemory(allocInfo);

    if (!image.deviceMemory)
        return image;

    device.bindImageMemory(image.image, image.deviceMemory, 0);

    image.imageView = device.createImageView(vk::ImageViewCreateInfo()
        .setImage(image.image)
        .setFormat(info.format)
        .setViewType(viewType)
        .setSubresourceRange(vk::ImageSubresourceRange()
            .setLayerCount(info.arrayLayers)
            .setLevelCount(info.mipLevels)
            .setAspectMask(vk::ImageAspectFlagBits::eColor)));

    image.width = info.extent.width;
    image.height = info.extent.height;
    image.depth = info.extent.depth;

    return image;
}

void DestroyCommittedImage(vk::Device device, Image& image)
{
    device.destroyImageView(image.imageView);
    image.imageView = nullptr;

    device.destroyImage(image.image);
    image.image = nullptr;

    device.freeMemory(image.deviceMemory);
    image.deviceMemory = nullptr;
}

struct ImageStateMapping
{
    vk::PipelineStageFlags stageMask;
    vk::AccessFlags accessMask;
    vk::ImageLayout layout;
};

static const ImageStateMapping g_ImageStates[uint32_t(ImageState::Count)] = {
    { vk::PipelineStageFlagBits::eTopOfPipe, vk::AccessFlagBits::eNoneKHR, vk::ImageLayout::eUndefined },
    { vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferRead, vk::ImageLayout::ePresentSrcKHR },
    { vk::PipelineStageFlagBits::eFragmentShader, vk::AccessFlagBits::eShaderRead, vk::ImageLayout::eShaderReadOnlyOptimal },
    { vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::AccessFlagBits::eColorAttachmentWrite, vk::ImageLayout::eColorAttachmentOptimal },
    { vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferRead, vk::ImageLayout::eTransferSrcOptimal },
    { vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferWrite, vk::ImageLayout::eTransferDstOptimal }
};

void ImageBarrier(vk::CommandBuffer cmdBuf, vk::Image image,
    ImageState before,
    ImageState after,
    uint32_t layerCount,
    uint32_t baseMipLevel,
    uint32_t mipLevels)
{
    assert(before < ImageState::Count);
    assert(after < ImageState::Count);

    const ImageStateMapping& mappingBefore = g_ImageStates[uint32_t(before)];
    const ImageStateMapping& mappingAfter = g_ImageStates[uint32_t(after)];

    cmdBuf.pipelineBarrier(mappingBefore.stageMask, mappingAfter.stageMask, vk::DependencyFlags(), {}, {}, {
        vk::ImageMemoryBarrier()
            .setSrcAccessMask(mappingBefore.accessMask)
            .setDstAccessMask(mappingAfter.accessMask)
            .setOldLayout(mappingBefore.layout)
            .setNewLayout(mappingAfter.layout)
            .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setImage(image)
            .setSubresourceRange(vk::ImageSubresourceRange()
                .setAspectMask(vk::ImageAspectFlagBits::eColor)
                .setBaseMipLevel(baseMipLevel)
                .setLevelCount(mipLevels)
                .setBaseArrayLayer(0)
                .setLayerCount(layerCount))
        });
}

void ClearImage(vk::CommandBuffer vkCmdBuf, vk::Image vkImage, uint32_t layerCount, ImageState stateBefore)
{
    ImageBarrier(vkCmdBuf, vkImage, stateBefore, ImageState::TransferDst, layerCount);

    vkCmdBuf.clearColorImage(vkImage, vk::ImageLayout::eTransferDstOptimal, vk::ClearColorValue(),
        { vk::ImageSubresourceRange()
            .setLayerCount(layerCount)
            .setLevelCount(1)
            .setAspectMask(vk::ImageAspectFlagBits::eColor) });

    ImageBarrier(vkCmdBuf, vkImage, ImageState::TransferDst, ImageState::ShaderResource, layerCount);
}
