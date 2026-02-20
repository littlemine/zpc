#pragma once
#include <array>
#include <map>
#include <optional>
#include <set>

#include "zensim/vulkan/VkContext.hpp"
#include "zensim/vulkan/VkDescriptor.hpp"

namespace zs {

  struct ZPC_CORE_API VertexInputStateDesc {
    std::vector<vk::VertexInputBindingDescription> bindings{};
    std::vector<vk::VertexInputAttributeDescription> attributes{};
  };

  struct ZPC_CORE_API InputAssemblyStateDesc {
    vk::PrimitiveTopology topology{vk::PrimitiveTopology::eTriangleList};
    bool primitiveRestartEnable{false};
  };

  struct ZPC_CORE_API ViewportStateDesc {
    u32 viewportCount{1};
    u32 scissorCount{1};
  };

  struct ZPC_CORE_API RasterizationStateDesc {
    bool depthClampEnable{false};
    bool rasterizerDiscardEnable{false};
    vk::PolygonMode polygonMode{vk::PolygonMode::eFill};
    vk::CullModeFlags cullMode{vk::CullModeFlagBits::eNone};
    vk::FrontFace frontFace{vk::FrontFace::eCounterClockwise};
    bool depthBiasEnable{false};
    float lineWidth{1.f};
    float depthBiasConstantFactor{0.f};
    float depthBiasClamp{0.f};
    float depthBiasSlopeFactor{0.f};
  };

  struct ZPC_CORE_API MultisampleStateDesc {
    bool sampleShadingEnable{false};
    vk::SampleCountFlagBits rasterizationSamples{vk::SampleCountFlagBits::e1};
    float minSampleShading{1.f};
    bool alphaToCoverageEnable{false};
    bool alphaToOneEnable{false};
  };

  struct ZPC_CORE_API DepthStencilStateDesc {
    bool depthTestEnable{true};
    bool depthWriteEnable{true};
    vk::CompareOp depthCompareOp{vk::CompareOp::eLessOrEqual};
    bool depthBoundsTestEnable{false};
    bool stencilTestEnable{false};
    vk::StencilOpState front{};
    vk::StencilOpState back{};
    float minDepthBounds{0.f};
    float maxDepthBounds{1.f};
  };

  struct ZPC_CORE_API ColorBlendStateDesc {
    bool logicOpEnable{false};
    vk::LogicOp logicOp{vk::LogicOp::eCopy};
    std::vector<vk::PipelineColorBlendAttachmentState> attachments{};
    std::array<float, 4> blendConstants{0.f, 0.f, 0.f, 0.f};
  };

  struct ZPC_CORE_API DynamicStateDesc {
    std::vector<vk::DynamicState> states{};
  };

  struct ZPC_CORE_API ShaderStageDesc {
    vk::ShaderStageFlagBits stage{};
    std::vector<u32> spirv{};
    std::string entryPoint{"main"};
  };

  struct ZPC_CORE_API GraphicsPipelineDesc {
    std::vector<ShaderStageDesc> shaderStages{};
    VertexInputStateDesc vertexInput{};
    InputAssemblyStateDesc inputAssembly{};
    ViewportStateDesc viewport{};
    RasterizationStateDesc rasterization{};
    MultisampleStateDesc multisample{};
    DepthStencilStateDesc depthStencil{};
    ColorBlendStateDesc colorBlend{};
    DynamicStateDesc dynamicState{};
    std::vector<vk::PushConstantRange> pushConstantRanges{};
    u32 subpass{0};

    /// @brief Produce the same defaults that PipelineBuilder::default_pipeline_configs() applies.
    /// The plain default-constructed GraphicsPipelineDesc is intentionally minimal (empty dynamic
    /// states, no blend attachments) so it round-trips cleanly through serialization.
    /// Call this factory when you need a ready-to-use starting point outside the builder.
    static GraphicsPipelineDesc defaultPipelineDesc();
  };

  // ref: little vulkan engine
  // (-1, -1) ---- x ----> (1, -1)
  //  |
  //  |
  //  y
  //  |
  //  |
  // (-1, 1)

  struct ZPC_CORE_API Pipeline {
    Pipeline(VulkanContext& ctx) : ctx{ctx}, pipeline{VK_NULL_HANDLE}, layout{VK_NULL_HANDLE} {}
    Pipeline(Pipeline&& o) noexcept : ctx{o.ctx}, pipeline{o.pipeline}, layout{o.layout} {
      o.pipeline = VK_NULL_HANDLE;
      o.layout = VK_NULL_HANDLE;
    }
    Pipeline(const ShaderModule& shader, u32 pushConstantSize = 0);
    ~Pipeline() {
      ctx.device.destroyPipeline(pipeline, nullptr, ctx.dispatcher);
      ctx.device.destroyPipelineLayout(layout, nullptr, ctx.dispatcher);
    }

    vk::Pipeline operator*() const { return pipeline; }
    operator vk::Pipeline() const { return pipeline; }
    operator vk::PipelineLayout() const { return layout; }

