#include "zensim/vulkan/VkShader.hpp"

#include <shaderc/shaderc.hpp>
#include <spirv_cross/spirv_glsl.hpp>

#include "zensim/vulkan/Vulkan.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <filesystem>

// DXC (DirectX Shader Compiler) for HLSL support
#if defined(_WIN32) || defined(_WIN64)
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <unknwn.h>
#  include <dxc/dxcapi.h>
#  include <wrl/client.h>
#  define ZS_HAS_DXC_API 1
#else
#  define ZS_HAS_DXC_API 0
#endif

namespace zs {

  static std::string reflect_basetype_string(spirv_cross::SPIRType::BaseType t) {
    switch (t) {
      case spirv_cross::SPIRType::Unknown:
        return "unknown";
      case spirv_cross::SPIRType::Void:
        return "void";
      case spirv_cross::SPIRType::Boolean:
        return "boolean";
      case spirv_cross::SPIRType::SByte:
        return "signed byte";
      case spirv_cross::SPIRType::UByte:
        return "unsigned byte";
      case spirv_cross::SPIRType::Short:
        return "short";
      case spirv_cross::SPIRType::UShort:
        return "unsigned short";
      case spirv_cross::SPIRType::Int:
        return "int";
      case spirv_cross::SPIRType::UInt:
        return "unsigned int";
      case spirv_cross::SPIRType::Int64:
        return "int64";
      case spirv_cross::SPIRType::UInt64:
        return "unsigned int64";
      case spirv_cross::SPIRType::AtomicCounter:
        return "atomic counter";
      case spirv_cross::SPIRType::Half:
        return "half";
      case spirv_cross::SPIRType::Float:
        return "float";
      case spirv_cross::SPIRType::Double:
        return "double";
      case spirv_cross::SPIRType::Struct:
        return "struct";
      case spirv_cross::SPIRType::Image:
        return "image";
      case spirv_cross::SPIRType::SampledImage:
        return "sampled image";
      case spirv_cross::SPIRType::Sampler:
        return "sampler";

      case spirv_cross::SPIRType::Char:
        return "char";
      default:;
    }
    return "wtf type";
  }
  static vk::Format reflect_basetype_vkformat(spirv_cross::SPIRType::BaseType t, u32 dim) {
    switch (t) {
      case spirv_cross::SPIRType::Boolean:
        return deduce_attribute_format(wrapt<bool>{}, dim);
      case spirv_cross::SPIRType::SByte:
        return deduce_attribute_format(wrapt<i8>{}, dim);
      case spirv_cross::SPIRType::UByte:
        return deduce_attribute_format(wrapt<u8>{}, dim);
      case spirv_cross::SPIRType::Short:
        return deduce_attribute_format(wrapt<short>{}, dim);
      case spirv_cross::SPIRType::UShort:
        return deduce_attribute_format(wrapt<unsigned short>{}, dim);
      case spirv_cross::SPIRType::Int:
        return deduce_attribute_format(wrapt<int>{}, dim);
      case spirv_cross::SPIRType::UInt:
        return deduce_attribute_format(wrapt<unsigned int>{}, dim);
      case spirv_cross::SPIRType::Int64:
        return deduce_attribute_format(wrapt<i64>{}, dim);
      case spirv_cross::SPIRType::UInt64:
        return deduce_attribute_format(wrapt<u64>{}, dim);
      // case spirv_cross::SPIRType::Half:
      //  return deduce_attribute_format(wrapt<i64>{}, dim);
      case spirv_cross::SPIRType::Float:
        return deduce_attribute_format(wrapt<float>{}, dim);
      case spirv_cross::SPIRType::Double:
        return deduce_attribute_format(wrapt<double>{}, dim);
      case spirv_cross::SPIRType::Char:
        return deduce_attribute_format(wrapt<char>{}, dim);
      default:;
    }
    return vk::Format::eUndefined;
  }

