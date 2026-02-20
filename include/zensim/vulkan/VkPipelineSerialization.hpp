#pragma once
#include <cstdint>
#include <iosfwd>
#include <string>

#include "zensim/vulkan/VkPipeline.hpp"
#include "zensim/vulkan/VkTransientResource.hpp"

namespace zs {

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

}  // namespace zs
