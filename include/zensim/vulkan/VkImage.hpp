#pragma once

#include "zensim/vulkan/VkBuffer.hpp"

namespace zs {

  struct VkTexture;
  struct ZPC_CORE_API Image {
    Image() = delete;
    Image(VulkanContext &ctx, vk::Image img = VK_NULL_HANDLE)
        : ctx{ctx},
          image{img},
#if ZS_VULKAN_USE_VMA
          allocation{0},
#else
          pmem{},
#endif
          pview{},
          usage{},
          extent{},
          mipLevels{1} {
    }
    Image(Image &&o) noexcept
        : ctx{o.ctx},
          image{o.image},
#if ZS_VULKAN_USE_VMA
          allocation{o.allocation},
#else
          pmem{std::move(o.pmem)},
#endif
          pview{std::move(o.pview)},
          usage{o.usage},
          extent{o.extent},
          mipLevels{o.mipLevels} {
      o.pview = {};
      o.image = VK_NULL_HANDLE;
#if ZS_VULKAN_USE_VMA
      o.allocation = 0;
#else
      pmem.reset();
#endif
      o.pview.reset();
    }
    ~Image() {
      if (pview.has_value()) {
        ctx.device.destroyImageView(*pview, nullptr, ctx.dispatcher);
        pview.reset();
      }
      ctx.device.destroyImage(image, nullptr, ctx.dispatcher);
      image = VK_NULL_HANDLE;
#if ZS_VULKAN_USE_VMA
      vmaFreeMemory(ctx.allocator(), allocation);
#else
      pmem.reset();
#endif
    }

    vk::Image operator*() const { return image; }
    operator vk::Image() const { return image; }
    operator vk::ImageView() const { return *pview; }
#if ZS_VULKAN_USE_VMA
    VkMemoryRange memory() const {
      VmaAllocationInfo allocInfo;
      vmaGetAllocationInfo(ctx.allocator(), allocation, &allocInfo);

      VkMemoryRange memRange;
      memRange.memory = allocInfo.deviceMemory;
      memRange.offset = allocInfo.offset;
      memRange.size = allocInfo.size;
      return memRange;
    }
#else
    const VkMemory &memory() const { return *pmem; }
#endif
    bool hasView() const { return static_cast<bool>(pview); }
    const vk::ImageView &view() const { return *pview; }

    vk::DeviceSize getSize() const noexcept {
#if ZS_VULKAN_USE_VMA
      VmaAllocationInfo allocInfo;
      vmaGetAllocationInfo(ctx.allocator(), allocation, &allocInfo);
      return allocInfo.size;
#else
      return memory().memSize;
#endif
    }
    vk::Extent3D getExtent() const noexcept { return extent; }

  protected:
    friend struct VulkanContext;
    friend struct VkTexture;

    VulkanContext &ctx;
    vk::Image image;
#if ZS_VULKAN_USE_VMA
    VmaAllocation allocation;
#else
    std::shared_ptr<VkMemory> pmem;
#endif

    std::optional<vk::ImageView> pview;
    vk::ImageUsageFlags usage;
    vk::Extent3D extent;
    u32 mipLevels;
  };

  struct ImageView {
    ImageView() = delete;
    ImageView(VulkanContext &ctx, vk::ImageView imgv = VK_NULL_HANDLE) : ctx{ctx}, imgv{imgv} {}
    
    ImageView(const ImageView &) = delete;
    ImageView &operator=(const ImageView &) = delete;
    
    ImageView(ImageView &&o) noexcept : ctx{o.ctx}, imgv{o.imgv} { o.imgv = VK_NULL_HANDLE; }
    
    ImageView &operator=(ImageView &&o) noexcept {
      if (this != &o) {
        if (&ctx != &o.ctx)
          throw std::runtime_error("unable to move-assign vk image view due to ctx mismatch");
        reset();
        imgv = o.imgv;
        o.imgv = VK_NULL_HANDLE;
      }
      return *this;
    }
    
    ~ImageView() { reset(); }
    
    /// @brief Release the image view resource
    void reset() {
      if (imgv != VK_NULL_HANDLE) {
        ctx.device.destroyImageView(imgv, nullptr, ctx.dispatcher);
        imgv = VK_NULL_HANDLE;
      }
    }

    /// @brief Check if the image view is valid
    bool isValid() const noexcept { return imgv != VK_NULL_HANDLE; }
    explicit operator bool() const noexcept { return isValid(); }

    /// @brief Access the underlying vk::ImageView handle
    vk::ImageView operator*() const { return imgv; }
    operator vk::ImageView() const { return imgv; }
    vk::ImageView get() const noexcept { return imgv; }

    VulkanContext &getContext() noexcept { return ctx; }
    const VulkanContext &getContext() const noexcept { return ctx; }

  protected:
    friend struct VulkanContext;