  static void display_resource(const spirv_cross::CompilerGLSL &glsl,
                               const spirv_cross::ShaderResources &resources) {
    auto displayBindingInfo = [&glsl](const auto &resources, std::string_view tag) {
      for (auto &resource : resources) {
        // spirv-cross/spirv_common.hpp spirv_cross.hpp main.cpp
        unsigned set = glsl.get_decoration(resource.id, spv::DecorationDescriptorSet);
        unsigned binding = glsl.get_decoration(resource.id, spv::DecorationBinding);
        unsigned location = glsl.get_decoration(resource.id, spv::DecorationLocation);

        const spirv_cross::SPIRType &type = glsl.get_type(resource.type_id);
        u32 typeArraySize = type.array.size();
        u32 count = typeArraySize == 0 ? 1 : type.array[0];
        auto typestr = reflect_basetype_string(type.basetype);

        fmt::print(
            "[{}] {} at set = {}, binding = {}, location = {}, basetype: {} (dim: width [{}], "
            "vecsize[{}], cols[{}]), typeArraySize: {}, count(s): {}",
            tag, resource.name.c_str(), set, binding, location, typestr, type.width, type.vecsize,
            type.columns, typeArraySize, count);
        for (int j = 1; j < typeArraySize; ++j) fmt::print(", {}", type.array[j]);
        fmt::print(". (inner->outer)\n");
      }
    };
    fmt::print("\n========displaying shader resource reflection========\n");
    displayBindingInfo(resources.uniform_buffers, "uniform buffer");
    displayBindingInfo(resources.storage_buffers, "storage buffer");
    displayBindingInfo(resources.stage_inputs, "stage inputs");
    displayBindingInfo(resources.stage_outputs, "stage outputs");
    displayBindingInfo(resources.subpass_inputs, "subpass inputs");
    displayBindingInfo(resources.storage_images, "storage images");
    displayBindingInfo(resources.sampled_images, "sampled images");
    displayBindingInfo(resources.atomic_counters, "atomic counters");
    displayBindingInfo(resources.acceleration_structures, "acceleration structures");
    displayBindingInfo(resources.push_constant_buffers, "push constant buffers");
    displayBindingInfo(resources.shader_record_buffers, "shader record buffers");
    displayBindingInfo(resources.separate_images, "separate images");
    displayBindingInfo(resources.separate_samplers, "separate samplers");
    fmt::print("=====================================================\n\n");
  }

  void ShaderModule::analyzeLayout(const u32 *code, size_t size) {
    // check spirv_parse.cpp for the meaning of 'size': word_count
    compiled = std::unique_ptr<void, void (*)(void const *)>(
        new spirv_cross::CompilerGLSL(code, size),
        [](void const *data) { delete static_cast<spirv_cross::CompilerGLSL const *>(data); });

    resources = std::unique_ptr<void, void (*)(void const *)>(

        new spirv_cross::ShaderResources(
            static_cast<spirv_cross::CompilerGLSL *>(compiled.get())->get_shader_resources()),

        [](void const *data) { delete static_cast<spirv_cross::ShaderResources const *>(data); });
  }

