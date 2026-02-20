#include "zensim/vulkan/VkPipeline.hpp"

#include "zensim/vulkan/VkBuffer.hpp"
#include "zensim/vulkan/VkImage.hpp"
#include "zensim/vulkan/VkRenderPass.hpp"
#include "zensim/vulkan/VkShader.hpp"

namespace zs {

  Pipeline::Pipeline(const ShaderModule& shader, u32 pushConstantSize) : ctx{shader.ctx} {
    /// layout
    const auto& setLayouts = shader.layouts();
    u32 nSets = setLayouts.empty() ? 0 : (setLayouts.rbegin()->first + 1);
    std::vector<vk::DescriptorSetLayout> descrSetLayouts(nSets, VK_NULL_HANDLE);
    std::vector<vk::DescriptorSetLayout> emptyLayouts;
    for (const auto& layout : setLayouts)
      descrSetLayouts[layout.first] = layout.second;
    for (u32 i = 0; i < nSets; ++i) {
      if (descrSetLayouts[i] == VK_NULL_HANDLE) {
        auto emptyLayout = ctx.device.createDescriptorSetLayout(
            vk::DescriptorSetLayoutCreateInfo{}, nullptr, ctx.dispatcher);
        emptyLayouts.push_back(emptyLayout);
        descrSetLayouts[i] = emptyLayout;
      }
    }
    auto pipelineLayoutCI = vk::PipelineLayoutCreateInfo{}
                                .setSetLayoutCount(descrSetLayouts.size())
                                .setPSetLayouts(descrSetLayouts.data());
    vk::PushConstantRange range{vk::ShaderStageFlagBits::eCompute, 0, pushConstantSize};
    if (pushConstantSize)
      pipelineLayoutCI.setPushConstantRangeCount(1).setPPushConstantRanges(&range);
    layout = ctx.device.createPipelineLayout(pipelineLayoutCI, nullptr, ctx.dispatcher);
    /// pipeline
    auto shaderStage = vk::PipelineShaderStageCreateInfo{}
                           .setStage(vk::ShaderStageFlagBits::eCompute)
                           .setModule(shader)
                           .setPName("main");
    auto pipelineInfo = vk::ComputePipelineCreateInfo{}.setStage(shaderStage).setLayout(layout);

    if (ctx.device.createComputePipelines(VK_NULL_HANDLE, (u32)1, &pipelineInfo, nullptr, &pipeline,
                                          ctx.dispatcher)
        != vk::Result::eSuccess)
      throw std::runtime_error("failed to create compute pipeline");
    for (auto emptyLayout : emptyLayouts)
      ctx.device.destroyDescriptorSetLayout(emptyLayout, nullptr, ctx.dispatcher);
  }

  PipelineBuilder& PipelineBuilder::setRenderPass(const RenderPass& rp, u32 subpass) {
    this->renderPass = rp;
    _desc.subpass = subpass;
    _desc.colorBlend.attachments.resize(
        rp.subpasses[subpass].colorRefs.size(),
        vk::PipelineColorBlendAttachmentState{}
            .setColorWriteMask(vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG
                               | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA)
            .setBlendEnable(true)  // required by imgui
            // optional
            .setSrcColorBlendFactor(vk::BlendFactor::eSrcAlpha)          // eOne
            .setDstColorBlendFactor(vk::BlendFactor::eOneMinusSrcAlpha)  // eZero
            .setColorBlendOp(vk::BlendOp::eAdd)
            .setSrcAlphaBlendFactor(vk::BlendFactor::eOne)               // eOne
            .setDstAlphaBlendFactor(vk::BlendFactor::eOneMinusSrcAlpha)  // eZero
            .setAlphaBlendOp(vk::BlendOp::eAdd));

    for (int i = 0; i < rp.subpasses[subpass].colorRefs.size(); ++i) {
      auto colorRef = rp.subpasses[subpass].colorRefs[i];
      auto str = reflect_vk_enum(rp.attachments[colorRef].format);
      // if (rp.attachments[colorRef].format == vk::Format::eR32G32B32A32Sint)
      if (str.find("int") != std::string::npos) _desc.colorBlend.attachments[i].setBlendEnable(false);
    }

    return *this;
  }
  PipelineBuilder& PipelineBuilder::setShader(const ShaderModule& shaderModule) {
    auto stage = shaderModule.getStage();
    setShader(stage, shaderModule, shaderModule.getEntryPoint());
    setDescriptorSetLayouts(shaderModule.layouts());
    if (stage == vk::ShaderStageFlagBits::eVertex)
      inputAttributes = shaderModule.getInputAttributes();
    return *this;
  }
  PipelineBuilder& PipelineBuilder::setDescriptorSetLayouts(
      const std::map<u32, DescriptorSetLayout>& layouts, bool reset) {
    if (reset) descriptorSetLayouts.clear();
    for (const auto& layout : layouts) descriptorSetLayouts[layout.first] = layout.second;
    return *this;
  }