  protected:
    friend struct VulkanContext;
    friend struct PipelineBuilder;

    VulkanContext& ctx;
    /// @note manage the following constructs
    vk::Pipeline pipeline;
    vk::PipelineLayout layout;
  };

  struct ZPC_CORE_API PipelineBuilder {
    PipelineBuilder(VulkanContext& ctx) : ctx{ctx} { default_pipeline_configs(); }
    PipelineBuilder(PipelineBuilder&& o) noexcept
        : ctx{o.ctx},
          _desc{std::move(o._desc)},
          shaders{std::move(o.shaders)},
          shaderEntryPoints{std::move(o.shaderEntryPoints)},
          inputAttributes{std::move(o.inputAttributes)},
          descriptorSetLayouts{std::move(o.descriptorSetLayouts)},
          renderPass{o.renderPass} {
      o.reset();
    }
    ~PipelineBuilder() { reset(); }

    void reset() { default_pipeline_configs(); }

    /// @brief Access the underlying pipeline description
    const GraphicsPipelineDesc& desc() const noexcept { return _desc; }
    GraphicsPipelineDesc& desc() noexcept { return _desc; }
    /// @brief Replace the entire pipeline description
    PipelineBuilder& setDesc(const GraphicsPipelineDesc& d) { _desc = d; return *this; }
    PipelineBuilder& setDesc(GraphicsPipelineDesc&& d) { _desc = std::move(d); return *this; }

    /// default minimum setup
    void default_pipeline_configs();

    PipelineBuilder& setShader(vk::ShaderStageFlagBits stage, vk::ShaderModule shaderModule,
                               const std::string& entryPoint = "main") {
      shaders[stage] = shaderModule;
      shaderEntryPoints[stage] = entryPoint;
      return *this;
    }
    /// @note convenience aliases for desc sub-fields
    VertexInputStateDesc& vertexInputDesc() noexcept { return _desc.vertexInput; }
    InputAssemblyStateDesc& inputAssemblyDesc() noexcept { return _desc.inputAssembly; }
    ViewportStateDesc& viewportDesc() noexcept { return _desc.viewport; }
    RasterizationStateDesc& rasterizationDesc() noexcept { return _desc.rasterization; }
    MultisampleStateDesc& multisampleDesc() noexcept { return _desc.multisample; }
    DepthStencilStateDesc& depthStencilDesc() noexcept { return _desc.depthStencil; }
    ColorBlendStateDesc& colorBlendDesc() noexcept { return _desc.colorBlend; }
    DynamicStateDesc& dynamicStateDesc() noexcept { return _desc.dynamicState; }
    PipelineBuilder& setShader(const ShaderModule& shaderModule);

    /// @note assume no padding and alignment involved
    /// @note if shaders are set through zs::ShaderModule and aos layout assumed, no need to
    /// explicitly configure input bindings here
    template <typename... ETs> PipelineBuilder& pushInputBinding(wrapt<ETs>...) {  // for aos layout
      constexpr int N = sizeof...(ETs);
      constexpr size_t szs[] = {sizeof(ETs)...};
      constexpr vk::Format fmts[N] = {deduce_attribute_format(wrapt<ETs>{})...};

      u32 binding = _desc.vertexInput.bindings.size();
      u32 offset = 0;
      for (int i = 0; i < N; ++i) {
        _desc.vertexInput.attributes.emplace_back(/*location*/ i,
                                                  /*binding*/ binding, fmts[i],
                                                  /*offset*/ offset);
        offset += szs[i];
      }
      _desc.vertexInput.bindings.emplace_back(/*binding*/ binding,
                                              /*stride*/ (u32)offset, vk::VertexInputRate::eVertex);
      return *this;
    }

    /// @note if shaders are set through zs::ShaderModule, no need to explicitly configure
    /// descriptor set layouts anymore
    PipelineBuilder& addDescriptorSetLayout(vk::DescriptorSetLayout descrSetLayout,
                                            int setNo = -1) {
      if (setNo == -1) {
        descriptorSetLayouts[descriptorSetLayouts.size()] = descrSetLayout;
      } else
        descriptorSetLayouts[setNo] = descrSetLayout;
      return *this;
    }
    PipelineBuilder& setDescriptorSetLayouts(const std::map<u32, DescriptorSetLayout>& layouts,
                                             bool reset = false);
    PipelineBuilder& setSubpass(u32 subpass) {
      _desc.subpass = subpass;
      return *this;
    }
    PipelineBuilder& setRenderPass(const RenderPass& rp, u32 subpass);
    PipelineBuilder& setRenderPass(vk::RenderPass rp) {
      this->renderPass = rp;
      return *this;
    }

    /// @note provide alternatives for overwrite
    PipelineBuilder& setPushConstantRange(const vk::PushConstantRange& range) {
      _desc.pushConstantRanges = {range};
      return *this;
    }
    PipelineBuilder& setPushConstantRanges(const std::vector<vk::PushConstantRange>& ranges) {
      _desc.pushConstantRanges = ranges;
      return *this;
    }
    PipelineBuilder& setBindingDescriptions(
        const std::vector<vk::VertexInputBindingDescription>& bindings) {
      _desc.vertexInput.bindings = bindings;
      return *this;
    }
    PipelineBuilder& setAttributeDescriptions(
        const std::vector<vk::VertexInputAttributeDescription>& attributes) {
      _desc.vertexInput.attributes = attributes;
      return *this;
    }

