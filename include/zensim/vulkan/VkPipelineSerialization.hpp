#pragma once
#pragma once
#include <cstdint>
#include <iosfwd>
#include <string>

#include "zensim/vulkan/VkPipeline.hpp"
#include "zensim/vulkan/VkTransientResource.hpp"

namespace zs {

  // ============================================================================
  // Descriptor file version metadata
  // ============================================================================

  /// @brief Header written at the start of every serialized descriptor stream.
  /// Encodes a magic number, a format version, the Vulkan SDK header version,
  /// and the zpc library version so that consumers can reject files produced by
  /// an incompatible toolchain.
  struct DescFileHeader {
    /// Fixed identification bytes: ASCII "ZSPD" (ZenSim Pipeline Descriptor)
    u32 magic{0x4450535A};
    /// Format revision.  Bump this when the binary / JSON layout changes.
    u32 formatVersion{1};
    /// VK_HEADER_VERSION_COMPLETE captured at compile time.
    u32 vkHeaderVersion{0};
    /// zpc version encoded as (major << 22) | (minor << 12) | patch.
    u32 zpcVersion{0};
  };

  /// @brief Build a DescFileHeader populated with the current compile-time versions.
  ZPC_CORE_API DescFileHeader current_desc_file_header();

  /// @brief Write the header to a binary stream.
  ZPC_CORE_API void write_header(std::ostream& os, const DescFileHeader& hdr);

  /// @brief Read a header from a binary stream.
  ZPC_CORE_API void read_header(std::istream& is, DescFileHeader& hdr);

  /// @brief Validate a previously-read header against the current build.
  /// @param hdr The header read from the stream.
  /// @param[out] errMsg Human-readable reason on failure.
  /// @return true if the header is compatible with this build.
  ZPC_CORE_API bool validate_header(const DescFileHeader& hdr, std::string* errMsg = nullptr);

  /// @brief Write the header as a JSON "_meta" object.
  ZPC_CORE_API void write_json_meta(std::ostream& os, const DescFileHeader& hdr, int indent = 0);

  /// @brief Read and validate the "_meta" JSON object.  Returns the parsed header.
  /// Fields not present in the stream are left at their default (0) values.
  ZPC_CORE_API DescFileHeader read_json_meta(std::istream& is);

  // ============================================================================
  // Binary serialization for pipeline state descriptors
  // ============================================================================

  ZPC_CORE_API void write_desc(std::ostream& os, const VertexInputStateDesc& desc);
  ZPC_CORE_API void read_desc(std::istream& is, VertexInputStateDesc& desc);

  ZPC_CORE_API void write_desc(std::ostream& os, const InputAssemblyStateDesc& desc);
  ZPC_CORE_API void read_desc(std::istream& is, InputAssemblyStateDesc& desc);

  ZPC_CORE_API void write_desc(std::ostream& os, const ViewportStateDesc& desc);
  ZPC_CORE_API void read_desc(std::istream& is, ViewportStateDesc& desc);

  ZPC_CORE_API void write_desc(std::ostream& os, const RasterizationStateDesc& desc);
  ZPC_CORE_API void read_desc(std::istream& is, RasterizationStateDesc& desc);

  ZPC_CORE_API void write_desc(std::ostream& os, const MultisampleStateDesc& desc);
  ZPC_CORE_API void read_desc(std::istream& is, MultisampleStateDesc& desc);

  ZPC_CORE_API void write_desc(std::ostream& os, const DepthStencilStateDesc& desc);
  ZPC_CORE_API void read_desc(std::istream& is, DepthStencilStateDesc& desc);

  ZPC_CORE_API void write_desc(std::ostream& os, const ColorBlendStateDesc& desc);
  ZPC_CORE_API void read_desc(std::istream& is, ColorBlendStateDesc& desc);

  ZPC_CORE_API void write_desc(std::ostream& os, const DynamicStateDesc& desc);
  ZPC_CORE_API void read_desc(std::istream& is, DynamicStateDesc& desc);

  ZPC_CORE_API void write_desc(std::ostream& os, const ShaderStageDesc& desc);
  ZPC_CORE_API void read_desc(std::istream& is, ShaderStageDesc& desc);

  ZPC_CORE_API void write_desc(std::ostream& os, const GraphicsPipelineDesc& desc);
  ZPC_CORE_API void read_desc(std::istream& is, GraphicsPipelineDesc& desc);

  // ============================================================================
  // Binary serialization for transient resource descriptors
  // ============================================================================

  ZPC_CORE_API void write_desc(std::ostream& os, const TransientBufferDesc& desc);
  ZPC_CORE_API void read_desc(std::istream& is, TransientBufferDesc& desc);

  ZPC_CORE_API void write_desc(std::ostream& os, const TransientImageDesc& desc);
  ZPC_CORE_API void read_desc(std::istream& is, TransientImageDesc& desc);

  // ============================================================================
  // Hashing for pipeline state descriptors (rapidhash-based)
  // ============================================================================

