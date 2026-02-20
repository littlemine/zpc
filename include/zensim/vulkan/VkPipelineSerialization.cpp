#include "zensim/vulkan/VkPipelineSerialization.hpp"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>

#include "zensim/io/Filesystem.hpp"
#include "zensim/zpc_tpls/fmt/format.h"
#include "zensim/zpc_tpls/rapidhash/rapidhash.h"

namespace zs {

  // ============================================================================
  // Helpers
  // ============================================================================

  namespace {
    template <typename T> void write_pod(std::ostream& os, const T& v) {
      static_assert(std::is_trivially_copyable_v<T>);
      os.write(reinterpret_cast<const char*>(&v), sizeof(T));
    }
    template <typename T> void read_pod(std::istream& is, T& v) {
      static_assert(std::is_trivially_copyable_v<T>);
      is.read(reinterpret_cast<char*>(&v), sizeof(T));
    }
    template <typename T> void write_pod_vector(std::ostream& os, const std::vector<T>& v) {
      static_assert(std::is_trivially_copyable_v<T>);
      u32 count = static_cast<u32>(v.size());
      write_pod(os, count);
      if (count > 0) os.write(reinterpret_cast<const char*>(v.data()), count * sizeof(T));
    }
    template <typename T> void read_pod_vector(std::istream& is, std::vector<T>& v) {
      static_assert(std::is_trivially_copyable_v<T>);
      u32 count = 0;
      read_pod(is, count);
      v.resize(count);
      if (count > 0) is.read(reinterpret_cast<char*>(v.data()), count * sizeof(T));
    }
    void write_string(std::ostream& os, const std::string& s) {
      u32 len = static_cast<u32>(s.size());
      write_pod(os, len);
      if (len > 0) os.write(s.data(), len);
    }
    void read_string(std::istream& is, std::string& s) {
      u32 len = 0;
      read_pod(is, len);
      s.resize(len);
      if (len > 0) is.read(s.data(), len);
    }
  }  // namespace

  // ============================================================================
  // VertexInputStateDesc
  // ============================================================================

  void write_desc(std::ostream& os, const VertexInputStateDesc& desc) {
    write_pod_vector(os, desc.bindings);
    write_pod_vector(os, desc.attributes);
  }
  void read_desc(std::istream& is, VertexInputStateDesc& desc) {
    read_pod_vector(is, desc.bindings);
    read_pod_vector(is, desc.attributes);
  }

  // ============================================================================
  // InputAssemblyStateDesc
  // ============================================================================

  void write_desc(std::ostream& os, const InputAssemblyStateDesc& desc) {
    write_pod(os, desc.topology);
    write_pod(os, desc.primitiveRestartEnable);
  }
  void read_desc(std::istream& is, InputAssemblyStateDesc& desc) {
    read_pod(is, desc.topology);
    read_pod(is, desc.primitiveRestartEnable);
  }

  // ============================================================================
  // ViewportStateDesc
  // ============================================================================

  void write_desc(std::ostream& os, const ViewportStateDesc& desc) {
    write_pod(os, desc.viewportCount);
    write_pod(os, desc.scissorCount);
  }
  void read_desc(std::istream& is, ViewportStateDesc& desc) {
    read_pod(is, desc.viewportCount);
    read_pod(is, desc.scissorCount);
  }

  // ============================================================================
  // RasterizationStateDesc
  // ============================================================================

  void write_desc(std::ostream& os, const RasterizationStateDesc& desc) {
    write_pod(os, desc.depthClampEnable);
    write_pod(os, desc.rasterizerDiscardEnable);
    write_pod(os, desc.polygonMode);
    write_pod(os, desc.cullMode);
    write_pod(os, desc.frontFace);
    write_pod(os, desc.depthBiasEnable);
    write_pod(os, desc.lineWidth);
    write_pod(os, desc.depthBiasConstantFactor);
    write_pod(os, desc.depthBiasClamp);
    write_pod(os, desc.depthBiasSlopeFactor);
  }
  void read_desc(std::istream& is, RasterizationStateDesc& desc) {
    read_pod(is, desc.depthClampEnable);
    read_pod(is, desc.rasterizerDiscardEnable);
    read_pod(is, desc.polygonMode);
    read_pod(is, desc.cullMode);
    read_pod(is, desc.frontFace);
    read_pod(is, desc.depthBiasEnable);
    read_pod(is, desc.lineWidth);
    read_pod(is, desc.depthBiasConstantFactor);
    read_pod(is, desc.depthBiasClamp);
    read_pod(is, desc.depthBiasSlopeFactor);
  }

  // ============================================================================
  // MultisampleStateDesc
  // ============================================================================

