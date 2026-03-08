#pragma once
#include <map>
#include <string>

#include "zensim/ZpcImplPattern.hpp"
#include "zensim/vulkan/VkPipeline.hpp"
#include "zensim/vulkan/VkShader.hpp"

namespace zs {

  /// @brief Manages named ShaderModule instances for a single VulkanContext.
  ///
  /// Provides compile-from-source (GLSL/HLSL) and load-from-SPIR-V, with deduplication
  /// by a user-supplied string label.  All modules are destroyed when the manager is
  /// destroyed or when the owning context is torn down.
  ///
  /// In addition to managing live Vulkan ShaderModule handles, the manager maintains
  /// a parallel cache of ShaderStageDesc objects (keyed by the same label) containing
  /// the compiled SPIR-V.  This allows callers to build fully self-contained
  /// GraphicsPipelineDesc objects suitable for disk-cache round-trips.
  ///
  /// Key derivation:
  ///   - **Label-based** (loadFromGlsl, loadFromHlsl, loadFromSpirv) -- the caller
  ///     supplies a string label used as the deduplication key.
  ///   - **File-based** (loadFromGlslFile, loadFromHlslFile, loadFromSpirvFile) --
  ///     the absolute file path is used as the deduplication key.
  ///   - **Source-hash-based** (loadGlsl, loadHlsl) -- a hash of the source code
  ///     string is used as the deduplication key, avoiding redundant compilation
  ///     when the same source is loaded from different call sites.
  struct ZPC_CORE_API ShaderManager {
    explicit ShaderManager(VulkanContext& ctx) : _ctx{ctx} {}
    ~ShaderManager() = default;
    ShaderManager(ShaderManager&&) = default;
    ShaderManager& operator=(ShaderManager&&) = default;
    ShaderManager(const ShaderManager&) = delete;
    ShaderManager& operator=(const ShaderManager&) = delete;

    // ---- label-based loading ----

    /// @brief Compile GLSL source and register the resulting module under @p label.
    /// If @p label already exists the existing module is returned and the source is
    /// *not* recompiled.
    /// @return reference to the (possibly pre-existing) ShaderModule.
    ShaderModule& loadFromGlsl(const std::string& label,
                               vk::ShaderStageFlagBits stage,
                               const char* glslCode,
                               std::string_view moduleName = {});

    /// @brief Compile HLSL source and register the resulting module under @p label.
    ShaderModule& loadFromHlsl(const std::string& label,
                               vk::ShaderStageFlagBits stage,
                               const char* hlslCode,
                               std::string_view moduleName = {},
                               std::string_view entryPoint = "main");

    /// @brief Create a module from pre-compiled SPIR-V and register under @p label.
    ShaderModule& loadFromSpirv(const std::string& label,
                                vk::ShaderStageFlagBits stage,
                                const u32* spirvCode, size_t spirvWordCount);

    // ---- file-based loading (absolute path as key) ----

    /// @brief Load and compile a GLSL shader from a file.
    /// The absolute file path is used as the deduplication key.
    /// @param filePath  Path to the .glsl / .vert / .frag / etc. file.
    /// @return reference to the (possibly pre-existing) ShaderModule.
    ShaderModule& loadFromGlslFile(const std::string& filePath,
                                   vk::ShaderStageFlagBits stage);

    /// @brief Load and compile an HLSL shader from a file.
    /// The absolute file path is used as the deduplication key.
    ShaderModule& loadFromHlslFile(const std::string& filePath,
                                   vk::ShaderStageFlagBits stage,
                                   std::string_view entryPoint = "main");

    /// @brief Load a pre-compiled SPIR-V shader from a file.
    /// The absolute file path is used as the deduplication key.
    ShaderModule& loadFromSpirvFile(const std::string& filePath,
                                    vk::ShaderStageFlagBits stage);

    // ---- source-hash-based loading (auto-derived key) ----

