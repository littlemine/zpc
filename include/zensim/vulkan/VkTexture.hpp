#pragma once
#include "zensim/ZpcImplPattern.hpp"
#include "zensim/vulkan/VkContext.hpp"
#include "zensim/vulkan/VkImage.hpp"

namespace zs {

  /// @brief Calculate the number of mip levels for an image of given dimensions
  /// @param width Image width
  /// @param height Image height
  /// @return Number of mip levels (including base level)
  inline u32 calculate_mip_levels(u32 width, u32 height) {
    return static_cast<u32>(std::floor(std::log2(std::max(width, height)))) + 1;
  }

  struct ZPC_CORE_API VkTexture {
    VkTexture() noexcept = default;
    VkTexture(VkTexture &&o) noexcept
        : image{std::move(o.image)},
          sampler{o.sampler},
          imageLayout(o.imageLayout) {
      o.sampler = VK_NULL_HANDLE;
    }
    VkTexture &operator=(VkTexture &&o) {
      if (image) {
        auto &ctx = image.get().ctx;
        ctx.device.destroySampler(sampler, nullptr, ctx.dispatcher);
      }
      image = zs::move(o.image);
      sampler = o.sampler;
      o.sampler = VK_NULL_HANDLE;
      imageLayout = o.imageLayout;
      return *this;
    }
    VkTexture &operator=(const VkTexture &o) = delete;
    VkTexture(const VkTexture &o) = delete;

    ~VkTexture() { reset(); }
    void reset() {
      if (image) {
        auto &ctx = image.get().ctx;
        ctx.device.destroySampler(sampler, nullptr, ctx.dispatcher);
        image.reset();
      }
    }
    explicit operator bool() const noexcept { return static_cast<bool>(image); }

    vk::DescriptorImageInfo descriptorInfo() {
      return vk::DescriptorImageInfo{sampler, (vk::ImageView)image.get(), imageLayout};
    }

    /// @brief Get the number of mip levels in this texture
    u32 getMipLevels() const noexcept { 
      return image ? image.get().mipLevels : 1; 
    }

    Owner<Image> image{};  // including view
    vk::Sampler sampler{VK_NULL_HANDLE};
    vk::ImageLayout imageLayout;
  };

  /// @brief Load a texture from raw pixel data with optional mipmap generation
  /// @param ctx Vulkan context
  /// @param data Raw pixel data
  /// @param numBytes Size of pixel data in bytes
  /// @param extent Image dimensions
  /// @param format Pixel format
  /// @param layout Final image layout
  /// @param generateMipmaps Whether to generate mipmaps
  /// @return VkTexture with the loaded image and sampler
  ZPC_CORE_API VkTexture load_texture(VulkanContext &ctx, u8 *data, size_t numBytes, vk::Extent2D extent,
                         vk::Format format = vk::Format::eR8G8B8A8Unorm,
                         vk::ImageLayout layout = vk::ImageLayout::eShaderReadOnlyOptimal,
                         bool generateMipmaps = false);

  /// @brief Generate mipmaps for an existing image using blit commands
  /// @param ctx Vulkan context  
  /// @param image The image to generate mipmaps for (must have mipLevels > 1)
  /// @param width Base level width
  /// @param height Base level height
  /// @param mipLevels Number of mip levels to generate
  /// @param finalLayout The layout to transition to after mipmap generation
  ZPC_CORE_API void generate_mipmaps(VulkanContext &ctx, vk::Image image, 
                        u32 width, u32 height, u32 mipLevels,
                        vk::ImageLayout finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal);

}  // namespace zs