  GraphicsPipelineDesc GraphicsPipelineDesc::defaultPipelineDesc() {
    GraphicsPipelineDesc desc{};
    desc.colorBlend.attachments.push_back(
        vk::PipelineColorBlendAttachmentState{}
            .setColorWriteMask(vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG
                               | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA)
            .setBlendEnable(true)  // required by imgui
            // optional
            .setSrcColorBlendFactor(vk::BlendFactor::eSrcAlpha)          // eOne
            .setDstColorBlendFactor(vk::BlendFactor::eOneMinusSrcAlpha)  // eZero
            .setColorBlendOp(vk::BlendOp::eAdd)
            .setSrcAlphaBlendFactor(vk::BlendFactor::eOne)               // eOne
            .setDstAlphaBlendFactor(vk::BlendFactor::eOneMinusSrcAlpha)  // eZero
            .setAlphaBlendOp(vk::BlendOp::eAdd));
    desc.dynamicState.states = {vk::DynamicState::eViewport, vk::DynamicState::eScissor};
    return desc;
  }

  void PipelineBuilder::default_pipeline_configs() {
    shaders.clear();
    shaderEntryPoints.clear();
    inputAttributes.clear();
    descriptorSetLayouts.clear();
    renderPass = VK_NULL_HANDLE;

    _desc = GraphicsPipelineDesc::defaultPipelineDesc();
  }

