// Copyright (c) zs contributors. Licensed under the MIT License.
// GpuDescriptors.hpp - API-agnostic resource, bind group, and pipeline
//                      description structs.
//
// These are pure data types (no GPU handles, no allocation). They describe
// *what* to create; each backend's Device implementation turns them into
// native objects.
//
// The types here are intentionally close to WebGPU's descriptor model because
// it represents the lowest-common-denominator that maps cleanly to Vulkan,
// DX12, Metal, and OpenGL.

#pragma once

#include "GpuTypes.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace zs::gpu {

  // =========================================================================
  // Forward declarations of opaque handle types (defined per-backend)
  // =========================================================================
  // These allow description structs to reference already-created GPU objects
  // (e.g. a bind group referencing a buffer) without including backend headers.
  //
  // Backend implementations define concrete types that can be implicitly
  // converted to/from these handles.
  // =========================================================================

  // =========================================================================
  // Buffer
  // =========================================================================
  struct BufferDesc {
    uint64_t    size  = 0;
    BufferUsage usage = BufferUsage::None;
    bool        mappedAtCreation = false;  // WebGPU/Metal style: map on create
    std::string label;                     // debug label (all modern APIs)
  };

  // =========================================================================
  // Texture
  // =========================================================================
  struct TextureDesc {
    TextureDimension dimension   = TextureDimension::e2D;
    Format           format      = Format::RGBA8Unorm;
    uint32_t         width       = 1;
    uint32_t         height      = 1;
    uint32_t         depthOrLayers = 1;
    uint32_t         mipLevels   = 1;
    SampleCount      samples     = SampleCount::x1;
    TextureUsage     usage       = TextureUsage::Sampled;
    std::string      label;
  };

  // =========================================================================
  // Texture view (a sub-range or reinterpretation of a texture)
  // =========================================================================
  struct TextureViewDesc {
    Format               format    = Format::Undefined;  // Undefined = inherit
    TextureViewDimension dimension = TextureViewDimension::e2D;
    uint32_t baseMipLevel   = 0;
    uint32_t mipLevelCount  = 0;  // 0 = remaining
    uint32_t baseArrayLayer = 0;
    uint32_t arrayLayerCount = 0; // 0 = remaining
    // aspect is deduced from format (color vs depth vs stencil)
  };

  // =========================================================================
  // Sampler
  // =========================================================================
  struct SamplerDesc {
    FilterMode  magFilter    = FilterMode::Linear;
    FilterMode  minFilter    = FilterMode::Linear;
    MipmapMode  mipmapFilter = MipmapMode::Linear;
    AddressMode addressU     = AddressMode::Repeat;
    AddressMode addressV     = AddressMode::Repeat;
    AddressMode addressW     = AddressMode::Repeat;
    float       lodMinClamp  = 0.0f;
    float       lodMaxClamp  = 1000.0f;
    float       maxAnisotropy = 1.0f;
    CompareOp   compare      = CompareOp::Always;  // Always = comparison disabled
    bool        compareEnable = false;
    BorderColor borderColor  = BorderColor::TransparentBlack;
    std::string label;
  };

  // =========================================================================
  // Bind group layout (describes the *shape* of resource bindings)
  // =========================================================================
  // Maps to:
  //   Vulkan  - VkDescriptorSetLayout
  //   DX12    - Root signature (descriptor table portion)
  //   Metal   - Argument buffer layout / reflection-driven
  //   WebGPU  - GPUBindGroupLayout
  //   OpenGL  - (emulated via program introspection)

  struct BindGroupLayoutEntry {
    uint32_t    binding     = 0;
    ShaderStage visibility  = ShaderStage::Vertex | ShaderStage::Fragment;
    BindingType type        = BindingType::UniformBuffer;

    // -- For buffer bindings --
    uint64_t    minBufferBindingSize = 0;  // 0 = no minimum
    bool        hasDynamicOffset     = false;  // Vulkan/DX12 optimization hint

    // -- For texture bindings --
    TextureViewDimension textureDimension = TextureViewDimension::e2D;
    Format       textureFormat  = Format::Undefined;  // for storage textures
    bool         textureMultisampled = false;
    // sample type for sampled textures (float / unfilterable-float / sint / uint / depth)
    // deferred: only needed when we implement full validation

    // -- For storage texture bindings --
    // access (read / write / read-write) deferred to backend
  };

  struct BindGroupLayoutDesc {
    std::vector<BindGroupLayoutEntry> entries;
    std::string label;
  };

  // =========================================================================
  // Bind group (concrete resource bindings, immutable once created)
  // =========================================================================
  // Each entry binds a concrete GPU resource to a slot described by the
  // layout. The backend resolves these to native descriptor writes.
  //
  // The bind group is *immutable* after creation — this matches WebGPU and
  // Metal semantics. For Vulkan, the backend can pool and reuse descriptor
  // sets. For DX12, it maps to descriptor table copies. For OpenGL, it maps
  // to deferred glBind* calls at draw time.
  //
  // **No dynamic offsets in the interface.** If you need per-draw UBO data,
  // write to different regions of the same buffer and create bind groups
  // pointing to those regions (via offset/size). The Vulkan backend MAY
  // internally optimize sequential same-buffer bind groups into dynamic
  // offset calls.

  struct BufferBinding {
    uint32_t binding = 0;
    // Backend-specific buffer handle is set via typed constructors in the
    // backend's BindGroupDesc wrapper. Here we store offset + size only;
    // the actual buffer reference is backend-owned.
    uint64_t offset  = 0;
    uint64_t size    = 0;  // 0 = whole buffer (from offset to end)
  };

  struct TextureBinding {
    uint32_t binding = 0;
    // Texture view handle is backend-owned.
  };

  struct SamplerBinding {
    uint32_t binding = 0;
    // Sampler handle is backend-owned.
  };

  // =========================================================================
  // Shader module
  // =========================================================================
  struct ShaderModuleDesc {
    ShaderStage       stage = ShaderStage::None;
    std::string       entryPoint = "main";

    // Exactly one of these should be non-empty:
    std::vector<uint32_t> spirv;   // SPIR-V bytecode (Vulkan, Dawn WebGPU)
    std::string           wgsl;    // WGSL source (WebGPU)
    std::string           glsl;    // GLSL source (Vulkan via shaderc, OpenGL)
    std::string           hlsl;    // HLSL source (DX12, Vulkan via DXC)
    // MSL: typically cross-compiled from SPIR-V; raw MSL deferred.

    std::string label;
  };

  // =========================================================================
  // Vertex input layout
  // =========================================================================
  struct VertexAttribute {
    uint32_t     location = 0;
    VertexFormat format   = VertexFormat::Float4;
    uint32_t     offset   = 0;
  };

  enum class VertexStepMode : uint8_t {
    Vertex,
    Instance,
  };

  struct VertexBufferLayout {
    uint32_t                    stride   = 0;
    VertexStepMode              stepMode = VertexStepMode::Vertex;
    std::vector<VertexAttribute> attributes;
  };

  // =========================================================================
  // Color target state (per-attachment blend + write mask)
  // =========================================================================
  struct BlendComponent {
    BlendOp    operation = BlendOp::Add;
    BlendFactor srcFactor = BlendFactor::One;
    BlendFactor dstFactor = BlendFactor::Zero;
  };

  struct ColorTargetState {
    Format         format     = Format::RGBA8Unorm;
    bool           blendEnable = false;
    BlendComponent color;
    BlendComponent alpha;
    ColorWriteMask writeMask  = ColorWriteMask::All;
  };

  // =========================================================================
  // Depth/stencil state
  // =========================================================================
  struct StencilFaceState {
    CompareOp compare   = CompareOp::Always;
    StencilOp failOp    = StencilOp::Keep;
    StencilOp depthFailOp = StencilOp::Keep;
    StencilOp passOp    = StencilOp::Keep;
  };

  struct DepthStencilState {
    Format          format            = Format::Undefined;  // Undefined = no depth/stencil
    bool            depthTestEnable   = false;
    bool            depthWriteEnable  = false;
    CompareOp       depthCompare      = CompareOp::Less;
    bool            stencilEnable     = false;
    StencilFaceState stencilFront;
    StencilFaceState stencilBack;
    uint32_t        stencilReadMask   = 0xFF;
    uint32_t        stencilWriteMask  = 0xFF;
    float           depthBiasConstant = 0.0f;
    float           depthBiasSlope    = 0.0f;
    float           depthBiasClamp    = 0.0f;
  };

  // =========================================================================
  // Render pipeline description
  // =========================================================================
  // This is the cross-API equivalent of VkGraphicsPipelineCreateInfo /
  // D3D12_GRAPHICS_PIPELINE_STATE_DESC / MTLRenderPipelineDescriptor /
  // GPURenderPipelineDescriptor.
  //
  // Key design decisions:
  //   - Render target *formats* only, not render pass objects. This decouples
  //     the pipeline from Vulkan's VkRenderPass concept. DX12, Metal, and
  //     WebGPU all use format signatures instead of render pass objects.
  //     The Vulkan backend creates/caches compatible render passes internally.
  //   - Bind group layouts are referenced by index (set number). The pipeline
  //     layout is the ordered list of bind group layouts.
  //   - Push constants are exposed as a simple byte range per stage. Maps to
  //     Vulkan push constants, DX12 root constants, Metal setBytes, and
  //     WebGPU (not natively supported; emulated via uniform buffer).

  struct PushConstantRange {
    ShaderStage stages = ShaderStage::None;
    uint32_t    offset = 0;
    uint32_t    size   = 0;
  };

  struct RenderPipelineDesc {
    // -- Shaders --
    // Backend-specific shader handles are set via typed fields in the
    // backend's pipeline desc wrapper. Here we store stage + entry point
    // for the cross-API description; backends may also accept pre-created
    // shader module handles.
    struct ShaderStageDesc {
      ShaderStage stage      = ShaderStage::None;
      std::string entryPoint = "main";
      // Backend attaches the actual module handle.
    };
    ShaderStageDesc vertex;
    ShaderStageDesc fragment;

    // -- Vertex input --
    std::vector<VertexBufferLayout> vertexBuffers;

    // -- Primitive assembly --
    Topology topology = Topology::TriangleList;
    // stripIndexFormat only relevant for strip topologies; deferred.

    // -- Rasterization --
    CullMode    cullMode  = CullMode::None;
    FrontFace   frontFace = FrontFace::CounterClockwise;
    PolygonMode polygonMode = PolygonMode::Fill;  // Line/Point may not be
                                                   // portable (WebGPU/Metal)

    // -- Multisampling --
    SampleCount sampleCount = SampleCount::x1;
    // alphaToCoverageEnable, sampleMask deferred.

    // -- Render target format signature --
    // This replaces VkRenderPass. The pipeline only needs to know what
    // formats it will render into, not the actual textures.
    std::vector<ColorTargetState> colorTargets;
    DepthStencilState             depthStencil;

    // -- Pipeline layout --
    // Bind group layouts are referenced via indices into a separate array
    // (set 0, set 1, ...). The backend resolves these to its native layout.
    // See Device::createRenderPipeline for how layouts are passed.
    std::vector<PushConstantRange> pushConstants;

    std::string label;
  };

  // =========================================================================
  // Compute pipeline description
  // =========================================================================
  struct ComputePipelineDesc {
    RenderPipelineDesc::ShaderStageDesc compute;
    std::vector<PushConstantRange>      pushConstants;
    std::string label;
  };

  // =========================================================================
  // Render pass (begin info — replaces VkRenderPassBeginInfo)
  // =========================================================================
  // Specified at command recording time, not at pipeline creation time.
  // This is the model used by DX12, Metal, WebGPU. For Vulkan, the backend
  // creates/caches compatible VkRenderPass objects from these descriptions.

  struct ClearValue {
    float r = 0.0f, g = 0.0f, b = 0.0f, a = 1.0f;
    float depth   = 1.0f;
    uint32_t stencil = 0;
  };

  struct ColorAttachmentDesc {
    // Backend-specific texture view handle attached via backend wrapper.
    LoadOp     loadOp   = LoadOp::Clear;
    StoreOp    storeOp  = StoreOp::Store;
    ClearValue clearValue;
    // resolveTarget: backend-specific handle for MSAA resolve; deferred.
  };

  struct DepthStencilAttachmentDesc {
    // Backend-specific texture view handle attached via backend wrapper.
    LoadOp     depthLoadOp    = LoadOp::Clear;
    StoreOp    depthStoreOp   = StoreOp::Store;
    float      depthClearValue = 1.0f;
    bool       depthReadOnly   = false;
    LoadOp     stencilLoadOp  = LoadOp::DontCare;
    StoreOp    stencilStoreOp = StoreOp::DontCare;
    uint32_t   stencilClearValue = 0;
    bool       stencilReadOnly  = true;
  };

  struct RenderPassDesc {
    std::vector<ColorAttachmentDesc> colorAttachments;
    DepthStencilAttachmentDesc       depthStencilAttachment;
    bool                             hasDepthStencil = false;
    // occlusionQuerySet: deferred
    // timestampWrites: deferred
    std::string label;
  };

}  // namespace zs::gpu