  void write_desc(std::ostream& os, const MultisampleStateDesc& desc) {
    write_pod(os, desc.sampleShadingEnable);
    write_pod(os, desc.rasterizationSamples);
    write_pod(os, desc.minSampleShading);
    write_pod(os, desc.alphaToCoverageEnable);
    write_pod(os, desc.alphaToOneEnable);
  }
  void read_desc(std::istream& is, MultisampleStateDesc& desc) {
    read_pod(is, desc.sampleShadingEnable);
    read_pod(is, desc.rasterizationSamples);
    read_pod(is, desc.minSampleShading);
    read_pod(is, desc.alphaToCoverageEnable);
    read_pod(is, desc.alphaToOneEnable);
  }

  // ============================================================================
  // DepthStencilStateDesc
  // ============================================================================

  void write_desc(std::ostream& os, const DepthStencilStateDesc& desc) {
    write_pod(os, desc.depthTestEnable);
    write_pod(os, desc.depthWriteEnable);
    write_pod(os, desc.depthCompareOp);
    write_pod(os, desc.depthBoundsTestEnable);
    write_pod(os, desc.stencilTestEnable);
    write_pod(os, desc.front);
    write_pod(os, desc.back);
    write_pod(os, desc.minDepthBounds);
    write_pod(os, desc.maxDepthBounds);
  }
  void read_desc(std::istream& is, DepthStencilStateDesc& desc) {
    read_pod(is, desc.depthTestEnable);
    read_pod(is, desc.depthWriteEnable);
    read_pod(is, desc.depthCompareOp);
    read_pod(is, desc.depthBoundsTestEnable);
    read_pod(is, desc.stencilTestEnable);
    read_pod(is, desc.front);
    read_pod(is, desc.back);
    read_pod(is, desc.minDepthBounds);
    read_pod(is, desc.maxDepthBounds);
  }

  // ============================================================================
  // ColorBlendStateDesc
  // ============================================================================

  void write_desc(std::ostream& os, const ColorBlendStateDesc& desc) {
    write_pod(os, desc.logicOpEnable);
    write_pod(os, desc.logicOp);
    write_pod_vector(os, desc.attachments);
    write_pod(os, desc.blendConstants);
  }
  void read_desc(std::istream& is, ColorBlendStateDesc& desc) {
    read_pod(is, desc.logicOpEnable);
    read_pod(is, desc.logicOp);
    read_pod_vector(is, desc.attachments);
    read_pod(is, desc.blendConstants);
  }

  // ============================================================================
  // DynamicStateDesc
  // ============================================================================

  void write_desc(std::ostream& os, const DynamicStateDesc& desc) {
    write_pod_vector(os, desc.states);
  }
  void read_desc(std::istream& is, DynamicStateDesc& desc) {
    read_pod_vector(is, desc.states);
  }

  // ============================================================================
  // ShaderStageDesc
  // ============================================================================

  void write_desc(std::ostream& os, const ShaderStageDesc& desc) {
    write_pod(os, desc.stage);
    write_pod_vector(os, desc.spirv);
    write_string(os, desc.entryPoint);
  }
  void read_desc(std::istream& is, ShaderStageDesc& desc) {
    read_pod(is, desc.stage);
    read_pod_vector(is, desc.spirv);
    read_string(is, desc.entryPoint);
  }

  // ============================================================================
  // GraphicsPipelineDesc
  // ============================================================================

  void write_desc(std::ostream& os, const GraphicsPipelineDesc& desc) {
    u32 numStages = static_cast<u32>(desc.shaderStages.size());
    write_pod(os, numStages);
    for (const auto& stage : desc.shaderStages) write_desc(os, stage);

    write_desc(os, desc.vertexInput);
    write_desc(os, desc.inputAssembly);
    write_desc(os, desc.viewport);
    write_desc(os, desc.rasterization);
    write_desc(os, desc.multisample);
    write_desc(os, desc.depthStencil);
    write_desc(os, desc.colorBlend);
    write_desc(os, desc.dynamicState);

    write_pod_vector(os, desc.pushConstantRanges);
    write_pod(os, desc.subpass);
  }
  void read_desc(std::istream& is, GraphicsPipelineDesc& desc) {
    u32 numStages = 0;
    read_pod(is, numStages);
    desc.shaderStages.resize(numStages);
    for (auto& stage : desc.shaderStages) read_desc(is, stage);

    read_desc(is, desc.vertexInput);
    read_desc(is, desc.inputAssembly);
    read_desc(is, desc.viewport);
    read_desc(is, desc.rasterization);
    read_desc(is, desc.multisample);
    read_desc(is, desc.depthStencil);
    read_desc(is, desc.colorBlend);
    read_desc(is, desc.dynamicState);

    read_pod_vector(is, desc.pushConstantRanges);
    read_pod(is, desc.subpass);
  }

