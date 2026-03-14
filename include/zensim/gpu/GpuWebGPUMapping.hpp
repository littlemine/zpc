// Copyright (c) zs contributors. Licensed under the MIT License.
// GpuWebGPUMapping.hpp - Bidirectional mappings between gpu:: enums and WebGPU.
//
// WebGPU enum values match the webgpu.h C API (Dawn / wgpu-native / Emscripten).
// When WEBGPU_H_ is not defined, we use integer constants that match the spec.

#pragma once

#include "GpuTypes.hpp"
#include <cstdint>

namespace zs::gpu::wgpu_map {

  // =========================================================================
  // WebGPU enum constants (matching webgpu.h spec values)
  // Used when the actual WebGPU header is not available.
  // =========================================================================
#ifndef WEBGPU_H_
  // WGPUTextureFormat values from the WebGPU spec
  enum WGPUTextureFormat_ : uint32_t {
    WGPUTextureFormat_Undefined = 0x00000000,
    WGPUTextureFormat_R8Unorm = 0x00000001,
    WGPUTextureFormat_R8Snorm = 0x00000002,
    WGPUTextureFormat_R8Uint = 0x00000003,
    WGPUTextureFormat_R8Sint = 0x00000004,
    WGPUTextureFormat_R16Uint = 0x00000005,
    WGPUTextureFormat_R16Sint = 0x00000006,
    WGPUTextureFormat_R16Float = 0x00000007,
    WGPUTextureFormat_RG8Unorm = 0x00000008,
    WGPUTextureFormat_RG8Uint = 0x0000000A,
    WGPUTextureFormat_RG8Sint = 0x0000000B,
    WGPUTextureFormat_R32Float = 0x0000000E,
    WGPUTextureFormat_R32Uint = 0x0000000F,
    WGPUTextureFormat_R32Sint = 0x00000010,
    WGPUTextureFormat_RG16Uint = 0x00000011,
    WGPUTextureFormat_RG16Sint = 0x00000012,
    WGPUTextureFormat_RG16Float = 0x00000013,
    WGPUTextureFormat_RGBA8Unorm = 0x00000014,
    WGPUTextureFormat_RGBA8UnormSrgb = 0x00000015,
    WGPUTextureFormat_RGBA8Snorm = 0x00000016,
    WGPUTextureFormat_RGBA8Uint = 0x00000017,
    WGPUTextureFormat_RGBA8Sint = 0x00000018,
    WGPUTextureFormat_BGRA8Unorm = 0x00000019,
    WGPUTextureFormat_BGRA8UnormSrgb = 0x0000001A,
    WGPUTextureFormat_RGB10A2Unorm = 0x0000001C,
    WGPUTextureFormat_RG32Float = 0x00000021,
    WGPUTextureFormat_RG32Uint = 0x00000022,
    WGPUTextureFormat_RG32Sint = 0x00000023,
    WGPUTextureFormat_RGBA16Uint = 0x00000024,
    WGPUTextureFormat_RGBA16Sint = 0x00000025,
    WGPUTextureFormat_RGBA16Float = 0x00000026,
    WGPUTextureFormat_RGBA32Float = 0x00000027,
    WGPUTextureFormat_RGBA32Uint = 0x00000028,
    WGPUTextureFormat_RGBA32Sint = 0x00000029,
    WGPUTextureFormat_Depth16Unorm = 0x0000002C,
    WGPUTextureFormat_Depth24Plus = 0x0000002D,
    WGPUTextureFormat_Depth24PlusStencil8 = 0x0000002E,
    WGPUTextureFormat_Depth32Float = 0x0000002F,
    WGPUTextureFormat_Depth32FloatStencil8 = 0x00000030,
  };