  void ShaderModule::initializeDescriptorSetLayouts() {
    setLayouts.clear();
    auto &glsl = *static_cast<spirv_cross::CompilerGLSL *>(compiled.get());
    auto &resources_ = *static_cast<spirv_cross::ShaderResources *>(resources.get());
    std::map<u32, DescriptorSetLayoutBuilder> setLayoutBuilders;
    auto generateDescriptors = [&glsl, &setLayoutBuilders, this](
                                   const auto &resources, vk::DescriptorType descriptorType) {
      for (auto &resource : resources) {
        u32 set = glsl.get_decoration(resource.id, spv::DecorationDescriptorSet);
        u32 binding = glsl.get_decoration(resource.id, spv::DecorationBinding);
        u32 location = glsl.get_decoration(resource.id, spv::DecorationLocation);
        const spirv_cross::SPIRType &type = glsl.get_type(resource.type_id);
        u32 typeArraySize = type.array.size();
        u32 count = typeArraySize == 0 ? 1 : type.array[0];

        fmt::print(
            "---->\tbuilding descriptor set layout [{}] at set [{}], binding [{}], location "
            "[{}], type[{}]\n",
            resource.name.c_str(), set, binding, location, reflect_vk_enum(descriptorType));

        if (setLayoutBuilders.find(set) == setLayoutBuilders.end())
          setLayoutBuilders.emplace(set, ctx.setlayout());
        // setLayoutBuilders.find(set)->second.addBinding(binding, descriptorType, stageFlag, 1);
        setLayoutBuilders.find(set)->second.addBinding(binding, descriptorType, stageFlag, count);
      }
    };
    
    // Buffer descriptors
    generateDescriptors(resources_.uniform_buffers, vk::DescriptorType::eUniformBufferDynamic);
    generateDescriptors(resources_.storage_buffers, vk::DescriptorType::eStorageBuffer);
    
    // Image/sampler descriptors
    generateDescriptors(resources_.storage_images, vk::DescriptorType::eStorageImage);
    generateDescriptors(resources_.sampled_images, vk::DescriptorType::eCombinedImageSampler);
    generateDescriptors(resources_.separate_images, vk::DescriptorType::eSampledImage);
    generateDescriptors(resources_.separate_samplers, vk::DescriptorType::eSampler);
    
    // Attachment descriptors
    generateDescriptors(resources_.subpass_inputs, vk::DescriptorType::eInputAttachment);
    
    // Ray tracing descriptors
    generateDescriptors(resources_.acceleration_structures, vk::DescriptorType::eAccelerationStructureKHR);

    for (auto &[setNo, builder] : setLayoutBuilders) {
      setLayouts.emplace(setNo, builder.build());
    }
  }

  void ShaderModule::initializeInputAttributes() {
    inputAttributes.clear();
    if (stageFlag != vk::ShaderStageFlagBits::eVertex) return;
    // std::map<u32, std::map<u32, AttributeDescriptor>>
    auto &glsl = *static_cast<spirv_cross::CompilerGLSL *>(compiled.get());
    auto &resources_ = *static_cast<spirv_cross::ShaderResources *>(resources.get());
    auto generateInputDescriptions = [&glsl, this](const auto &resources) {
      for (auto &resource : resources) {
        // u32 set = glsl.get_decoration(resource.id, spv::DecorationDescriptorSet);
        // u32 binding = glsl.get_decoration(resource.id, spv::DecorationBinding);
        u32 location = glsl.get_decoration(resource.id, spv::DecorationLocation);

        const spirv_cross::SPIRType &type = glsl.get_type(resource.type_id);
        u32 typeArraySize = type.array.size();
        u32 count = typeArraySize == 0 ? 1 : type.array[0];
        auto typestr = reflect_basetype_string(type.basetype);

        std::vector<u32> dims(typeArraySize);
        for (int j = 0; j < typeArraySize; ++j) dims[j] = type.array[j];

        fmt::print(
            "---->\tprepare input attrb [{}] at location = {}, basetype: {} (dim: width [{}], "
            "vecsize[{}], cols[{}]), typeArraySize: {}, count(s): {}",
            resource.name.c_str(), location, typestr, type.width, type.vecsize, type.columns,
            typeArraySize, count);
        for (int j = 1; j < typeArraySize; ++j) fmt::print(", {}", type.array[j]);
        fmt::print(". (inner->outer)\n");

        /// @note num rows: vecsize; num cols: columns
        auto format = reflect_basetype_vkformat(type.basetype, type.vecsize);
        if (format == vk::Format::eUndefined)
          throw std::runtime_error(
              fmt::format("no appropriate vkformat duduced for this (column) type: <{}, {}>",
                          typestr, type.vecsize));
        inputAttributes[location]
            = AttributeDescriptor{/*alignment bits*/ type.width,
                                  /*size*/ (u32)(type.vecsize * type.columns * type.width / 8),
                                  /*format*/ format, std::move(dims)};
      }
    };
    generateInputDescriptions(resources_.stage_inputs);
  }