  Pipeline PipelineBuilder::build() {
    Pipeline ret{ctx};

    if (shaders.size() < 2) {
      std::string stagesPresent;
      for (const auto& [stage, _] : shaders) {
        if (!stagesPresent.empty()) stagesPresent += ", ";
        stagesPresent += reflect_vk_enum(stage);
      }
      throw std::runtime_error(
          fmt::format("shaders are not fully prepared yet. Expected at least vertex and fragment "
                      "shaders. Currently {} shader(s) set: [{}]",
                      shaders.size(), stagesPresent.empty() ? "none" : stagesPresent));
    }
    if (renderPass == VK_NULL_HANDLE) throw std::runtime_error("renderpass not yet specified.");

    // pipeline layout
    u32 nSets = descriptorSetLayouts.empty() ? 0 : (descriptorSetLayouts.rbegin()->first + 1);
    std::vector<vk::DescriptorSetLayout> descrSetLayouts(nSets, VK_NULL_HANDLE);
    std::vector<vk::DescriptorSetLayout> emptyLayouts;
    for (const auto& layout : descriptorSetLayouts) {
      descrSetLayouts[layout.first] = layout.second;
    }
    for (u32 i = 0; i < nSets; ++i) {
      if (descrSetLayouts[i] == VK_NULL_HANDLE) {
        auto emptyLayout = ctx.device.createDescriptorSetLayout(
            vk::DescriptorSetLayoutCreateInfo{}, nullptr, ctx.dispatcher);
        emptyLayouts.push_back(emptyLayout);
        descrSetLayouts[i] = emptyLayout;
      }
    }
    auto pipelineLayoutCI = vk::PipelineLayoutCreateInfo{}
                                .setSetLayoutCount(descrSetLayouts.size())
                                .setPSetLayouts(descrSetLayouts.data());
    if (_desc.pushConstantRanges.size())
      pipelineLayoutCI.setPushConstantRangeCount(_desc.pushConstantRanges.size())
          .setPPushConstantRanges(_desc.pushConstantRanges.data());
    auto pipelineLayout
        = ctx.device.createPipelineLayout(pipelineLayoutCI, nullptr, ctx.dispatcher);
    ret.layout = pipelineLayout;

    // shader stages (from runtime handles, not from _desc)
    std::vector<vk::PipelineShaderStageCreateInfo> shaderStages;
    for (const auto& [stage, module] : shaders) {
      const char* pName = "main";
      auto epIt = shaderEntryPoints.find(stage);
      if (epIt != shaderEntryPoints.end() && !epIt->second.empty())
        pName = epIt->second.c_str();
      shaderStages.emplace_back(vk::PipelineShaderStageCreateInfo{}
                                    .setStage(stage)
                                    .setModule(module)
                                    .setPName(pName)
                                    .setPNext(nullptr)
                                    .setPSpecializationInfo(nullptr));
    }

    // vertex input bindings
    /// @ref https://gist.github.com/SaschaWillems/428d15ed4b5d71ead462bc63adffa93a
    /// @ref
    /// https://github.com/KhronosGroup/Vulkan-Guide/blob/main/chapters/vertex_input_data_processing.adoc
    /// @ref https://www.reddit.com/r/vulkan/comments/8zx1hn/matrix_as_vertex_input/
    auto& bindingDescriptions = _desc.vertexInput.bindings;
    auto& attributeDescriptions = _desc.vertexInput.attributes;
    if ((bindingDescriptions.size() == 0 || attributeDescriptions.size() == 0)
        && inputAttributes.size() > 0) {
      bindingDescriptions.resize(1);
      attributeDescriptions.clear();
      auto& bindingDescription = bindingDescriptions[0];
      /// @note assume aos layout here, binding is 0
      u32 attribNo = 0;
      u32 offset = 0, alignment = 0;

      for (const auto& attrib : inputAttributes) {
        const auto& [location, attribInfo] = attrib;

        // this requirement guarantee no padding bits inside
        if (attribInfo.alignmentBits != alignment) {
          if (alignment != 0)
            throw std::runtime_error(
                fmt::format("[pipeline building location {} attribute alignment] expect "
                            "{}-bits alignment, "
                            "encountered {}-bits\n",
                            location, alignment, attribInfo.alignmentBits));
          alignment = attribInfo.alignmentBits;
        }

        // push back attribute description
        attributeDescriptions.emplace_back(/*location*/ location,
                                           /*binding*/ 0, attribInfo.format,
                                           /*offset*/ offset);
        offset += attribInfo.size;

        attribNo++;
      }

      bindingDescription
          = vk::VertexInputBindingDescription{0, offset, vk::VertexInputRate::eVertex};
    }
    auto vertexInputInfo = vk::PipelineVertexInputStateCreateInfo{};
    auto tempDepthStencilDesc = _desc.depthStencil;
    auto tempRasterizationDesc = _desc.rasterization;
    if (attributeDescriptions.size() > 0 && bindingDescriptions.size() > 0)
      vertexInputInfo.setVertexAttributeDescriptionCount(attributeDescriptions.size())
          .setPVertexAttributeDescriptions(attributeDescriptions.data())
          .setVertexBindingDescriptionCount(bindingDescriptions.size())
          .setPVertexBindingDescriptions(bindingDescriptions.data());
    else {
      tempDepthStencilDesc.depthWriteEnable = false;
      tempRasterizationDesc.cullMode = vk::CullModeFlagBits::eNone;
    }

    // construct vk CI structs from desc structs
    auto inputAssemblyInfo
        = vk::PipelineInputAssemblyStateCreateInfo{}
              .setTopology(_desc.inputAssembly.topology)
              .setPrimitiveRestartEnable(_desc.inputAssembly.primitiveRestartEnable);

    auto viewportInfo
        = vk::PipelineViewportStateCreateInfo{}
              .setViewportCount(_desc.viewport.viewportCount)
              .setPViewports(nullptr)
              .setScissorCount(_desc.viewport.scissorCount)
              .setPScissors(nullptr);

    auto rasterizationInfo
        = vk::PipelineRasterizationStateCreateInfo{}
              .setDepthClampEnable(tempRasterizationDesc.depthClampEnable)
              .setRasterizerDiscardEnable(tempRasterizationDesc.rasterizerDiscardEnable)
              .setPolygonMode(tempRasterizationDesc.polygonMode)
              .setLineWidth(tempRasterizationDesc.lineWidth)
              .setCullMode(tempRasterizationDesc.cullMode)
              .setFrontFace(tempRasterizationDesc.frontFace)
              .setDepthBiasEnable(tempRasterizationDesc.depthBiasEnable)
              .setDepthBiasConstantFactor(tempRasterizationDesc.depthBiasConstantFactor)
              .setDepthBiasClamp(tempRasterizationDesc.depthBiasClamp)
              .setDepthBiasSlopeFactor(tempRasterizationDesc.depthBiasSlopeFactor);

    auto multisampleInfo
        = vk::PipelineMultisampleStateCreateInfo{}
              .setSampleShadingEnable(_desc.multisample.sampleShadingEnable)
              .setRasterizationSamples(_desc.multisample.rasterizationSamples)
              .setMinSampleShading(_desc.multisample.minSampleShading)
              .setPSampleMask(nullptr)
              .setAlphaToCoverageEnable(_desc.multisample.alphaToCoverageEnable)
              .setAlphaToOneEnable(_desc.multisample.alphaToOneEnable);

    auto depthStencilInfo
        = vk::PipelineDepthStencilStateCreateInfo{}
              .setDepthTestEnable(tempDepthStencilDesc.depthTestEnable)
              .setDepthWriteEnable(tempDepthStencilDesc.depthWriteEnable)
              .setDepthCompareOp(tempDepthStencilDesc.depthCompareOp)
              .setDepthBoundsTestEnable(tempDepthStencilDesc.depthBoundsTestEnable)
              .setStencilTestEnable(tempDepthStencilDesc.stencilTestEnable)
              .setFront(tempDepthStencilDesc.front)
              .setBack(tempDepthStencilDesc.back)
              .setMinDepthBounds(tempDepthStencilDesc.minDepthBounds)
              .setMaxDepthBounds(tempDepthStencilDesc.maxDepthBounds);

    auto colorBlendInfo
        = vk::PipelineColorBlendStateCreateInfo{}
              .setLogicOpEnable(_desc.colorBlend.logicOpEnable)
              .setLogicOp(_desc.colorBlend.logicOp)
              .setAttachmentCount(static_cast<u32>(_desc.colorBlend.attachments.size()))
              .setPAttachments(_desc.colorBlend.attachments.data())
              .setBlendConstants(_desc.colorBlend.blendConstants);

    // dynamic state
    vk::PipelineDynamicStateCreateInfo dynamicStateInfo{
        {}, (u32)_desc.dynamicState.states.size(), _desc.dynamicState.states.data()};

    // pipeline
    auto pipelineInfo = vk::GraphicsPipelineCreateInfo{{},
                                                       (u32)shaderStages.size(),
                                                       shaderStages.data(),
                                                       &vertexInputInfo,
                                                       &inputAssemblyInfo,
                                                       /*tessellation*/ nullptr,
                                                       &viewportInfo,
                                                       &rasterizationInfo,
                                                       &multisampleInfo,
                                                       &depthStencilInfo,
                                                       &colorBlendInfo,
                                                       &dynamicStateInfo,
                                                       pipelineLayout,
                                                       renderPass,
                                                       _desc.subpass,
                                                       /*basePipelineHandle*/ VK_NULL_HANDLE,
                                                       /*basePipelineIndex*/ -1};

    if (ctx.device.createGraphicsPipelines(VK_NULL_HANDLE, (u32)1, &pipelineInfo, nullptr,
                                           &ret.pipeline, ctx.dispatcher)
        != vk::Result::eSuccess)
      throw std::runtime_error("failed to create graphics pipeline");

    for (auto emptyLayout : emptyLayouts)
      ctx.device.destroyDescriptorSetLayout(emptyLayout, nullptr, ctx.dispatcher);

    return ret;
  }

}  // namespace zs