  enum WGPUVertexFormat_ : uint32_t {
    WGPUVertexFormat_Uint8x2 = 0x00000001,
    WGPUVertexFormat_Uint8x4 = 0x00000002,
    WGPUVertexFormat_Sint8x2 = 0x00000003,
    WGPUVertexFormat_Sint8x4 = 0x00000004,
    WGPUVertexFormat_Unorm8x2 = 0x00000005,
    WGPUVertexFormat_Unorm8x4 = 0x00000006,
    WGPUVertexFormat_Snorm8x2 = 0x00000007,
    WGPUVertexFormat_Snorm8x4 = 0x00000008,
    WGPUVertexFormat_Uint16x2 = 0x00000009,
    WGPUVertexFormat_Uint16x4 = 0x0000000A,
    WGPUVertexFormat_Sint16x2 = 0x0000000B,
    WGPUVertexFormat_Sint16x4 = 0x0000000C,
    WGPUVertexFormat_Unorm16x2 = 0x0000000D,
    WGPUVertexFormat_Unorm16x4 = 0x0000000E,
    WGPUVertexFormat_Snorm16x2 = 0x0000000F,
    WGPUVertexFormat_Snorm16x4 = 0x00000010,
    WGPUVertexFormat_Float16x2 = 0x00000011,
    WGPUVertexFormat_Float16x4 = 0x00000012,
    WGPUVertexFormat_Float32 = 0x00000013,
    WGPUVertexFormat_Float32x2 = 0x00000014,
    WGPUVertexFormat_Float32x3 = 0x00000015,
    WGPUVertexFormat_Float32x4 = 0x00000016,
    WGPUVertexFormat_Uint32 = 0x00000017,
    WGPUVertexFormat_Uint32x2 = 0x00000018,
    WGPUVertexFormat_Uint32x3 = 0x00000019,
    WGPUVertexFormat_Uint32x4 = 0x0000001A,
    WGPUVertexFormat_Sint32 = 0x0000001B,
    WGPUVertexFormat_Sint32x2 = 0x0000001C,
    WGPUVertexFormat_Sint32x3 = 0x0000001D,
    WGPUVertexFormat_Sint32x4 = 0x0000001E,
  };

  enum WGPUPrimitiveTopology_ : uint32_t {
    WGPUPrimitiveTopology_PointList = 0,
    WGPUPrimitiveTopology_LineList = 1,
    WGPUPrimitiveTopology_LineStrip = 2,
    WGPUPrimitiveTopology_TriangleList = 3,
    WGPUPrimitiveTopology_TriangleStrip = 4,
  };

  enum WGPUIndexFormat_ : uint32_t {
    WGPUIndexFormat_Undefined = 0,
    WGPUIndexFormat_Uint16 = 1,
    WGPUIndexFormat_Uint32 = 2,
  };

  enum WGPUBlendFactor_ : uint32_t {
    WGPUBlendFactor_Zero = 0,
    WGPUBlendFactor_One = 1,
    WGPUBlendFactor_Src = 2,
    WGPUBlendFactor_OneMinusSrc = 3,
    WGPUBlendFactor_SrcAlpha = 4,
    WGPUBlendFactor_OneMinusSrcAlpha = 5,
    WGPUBlendFactor_Dst = 6,
    WGPUBlendFactor_OneMinusDst = 7,
    WGPUBlendFactor_DstAlpha = 8,
    WGPUBlendFactor_OneMinusDstAlpha = 9,
    WGPUBlendFactor_SrcAlphaSaturated = 10,
    WGPUBlendFactor_Constant = 11,
    WGPUBlendFactor_OneMinusConstant = 12,
  };

  enum WGPUBlendOperation_ : uint32_t {
    WGPUBlendOperation_Add = 0,
    WGPUBlendOperation_Subtract = 1,
    WGPUBlendOperation_ReverseSubtract = 2,
    WGPUBlendOperation_Min = 3,
    WGPUBlendOperation_Max = 4,
  };

  enum WGPUCompareFunction_ : uint32_t {
    WGPUCompareFunction_Undefined = 0,
    WGPUCompareFunction_Never = 1,
    WGPUCompareFunction_Less = 2,
    WGPUCompareFunction_LessEqual = 3,
    WGPUCompareFunction_Greater = 4,
    WGPUCompareFunction_GreaterEqual = 5,
    WGPUCompareFunction_Equal = 6,
    WGPUCompareFunction_NotEqual = 7,
    WGPUCompareFunction_Always = 8,
  };

  enum WGPUFilterMode_ : uint32_t {
    WGPUFilterMode_Nearest = 0,
    WGPUFilterMode_Linear = 1,
  };

  enum WGPUMipmapFilterMode_ : uint32_t {
    WGPUMipmapFilterMode_Nearest = 0,
    WGPUMipmapFilterMode_Linear = 1,
  };

  enum WGPUAddressMode_ : uint32_t {
    WGPUAddressMode_Repeat = 0,
    WGPUAddressMode_MirrorRepeat = 1,
    WGPUAddressMode_ClampToEdge = 2,
  };

  enum WGPUCullMode_ : uint32_t {
    WGPUCullMode_None = 0,
    WGPUCullMode_Front = 1,
    WGPUCullMode_Back = 2,
  };