  void ShaderModule::displayLayoutInfo() {
    display_resource(*static_cast<spirv_cross::CompilerGLSL *>(compiled.get()),
                     *static_cast<spirv_cross::ShaderResources *>(resources.get()));
  }

  ShaderModule VulkanContext::createShaderModule(const std::vector<char> &code,
                                                 vk::ShaderStageFlagBits stageFlag) {
    if (code.size() & (sizeof(u32) - 1))
      throw std::runtime_error(
          "the number of bytes of the spirv code should be a multiple of u32 type size.");
    return createShaderModule(reinterpret_cast<const u32 *>(code.data()), code.size() / sizeof(u32),
                              stageFlag);
  }
  ShaderModule VulkanContext::createShaderModule(const u32 *spirvCode, size_t size,
                                                 vk::ShaderStageFlagBits stageFlag) {
    ShaderModule ret{*this};
    vk::ShaderModuleCreateInfo smCI{{}, size * sizeof(u32), spirvCode};
    ret.shaderModule = device.createShaderModule(smCI, nullptr, dispatcher);
    ret.stageFlag = stageFlag;
    /// @note strictly call in this order
    ret.analyzeLayout(spirvCode, size);
    ret.initializeDescriptorSetLayouts();
    ret.initializeInputAttributes();
    return ret;
  }
  ShaderModule VulkanContext::createShaderModule(const ShaderModuleDesc &desc) {
    return createShaderModule(desc.spirvCode, desc.size, desc.stageFlag);
  }
  ShaderModule VulkanContext::createShaderModuleFromGlsl(const char *glslCode,
                                                         vk::ShaderStageFlagBits stage,
                                                         std::string_view moduleName) {
    using namespace spirv_cross;
    shaderc_shader_kind shaderKind;
    switch (stage) {
      case vk::ShaderStageFlagBits::eVertex:
        shaderKind = shaderc_vertex_shader;
        break;
      case vk::ShaderStageFlagBits::eFragment:
        shaderKind = shaderc_fragment_shader;
        break;

        /// geomtry
      case vk::ShaderStageFlagBits::eGeometry:
        shaderKind = shaderc_geometry_shader;
        break;

        /// compute
      case vk::ShaderStageFlagBits::eCompute:
        shaderKind = shaderc_compute_shader;
        break;

        /// tessellation
      case vk::ShaderStageFlagBits::eTessellationControl:
        shaderKind = shaderc_tess_control_shader;
        break;
      case vk::ShaderStageFlagBits::eTessellationEvaluation:
        shaderKind = shaderc_tess_evaluation_shader;
        break;

        /// ray tracing
      case vk::ShaderStageFlagBits::eRaygenKHR:
        shaderKind = shaderc_raygen_shader;
        break;
      case vk::ShaderStageFlagBits::eAnyHitKHR:
        shaderKind = shaderc_anyhit_shader;
        break;
      case vk::ShaderStageFlagBits::eClosestHitKHR:
        shaderKind = shaderc_closesthit_shader;
        break;
      case vk::ShaderStageFlagBits::eMissKHR:
        shaderKind = shaderc_miss_shader;
        break;
      case vk::ShaderStageFlagBits::eIntersectionKHR:
        shaderKind = shaderc_intersection_shader;
        break;
      case vk::ShaderStageFlagBits::eCallableKHR:
        shaderKind = shaderc_callable_shader;
        break;

        /// extensions
      case vk::ShaderStageFlagBits::eTaskEXT:
        shaderKind = shaderc_task_shader;
        break;
      case vk::ShaderStageFlagBits::eMeshEXT:
        shaderKind = shaderc_mesh_shader;
        break;

      default:
        throw std::runtime_error(
            fmt::format("unsupported shader stage [{}]!", reflect_vk_enum(stage)));
        break;
    }
    std::string tmp{glslCode};
    shaderc::CompileOptions options;
    options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_3);
    options.SetTargetSpirv(shaderc_spirv_version_1_3);
    auto compiled = shaderc::Compiler().CompileGlslToSpv(tmp.data(), tmp.size(), shaderKind,
                                                         moduleName.data(), options);
    if (compiled.GetNumErrors()) {
      fmt::print("\n\tGLSL module [{}] compilation error log: \n{}\n", moduleName,
                 compiled.GetErrorMessage());

      throw std::runtime_error(
          fmt::format("compilation of the glsl module [{}] failed with {} errors!\n", moduleName,
                      compiled.GetNumErrors()));
    }
    const std::vector<uint32_t> spirv(compiled.cbegin(), compiled.cend());
    // displayLayoutInfo();
    return createShaderModule(spirv.data(), spirv.size(), stage);
  }

  /// @brief Get HLSL target profile string for a given shader stage
  static const char* get_hlsl_target_profile_str(vk::ShaderStageFlagBits stage) {
    switch (stage) {
      case vk::ShaderStageFlagBits::eVertex:
        return "vs_6_0";
      case vk::ShaderStageFlagBits::eFragment:
        return "ps_6_0";
      case vk::ShaderStageFlagBits::eGeometry:
        return "gs_6_0";
      case vk::ShaderStageFlagBits::eCompute:
        return "cs_6_0";
      case vk::ShaderStageFlagBits::eTessellationControl:
        return "hs_6_0";
      case vk::ShaderStageFlagBits::eTessellationEvaluation:
        return "ds_6_0";
      case vk::ShaderStageFlagBits::eRaygenKHR:
      case vk::ShaderStageFlagBits::eAnyHitKHR:
      case vk::ShaderStageFlagBits::eClosestHitKHR:
      case vk::ShaderStageFlagBits::eMissKHR:
      case vk::ShaderStageFlagBits::eIntersectionKHR:
      case vk::ShaderStageFlagBits::eCallableKHR:
        return "lib_6_3";
      case vk::ShaderStageFlagBits::eTaskEXT:
        return "as_6_5";
      case vk::ShaderStageFlagBits::eMeshEXT:
        return "ms_6_5";
      default:
        return nullptr;
    }
  }

