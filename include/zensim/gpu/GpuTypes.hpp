// Copyright (c) zs contributors. Licensed under the MIT License.
// GpuTypes.hpp - API-agnostic GPU enum types and fundamental definitions.
//
// These types form the vocabulary shared by all GPU backend implementations
// (Vulkan, WebGPU, DX12, Metal, OpenGL). Each backend provides mapping
// functions from these enums to its native equivalents.
//
// Design principle: include only concepts that exist across ALL target APIs.
// API-specific extensions (e.g. Vulkan subpasses, DX12 root signatures)
// belong in the backend headers, not here.

#pragma once

#include <cstdint>

namespace zs::gpu {

  // =========================================================================
  // Pixel / vertex format
  // =========================================================================
  // Naming convention: <components><bitwidth><type>
  //   Components: R, RG, RGB, RGBA, D (depth), DS (depth-stencil), BC (block-compressed)
  //   Type: Unorm, Snorm, Uint, Sint, Float, Srgb
  //
  // Coverage: common subset supported by Vulkan, DX12, Metal, WebGPU, and GL 4.x.
  // Exotic / vendor-specific formats are intentionally omitted.
  enum class Format : uint16_t {
    Undefined = 0,

    // -- 8-bit per channel --
    R8Unorm,
    R8Snorm,
    R8Uint,
    R8Sint,

    RG8Unorm,
    RG8Snorm,
    RG8Uint,
    RG8Sint,

    RGBA8Unorm,
    RGBA8UnormSrgb,
    RGBA8Snorm,
    RGBA8Uint,
    RGBA8Sint,

    BGRA8Unorm,
    BGRA8UnormSrgb,

    // -- 16-bit per channel --
    R16Uint,
    R16Sint,
    R16Float,

    RG16Uint,
    RG16Sint,
    RG16Float,

    RGBA16Uint,
    RGBA16Sint,
    RGBA16Float,

    // -- 32-bit per channel --
    R32Uint,
    R32Sint,
    R32Float,

    RG32Uint,
    RG32Sint,
    RG32Float,

    RGB32Float,   // Note: not supported by WebGPU as render target

    RGBA32Uint,
    RGBA32Sint,
    RGBA32Float,

    // -- Packed --
    RGB10A2Unorm,
    RG11B10Float,
    RGB9E5Float,  // shared exponent (read-only in most APIs)

    // -- Depth / stencil --
    D16Unorm,
    D24UnormS8Uint,
    D32Float,
    D32FloatS8Uint,

    // -- Block-compressed (BC / S3TC / RGTC / BPTC) --
    BC1RGBAUnorm,     // DXT1
    BC1RGBAUnormSrgb,
    BC2RGBAUnorm,     // DXT3
    BC2RGBAUnormSrgb,
    BC3RGBAUnorm,     // DXT5
    BC3RGBAUnormSrgb,
    BC4RUnorm,        // RGTC1
    BC4RSnorm,
    BC5RGUnorm,       // RGTC2
    BC5RGSnorm,
    BC6HRGBUfloat,    // BPTC unsigned float
    BC6HRGBFloat,     // BPTC signed float
    BC7RGBAUnorm,     // BPTC unorm
    BC7RGBAUnormSrgb,

    _count
  };

  /// True if the format has a depth component.
  constexpr bool formatHasDepth(Format f) {
    return f == Format::D16Unorm || f == Format::D24UnormS8Uint
        || f == Format::D32Float || f == Format::D32FloatS8Uint;
  }

  /// True if the format has a stencil component.
  constexpr bool formatHasStencil(Format f) {
    return f == Format::D24UnormS8Uint || f == Format::D32FloatS8Uint;
  }

  /// Bytes per pixel (0 for block-compressed or undefined).
  constexpr uint32_t formatBytesPerPixel(Format f) {
    switch (f) {
      case Format::R8Unorm: case Format::R8Snorm:
      case Format::R8Uint:  case Format::R8Sint:
        return 1;
      case Format::RG8Unorm:  case Format::RG8Snorm:
      case Format::RG8Uint:   case Format::RG8Sint:
      case Format::R16Uint:   case Format::R16Sint:
      case Format::R16Float:  case Format::D16Unorm:
        return 2;
      case Format::RGBA8Unorm:     case Format::RGBA8UnormSrgb:
      case Format::RGBA8Snorm:     case Format::RGBA8Uint:
      case Format::RGBA8Sint:      case Format::BGRA8Unorm:
      case Format::BGRA8UnormSrgb: case Format::RG16Uint:
      case Format::RG16Sint:       case Format::RG16Float:
      case Format::R32Uint:        case Format::R32Sint:
      case Format::R32Float:       case Format::D24UnormS8Uint:
      case Format::D32Float:       case Format::RGB10A2Unorm:
      case Format::RG11B10Float:   case Format::RGB9E5Float:
        return 4;
      case Format::RGBA16Uint: case Format::RGBA16Sint:
      case Format::RGBA16Float:
      case Format::RG32Uint:  case Format::RG32Sint:
      case Format::RG32Float: case Format::D32FloatS8Uint:
        return 8;
      case Format::RGB32Float:
        return 12;
      case Format::RGBA32Uint: case Format::RGBA32Sint:
      case Format::RGBA32Float:
        return 16;
      default: return 0;  // block-compressed or undefined
    }
  }