  enum WGPUFrontFace_ : uint32_t {
    WGPUFrontFace_CCW = 0,
    WGPUFrontFace_CW = 1,
  };

  enum WGPUStencilOperation_ : uint32_t {
    WGPUStencilOperation_Keep = 0,
    WGPUStencilOperation_Zero = 1,
    WGPUStencilOperation_Replace = 2,
    WGPUStencilOperation_Invert = 3,
    WGPUStencilOperation_IncrementClamp = 4,
    WGPUStencilOperation_DecrementClamp = 5,
    WGPUStencilOperation_IncrementWrap = 6,
    WGPUStencilOperation_DecrementWrap = 7,
  };

  enum WGPULoadOp_ : uint32_t {
    WGPULoadOp_Undefined = 0,
    WGPULoadOp_Clear = 1,
    WGPULoadOp_Load = 2,
  };

  enum WGPUStoreOp_ : uint32_t {
    WGPUStoreOp_Undefined = 0,
    WGPUStoreOp_Store = 1,
    WGPUStoreOp_Discard = 2,
  };

  using WGPUBufferUsageFlags = uint32_t;
  constexpr WGPUBufferUsageFlags WGPUBufferUsage_MapRead   = 0x0001;
  constexpr WGPUBufferUsageFlags WGPUBufferUsage_MapWrite  = 0x0002;
  constexpr WGPUBufferUsageFlags WGPUBufferUsage_CopySrc   = 0x0004;
  constexpr WGPUBufferUsageFlags WGPUBufferUsage_CopyDst   = 0x0008;
  constexpr WGPUBufferUsageFlags WGPUBufferUsage_Index     = 0x0010;
  constexpr WGPUBufferUsageFlags WGPUBufferUsage_Vertex    = 0x0020;
  constexpr WGPUBufferUsageFlags WGPUBufferUsage_Uniform   = 0x0040;
  constexpr WGPUBufferUsageFlags WGPUBufferUsage_Storage   = 0x0080;
  constexpr WGPUBufferUsageFlags WGPUBufferUsage_Indirect  = 0x0100;

  using WGPUTextureUsageFlags = uint32_t;
  constexpr WGPUTextureUsageFlags WGPUTextureUsage_CopySrc          = 0x01;
  constexpr WGPUTextureUsageFlags WGPUTextureUsage_CopyDst          = 0x02;
  constexpr WGPUTextureUsageFlags WGPUTextureUsage_TextureBinding   = 0x04;
  constexpr WGPUTextureUsageFlags WGPUTextureUsage_StorageBinding   = 0x08;
  constexpr WGPUTextureUsageFlags WGPUTextureUsage_RenderAttachment = 0x10;

  using WGPUShaderStageFlags = uint32_t;
  constexpr WGPUShaderStageFlags WGPUShaderStage_Vertex   = 0x01;
  constexpr WGPUShaderStageFlags WGPUShaderStage_Fragment = 0x02;
  constexpr WGPUShaderStageFlags WGPUShaderStage_Compute  = 0x04;

  using WGPUColorWriteMaskFlags = uint32_t;
  constexpr WGPUColorWriteMaskFlags WGPUColorWriteMask_None  = 0x00;
  constexpr WGPUColorWriteMaskFlags WGPUColorWriteMask_Red   = 0x01;
  constexpr WGPUColorWriteMaskFlags WGPUColorWriteMask_Green = 0x02;
  constexpr WGPUColorWriteMaskFlags WGPUColorWriteMask_Blue  = 0x04;
  constexpr WGPUColorWriteMaskFlags WGPUColorWriteMask_Alpha = 0x08;
  constexpr WGPUColorWriteMaskFlags WGPUColorWriteMask_All   = 0x0F;

#endif  // WEBGPU_H_

  // =========================================================================
  // gpu:: → WebGPU conversions
  // =========================================================================

