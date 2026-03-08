#include "zensim/vulkan/VkShaderManager.hpp"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "zensim/zpc_tpls/fmt/format.h"
#include "zensim/zpc_tpls/rapidhash/rapidhash.h"

namespace zs {

  // ============================================================================
  // Private helpers
  // ============================================================================

  std::string ShaderManager::sourceHashKey(const char* src, size_t len,
                                           vk::ShaderStageFlagBits stage) {
    auto stageVal = static_cast<uint32_t>(stage);
    uint64_t h = rapidhash_withSeed(src, len, rapidhash(&stageVal, sizeof(stageVal)));
    return fmt::format("__src_{:016x}", h);
  }

  std::string ShaderManager::readFileContents(const std::string& filePath) {
    std::ifstream ifs(filePath, std::ios::binary);
    if (!ifs.good())
      throw std::runtime_error(fmt::format("ShaderManager: cannot open file '{}'", filePath));
    std::ostringstream ss;
    ss << ifs.rdbuf();
    return ss.str();
  }

  std::string ShaderManager::absolutePath(const std::string& filePath) {
    return std::filesystem::absolute(filePath).string();
  }

  void ShaderManager::insertStageDesc(const std::string& key, vk::ShaderStageFlagBits stage,
                                      std::vector<u32> spirv, std::string entryPoint) {
    if (_stageDescs.find(key) != _stageDescs.end()) return;
    ShaderStageDesc sd;
    sd.stage = stage;
    sd.spirv = std::move(spirv);
    sd.entryPoint = std::move(entryPoint);
    sd.sourceKey = key;
    _stageDescs[key] = std::move(sd);
  }

  // ============================================================================
  // Label-based loading
  // ============================================================================

  ShaderModule& ShaderManager::loadFromGlsl(const std::string& label,
                                            vk::ShaderStageFlagBits stage,
                                            const char* glslCode,
                                            std::string_view moduleName) {
    if (auto it = _modules.find(label); it != _modules.end()) return it->second.get();
    auto name = moduleName.empty() ? std::string_view{label} : moduleName;

    auto spirvVec = _ctx.compileGlslToSpirv(glslCode, stage, name);
    _modules[label] = _ctx.createShaderModule(spirvVec.data(), spirvVec.size(), stage);
    insertStageDesc(label, stage, std::move(spirvVec), "main");
    return _modules[label].get();
  }

  ShaderModule& ShaderManager::loadFromHlsl(const std::string& label,
                                            vk::ShaderStageFlagBits stage,
                                            const char* hlslCode,
                                            std::string_view moduleName,
                                            std::string_view entryPoint) {
    if (auto it = _modules.find(label); it != _modules.end()) return it->second.get();
    auto name = moduleName.empty() ? std::string_view{label} : moduleName;

    auto spirvVec = _ctx.compileHlslToSpirv(hlslCode, stage, name, entryPoint);
    auto mod = _ctx.createShaderModule(spirvVec.data(), spirvVec.size(), stage);
    mod.setEntryPoint(std::string(entryPoint));
    _modules[label] = std::move(mod);
    insertStageDesc(label, stage, std::move(spirvVec), std::string(entryPoint));
    return _modules[label].get();
  }

  ShaderModule& ShaderManager::loadFromSpirv(const std::string& label,
                                             vk::ShaderStageFlagBits stage,
                                             const u32* spirvCode, size_t spirvWordCount) {
    if (auto it = _modules.find(label); it != _modules.end()) return it->second.get();
    _modules[label] = _ctx.createShaderModule(spirvCode, spirvWordCount, stage);
    insertStageDesc(label, stage,
                    std::vector<u32>(spirvCode, spirvCode + spirvWordCount), "main");
    return _modules[label].get();
  }

  // ============================================================================
  // File-based loading (absolute path as key)
  // ============================================================================

  ShaderModule& ShaderManager::loadFromGlslFile(const std::string& filePath,
                                                vk::ShaderStageFlagBits stage) {
    auto key = absolutePath(filePath);
    if (auto it = _modules.find(key); it != _modules.end()) return it->second.get();

    auto src = readFileContents(filePath);
    auto spirvVec = _ctx.compileGlslToSpirv(src.c_str(), stage, key);
    _modules[key] = _ctx.createShaderModule(spirvVec.data(), spirvVec.size(), stage);
    insertStageDesc(key, stage, std::move(spirvVec), "main");
    return _modules[key].get();
  }

  ShaderModule& ShaderManager::loadFromHlslFile(const std::string& filePath,
                                                vk::ShaderStageFlagBits stage,
                                                std::string_view entryPoint) {
    auto key = absolutePath(filePath);
    if (auto it = _modules.find(key); it != _modules.end()) return it->second.get();

    auto src = readFileContents(filePath);
    auto spirvVec = _ctx.compileHlslToSpirv(src.c_str(), stage, key, entryPoint);
    auto mod = _ctx.createShaderModule(spirvVec.data(), spirvVec.size(), stage);
    mod.setEntryPoint(std::string(entryPoint));
    _modules[key] = std::move(mod);
    insertStageDesc(key, stage, std::move(spirvVec), std::string(entryPoint));
    return _modules[key].get();
  }

  ShaderModule& ShaderManager::loadFromSpirvFile(const std::string& filePath,
                                                 vk::ShaderStageFlagBits stage) {
    auto key = absolutePath(filePath);
    if (auto it = _modules.find(key); it != _modules.end()) return it->second.get();

    auto data = readFileContents(filePath);
    if (data.size() % sizeof(u32) != 0)
      throw std::runtime_error(
          fmt::format("ShaderManager: SPIR-V file '{}' size not a multiple of 4", filePath));
    auto wordCount = data.size() / sizeof(u32);
    auto* words = reinterpret_cast<const u32*>(data.data());
    _modules[key] = _ctx.createShaderModule(words, wordCount, stage);
    insertStageDesc(key, stage,
                    std::vector<u32>(words, words + wordCount), "main");
    return _modules[key].get();
  }