  // ============================================================================
  // TransientBufferDesc
  // ============================================================================

  void write_desc(std::ostream& os, const TransientBufferDesc& desc) {
    write_pod(os, desc.size);
    write_pod(os, desc.usage);
    write_pod(os, desc.memoryProperties);
  }
  void read_desc(std::istream& is, TransientBufferDesc& desc) {
    read_pod(is, desc.size);
    read_pod(is, desc.usage);
    read_pod(is, desc.memoryProperties);
  }

  // ============================================================================
  // TransientImageDesc
  // ============================================================================

  void write_desc(std::ostream& os, const TransientImageDesc& desc) {
    write_pod(os, desc.extent);
    write_pod(os, desc.format);
    write_pod(os, desc.usage);
    write_pod(os, desc.samples);
    write_pod(os, desc.mipLevels);
    write_pod(os, desc.arrayLayers);
    write_pod(os, desc.imageType);
    write_pod(os, desc.memoryProperties);
  }
  void read_desc(std::istream& is, TransientImageDesc& desc) {
    read_pod(is, desc.extent);
    read_pod(is, desc.format);
    read_pod(is, desc.usage);
    read_pod(is, desc.samples);
    read_pod(is, desc.mipLevels);
    read_pod(is, desc.arrayLayers);
    read_pod(is, desc.imageType);
    read_pod(is, desc.memoryProperties);
  }

  // ============================================================================
  // Hashing helpers
  // ============================================================================

  namespace {
    template <typename T> uint64_t hash_pod(const T& v, uint64_t seed) {
      static_assert(std::is_trivially_copyable_v<T>);
      return rapidhash_withSeed(&v, sizeof(T), seed);
    }
    template <typename T> uint64_t hash_pod_vector(const std::vector<T>& v, uint64_t seed) {
      static_assert(std::is_trivially_copyable_v<T>);
      seed = hash_pod(static_cast<u32>(v.size()), seed);
      if (!v.empty()) seed = rapidhash_withSeed(v.data(), v.size() * sizeof(T), seed);
      return seed;
    }
    uint64_t hash_string(const std::string& s, uint64_t seed) {
      seed = hash_pod(static_cast<u32>(s.size()), seed);
      if (!s.empty()) seed = rapidhash_withSeed(s.data(), s.size(), seed);
      return seed;
    }
  }  // namespace

  // ============================================================================
  // Hashing for pipeline state descriptors
  // ============================================================================

  uint64_t hash_desc(const VertexInputStateDesc& desc, uint64_t seed) {
    seed = hash_pod_vector(desc.bindings, seed);
    seed = hash_pod_vector(desc.attributes, seed);
    return seed;
  }

  uint64_t hash_desc(const InputAssemblyStateDesc& desc, uint64_t seed) {
    seed = hash_pod(desc.topology, seed);
    seed = hash_pod(desc.primitiveRestartEnable, seed);
    return seed;
  }

  uint64_t hash_desc(const ViewportStateDesc& desc, uint64_t seed) {
    seed = hash_pod(desc.viewportCount, seed);
    seed = hash_pod(desc.scissorCount, seed);
    return seed;
  }

  uint64_t hash_desc(const RasterizationStateDesc& desc, uint64_t seed) {
    seed = hash_pod(desc.depthClampEnable, seed);
    seed = hash_pod(desc.rasterizerDiscardEnable, seed);
    seed = hash_pod(desc.polygonMode, seed);
    seed = hash_pod(desc.cullMode, seed);
    seed = hash_pod(desc.frontFace, seed);
    seed = hash_pod(desc.depthBiasEnable, seed);
    seed = hash_pod(desc.lineWidth, seed);
    seed = hash_pod(desc.depthBiasConstantFactor, seed);
    seed = hash_pod(desc.depthBiasClamp, seed);
    seed = hash_pod(desc.depthBiasSlopeFactor, seed);
    return seed;
  }

  uint64_t hash_desc(const MultisampleStateDesc& desc, uint64_t seed) {
    seed = hash_pod(desc.sampleShadingEnable, seed);
    seed = hash_pod(desc.rasterizationSamples, seed);
    seed = hash_pod(desc.minSampleShading, seed);
    seed = hash_pod(desc.alphaToCoverageEnable, seed);
    seed = hash_pod(desc.alphaToOneEnable, seed);
    return seed;
  }

  uint64_t hash_desc(const DepthStencilStateDesc& desc, uint64_t seed) {
    seed = hash_pod(desc.depthTestEnable, seed);
    seed = hash_pod(desc.depthWriteEnable, seed);
    seed = hash_pod(desc.depthCompareOp, seed);
    seed = hash_pod(desc.depthBoundsTestEnable, seed);
    seed = hash_pod(desc.stencilTestEnable, seed);
    seed = hash_pod(desc.front, seed);
    seed = hash_pod(desc.back, seed);
    seed = hash_pod(desc.minDepthBounds, seed);
    seed = hash_pod(desc.maxDepthBounds, seed);
    return seed;
  }

