#include "zensim/vulkan/VkPipelineSerialization.hpp"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

#include "zensim/io/Filesystem.hpp"
#include "zensim/zpc_tpls/fmt/format.h"
#include "zensim/zpc_tpls/rapidhash/rapidhash.h"

// Vulkan SDK header version (compile-time constant)
#include <vulkan/vulkan_core.h>

namespace zs {

  // ============================================================================
  // DescFileHeader
  // ============================================================================

  static constexpr u32 kDescFileMagic = 0x4450535A;       // "ZSPD"
  static constexpr u32 kDescFileFormatVersion = 1;
  static constexpr u32 kZpcVersionEncoded =
      (0u << 22) | (0u << 12) | 0u;  // 0.0.0 -- update when project_info.in changes

  DescFileHeader current_desc_file_header() {
    DescFileHeader h;
    h.magic = kDescFileMagic;
    h.formatVersion = kDescFileFormatVersion;
    h.vkHeaderVersion = VK_HEADER_VERSION_COMPLETE;
    h.zpcVersion = kZpcVersionEncoded;
    return h;
  }

  void write_header(std::ostream& os, const DescFileHeader& hdr) {
    os.write(reinterpret_cast<const char*>(&hdr), sizeof(DescFileHeader));
  }

  void read_header(std::istream& is, DescFileHeader& hdr) {
    is.read(reinterpret_cast<char*>(&hdr), sizeof(DescFileHeader));
  }