  constexpr uint32_t toWGPU(Format fmt) {
    switch (fmt) {
      case Format::R8Unorm:    return WGPUTextureFormat_R8Unorm;
      case Format::R8Snorm:    return WGPUTextureFormat_R8Snorm;
      case Format::R8Uint:     return WGPUTextureFormat_R8Uint;
      case Format::R16Float:   return WGPUTextureFormat_R16Float;
      case Format::RG8Unorm:   return WGPUTextureFormat_RG8Unorm;
      case Format::R32Float:   return WGPUTextureFormat_R32Float;
      case Format::RG16Float:  return WGPUTextureFormat_RG16Float;
      case Format::RGBA8Unorm: return WGPUTextureFormat_RGBA8Unorm;
      case Format::RGBA8UnormSrgb: return WGPUTextureFormat_RGBA8UnormSrgb;
      case Format::RGBA8Snorm: return WGPUTextureFormat_RGBA8Snorm;
      case Format::BGRA8Unorm: return WGPUTextureFormat_BGRA8Unorm;
      case Format::BGRA8UnormSrgb: return WGPUTextureFormat_BGRA8UnormSrgb;
      case Format::RGB10A2Unorm: return WGPUTextureFormat_RGB10A2Unorm;
      case Format::RG32Float:  return WGPUTextureFormat_RG32Float;
      case Format::RGBA16Float:return WGPUTextureFormat_RGBA16Float;
      case Format::RGBA32Float:return WGPUTextureFormat_RGBA32Float;
      case Format::RGBA32Sint: return WGPUTextureFormat_RGBA32Sint;
      case Format::RGBA32Uint: return WGPUTextureFormat_RGBA32Uint;
      case Format::D16Unorm: return WGPUTextureFormat_Depth16Unorm;
      case Format::D24UnormS8Uint: return WGPUTextureFormat_Depth24PlusStencil8;
      case Format::D32Float: return WGPUTextureFormat_Depth32Float;
      case Format::D32FloatS8Uint: return WGPUTextureFormat_Depth32FloatStencil8;
      default: return WGPUTextureFormat_Undefined;
    }
  }

  constexpr uint32_t toWGPU(VertexFormat fmt) {
    switch (fmt) {
      case VertexFormat::Float:   return WGPUVertexFormat_Float32;
      case VertexFormat::Float2: return WGPUVertexFormat_Float32x2;
      case VertexFormat::Float3: return WGPUVertexFormat_Float32x3;
      case VertexFormat::Float4: return WGPUVertexFormat_Float32x4;
      case VertexFormat::Uint:    return WGPUVertexFormat_Uint32;
      case VertexFormat::Uint2:  return WGPUVertexFormat_Uint32x2;
      case VertexFormat::Uint3:  return WGPUVertexFormat_Uint32x3;
      case VertexFormat::Uint4:  return WGPUVertexFormat_Uint32x4;
      case VertexFormat::Int:    return WGPUVertexFormat_Sint32;
      case VertexFormat::Int2:  return WGPUVertexFormat_Sint32x2;
      case VertexFormat::Int3:  return WGPUVertexFormat_Sint32x3;
      case VertexFormat::Int4:  return WGPUVertexFormat_Sint32x4;
      case VertexFormat::UChar4:   return WGPUVertexFormat_Uint8x4;
      case VertexFormat::UChar4Norm:  return WGPUVertexFormat_Unorm8x4;
      case VertexFormat::UShort2:  return WGPUVertexFormat_Uint16x2;
      case VertexFormat::UShort4:  return WGPUVertexFormat_Uint16x4;
      case VertexFormat::Half2: return WGPUVertexFormat_Float16x2;
      case VertexFormat::Half4: return WGPUVertexFormat_Float16x4;
      default: return WGPUVertexFormat_Float32;
    }
  }

  constexpr uint32_t toWGPU(Topology t) {
    switch (t) {
      case Topology::PointList:     return WGPUPrimitiveTopology_PointList;
      case Topology::LineList:      return WGPUPrimitiveTopology_LineList;
      case Topology::LineStrip:     return WGPUPrimitiveTopology_LineStrip;
      case Topology::TriangleList:  return WGPUPrimitiveTopology_TriangleList;
      case Topology::TriangleStrip: return WGPUPrimitiveTopology_TriangleStrip;
      default: return WGPUPrimitiveTopology_TriangleList;
    }
  }

  constexpr uint32_t toWGPU(IndexFormat f) {
    switch (f) {
      case IndexFormat::Uint16: return WGPUIndexFormat_Uint16;
      case IndexFormat::Uint32: return WGPUIndexFormat_Uint32;
      default: return WGPUIndexFormat_Undefined;
    }
  }

