#include "zensim/vulkan/VkPipeline.hpp"

#include <vulkan/vulkan_core.h>

#include <type_traits>

#include "zensim/vulkan/VkBuffer.hpp"
#include "zensim/vulkan/VkImage.hpp"
#include "zensim/vulkan/VkRenderPass.hpp"
#include "zensim/vulkan/VkShader.hpp"

namespace zs {

  PipelineBuilder& PipelineBuilder::setShader(const ShaderModule& shaderModule) {
    auto stage = shaderModule.getStage();
    setShader(stage, shaderModule);
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

  void PipelineBuilder::default_pipeline_configs() {
    shaders.clear();
    inputAttributes.clear();
    bindingDescriptions.clear();
    attributeDescriptions.clear();

    viewportInfo = vk::PipelineViewportStateCreateInfo{}
                       .setViewportCount(1)
                       .setPViewports(nullptr)
                       .setScissorCount(1)
                       .setPScissors(nullptr);

    inputAssemblyInfo = vk::PipelineInputAssemblyStateCreateInfo{}
                            .setTopology(vk::PrimitiveTopology::eTriangleList)
                            .setPrimitiveRestartEnable(false);

    rasterizationInfo = vk::PipelineRasterizationStateCreateInfo{}
                            .setDepthClampEnable(false)
                            .setRasterizerDiscardEnable(false)
                            .setPolygonMode(vk::PolygonMode::eFill)
                            .setLineWidth(1.f)
                            .setCullMode(vk::CullModeFlagBits::eNone)
                            .setFrontFace(vk::FrontFace::eCounterClockwise)
                            .setDepthBiasEnable(false)
                            // optional
                            .setDepthBiasConstantFactor(0.f)
                            .setDepthBiasClamp(0.f)
                            .setDepthBiasSlopeFactor(0.f);

    multisampleInfo = vk::PipelineMultisampleStateCreateInfo{}
                          .setSampleShadingEnable(false)
                          .setRasterizationSamples(vk::SampleCountFlagBits::e1)
                          // optional
                          .setMinSampleShading(1.f)
                          .setPSampleMask(nullptr)
                          .setAlphaToCoverageEnable(false)
                          .setAlphaToOneEnable(false);

    colorBlendAttachment
        = vk::PipelineColorBlendAttachmentState{}
              .setColorWriteMask(vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG
                                 | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA)
              .setBlendEnable(true) // required by imgui
              // optional
              .setSrcColorBlendFactor(vk::BlendFactor::eSrcAlpha)          // eOne
              .setDstColorBlendFactor(vk::BlendFactor::eOneMinusSrcAlpha)  // eZero
              .setColorBlendOp(vk::BlendOp::eAdd)
              .setSrcAlphaBlendFactor(vk::BlendFactor::eOne)               // eOne
              .setDstAlphaBlendFactor(vk::BlendFactor::eOneMinusSrcAlpha)  // eZero
              .setAlphaBlendOp(vk::BlendOp::eAdd);

    colorBlendInfo = vk::PipelineColorBlendStateCreateInfo{}
                         .setLogicOpEnable(false)
                         .setAttachmentCount(1)
                         .setPAttachments(&colorBlendAttachment)
                         // optional
                         .setLogicOp(vk::LogicOp::eCopy)
                         .setBlendConstants({0.f, 0.f, 0.f, 0.f});

    depthStencilInfo = vk::PipelineDepthStencilStateCreateInfo{}
                           .setDepthTestEnable(true)
                           .setDepthWriteEnable(true)
                           .setDepthCompareOp(vk::CompareOp::eLess)
                           .setDepthBoundsTestEnable(false)
                           .setStencilTestEnable(false)
                           // optional
                           .setMinDepthBounds(0.f)
                           .setMaxDepthBounds(1.f)
                           .setFront({})
                           .setBack({});

    dynamicStateEnables = {vk::DynamicState::eViewport, vk::DynamicState::eScissor};

    pushConstantRange.reset();
    descriptorSetLayouts.clear();

    renderPass = VK_NULL_HANDLE;
    subpass = 0;
  }

  Pipeline PipelineBuilder::build() {
    Pipeline ret{ctx};

    if (shaders.size() < 2) throw std::runtime_error("shaders are not fully prepared yet.");
    if (renderPass == VK_NULL_HANDLE) throw std::runtime_error("renderpass not yet specified.");

    // pipeline layout
    u32 nSets = descriptorSetLayouts.size();
    std::vector<vk::DescriptorSetLayout> descrSetLayouts(nSets);
    for (const auto& layout : descriptorSetLayouts) {
      if (layout.first < nSets) descrSetLayouts[layout.first] = layout.second;
    }
    auto pipelineLayoutCI = vk::PipelineLayoutCreateInfo{}
                                .setSetLayoutCount(descrSetLayouts.size())
                                .setPSetLayouts(descrSetLayouts.data());
    if (pushConstantRange.has_value())
      pipelineLayoutCI.setPushConstantRangeCount(1).setPPushConstantRanges(&(*pushConstantRange));
    auto pipelineLayout
        = ctx.device.createPipelineLayout(pipelineLayoutCI, nullptr, ctx.dispatcher);
    ret.layout = pipelineLayout;

    // shaders
    std::vector<vk::PipelineShaderStageCreateInfo> shaderStages;
    for (const auto& iter : shaders) {
      shaderStages.emplace_back(vk::PipelineShaderStageCreateInfo{}
                                    .setStage(iter.first)
                                    .setModule(iter.second)
                                    .setPName("main")
                                    .setPNext(nullptr)
                                    .setPSpecializationInfo(nullptr));
    }

    // vertex input bindings
    /// @ref https://gist.github.com/SaschaWillems/428d15ed4b5d71ead462bc63adffa93a
    /// @ref
    /// https://github.com/KhronosGroup/Vulkan-Guide/blob/main/chapters/vertex_input_data_processing.adoc
    /// @ref https://www.reddit.com/r/vulkan/comments/8zx1hn/matrix_as_vertex_input/
    if ((bindingDescriptions.size() == 0 || attributeDescriptions.size() == 0)
        && inputAttributes.size() > 0) {
      bindingDescriptions.resize(1);
      attributeDescriptions.clear();
      // attributeDescriptions.resize(inputAttributes.size());
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
    auto vertexInputInfo = vk::PipelineVertexInputStateCreateInfo{}
                               .setVertexAttributeDescriptionCount(attributeDescriptions.size())
                               .setPVertexAttributeDescriptions(attributeDescriptions.data())
                               .setVertexBindingDescriptionCount(bindingDescriptions.size())
                               .setPVertexBindingDescriptions(bindingDescriptions.data());

    // dynamic state
    std::vector<vk::DynamicState> enabledDynamicStates{dynamicStateEnables.begin(),
                                                       dynamicStateEnables.end()};
    vk::PipelineDynamicStateCreateInfo dynamicStateInfo{
        {}, (u32)enabledDynamicStates.size(), enabledDynamicStates.data()};

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
                                                       subpass,
                                                       /*basePipelineHandle*/ VK_NULL_HANDLE,
                                                       /*basePipelineIndex*/ -1};

    if (ctx.device.createGraphicsPipelines(VK_NULL_HANDLE, (u32)1, &pipelineInfo, nullptr,
                                           &ret.pipeline, ctx.dispatcher)
        != vk::Result::eSuccess)
      throw std::runtime_error("failed to create graphics pipeline");

    return ret;
  }

}  // namespace zs