  // ============================================================================
  // Source-hash-based loading (auto-derived key)
  // ============================================================================

  ShaderModule& ShaderManager::loadGlsl(vk::ShaderStageFlagBits stage,
                                        const char* glslCode,
                                        std::string_view moduleName) {
    auto key = sourceHashKey(glslCode, std::strlen(glslCode), stage);
    return loadFromGlsl(key, stage, glslCode, moduleName);
  }

  ShaderModule& ShaderManager::loadHlsl(vk::ShaderStageFlagBits stage,
                                        const char* hlslCode,
                                        std::string_view moduleName,
                                        std::string_view entryPoint) {
    auto key = sourceHashKey(hlslCode, std::strlen(hlslCode), stage);
    return loadFromHlsl(key, stage, hlslCode, moduleName, entryPoint);
  }

  // ============================================================================
  // Module lookup / bookkeeping
  // ============================================================================

  ShaderModule& ShaderManager::get(const std::string& label) {
    auto it = _modules.find(label);
    if (it == _modules.end())
      throw std::runtime_error(fmt::format("ShaderManager: no module with label '{}'", label));
    return it->second.get();
  }

  const ShaderModule& ShaderManager::get(const std::string& label) const {
    auto it = _modules.find(label);
    if (it == _modules.end())
      throw std::runtime_error(fmt::format("ShaderManager: no module with label '{}'", label));
    return it->second.get();
  }

  bool ShaderManager::contains(const std::string& label) const noexcept {
    return _modules.find(label) != _modules.end();
  }

  bool ShaderManager::erase(const std::string& label) {
    _stageDescs.erase(label);
    return _modules.erase(label) > 0;
  }

  void ShaderManager::clear() noexcept {
    _modules.clear();
    _stageDescs.clear();
  }

  // ============================================================================
  // ShaderStageDesc access
  // ============================================================================

  const ShaderStageDesc& ShaderManager::getStageDesc(const std::string& label) const {
    auto it = _stageDescs.find(label);
    if (it == _stageDescs.end())
      throw std::runtime_error(
          fmt::format("ShaderManager: no ShaderStageDesc with label '{}'", label));
    return it->second;
  }

  bool ShaderManager::containsStageDesc(const std::string& label) const noexcept {
    return _stageDescs.find(label) != _stageDescs.end();
  }

  const ShaderStageDesc& ShaderManager::stageDescFromGlsl(const std::string& label,
                                                          vk::ShaderStageFlagBits stage,
                                                          const char* glslCode,
                                                          std::string_view moduleName) {
    if (auto it = _stageDescs.find(label); it != _stageDescs.end()) return it->second;
    loadFromGlsl(label, stage, glslCode, moduleName);
    return _stageDescs.at(label);
  }

  const ShaderStageDesc& ShaderManager::stageDescFromHlsl(const std::string& label,
                                                          vk::ShaderStageFlagBits stage,
                                                          const char* hlslCode,
                                                          std::string_view moduleName,
                                                          std::string_view entryPoint) {
    if (auto it = _stageDescs.find(label); it != _stageDescs.end()) return it->second;
    loadFromHlsl(label, stage, hlslCode, moduleName, entryPoint);
    return _stageDescs.at(label);
  }

  const ShaderStageDesc& ShaderManager::stageDescFromSpirv(const std::string& label,
                                                           vk::ShaderStageFlagBits stage,
                                                           const u32* spirvCode,
                                                           size_t spirvWordCount,
                                                           std::string_view entryPoint) {
    if (auto it = _stageDescs.find(label); it != _stageDescs.end()) return it->second;
    insertStageDesc(label, stage,
                    std::vector<u32>(spirvCode, spirvCode + spirvWordCount),
                    std::string(entryPoint));
    if (_modules.find(label) == _modules.end()) {
      _modules[label] = _ctx.createShaderModule(spirvCode, spirvWordCount, stage);
      _modules[label].get().setEntryPoint(std::string(entryPoint));
    }
    return _stageDescs.at(label);
  }

  const ShaderStageDesc& ShaderManager::stageDescFromGlslFile(const std::string& filePath,
                                                              vk::ShaderStageFlagBits stage) {
    auto key = absolutePath(filePath);
    if (auto it = _stageDescs.find(key); it != _stageDescs.end()) return it->second;
    loadFromGlslFile(filePath, stage);
    return _stageDescs.at(key);
  }

  const ShaderStageDesc& ShaderManager::stageDescFromHlslFile(const std::string& filePath,
                                                              vk::ShaderStageFlagBits stage,
                                                              std::string_view entryPoint) {
    auto key = absolutePath(filePath);
    if (auto it = _stageDescs.find(key); it != _stageDescs.end()) return it->second;
    loadFromHlslFile(filePath, stage, entryPoint);
    return _stageDescs.at(key);
  }

  const ShaderStageDesc& ShaderManager::stageDescFromSpirvFile(const std::string& filePath,
                                                               vk::ShaderStageFlagBits stage) {
    auto key = absolutePath(filePath);
    if (auto it = _stageDescs.find(key); it != _stageDescs.end()) return it->second;
    loadFromSpirvFile(filePath, stage);
    return _stageDescs.at(key);
  }

}  // namespace zs