  constexpr uint32_t toWGPU(BlendFactor f) {
    switch (f) {
      case BlendFactor::Zero:             return WGPUBlendFactor_Zero;
      case BlendFactor::One:              return WGPUBlendFactor_One;
      case BlendFactor::SrcColor:              return WGPUBlendFactor_Src;
      case BlendFactor::OneMinusSrcColor:      return WGPUBlendFactor_OneMinusSrc;
      case BlendFactor::SrcAlpha:         return WGPUBlendFactor_SrcAlpha;
      case BlendFactor::OneMinusSrcAlpha: return WGPUBlendFactor_OneMinusSrcAlpha;
      case BlendFactor::DstColor:              return WGPUBlendFactor_Dst;
      case BlendFactor::OneMinusDstColor:      return WGPUBlendFactor_OneMinusDst;
      case BlendFactor::DstAlpha:         return WGPUBlendFactor_DstAlpha;
      case BlendFactor::OneMinusDstAlpha: return WGPUBlendFactor_OneMinusDstAlpha;
      case BlendFactor::SrcAlphaSaturated:return WGPUBlendFactor_SrcAlphaSaturated;
      case BlendFactor::BlendColor:         return WGPUBlendFactor_Constant;
      case BlendFactor::OneMinusBlendColor: return WGPUBlendFactor_OneMinusConstant;
      default: return WGPUBlendFactor_One;
    }
  }

  constexpr uint32_t toWGPU(BlendOp op) {
    switch (op) {
      case BlendOp::Add:             return WGPUBlendOperation_Add;
      case BlendOp::Subtract:        return WGPUBlendOperation_Subtract;
      case BlendOp::ReverseSubtract: return WGPUBlendOperation_ReverseSubtract;
      case BlendOp::Min:             return WGPUBlendOperation_Min;
      case BlendOp::Max:             return WGPUBlendOperation_Max;
      default: return WGPUBlendOperation_Add;
    }
  }

  constexpr uint32_t toWGPU(CompareOp op) {
    switch (op) {
      case CompareOp::Never:        return WGPUCompareFunction_Never;
      case CompareOp::Less:         return WGPUCompareFunction_Less;
      case CompareOp::LessOrEqual:    return WGPUCompareFunction_LessEqual;
      case CompareOp::Greater:      return WGPUCompareFunction_Greater;
      case CompareOp::GreaterOrEqual: return WGPUCompareFunction_GreaterEqual;
      case CompareOp::Equal:        return WGPUCompareFunction_Equal;
      case CompareOp::NotEqual:     return WGPUCompareFunction_NotEqual;
      case CompareOp::Always:       return WGPUCompareFunction_Always;
      default: return WGPUCompareFunction_Always;
    }
  }

  constexpr uint32_t toWGPU(StencilOp op) {
    switch (op) {
      case StencilOp::Keep:           return WGPUStencilOperation_Keep;
      case StencilOp::Zero:           return WGPUStencilOperation_Zero;
      case StencilOp::Replace:        return WGPUStencilOperation_Replace;
      case StencilOp::Invert:         return WGPUStencilOperation_Invert;
      case StencilOp::IncrementClamp: return WGPUStencilOperation_IncrementClamp;
      case StencilOp::DecrementClamp: return WGPUStencilOperation_DecrementClamp;
      case StencilOp::IncrementWrap:  return WGPUStencilOperation_IncrementWrap;
      case StencilOp::DecrementWrap:  return WGPUStencilOperation_DecrementWrap;
      default: return WGPUStencilOperation_Keep;
    }
  }

  constexpr uint32_t toWGPU(FilterMode m) {
    return m == FilterMode::Nearest ? WGPUFilterMode_Nearest : WGPUFilterMode_Linear;
  }

  constexpr uint32_t toWGPU(MipmapMode m) {
    return m == MipmapMode::Nearest ? WGPUMipmapFilterMode_Nearest
                                    : WGPUMipmapFilterMode_Linear;
  }

  constexpr uint32_t toWGPU(AddressMode m) {
    switch (m) {
      case AddressMode::Repeat:       return WGPUAddressMode_Repeat;
      case AddressMode::MirroredRepeat: return WGPUAddressMode_MirrorRepeat;
      case AddressMode::ClampToEdge:  return WGPUAddressMode_ClampToEdge;
      default: return WGPUAddressMode_ClampToEdge;
    }
  }

  constexpr uint32_t toWGPU(CullMode m) {
    switch (m) {
      case CullMode::None:  return WGPUCullMode_None;
      case CullMode::Front: return WGPUCullMode_Front;
      case CullMode::Back:  return WGPUCullMode_Back;
      default: return WGPUCullMode_None;
    }
  }

  constexpr uint32_t toWGPU(FrontFace f) {
    return f == FrontFace::Clockwise ? WGPUFrontFace_CW : WGPUFrontFace_CCW;
  }