  ZPC_CORE_API uint64_t hash_desc(const VertexInputStateDesc& desc, uint64_t seed = 0);
  ZPC_CORE_API uint64_t hash_desc(const InputAssemblyStateDesc& desc, uint64_t seed = 0);
  ZPC_CORE_API uint64_t hash_desc(const ViewportStateDesc& desc, uint64_t seed = 0);
  ZPC_CORE_API uint64_t hash_desc(const RasterizationStateDesc& desc, uint64_t seed = 0);
  ZPC_CORE_API uint64_t hash_desc(const MultisampleStateDesc& desc, uint64_t seed = 0);
  ZPC_CORE_API uint64_t hash_desc(const DepthStencilStateDesc& desc, uint64_t seed = 0);
  ZPC_CORE_API uint64_t hash_desc(const ColorBlendStateDesc& desc, uint64_t seed = 0);
  ZPC_CORE_API uint64_t hash_desc(const DynamicStateDesc& desc, uint64_t seed = 0);
  ZPC_CORE_API uint64_t hash_desc(const ShaderStageDesc& desc, uint64_t seed = 0);
  ZPC_CORE_API uint64_t hash_desc(const GraphicsPipelineDesc& desc, uint64_t seed = 0);

  // ============================================================================
  // Hashing for transient resource descriptors (rapidhash-based)
  // ============================================================================

  ZPC_CORE_API uint64_t hash_desc(const TransientBufferDesc& desc, uint64_t seed = 0);
  ZPC_CORE_API uint64_t hash_desc(const TransientImageDesc& desc, uint64_t seed = 0);

  // ============================================================================
  // JSON serialization for pipeline state descriptors
  // ============================================================================

  ZPC_CORE_API void write_json(std::ostream& os, const VertexInputStateDesc& desc, int indent = 0);
  ZPC_CORE_API void read_json(std::istream& is, VertexInputStateDesc& desc);

  ZPC_CORE_API void write_json(std::ostream& os, const InputAssemblyStateDesc& desc, int indent = 0);
  ZPC_CORE_API void read_json(std::istream& is, InputAssemblyStateDesc& desc);

  ZPC_CORE_API void write_json(std::ostream& os, const ViewportStateDesc& desc, int indent = 0);
  ZPC_CORE_API void read_json(std::istream& is, ViewportStateDesc& desc);

  ZPC_CORE_API void write_json(std::ostream& os, const RasterizationStateDesc& desc, int indent = 0);
  ZPC_CORE_API void read_json(std::istream& is, RasterizationStateDesc& desc);

  ZPC_CORE_API void write_json(std::ostream& os, const MultisampleStateDesc& desc, int indent = 0);
  ZPC_CORE_API void read_json(std::istream& is, MultisampleStateDesc& desc);

  ZPC_CORE_API void write_json(std::ostream& os, const DepthStencilStateDesc& desc, int indent = 0);
  ZPC_CORE_API void read_json(std::istream& is, DepthStencilStateDesc& desc);

  ZPC_CORE_API void write_json(std::ostream& os, const ColorBlendStateDesc& desc, int indent = 0);
  ZPC_CORE_API void read_json(std::istream& is, ColorBlendStateDesc& desc);

  ZPC_CORE_API void write_json(std::ostream& os, const DynamicStateDesc& desc, int indent = 0);
  ZPC_CORE_API void read_json(std::istream& is, DynamicStateDesc& desc);

  ZPC_CORE_API void write_json(std::ostream& os, const ShaderStageDesc& desc, int indent = 0);
  ZPC_CORE_API void read_json(std::istream& is, ShaderStageDesc& desc);

  ZPC_CORE_API void write_json(std::ostream& os, const GraphicsPipelineDesc& desc, int indent = 0);
  ZPC_CORE_API void read_json(std::istream& is, GraphicsPipelineDesc& desc);

  // ============================================================================
  // JSON serialization for transient resource descriptors
  // ============================================================================

  ZPC_CORE_API void write_json(std::ostream& os, const TransientBufferDesc& desc, int indent = 0);
  ZPC_CORE_API void read_json(std::istream& is, TransientBufferDesc& desc);

  ZPC_CORE_API void write_json(std::ostream& os, const TransientImageDesc& desc, int indent = 0);
  ZPC_CORE_API void read_json(std::istream& is, TransientImageDesc& desc);

  // ============================================================================
  // Temporary file location management
  // ============================================================================

  /// @brief Manages temporary file locations for pipeline caches, SPIR-V blobs, etc.
  /// @note Uses a configurable root directory; defaults to a platform temp folder.
  struct ZPC_CORE_API TempFileManager {
    /// @brief Initialize with an optional root directory.
    /// If empty, defaults to <exe_directory>/tmp/zs_vk_cache/
    explicit TempFileManager(std::string rootDir = {});
    ~TempFileManager() = default;

    TempFileManager(const TempFileManager&) = default;
    TempFileManager& operator=(const TempFileManager&) = default;
    TempFileManager(TempFileManager&&) = default;
    TempFileManager& operator=(TempFileManager&&) = default;