#if ZS_HAS_DXC_API
  static const wchar_t* get_hlsl_target_profile(vk::ShaderStageFlagBits stage) {
    switch (stage) {
      case vk::ShaderStageFlagBits::eVertex:
        return L"vs_6_0";
      case vk::ShaderStageFlagBits::eFragment:
        return L"ps_6_0";
      case vk::ShaderStageFlagBits::eGeometry:
        return L"gs_6_0";
      case vk::ShaderStageFlagBits::eCompute:
        return L"cs_6_0";
      case vk::ShaderStageFlagBits::eTessellationControl:
        return L"hs_6_0";
      case vk::ShaderStageFlagBits::eTessellationEvaluation:
        return L"ds_6_0";
      case vk::ShaderStageFlagBits::eRaygenKHR:
      case vk::ShaderStageFlagBits::eAnyHitKHR:
      case vk::ShaderStageFlagBits::eClosestHitKHR:
      case vk::ShaderStageFlagBits::eMissKHR:
      case vk::ShaderStageFlagBits::eIntersectionKHR:
      case vk::ShaderStageFlagBits::eCallableKHR:
        return L"lib_6_3";
      case vk::ShaderStageFlagBits::eTaskEXT:
        return L"as_6_5";
      case vk::ShaderStageFlagBits::eMeshEXT:
        return L"ms_6_5";
      default:
        return nullptr;
    }
  }

  /// @brief Compile HLSL to SPIR-V using DXC COM API (Windows only)
  static std::vector<u32> compileHlslToSpirvViaDxcApi(const char *hlslCode,
                                                       vk::ShaderStageFlagBits stage,
                                                       std::string_view moduleName,
                                                       std::string_view entryPoint) {
    using Microsoft::WRL::ComPtr;

    const wchar_t *targetProfile = get_hlsl_target_profile(stage);
    if (!targetProfile) {
      throw std::runtime_error(
          fmt::format("unsupported shader stage [{}] for HLSL!", reflect_vk_enum(stage)));
    }

    ComPtr<IDxcUtils> utils;
    ComPtr<IDxcCompiler3> compiler;

    HRESULT hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utils));
    if (FAILED(hr) || !utils) throw std::runtime_error("DxcCreateInstance(CLSID_DxcUtils) failed");

    hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler));
    if (FAILED(hr) || !compiler)
      throw std::runtime_error("DxcCreateInstance(CLSID_DxcCompiler) failed");

    std::string hlslStr{hlslCode};
    DxcBuffer source{};
    source.Ptr = hlslStr.data();
    source.Size = hlslStr.size();
    source.Encoding = DXC_CP_UTF8;

    std::wstring entryW(entryPoint.begin(), entryPoint.end());

    const wchar_t *args[] = {
        L"-T",           targetProfile, L"-E", entryW.c_str(),  L"-spirv", L"-fvk-use-dx-layout",
        L"-fvk-b-shift", L"0",          L"0",  L"-fvk-t-shift", L"0",      L"0",
        L"-fvk-u-shift", L"0",          L"0",  L"-O3",
    };

    ComPtr<IDxcResult> result;
    hr = compiler->Compile(&source, args, static_cast<UINT>(std::size(args)), nullptr,
                           IID_PPV_ARGS(&result));
    if (FAILED(hr) || !result) throw std::runtime_error("DXC compile failed (no result)");

    {
      ComPtr<IDxcBlobUtf8> errors;
      if (SUCCEEDED(result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr)) && errors
          && errors->GetStringLength() > 0) {
        fmt::print("\n\tHLSL module [{}] compilation message: \n{}\n", moduleName,
                   errors->GetStringPointer());
      }
    }

    HRESULT status = S_OK;
    hr = result->GetStatus(&status);
    if (FAILED(hr) || FAILED(status)) {
      throw std::runtime_error(
          fmt::format("compilation of the HLSL module [{}] failed!", moduleName));
    }

    ComPtr<IDxcBlob> spv;
    hr = result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&spv), nullptr);
    if (FAILED(hr) || !spv) throw std::runtime_error("DXC did not produce SPIR-V object");

    if ((spv->GetBufferSize() % 4) != 0)
      throw std::runtime_error("DXC SPIR-V blob size not 4-byte aligned");

    std::vector<u32> spirv(spv->GetBufferSize() / 4);
    std::memcpy(spirv.data(), spv->GetBufferPointer(), spv->GetBufferSize());

    return spirv;
  }