  // =========================================================================
  // Vertex format (for vertex attribute descriptions)
  // =========================================================================
  // Separate from pixel Format because vertex formats include types like
  // Float2/Float3/Float4 that don't map to pixel formats, and pixel formats
  // include depth/BC types that don't apply to vertices.
  enum class VertexFormat : uint8_t {
    Float,
    Float2,
    Float3,
    Float4,
    Int,
    Int2,
    Int3,
    Int4,
    Uint,
    Uint2,
    Uint3,
    Uint4,
    Short2,
    Short4,
    UShort2,
    UShort4,
    Short2Norm,
    Short4Norm,
    Half2,
    Half4,
    UChar4,
    UChar4Norm,
    _count
  };

  // =========================================================================
  // Primitive topology
  // =========================================================================
  enum class Topology : uint8_t {
    PointList,
    LineList,
    LineStrip,
    TriangleList,
    TriangleStrip,
    // Note: TriangleFan is not supported by DX12 or WebGPU.
    // Note: Patches are Vulkan/DX12/GL-only (tessellation); omitted.
    _count
  };

  // =========================================================================
  // Index format
  // =========================================================================
  enum class IndexFormat : uint8_t {
    Uint16,
    Uint32,
  };

  // =========================================================================
  // Blend
  // =========================================================================
  enum class BlendFactor : uint8_t {
    Zero,
    One,
    SrcColor,
    OneMinusSrcColor,
    SrcAlpha,
    OneMinusSrcAlpha,
    DstColor,
    OneMinusDstColor,
    DstAlpha,
    OneMinusDstAlpha,
    SrcAlphaSaturated,
    BlendColor,       // "constant" in Vulkan/DX12
    OneMinusBlendColor,
    Src1Color,         // dual-source blending
    OneMinusSrc1Color,
    Src1Alpha,
    OneMinusSrc1Alpha,
    _count
  };

  enum class BlendOp : uint8_t {
    Add,
    Subtract,
    ReverseSubtract,
    Min,
    Max,
    _count
  };

  // =========================================================================
  // Color write mask (bitmask)
  // =========================================================================
  enum class ColorWriteMask : uint8_t {
    None  = 0,
    Red   = 1 << 0,
    Green = 1 << 1,
    Blue  = 1 << 2,
    Alpha = 1 << 3,
    All   = Red | Green | Blue | Alpha,
  };
  constexpr ColorWriteMask operator|(ColorWriteMask a, ColorWriteMask b) {
    return static_cast<ColorWriteMask>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
  }
  constexpr ColorWriteMask operator&(ColorWriteMask a, ColorWriteMask b) {
    return static_cast<ColorWriteMask>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
  }

  // =========================================================================
  // Depth / stencil
  // =========================================================================
  enum class CompareOp : uint8_t {
    Never,
    Less,
    Equal,
    LessOrEqual,
    Greater,
    NotEqual,
    GreaterOrEqual,
    Always,
    _count
  };

  enum class StencilOp : uint8_t {
    Keep,
    Zero,
    Replace,
    IncrementClamp,
    DecrementClamp,
    Invert,
    IncrementWrap,
    DecrementWrap,
    _count
  };

  // =========================================================================
  // Rasterization
  // =========================================================================
  enum class CullMode : uint8_t {
    None,
    Front,
    Back,
    // FrontAndBack omitted — not meaningful for most use cases; Vulkan-only.
    _count
  };

  enum class FrontFace : uint8_t {
    CounterClockwise,
    Clockwise,
    _count
  };

  enum class PolygonMode : uint8_t {
    Fill,
    Line,
    Point,   // Vulkan/GL only; Metal uses point topology, DX12 doesn't support.
    _count
  };

  // =========================================================================
  // Multisampling
  // =========================================================================
  enum class SampleCount : uint8_t {
    x1  = 1,
    x2  = 2,
    x4  = 4,
    x8  = 8,
    x16 = 16,
    x32 = 32,
    x64 = 64,
  };

  // =========================================================================
  // Texture / image
  // =========================================================================
  enum class TextureDimension : uint8_t {
    e1D,
    e2D,
    e3D,
  };

  enum class TextureViewDimension : uint8_t {
    e1D,
    e2D,
    e2DArray,
    eCube,
    eCubeArray,
    e3D,
  };

  // =========================================================================
  // Sampler
  // =========================================================================
  enum class FilterMode : uint8_t {
    Nearest,
    Linear,
  };