    VulkanContext &ctx;
    vk::ImageView imgv;
  };

  /// @brief RAII wrapper for vk::Sampler supporting separated image/sampler descriptor usage
  /// @note Can be used standalone for VK_DESCRIPTOR_TYPE_SAMPLER descriptors
  struct ZPC_CORE_API ImageSampler {
    ImageSampler() = delete;
    ImageSampler(VulkanContext &ctx, vk::Sampler sampler = VK_NULL_HANDLE)
        : ctx{ctx}, sampler{sampler} {}
    
    ImageSampler(const ImageSampler &) = delete;
    ImageSampler &operator=(const ImageSampler &) = delete;
    
    ImageSampler(ImageSampler &&o) noexcept : ctx{o.ctx}, sampler{o.sampler} {
      o.sampler = VK_NULL_HANDLE;
    }
    
    ImageSampler &operator=(ImageSampler &&o) noexcept {
      if (this != &o) {
        if (&ctx != &o.ctx) 
          throw std::runtime_error("unable to move-assign vk sampler due to ctx mismatch");
        reset();
        sampler = o.sampler;
        o.sampler = VK_NULL_HANDLE;
      }
      return *this;
    }
    
    ~ImageSampler() { reset(); }
    
    /// @brief Release the sampler resource
    void reset() {
      if (sampler != VK_NULL_HANDLE) {
        ctx.device.destroySampler(sampler, nullptr, ctx.dispatcher);
        sampler = VK_NULL_HANDLE;
      }
    }

    /// @brief Check if the sampler is valid
    bool isValid() const noexcept { return sampler != VK_NULL_HANDLE; }
    explicit operator bool() const noexcept { return isValid(); }

    /// @brief Access the underlying vk::Sampler handle
    vk::Sampler operator*() const { return sampler; }
    operator vk::Sampler() const { return sampler; }
    vk::Sampler get() const noexcept { return sampler; }
    
    /// @brief Get descriptor info for separate sampler binding (VK_DESCRIPTOR_TYPE_SAMPLER)
    /// @note imageView and imageLayout are ignored for sampler-only descriptors
    vk::DescriptorImageInfo descriptorInfo() const {
      return vk::DescriptorImageInfo{sampler, VK_NULL_HANDLE, vk::ImageLayout::eUndefined};
    }
    
    /// @brief Get descriptor info with specified image view for combined image sampler
    vk::DescriptorImageInfo descriptorInfo(vk::ImageView imageView, 
                                            vk::ImageLayout layout = vk::ImageLayout::eShaderReadOnlyOptimal) const {
      return vk::DescriptorImageInfo{sampler, imageView, layout};
    }

    VulkanContext &getContext() noexcept { return ctx; }
    const VulkanContext &getContext() const noexcept { return ctx; }

  protected:
    friend struct VulkanContext;

    VulkanContext &ctx;
    vk::Sampler sampler;
  };

  struct Framebuffer {
    Framebuffer() = delete;
    Framebuffer(VulkanContext &ctx, vk::Framebuffer fb = VK_NULL_HANDLE)
        : ctx{ctx}, framebuffer{fb} {}
    
    Framebuffer(const Framebuffer &) = delete;
    Framebuffer &operator=(const Framebuffer &) = delete;
    
    Framebuffer(Framebuffer &&o) noexcept : ctx{o.ctx}, framebuffer{o.framebuffer} {
      o.framebuffer = VK_NULL_HANDLE;
    }
    
    Framebuffer &operator=(Framebuffer &&o) noexcept {
      if (this != &o) {
        if (&ctx != &o.ctx)
          throw std::runtime_error("unable to move-assign vk framebuffer due to ctx mismatch");
        reset();
        framebuffer = o.framebuffer;
        o.framebuffer = VK_NULL_HANDLE;
      }
      return *this;
    }
    
    ~Framebuffer() { reset(); }
    
    /// @brief Release the framebuffer resource
    void reset() {
      if (framebuffer != VK_NULL_HANDLE) {
        ctx.device.destroyFramebuffer(framebuffer, nullptr, ctx.dispatcher);
        framebuffer = VK_NULL_HANDLE;
      }
    }

    /// @brief Check if the framebuffer is valid
    bool isValid() const noexcept { return framebuffer != VK_NULL_HANDLE; }
    explicit operator bool() const noexcept { return isValid(); }

    /// @brief Access the underlying vk::Framebuffer handle
    vk::Framebuffer operator*() const { return framebuffer; }
    operator vk::Framebuffer() const { return framebuffer; }
    vk::Framebuffer get() const noexcept { return framebuffer; }

    VulkanContext &getContext() noexcept { return ctx; }
    const VulkanContext &getContext() const noexcept { return ctx; }

  protected:
    friend struct VulkanContext;

    VulkanContext &ctx;
    vk::Framebuffer framebuffer;
  };

}  // namespace zs