  uint64_t hash_desc(const ColorBlendStateDesc& desc, uint64_t seed) {
    seed = hash_pod(desc.logicOpEnable, seed);
    seed = hash_pod(desc.logicOp, seed);
    seed = hash_pod_vector(desc.attachments, seed);
    seed = hash_pod(desc.blendConstants, seed);
    return seed;
  }

  uint64_t hash_desc(const DynamicStateDesc& desc, uint64_t seed) {
    seed = hash_pod_vector(desc.states, seed);
    return seed;
  }

  uint64_t hash_desc(const ShaderStageDesc& desc, uint64_t seed) {
    seed = hash_pod(desc.stage, seed);
    seed = hash_pod_vector(desc.spirv, seed);
    seed = hash_string(desc.entryPoint, seed);
    return seed;
  }

  uint64_t hash_desc(const GraphicsPipelineDesc& desc, uint64_t seed) {
    seed = hash_pod(static_cast<u32>(desc.shaderStages.size()), seed);
    for (const auto& stage : desc.shaderStages) seed = hash_desc(stage, seed);
    seed = hash_desc(desc.vertexInput, seed);
    seed = hash_desc(desc.inputAssembly, seed);
    seed = hash_desc(desc.viewport, seed);
    seed = hash_desc(desc.rasterization, seed);
    seed = hash_desc(desc.multisample, seed);
    seed = hash_desc(desc.depthStencil, seed);
    seed = hash_desc(desc.colorBlend, seed);
    seed = hash_desc(desc.dynamicState, seed);
    seed = hash_pod_vector(desc.pushConstantRanges, seed);
    seed = hash_pod(desc.subpass, seed);
    return seed;
  }

  // ============================================================================
  // Hashing for transient resource descriptors
  // ============================================================================

  uint64_t hash_desc(const TransientBufferDesc& desc, uint64_t seed) {
    seed = hash_pod(desc.size, seed);
    seed = hash_pod(desc.usage, seed);
    seed = hash_pod(desc.memoryProperties, seed);
    return seed;
  }

  uint64_t hash_desc(const TransientImageDesc& desc, uint64_t seed) {
    seed = hash_pod(desc.extent, seed);
    seed = hash_pod(desc.format, seed);
    seed = hash_pod(desc.usage, seed);
    seed = hash_pod(desc.samples, seed);
    seed = hash_pod(desc.mipLevels, seed);
    seed = hash_pod(desc.arrayLayers, seed);
    seed = hash_pod(desc.imageType, seed);
    seed = hash_pod(desc.memoryProperties, seed);
    return seed;
  }

  // ============================================================================
  // TempFileManager
  // ============================================================================

  namespace fs = std::filesystem;

  TempFileManager::TempFileManager(std::string rootDir) {
    if (rootDir.empty())
      _rootDir = abs_exe_directory() + "/tmp/zs_vk_cache";
    else
      _rootDir = std::move(rootDir);
  }

  void TempFileManager::setRootDirectory(std::string dir) { _rootDir = std::move(dir); }

  std::string TempFileManager::resolve(std::string_view relativePath) const {
    fs::path root(_rootDir);
    fs::path rel(relativePath);
    return (root / rel).string();
  }

  bool TempFileManager::ensureDirectoryExists() const {
    std::error_code ec;
    if (fs::exists(_rootDir, ec)) return true;
    return fs::create_directories(_rootDir, ec);
  }

  bool TempFileManager::ensureSubdirectoryExists(std::string_view subdir) const {
    std::error_code ec;
    auto path = fs::path(_rootDir) / subdir;
    if (fs::exists(path, ec)) return true;
    return fs::create_directories(path, ec);
  }

  bool TempFileManager::removeFile(std::string_view relativePath) const {
    std::error_code ec;
    auto path = resolve(relativePath);
    if (!fs::exists(path, ec)) return true;
    return fs::remove(path, ec);
  }

  size_t TempFileManager::clearAll() const {
    std::error_code ec;
    if (!fs::exists(_rootDir, ec)) return 0;
    size_t count = 0;
    for (const auto& entry : fs::directory_iterator(_rootDir, ec)) {
      if (entry.is_regular_file(ec)) {
        if (fs::remove(entry.path(), ec)) ++count;
      }
    }
    return count;
  }

  TempFileManager& default_temp_file_manager() {
    static TempFileManager instance{};
    return instance;
  }

}  // namespace zs