    PipelineBuilder& setBlendEnable(bool enable, u32 i = 0) {
      _desc.colorBlend.attachments[i].setBlendEnable(enable);
      return *this;
    }
    PipelineBuilder& setAlphaBlendOp(vk::BlendOp blendOp, u32 i = 0) {
      _desc.colorBlend.attachments[i].setAlphaBlendOp(blendOp);
      return *this;
    }
    PipelineBuilder& setAlphaBlendFactor(vk::BlendFactor srcFactor, vk::BlendFactor dstFactor,
                                         u32 i = 0) {
      _desc.colorBlend.attachments[i].setSrcAlphaBlendFactor(srcFactor);
      _desc.colorBlend.attachments[i].setDstAlphaBlendFactor(dstFactor);
      return *this;
    }
    PipelineBuilder& setColorBlendOp(vk::BlendOp blendOp, u32 i = 0) {
      _desc.colorBlend.attachments[i].setColorBlendOp(blendOp);
      return *this;
    }
    PipelineBuilder& setColorBlendFactor(vk::BlendFactor srcFactor, vk::BlendFactor dstFactor,
                                         u32 i = 0) {
      _desc.colorBlend.attachments[i].setSrcColorBlendFactor(srcFactor);
      _desc.colorBlend.attachments[i].setDstColorBlendFactor(dstFactor);
      return *this;
    }
    PipelineBuilder& setColorWriteMask(vk::ColorComponentFlagBits colorWriteMask, u32 i = 0) {
      _desc.colorBlend.attachments[i].setColorWriteMask(colorWriteMask);
      return *this;
    }
    PipelineBuilder& setDepthTestEnable(bool enable) {
      _desc.depthStencil.depthTestEnable = enable;
      return *this;
    }
    PipelineBuilder& setDepthWriteEnable(bool enable) {
      _desc.depthStencil.depthWriteEnable = enable;
      return *this;
    }
    PipelineBuilder& setDepthCompareOp(vk::CompareOp compareOp) {
      _desc.depthStencil.depthCompareOp = compareOp;
      return *this;
    }
    PipelineBuilder& setTopology(vk::PrimitiveTopology topology) {
      _desc.inputAssembly.topology = topology;
      return *this;
    }
    PipelineBuilder& setPolygonMode(vk::PolygonMode mode) {
      _desc.rasterization.polygonMode = mode;
      return *this;
    }
    PipelineBuilder& setCullMode(vk::CullModeFlagBits cm) {
      _desc.rasterization.cullMode = cm;
      return *this;
    }
    PipelineBuilder& setFrontFace(vk::FrontFace ff) {
      _desc.rasterization.frontFace = ff;
      return *this;
    }
    PipelineBuilder& setRasterizationSamples(vk::SampleCountFlagBits sampleBits) {
      _desc.multisample.rasterizationSamples = sampleBits;
      return *this;
    }
    PipelineBuilder& enableDepthBias(float constant = 1.25f, float slope = 1.75f) {
      _desc.rasterization.depthBiasEnable = true;
      _desc.rasterization.depthBiasConstantFactor = constant;
      _desc.rasterization.depthBiasSlopeFactor = slope;
      return *this;
    }
    PipelineBuilder& disableDepthBias() {
      _desc.rasterization.depthBiasEnable = false;
      _desc.rasterization.depthBiasConstantFactor = 0.f;
      _desc.rasterization.depthBiasSlopeFactor = 0.f;
      return *this;
    }

    PipelineBuilder& enableDynamicState(vk::DynamicState state) {
      for (const auto& s : _desc.dynamicState.states)
        if (s == state) return *this;
      _desc.dynamicState.states.push_back(state);
      return *this;
    }

    vk::PipelineColorBlendAttachmentState& refColorBlendAttachment(u32 i = 0) {
      return _desc.colorBlend.attachments[i];
    }

    Pipeline build();

  protected:
    friend struct VulkanContext;

    VulkanContext& ctx;

    /// @brief Serializable pipeline state description
    GraphicsPipelineDesc _desc;

    /// @brief Runtime-only state (Vulkan handles, not serializable)
    std::map<vk::ShaderStageFlagBits, vk::ShaderModule> shaders;  // managed outside
    std::map<vk::ShaderStageFlagBits, std::string> shaderEntryPoints;
    /// @note structure <binding, attributes (<location, <alignment bits, size, format, dims>>)>
    std::map<u32, AttributeDescriptor> inputAttributes;
    std::map<u32, vk::DescriptorSetLayout> descriptorSetLayouts;  // managed outside
    vk::RenderPass renderPass = VK_NULL_HANDLE;  // managed outside
  };

}  // namespace zs