    /// @brief Get the root temp directory (created on demand)
    const std::string& rootDirectory() const noexcept { return _rootDir; }

    /// @brief Set a new root directory
    void setRootDirectory(std::string dir);

    /// @brief Resolve a relative filename under the root temp directory
    std::string resolve(std::string_view relativePath) const;

    /// @brief Ensure the root directory exists (creates it if needed)
    /// @return true if the directory exists or was successfully created
    bool ensureDirectoryExists() const;

    /// @brief Ensure a subdirectory under root exists
    /// @return true if the directory exists or was successfully created
    bool ensureSubdirectoryExists(std::string_view subdir) const;

    /// @brief Remove a specific temporary file
    /// @return true if the file was successfully removed or didn't exist
    bool removeFile(std::string_view relativePath) const;

    /// @brief Remove all files in the root temp directory
    /// @return number of files removed
    size_t clearAll() const;

  private:
    std::string _rootDir;
  };

  /// @brief Get the default global TempFileManager instance
  ZPC_CORE_API TempFileManager& default_temp_file_manager();

  // ============================================================================
  // Descriptor disk cache
  // ============================================================================

  /// @brief Manages on-disk caching of pipeline descriptors keyed by 64-bit hash.
  ///
  /// Stores both binary (.bin) and human-readable JSON (.json) representations.
  /// On load the binary file is tried first for speed; if absent or invalid the
  /// JSON file is tried as fallback.
  struct ZPC_CORE_API DescriptorCache {
    /// @brief Construct with a TempFileManager and an application-specific tag.
    /// The tag (e.g. an exe checksum) is used as a subdirectory under the temp root
    /// so that different application builds don't collide.
    explicit DescriptorCache(TempFileManager& tfm, std::string appTag = {});
    ~DescriptorCache() = default;

    DescriptorCache(const DescriptorCache&) = default;
    DescriptorCache& operator=(const DescriptorCache&) = default;
    DescriptorCache(DescriptorCache&&) = default;
    DescriptorCache& operator=(DescriptorCache&&) = default;

    /// @brief Try to load a GraphicsPipelineDesc for the given key.
    /// Checks binary first, then JSON.  Returns true on success.
    bool load(uint64_t key, GraphicsPipelineDesc& desc) const;

    /// @brief Save a GraphicsPipelineDesc under the given key (binary + JSON).
    void save(uint64_t key, const GraphicsPipelineDesc& desc) const;

    /// @brief Check whether a cached entry exists for the key.
    bool exists(uint64_t key) const;

    /// @brief Remove cached files for a given key.
    void remove(uint64_t key) const;

    /// @brief Remove all cached entries.
    void clearAll() const;

    /// @brief Get the subdirectory used by this cache.
    const std::string& subdirectory() const noexcept { return _subdir; }

  private:
    std::string binaryPath(uint64_t key) const;
    std::string jsonPath(uint64_t key) const;

    TempFileManager* _tfm;
    std::string _subdir;
  };

  // ============================================================================
  // Vulkan pipeline cache (driver-level PSO blob)
  // ============================================================================

  struct VulkanContext;

  /// @brief Manages a Vulkan VkPipelineCache object backed by an on-disk file.
  ///
  /// The Vulkan pipeline cache stores driver-specific compiled pipeline data
  /// (shader microcode, hardware state).  When supplied to vkCreateGraphicsPipelines
  /// the driver can skip redundant compilation, significantly reducing pipeline
  /// creation time on subsequent application launches.
  ///
  /// Lifecycle:
  ///   1. Construct -- creates or loads a VkPipelineCache from the .pso file.
  ///   2. Use -- pass handle() to pipeline creation calls.
  ///   3. Flush -- call save() or rely on destructor to write back to disk.
  struct ZPC_CORE_API PipelineCacheManager {
    /// @brief Create or load a pipeline cache for the given context.
    /// @param ctx       The Vulkan context whose device owns the cache.
    /// @param tfm       TempFileManager for resolving the cache file path.
    /// @param appTag    Application-specific subdirectory tag.
    PipelineCacheManager(VulkanContext& ctx, TempFileManager& tfm, std::string appTag);
    ~PipelineCacheManager();

    PipelineCacheManager(const PipelineCacheManager&) = delete;
    PipelineCacheManager& operator=(const PipelineCacheManager&) = delete;
    PipelineCacheManager(PipelineCacheManager&& o) noexcept;
    PipelineCacheManager& operator=(PipelineCacheManager&& o) noexcept;

    /// @brief Get the underlying VkPipelineCache handle.
    vk::PipelineCache handle() const noexcept { return _cache; }

    /// @brief Flush the current cache contents to disk.
    /// Called automatically by the destructor.
    void save() const;

  private:
    std::vector<uint8_t> loadBlob() const;

    VulkanContext* _ctx;
    TempFileManager* _tfm;
    std::string _filePath;
    vk::PipelineCache _cache;
  };

}  // namespace zs
