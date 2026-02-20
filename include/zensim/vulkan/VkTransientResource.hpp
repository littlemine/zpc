#pragma once
#include <string>
#include <unordered_map>
#include <vector>

#include "zensim/ZpcImplPattern.hpp"
#include "zensim/vulkan/VkContext.hpp"
#include "zensim/vulkan/VkImage.hpp"

namespace zs {

  // ============================================================================
  // Transient resource descriptors
  // ============================================================================

  /// @brief Description of a transient buffer resource (not yet allocated)
  struct TransientBufferDesc {
    vk::DeviceSize size{0};
    vk::BufferUsageFlags usage{};
    vk::MemoryPropertyFlags memoryProperties{vk::MemoryPropertyFlagBits::eDeviceLocal};

    bool operator==(const TransientBufferDesc& o) const noexcept {
      return size == o.size && usage == o.usage && memoryProperties == o.memoryProperties;
    }
    bool operator!=(const TransientBufferDesc& o) const noexcept { return !(*this == o); }

    /// @brief Check if an existing buffer satisfies this description
    bool isSatisfiedBy(const TransientBufferDesc& existing) const noexcept {
      return existing.size >= size && (existing.usage & usage) == usage
             && (existing.memoryProperties & memoryProperties) == memoryProperties;
    }
  };

  /// @brief Description of a transient image resource (not yet allocated)
  struct TransientImageDesc {
    vk::Extent3D extent{0, 0, 1};
    vk::Format format{vk::Format::eUndefined};
    vk::ImageUsageFlags usage{};
    vk::SampleCountFlagBits samples{vk::SampleCountFlagBits::e1};
    u32 mipLevels{1};
    u32 arrayLayers{1};
    vk::ImageType imageType{vk::ImageType::e2D};
    vk::MemoryPropertyFlags memoryProperties{vk::MemoryPropertyFlagBits::eDeviceLocal};

    bool operator==(const TransientImageDesc& o) const noexcept {
      return extent.width == o.extent.width && extent.height == o.extent.height
             && extent.depth == o.extent.depth && format == o.format && usage == o.usage
             && samples == o.samples && mipLevels == o.mipLevels && arrayLayers == o.arrayLayers
             && imageType == o.imageType && memoryProperties == o.memoryProperties;
    }
    bool operator!=(const TransientImageDesc& o) const noexcept { return !(*this == o); }

    /// @brief Check if an existing image satisfies this description
    bool isSatisfiedBy(const TransientImageDesc& existing) const noexcept {
      return existing.extent.width >= extent.width && existing.extent.height >= extent.height
             && existing.extent.depth >= extent.depth && existing.format == format
             && (existing.usage & usage) == usage && existing.samples == samples
             && existing.mipLevels >= mipLevels && existing.arrayLayers >= arrayLayers
             && existing.imageType == imageType
             && (existing.memoryProperties & memoryProperties) == memoryProperties;
    }

    /// @brief Create a 2D image description
    static TransientImageDesc image2D(u32 width, u32 height, vk::Format format,
                                      vk::ImageUsageFlags usage,
                                      vk::SampleCountFlagBits samples
                                      = vk::SampleCountFlagBits::e1) {
      TransientImageDesc desc{};
      desc.extent = vk::Extent3D{width, height, 1};
      desc.format = format;
      desc.usage = usage;
      desc.samples = samples;
      return desc;
    }

    /// @brief Create a 2D image description matching an extent
    static TransientImageDesc image2D(vk::Extent2D ext, vk::Format format,
                                      vk::ImageUsageFlags usage,
                                      vk::SampleCountFlagBits samples
                                      = vk::SampleCountFlagBits::e1) {
      return image2D(ext.width, ext.height, format, usage, samples);
    }
  };

  // ============================================================================
  // Transient resource entries (tagged union: buffer or image)
  // ============================================================================

  enum class transient_resource_type_e : u8 { buffer = 0, image };

  /// @brief A virtual resource handle used during render graph construction
  /// @note Does not own any GPU resource; purely a descriptor + bookkeeping tag
  struct TransientResourceEntry {
    transient_resource_type_e type;
    std::string name;

    union {
      TransientBufferDesc bufferDesc;
      TransientImageDesc imageDesc;
    };

    /// @brief Construct a transient buffer entry
    static TransientResourceEntry makeBuffer(std::string_view name,
                                             const TransientBufferDesc& desc) {
      TransientResourceEntry e{};
      e.type = transient_resource_type_e::buffer;
      e.name = name;
      e.bufferDesc = desc;
      return e;
    }

    /// @brief Construct a transient image entry
    static TransientResourceEntry makeImage(std::string_view name,
                                            const TransientImageDesc& desc) {
      TransientResourceEntry e{};
      e.type = transient_resource_type_e::image;
      e.name = name;
      e.imageDesc = desc;
      return e;
    }