  bool validate_header(const DescFileHeader& hdr, std::string* errMsg) {
    if (hdr.magic != kDescFileMagic) {
      if (errMsg) *errMsg = fmt::format("bad magic: expected 0x{:08X}, got 0x{:08X}",
                                        kDescFileMagic, hdr.magic);
      return false;
    }
    if (hdr.formatVersion != kDescFileFormatVersion) {
      if (errMsg)
        *errMsg = fmt::format("format version mismatch: expected {}, got {}",
                              kDescFileFormatVersion, hdr.formatVersion);
      return false;
    }
    if (hdr.vkHeaderVersion != VK_HEADER_VERSION_COMPLETE) {
      if (errMsg)
        *errMsg = fmt::format("Vulkan header version mismatch: expected 0x{:08X}, got 0x{:08X}",
                              static_cast<u32>(VK_HEADER_VERSION_COMPLETE), hdr.vkHeaderVersion);
      return false;
    }
    if (hdr.zpcVersion != kZpcVersionEncoded) {
      if (errMsg)
        *errMsg = fmt::format("zpc version mismatch: expected 0x{:08X}, got 0x{:08X}",
                              kZpcVersionEncoded, hdr.zpcVersion);
      return false;
    }
    return true;
  }

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
    write_string(os, desc.sourceKey);
  }
  void read_desc(std::istream& is, ShaderStageDesc& desc) {
    read_pod(is, desc.stage);
    read_pod_vector(is, desc.spirv);
    read_string(is, desc.entryPoint);
    read_string(is, desc.sourceKey);
  }

  // ============================================================================
  // GraphicsPipelineDesc
  // ============================================================================

  void write_desc(std::ostream& os, const GraphicsPipelineDesc& desc) {
    write_header(os, current_desc_file_header());
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
    DescFileHeader hdr;
    read_header(is, hdr);
    std::string err;
    if (!validate_header(hdr, &err))
      throw std::runtime_error("GraphicsPipelineDesc binary: " + err);
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
    write_header(os, current_desc_file_header());
    write_pod(os, desc.size);
    write_pod(os, desc.usage);
    write_pod(os, desc.memoryProperties);
  }
  void read_desc(std::istream& is, TransientBufferDesc& desc) {
    DescFileHeader hdr;
    read_header(is, hdr);
    std::string err;
    if (!validate_header(hdr, &err))
      throw std::runtime_error("TransientBufferDesc binary: " + err);
    read_pod(is, desc.size);
    read_pod(is, desc.usage);
    read_pod(is, desc.memoryProperties);
  }

  // ============================================================================
  // TransientImageDesc
  // ============================================================================

  void write_desc(std::ostream& os, const TransientImageDesc& desc) {
    write_header(os, current_desc_file_header());
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
    DescFileHeader hdr;
    read_header(is, hdr);
    std::string err;
    if (!validate_header(hdr, &err))
      throw std::runtime_error("TransientImageDesc binary: " + err);
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

  // ============================================================================
  // DescriptorCache
  // ============================================================================

  DescriptorCache::DescriptorCache(TempFileManager& tfm, std::string appTag)
      : _tfm{&tfm} {
    if (appTag.empty()) {
      auto path = abs_exe_path();
      auto h = rapidhash(path.data(), path.size());
      _subdir = fmt::format("app_{:016x}", h);
    } else {
      _subdir = std::move(appTag);
    }
    _tfm->ensureSubdirectoryExists(_subdir);
  }

  std::string DescriptorCache::binaryPath(uint64_t key) const {
    return _tfm->resolve(fmt::format("{}/pso_{:016x}.bin", _subdir, key));
  }

  std::string DescriptorCache::jsonPath(uint64_t key) const {
    return _tfm->resolve(fmt::format("{}/pso_{:016x}.json", _subdir, key));
  }

  bool DescriptorCache::load(uint64_t key, GraphicsPipelineDesc& desc) const {
    // try binary first
    {
      auto path = binaryPath(key);
      std::ifstream ifs(path, std::ios::binary);
      if (ifs.good()) {
        try {
          read_desc(ifs, desc);
          if (hash_desc(desc) == key) return true;
        } catch (...) { /* fall through to JSON */ }
      }
    }
    // try JSON fallback
    {
      auto path = jsonPath(key);
      std::ifstream ifs(path);
      if (ifs.good()) {
        try {
          read_json(ifs, desc);
          if (hash_desc(desc) == key) return true;
        } catch (...) { /* cache miss */ }
      }
    }
    return false;
  }

  void DescriptorCache::save(uint64_t key, const GraphicsPipelineDesc& desc) const {
    _tfm->ensureSubdirectoryExists(_subdir);
    // write binary
    {
      auto path = binaryPath(key);
      std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
      if (ofs.good()) write_desc(ofs, desc);
    }
    // write JSON (human-readable)
    {
      auto path = jsonPath(key);
      std::ofstream ofs(path, std::ios::trunc);
      if (ofs.good()) write_json(ofs, desc);
    }
  }

  bool DescriptorCache::exists(uint64_t key) const {
    std::error_code ec;
    return fs::exists(binaryPath(key), ec) || fs::exists(jsonPath(key), ec);
  }

  void DescriptorCache::remove(uint64_t key) const {
    std::error_code ec;
    fs::remove(binaryPath(key), ec);
    fs::remove(jsonPath(key), ec);
  }

  void DescriptorCache::clearAll() const {
    std::error_code ec;
    auto dir = _tfm->resolve(_subdir);
    if (!fs::exists(dir, ec)) return;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
      if (entry.is_regular_file(ec)) fs::remove(entry.path(), ec);
    }
  }

  // ============================================================================
  // PipelineCacheManager
  // ============================================================================

  PipelineCacheManager::PipelineCacheManager(VulkanContext& ctx, TempFileManager& tfm,
                                             std::string appTag)
      : _ctx{&ctx}, _tfm{&tfm}, _cache{VK_NULL_HANDLE} {
    _tfm->ensureSubdirectoryExists(appTag);
    _filePath = _tfm->resolve(fmt::format("{}/vk_pipeline.pso", appTag));

    auto blob = loadBlob();
    vk::PipelineCacheCreateInfo ci{};
    if (!blob.empty()) {
      ci.setInitialDataSize(blob.size());
      ci.setPInitialData(blob.data());
    }
    _cache = _ctx->device.createPipelineCache(ci, nullptr, _ctx->dispatcher);
  }

  PipelineCacheManager::~PipelineCacheManager() {
    if (_cache && _ctx) {
      try {
        save();
      } catch (...) { /* best-effort */ }
      _ctx->device.destroyPipelineCache(_cache, nullptr, _ctx->dispatcher);
    }
  }

  PipelineCacheManager::PipelineCacheManager(PipelineCacheManager&& o) noexcept
      : _ctx{o._ctx},
        _tfm{o._tfm},
        _filePath{std::move(o._filePath)},
        _cache{o._cache} {
    o._cache = VK_NULL_HANDLE;
    o._ctx = nullptr;
  }

  PipelineCacheManager& PipelineCacheManager::operator=(PipelineCacheManager&& o) noexcept {
    if (this != &o) {
      if (_cache && _ctx)
        _ctx->device.destroyPipelineCache(_cache, nullptr, _ctx->dispatcher);
      _ctx = o._ctx;
      _tfm = o._tfm;
      _filePath = std::move(o._filePath);
      _cache = o._cache;
      o._cache = VK_NULL_HANDLE;
      o._ctx = nullptr;
    }
    return *this;
  }

  std::vector<uint8_t> PipelineCacheManager::loadBlob() const {
    std::error_code ec;
    if (!fs::exists(_filePath, ec)) return {};
    std::ifstream ifs(_filePath, std::ios::binary | std::ios::ate);
    if (!ifs.good()) return {};
    auto size = static_cast<size_t>(ifs.tellg());
    if (size == 0) return {};
    ifs.seekg(0);
    std::vector<uint8_t> data(size);
    ifs.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(size));
    return data;
  }

  void PipelineCacheManager::save() const {
    if (!_cache || !_ctx) return;
    size_t dataSize = 0;
    if (_ctx->device.getPipelineCacheData(_cache, &dataSize, nullptr, _ctx->dispatcher)
        != vk::Result::eSuccess)
      return;
    if (dataSize == 0) return;
    std::vector<uint8_t> data(dataSize);
    if (_ctx->device.getPipelineCacheData(_cache, &dataSize, data.data(), _ctx->dispatcher)
        != vk::Result::eSuccess)
      return;
    std::ofstream ofs(_filePath, std::ios::binary | std::ios::trunc);
    if (ofs.good())
      ofs.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(dataSize));
  }

}  // namespace zs