    /// @brief Compile GLSL source using a hash of the source code as the key.
    /// Identical source strings will hit the cache and skip recompilation.
    /// @return reference to the (possibly pre-existing) ShaderModule.
    ShaderModule& loadGlsl(vk::ShaderStageFlagBits stage,
                           const char* glslCode,
                           std::string_view moduleName = {});

    /// @brief Compile HLSL source using a hash of the source code as the key.
    ShaderModule& loadHlsl(vk::ShaderStageFlagBits stage,
                           const char* hlslCode,
                           std::string_view moduleName = {},
                           std::string_view entryPoint = "main");

    // ---- module lookup / bookkeeping ----

    /// @brief Look up a previously registered module.  Throws if not found.
    ShaderModule& get(const std::string& label);
    const ShaderModule& get(const std::string& label) const;

    /// @brief Check whether a label has been registered.
    bool contains(const std::string& label) const noexcept;

    /// @brief Remove a single entry (destroying the ShaderModule).
    /// @return true if the entry existed and was removed.
    bool erase(const std::string& label);

    /// @brief Destroy all managed modules.
    void clear() noexcept;

    /// @brief Number of managed modules.
    size_t size() const noexcept { return _modules.size(); }

    // ---- ShaderStageDesc access ----

    /// @brief Retrieve the cached ShaderStageDesc for a previously loaded shader.
    /// Throws if @p label was not registered via a load* method.
    const ShaderStageDesc& getStageDesc(const std::string& label) const;

    /// @brief Check whether a ShaderStageDesc is cached for @p label.
    bool containsStageDesc(const std::string& label) const noexcept;

    /// @brief Build a ShaderStageDesc from GLSL, caching the compiled SPIR-V.
    /// If the label already exists the previously compiled desc is returned.
    const ShaderStageDesc& stageDescFromGlsl(const std::string& label,
                                             vk::ShaderStageFlagBits stage,
                                             const char* glslCode,
                                             std::string_view moduleName = {});

    /// @brief Build a ShaderStageDesc from HLSL, caching the compiled SPIR-V.
    const ShaderStageDesc& stageDescFromHlsl(const std::string& label,
                                             vk::ShaderStageFlagBits stage,
                                             const char* hlslCode,
                                             std::string_view moduleName = {},
                                             std::string_view entryPoint = "main");

    /// @brief Build a ShaderStageDesc from pre-compiled SPIR-V.
    const ShaderStageDesc& stageDescFromSpirv(const std::string& label,
                                              vk::ShaderStageFlagBits stage,
                                              const u32* spirvCode, size_t spirvWordCount,
                                              std::string_view entryPoint = "main");

    /// @brief Build a ShaderStageDesc from a GLSL file (absolute path as key).
    const ShaderStageDesc& stageDescFromGlslFile(const std::string& filePath,
                                                 vk::ShaderStageFlagBits stage);

    /// @brief Build a ShaderStageDesc from an HLSL file (absolute path as key).
    const ShaderStageDesc& stageDescFromHlslFile(const std::string& filePath,
                                                 vk::ShaderStageFlagBits stage,
                                                 std::string_view entryPoint = "main");

    /// @brief Build a ShaderStageDesc from a SPIR-V file (absolute path as key).
    const ShaderStageDesc& stageDescFromSpirvFile(const std::string& filePath,
                                                  vk::ShaderStageFlagBits stage);

    VulkanContext& context() noexcept { return _ctx; }
    const VulkanContext& context() const noexcept { return _ctx; }

  private:
    static std::string sourceHashKey(const char* src, size_t len,
                                     vk::ShaderStageFlagBits stage);
    static std::string readFileContents(const std::string& filePath);
    static std::string absolutePath(const std::string& filePath);

    void insertStageDesc(const std::string& key, vk::ShaderStageFlagBits stage,
                         std::vector<u32> spirv, std::string entryPoint);

    VulkanContext& _ctx;
    std::map<std::string, Owner<ShaderModule>> _modules;
    std::map<std::string, ShaderStageDesc> _stageDescs;
  };

}  // namespace zs