#endif

  /// @brief Compile HLSL to SPIR-V using dxc executable (cross-platform)
  /// @note Requires dxc to be available in PATH (typically from Vulkan SDK)
  static std::vector<u32> compileHlslToSpirvViaDxcExe(const char *hlslCode,
                                                       vk::ShaderStageFlagBits stage,
                                                       std::string_view moduleName,
                                                       std::string_view entryPoint) {
    namespace fs = std::filesystem;

    const char *targetProfile = get_hlsl_target_profile_str(stage);
    if (!targetProfile) {
      throw std::runtime_error(
          fmt::format("unsupported shader stage [{}] for HLSL!", reflect_vk_enum(stage)));
    }

    // Create temporary files for input HLSL and output SPIR-V
    fs::path tempDir = fs::temp_directory_path();
    auto moduleHash = std::hash<std::string_view>{}(moduleName);
    fs::path hlslFile = tempDir / fmt::format("zs_hlsl_{}.hlsl", moduleHash);
    fs::path spvFile = tempDir / fmt::format("zs_hlsl_{}.spv", moduleHash);

    // Write HLSL source to temp file
    {
      std::ofstream ofs(hlslFile, std::ios::binary);
      if (!ofs) {
        throw std::runtime_error(
            fmt::format("failed to create temporary HLSL file: {}", hlslFile.string()));
      }
      ofs.write(hlslCode, std::strlen(hlslCode));
    }

    // Build dxc command
    // dxc -T <profile> -E <entry> -spirv -fvk-use-dx-layout -O3 -Fo <output> <input>
    std::string cmd = fmt::format(
        "dxc -T {} -E {} -spirv -fvk-use-dx-layout "
        "-fvk-b-shift 0 0 -fvk-t-shift 0 0 -fvk-u-shift 0 0 "
        "-O3 -Fo \"{}\" \"{}\" 2>&1",
        targetProfile, entryPoint, spvFile.string(), hlslFile.string());

    fmt::print("[DXC] Compiling HLSL module [{}] via dxc executable...\n", moduleName);

    // Execute dxc
    std::array<char, 4096> buffer;
    std::string compilerOutput;

#if defined(_WIN32) || defined(_WIN64)
    FILE *pipe = _popen(cmd.c_str(), "r");
#else
    FILE *pipe = popen(cmd.c_str(), "r");
#endif

    if (!pipe) {
      fs::remove(hlslFile);
      throw std::runtime_error(
          fmt::format("failed to execute dxc for module [{}]. "
                      "Ensure dxc is in your PATH (typically from Vulkan SDK).",
                      moduleName));
    }

    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
      compilerOutput += buffer.data();
    }

