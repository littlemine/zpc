#include "zensim/vulkan/VkTexture.hpp"
#include "zensim/vulkan/VkCommand.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "zensim/zpc_tpls/stb/stb_image.h"

namespace zs {

  void generate_mipmaps(VulkanContext &ctx, vk::Image image, 
                        u32 width, u32 height, u32 mipLevels,
                        vk::ImageLayout finalLayout) {
    if (mipLevels <= 1) return;

    SingleUseCommandBuffer cmd{ctx, vk_queue_e::graphics};

    vk::ImageMemoryBarrier barrier{};
    barrier.image = image;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.subresourceRange.levelCount = 1;

    i32 mipWidth = static_cast<i32>(width);
    i32 mipHeight = static_cast<i32>(height);

    for (u32 i = 1; i < mipLevels; i++) {
      barrier.subresourceRange.baseMipLevel = i - 1;
      barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
      barrier.newLayout = vk::ImageLayout::eTransferSrcOptimal;
      barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
      barrier.dstAccessMask = vk::AccessFlagBits::eTransferRead;

      (*cmd).pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                             vk::PipelineStageFlagBits::eTransfer,
                             vk::DependencyFlags{}, {}, {}, {barrier}, ctx.dispatcher);

      vk::ImageBlit blit{};
      blit.srcOffsets[0] = vk::Offset3D{0, 0, 0};
      blit.srcOffsets[1] = vk::Offset3D{mipWidth, mipHeight, 1};
      blit.srcSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
      blit.srcSubresource.mipLevel = i - 1;
      blit.srcSubresource.baseArrayLayer = 0;
      blit.srcSubresource.layerCount = 1;
      blit.dstOffsets[0] = vk::Offset3D{0, 0, 0};
      blit.dstOffsets[1] = vk::Offset3D{mipWidth > 1 ? mipWidth / 2 : 1, 
                                         mipHeight > 1 ? mipHeight / 2 : 1, 1};
      blit.dstSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
      blit.dstSubresource.mipLevel = i;
      blit.dstSubresource.baseArrayLayer = 0;
      blit.dstSubresource.layerCount = 1;

      (*cmd).blitImage(image, vk::ImageLayout::eTransferSrcOptimal,
                       image, vk::ImageLayout::eTransferDstOptimal,
                       {blit}, vk::Filter::eLinear, ctx.dispatcher);

      barrier.oldLayout = vk::ImageLayout::eTransferSrcOptimal;
      barrier.newLayout = finalLayout;
      barrier.srcAccessMask = vk::AccessFlagBits::eTransferRead;
      barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

      (*cmd).pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                             vk::PipelineStageFlagBits::eFragmentShader,
                             vk::DependencyFlags{}, {}, {}, {barrier}, ctx.dispatcher);

      if (mipWidth > 1) mipWidth /= 2;
      if (mipHeight > 1) mipHeight /= 2;
    }

    barrier.subresourceRange.baseMipLevel = mipLevels - 1;
    barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
    barrier.newLayout = finalLayout;
    barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
    barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

    (*cmd).pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                           vk::PipelineStageFlagBits::eFragmentShader,
                           vk::DependencyFlags{}, {}, {}, {barrier}, ctx.dispatcher);
    // cmd automatically submits and waits on destruction
  }

  VkTexture load_texture(VulkanContext &ctx, u8 *data, size_t numBytes, vk::Extent2D extent,
                         vk::Format format, vk::ImageLayout layout, bool generateMipmaps) {
    VkTexture ret;
    /// attribs
    ret.imageLayout = layout;
    
    u32 mipLevels = generateMipmaps ? calculate_mip_levels(extent.width, extent.height) : 1;
    
    /// sampler - configure for mipmaps
    auto samplerCI = vk::SamplerCreateInfo{}
                         .setMaxAnisotropy(1.f)
                         .setMagFilter(vk::Filter::eLinear)
                         .setMinFilter(vk::Filter::eLinear)
                         .setMipmapMode(vk::SamplerMipmapMode::eLinear)
                         .setAddressModeU(vk::SamplerAddressMode::eClampToEdge)
                         .setAddressModeV(vk::SamplerAddressMode::eClampToEdge)
                         .setAddressModeW(vk::SamplerAddressMode::eClampToEdge)
                         .setBorderColor(vk::BorderColor::eFloatOpaqueWhite)
                         .setMinLod(0.0f)
                         .setMaxLod(generateMipmaps ? static_cast<float>(mipLevels) : 0.0f)
                         .setMipLodBias(0.0f);
    ret.sampler = ctx.device.createSampler(samplerCI, nullptr, ctx.dispatcher);
    
    /// image
    auto stagingBuffer = ctx.createStagingBuffer(numBytes, vk::BufferUsageFlagBits::eTransferSrc);
    stagingBuffer.map();
    memcpy(stagingBuffer.mappedAddress(), data, numBytes);
    stagingBuffer.unmap();

    vk::ImageUsageFlags imageUsage = vk::ImageUsageFlagBits::eSampled 
                                   | vk::ImageUsageFlagBits::eTransferDst;
    if (generateMipmaps) {
      imageUsage |= vk::ImageUsageFlagBits::eTransferSrc;
    }

    auto img = ctx.createOptimal2DImage(
        extent, format, imageUsage,
        /*MemoryPropertyFlags*/ vk::MemoryPropertyFlagBits::eDeviceLocal,
        /*mipmaps*/ generateMipmaps,
        /*createView*/ true);

    // transition and copy using SingleUseCommandBuffer
    {
      SingleUseCommandBuffer cmd{ctx, vk_queue_e::graphics};

      vk::ImageMemoryBarrier imageBarrier{};
      imageBarrier.image = img;
      imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      imageBarrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
      imageBarrier.subresourceRange.baseMipLevel = 0;
      imageBarrier.subresourceRange.levelCount = mipLevels;
      imageBarrier.subresourceRange.baseArrayLayer = 0;
      imageBarrier.subresourceRange.layerCount = 1;
      imageBarrier.oldLayout = vk::ImageLayout::eUndefined;
      imageBarrier.newLayout = vk::ImageLayout::eTransferDstOptimal;
      imageBarrier.srcAccessMask = vk::AccessFlags{};
      imageBarrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;

      (*cmd).pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, 
                             vk::PipelineStageFlagBits::eTransfer,
                             vk::DependencyFlags(), {}, {}, {imageBarrier}, ctx.dispatcher);

      auto bufferCopyRegion = vk::BufferImageCopy{};
      bufferCopyRegion.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
      bufferCopyRegion.imageSubresource.mipLevel = 0;
      bufferCopyRegion.imageSubresource.layerCount = 1;
      bufferCopyRegion.imageExtent = vk::Extent3D{(u32)extent.width, (u32)extent.height, (u32)1};

      (*cmd).copyBufferToImage(stagingBuffer, img, vk::ImageLayout::eTransferDstOptimal,
                               {bufferCopyRegion}, ctx.dispatcher);

      if (!generateMipmaps) {
        imageBarrier.subresourceRange.levelCount = 1;
        imageBarrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
        imageBarrier.newLayout = layout;
        imageBarrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        imageBarrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

        (*cmd).pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                               vk::PipelineStageFlagBits::eFragmentShader, vk::DependencyFlags(), {}, {},
                               {imageBarrier}, ctx.dispatcher);
      }
      // cmd automatically submits and waits on destruction
    }

    if (generateMipmaps && mipLevels > 1) {
      generate_mipmaps(ctx, img, extent.width, extent.height, mipLevels, layout);
    }

    ret.image = std::move(img);
    return ret;
  }

}  // namespace zs