  enum class MipmapMode : uint8_t {
    Nearest,
    Linear,
  };

  enum class AddressMode : uint8_t {
    Repeat,
    MirroredRepeat,
    ClampToEdge,
    ClampToBorder,  // Not in WebGPU core; available via extension.
    // MirrorClampToEdge omitted — Vulkan/DX12 only.
    _count
  };

  enum class BorderColor : uint8_t {
    TransparentBlack,
    OpaqueBlack,
    OpaqueWhite,
    _count
  };

  // =========================================================================
  // Buffer usage (bitmask)
  // =========================================================================
  enum class BufferUsage : uint32_t {
    None           = 0,
    Vertex         = 1 << 0,
    Index          = 1 << 1,
    Uniform        = 1 << 2,
    Storage        = 1 << 3,
    Indirect       = 1 << 4,
    CopySrc        = 1 << 5,
    CopyDst        = 1 << 6,
    MapRead        = 1 << 7,
    MapWrite       = 1 << 8,
  };
  constexpr BufferUsage operator|(BufferUsage a, BufferUsage b) {
    return static_cast<BufferUsage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
  }
  constexpr BufferUsage operator&(BufferUsage a, BufferUsage b) {
    return static_cast<BufferUsage>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
  }
  constexpr bool operator!(BufferUsage a) { return static_cast<uint32_t>(a) == 0; }

  // =========================================================================
  // Texture usage (bitmask)
  // =========================================================================
  enum class TextureUsage : uint32_t {
    None              = 0,
    Sampled           = 1 << 0,   // shader read (sampled image)
    Storage           = 1 << 1,   // shader read/write (storage image / UAV)
    ColorAttachment   = 1 << 2,   // render target
    DepthStencil      = 1 << 3,   // depth/stencil attachment
    CopySrc           = 1 << 4,
    CopyDst           = 1 << 5,
    InputAttachment   = 1 << 6,   // Vulkan subpass input; ignored by other backends
  };
  constexpr TextureUsage operator|(TextureUsage a, TextureUsage b) {
    return static_cast<TextureUsage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
  }
  constexpr TextureUsage operator&(TextureUsage a, TextureUsage b) {
    return static_cast<TextureUsage>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
  }
  constexpr bool operator!(TextureUsage a) { return static_cast<uint32_t>(a) == 0; }

  // =========================================================================
  // Shader stage (bitmask)
  // =========================================================================
  enum class ShaderStage : uint32_t {
    None     = 0,
    Vertex   = 1 << 0,
    Fragment = 1 << 1,
    Compute  = 1 << 2,
    // Geometry / TessControl / TessEval omitted from the cross-API interface:
    //   - Not supported by WebGPU or Metal.
    //   - Mesh shaders (DX12/Vulkan) are a future consideration.
  };
  constexpr ShaderStage operator|(ShaderStage a, ShaderStage b) {
    return static_cast<ShaderStage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
  }
  constexpr ShaderStage operator&(ShaderStage a, ShaderStage b) {
    return static_cast<ShaderStage>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
  }
  constexpr bool operator!(ShaderStage a) { return static_cast<uint32_t>(a) == 0; }

  // =========================================================================
  // Bind group (descriptor) types
  // =========================================================================
  // Resource type within a bind group — the cross-API common denominator.
  // Maps to Vulkan descriptor types, DX12 CBV/SRV/UAV, Metal buffer/texture
  // bindings, WebGPU binding types, and GL uniform/sampler bindings.
  enum class BindingType : uint8_t {
    UniformBuffer,          // CBV (DX12), uniform buffer (Vk/GL), constant buffer (Metal)
    StorageBuffer,          // UAV (DX12), storage buffer (Vk/GL), device buffer (Metal)
    StorageBufferReadOnly,  // SRV of buffer (DX12), readonly storage (Vk/WGPU/GL)
    SampledTexture,         // SRV (DX12), sampled image (Vk), texture (Metal/WGPU)
    StorageTexture,         // UAV of texture (DX12), storage image (Vk), rw texture (Metal)
    Sampler,                // sampler (all APIs)
    ComparisonSampler,      // sampler with compare (shadow sampler)
    // CombinedImageSampler intentionally omitted from the cross-API interface.
    // Vulkan's combined image sampler is a Vulkan-ism; DX12, Metal, and WebGPU
    // all separate textures from samplers. The Vulkan backend can internally
    // combine them when both a SampledTexture and Sampler share the same binding.
    _count
  };

  // =========================================================================
  // Load / store operations (for render pass attachments)
  // =========================================================================
  enum class LoadOp : uint8_t {
    Load,
    Clear,
    DontCare,
  };

  enum class StoreOp : uint8_t {
    Store,
    DontCare,
    // Discard is the same as DontCare semantically on most APIs.
  };

}  // namespace zs::gpu