#if defined(_WIN32) || defined(_WIN64)
    int exitCode = _pclose(pipe);
#else
    int exitCode = pclose(pipe);
#endif

    // Clean up input file
    fs::remove(hlslFile);

    if (exitCode != 0) {
      fs::remove(spvFile);
      fmt::print("\n\tHLSL module [{}] compilation error:\n{}\n", moduleName, compilerOutput);
      throw std::runtime_error(
          fmt::format("dxc compilation of HLSL module [{}] failed with exit code {}",
                      moduleName, exitCode));
    }

    if (!compilerOutput.empty()) {
      fmt::print("\n\tHLSL module [{}] compilation message:\n{}\n", moduleName, compilerOutput);
    }

    // Read the SPIR-V output
    std::ifstream ifs(spvFile, std::ios::binary | std::ios::ate);
    if (!ifs) {
      throw std::runtime_error(
          fmt::format("dxc did not produce SPIR-V output for module [{}]", moduleName));
    }

    std::streamsize fileSize = ifs.tellg();
    ifs.seekg(0, std::ios::beg);

    if (fileSize % 4 != 0) {
      fs::remove(spvFile);
      throw std::runtime_error("DXC SPIR-V output size not 4-byte aligned");
    }

    std::vector<u32> spirv(static_cast<size_t>(fileSize) / 4);
    ifs.read(reinterpret_cast<char *>(spirv.data()), fileSize);
    ifs.close();

    // Clean up output file
    fs::remove(spvFile);

    fmt::print("[DXC] Successfully compiled HLSL module [{}] ({} bytes SPIR-V)\n",
               moduleName, static_cast<size_t>(fileSize));

    return spirv;
  }

  std::vector<u32> VulkanContext::compileHlslToSpirv(const char *hlslCode,
                                                     vk::ShaderStageFlagBits stage,
                                                     std::string_view moduleName,
                                                     std::string_view entryPoint) {
#if ZS_HAS_DXC_API
    // On Windows, prefer the faster COM API
    return compileHlslToSpirvViaDxcApi(hlslCode, stage, moduleName, entryPoint);
#else
    // On other platforms, use the dxc executable
    return compileHlslToSpirvViaDxcExe(hlslCode, stage, moduleName, entryPoint);
#endif
  }

  ShaderModule VulkanContext::createShaderModuleFromHlsl(const char *hlslCode,
                                                         vk::ShaderStageFlagBits stage,
                                                         std::string_view moduleName,
                                                         std::string_view entryPoint) {
    std::vector<u32> spirv = compileHlslToSpirv(hlslCode, stage, moduleName, entryPoint);
    auto ret = createShaderModule(spirv.data(), spirv.size(), stage);
    ret.setEntryPoint(std::string(entryPoint));
    return ret;
  }

}  // namespace zs