  constexpr uint32_t toWGPU(LoadOp op) {
    switch (op) {
      case LoadOp::Clear: return WGPULoadOp_Clear;
      case LoadOp::Load:  return WGPULoadOp_Load;
      default: return WGPULoadOp_Clear;
    }
  }

  constexpr uint32_t toWGPU(StoreOp op) {
    switch (op) {
      case StoreOp::Store:   return WGPUStoreOp_Store;
      case StoreOp::DontCare: return WGPUStoreOp_Discard;
      default: return WGPUStoreOp_Store;
    }
  }

  constexpr WGPUBufferUsageFlags toWGPU(BufferUsage u) {
    WGPUBufferUsageFlags flags = 0;
    auto raw = static_cast<uint32_t>(u);
    if (raw & static_cast<uint32_t>(BufferUsage::Vertex))   flags |= WGPUBufferUsage_Vertex;
    if (raw & static_cast<uint32_t>(BufferUsage::Index))    flags |= WGPUBufferUsage_Index;
    if (raw & static_cast<uint32_t>(BufferUsage::Uniform))  flags |= WGPUBufferUsage_Uniform;
    if (raw & static_cast<uint32_t>(BufferUsage::Storage))  flags |= WGPUBufferUsage_Storage;
    if (raw & static_cast<uint32_t>(BufferUsage::Indirect)) flags |= WGPUBufferUsage_Indirect;
    if (raw & static_cast<uint32_t>(BufferUsage::CopySrc))  flags |= WGPUBufferUsage_CopySrc;
    if (raw & static_cast<uint32_t>(BufferUsage::CopyDst))  flags |= WGPUBufferUsage_CopyDst;
    if (raw & static_cast<uint32_t>(BufferUsage::MapRead))  flags |= WGPUBufferUsage_MapRead;
    if (raw & static_cast<uint32_t>(BufferUsage::MapWrite)) flags |= WGPUBufferUsage_MapWrite;
    return flags;
  }

  constexpr WGPUTextureUsageFlags toWGPU(TextureUsage u) {
    WGPUTextureUsageFlags flags = 0;
    auto raw = static_cast<uint32_t>(u);
    if (raw & static_cast<uint32_t>(TextureUsage::CopySrc))         flags |= WGPUTextureUsage_CopySrc;
    if (raw & static_cast<uint32_t>(TextureUsage::CopyDst))         flags |= WGPUTextureUsage_CopyDst;
    if (raw & static_cast<uint32_t>(TextureUsage::Sampled))         flags |= WGPUTextureUsage_TextureBinding;
    if (raw & static_cast<uint32_t>(TextureUsage::Storage))         flags |= WGPUTextureUsage_StorageBinding;
    if (raw & static_cast<uint32_t>(TextureUsage::ColorAttachment)) flags |= WGPUTextureUsage_RenderAttachment;
    if (raw & static_cast<uint32_t>(TextureUsage::DepthStencil)) flags |= WGPUTextureUsage_RenderAttachment;
    return flags;
  }

  constexpr WGPUShaderStageFlags toWGPU(ShaderStage s) {
    WGPUShaderStageFlags flags = 0;
    auto raw = static_cast<uint32_t>(s);
    if (raw & static_cast<uint32_t>(ShaderStage::Vertex))   flags |= WGPUShaderStage_Vertex;
    if (raw & static_cast<uint32_t>(ShaderStage::Fragment)) flags |= WGPUShaderStage_Fragment;
    if (raw & static_cast<uint32_t>(ShaderStage::Compute))  flags |= WGPUShaderStage_Compute;
    return flags;
  }

  constexpr WGPUColorWriteMaskFlags toWGPU(ColorWriteMask m) {
    WGPUColorWriteMaskFlags flags = 0;
    auto raw = static_cast<uint32_t>(m);
    if (raw & static_cast<uint32_t>(ColorWriteMask::Red))   flags |= WGPUColorWriteMask_Red;
    if (raw & static_cast<uint32_t>(ColorWriteMask::Green)) flags |= WGPUColorWriteMask_Green;
    if (raw & static_cast<uint32_t>(ColorWriteMask::Blue))  flags |= WGPUColorWriteMask_Blue;
    if (raw & static_cast<uint32_t>(ColorWriteMask::Alpha)) flags |= WGPUColorWriteMask_Alpha;
    return flags;
  }

}  // namespace zs::gpu::wgpu_map