    TransientResourceEntry() : type{transient_resource_type_e::buffer}, bufferDesc{} {}
    ~TransientResourceEntry() = default;
    TransientResourceEntry(const TransientResourceEntry& o)
        : type{o.type}, name{o.name} {
      if (type == transient_resource_type_e::buffer)
        bufferDesc = o.bufferDesc;
      else
        imageDesc = o.imageDesc;
    }
    TransientResourceEntry& operator=(const TransientResourceEntry& o) {
      type = o.type;
      name = o.name;
      if (type == transient_resource_type_e::buffer)
        bufferDesc = o.bufferDesc;
      else
        imageDesc = o.imageDesc;
      return *this;
    }
  };

  // ============================================================================
  // Transient resource pool (caching & reuse)
  // ============================================================================

  /// @brief Pool for caching and reusing transient GPU resources across frames
  /// @note Resources are keyed by their description; unused resources are recycled
  ///
  /// Usage pattern:
  /// @code
  /// TransientResourcePool pool(ctx);
  /// // At frame begin:
  /// pool.beginFrame();
  /// // Acquire resources:
  /// Buffer& buf = pool.acquireBuffer(bufDesc);
  /// Image& img = pool.acquireImage(imgDesc);
  /// // At frame end:
  /// pool.endFrame(); // marks resources for potential reuse
  /// @endcode
  struct ZPC_CORE_API TransientResourcePool {
    TransientResourcePool() = delete;
    explicit TransientResourcePool(VulkanContext& ctx) : ctx{ctx} {}

    TransientResourcePool(const TransientResourcePool&) = delete;
    TransientResourcePool& operator=(const TransientResourcePool&) = delete;
    TransientResourcePool(TransientResourcePool&&) = default;
    TransientResourcePool& operator=(TransientResourcePool&&) = default;
    ~TransientResourcePool() = default;

    /// @brief Begin a new frame; marks all cached resources as available for reuse
    void beginFrame() {
      for (auto& e : cachedBuffers) e.inUse = false;
      for (auto& e : cachedImages) e.inUse = false;
    }

    /// @brief End the current frame; evicts resources unused for too many frames
    /// @param maxUnusedFrames Resources unused for more than this many frames are freed
    void endFrame(u32 maxUnusedFrames = 4) {
      evict(cachedBuffers, maxUnusedFrames);
      evict(cachedImages, maxUnusedFrames);
    }

    /// @brief Acquire a buffer matching the given description
    /// @note Returns a reference into the pool; valid until the pool is destroyed or reset
    Buffer& acquireBuffer(const TransientBufferDesc& desc) {
      for (auto& e : cachedBuffers) {
        if (!e.inUse && e.desc.isSatisfiedBy(desc)) {
          e.inUse = true;
          e.unusedFrames = 0;
          return e.resource.get();
        }
      }
      auto& entry = cachedBuffers.emplace_back();
      entry.desc = desc;
      entry.desc.usage = desc.usage;
      entry.resource = ctx.createBuffer(desc.size, desc.usage, desc.memoryProperties);
      entry.inUse = true;
      entry.unusedFrames = 0;
      return entry.resource.get();
    }

    /// @brief Acquire an image matching the given description
    /// @note Returns a reference into the pool; valid until the pool is destroyed or reset
    Image& acquireImage(const TransientImageDesc& desc) {
      for (auto& e : cachedImages) {
        if (!e.inUse && e.desc.isSatisfiedBy(desc)) {
          e.inUse = true;
          e.unusedFrames = 0;
          return e.resource.get();
        }
      }
      auto imageCI = vk::ImageCreateInfo{}
                         .setImageType(desc.imageType)
                         .setFormat(desc.format)
                         .setExtent(desc.extent)
                         .setMipLevels(desc.mipLevels)
                         .setArrayLayers(desc.arrayLayers)
                         .setUsage(desc.usage)
                         .setSamples(desc.samples)
                         .setTiling(vk::ImageTiling::eOptimal)
                         .setSharingMode(vk::SharingMode::eExclusive);
      auto& entry = cachedImages.emplace_back();
      entry.desc = desc;
      entry.resource = ctx.createImage(imageCI, desc.memoryProperties, /*createView*/ true);
      entry.inUse = true;
      entry.unusedFrames = 0;
      return entry.resource.get();
    }

    /// @brief Release all cached resources
    void reset() {
      cachedBuffers.clear();
      cachedImages.clear();
    }

    VulkanContext& getContext() noexcept { return ctx; }
    const VulkanContext& getContext() const noexcept { return ctx; }

  private:
    template <typename EntryVec> static void evict(EntryVec& entries, u32 maxUnusedFrames) {
      for (auto it = entries.begin(); it != entries.end();) {
        if (!it->inUse) {
          it->unusedFrames++;
          if (it->unusedFrames > maxUnusedFrames) {
            it = entries.erase(it);
            continue;
          }
        }
        ++it;
      }
    }

    struct CachedBuffer {
      TransientBufferDesc desc{};
      Owner<Buffer> resource{};
      bool inUse{false};
      u32 unusedFrames{0};
    };

    struct CachedImage {
      TransientImageDesc desc{};
      Owner<Image> resource{};
      bool inUse{false};
      u32 unusedFrames{0};
    };

    VulkanContext& ctx;
    std::vector<CachedBuffer> cachedBuffers;
    std::vector<CachedImage> cachedImages;
  };

}  // namespace zs
