// Copyright (c) zs contributors. Licensed under the MIT License.
// GpuVkMapping.hpp - Mapping functions from gpu:: types to Vulkan types.
//
// This header bridges the API-agnostic gpu:: enums/structs with their
// Vulkan equivalents. It is only included by the Vulkan backend
// implementation, never by cross-API client code.
//
// Each function is constexpr where possible and compiles to a simple
// lookup or switch, producing zero runtime overhead.

#pragma once

#include "GpuTypes.hpp"
#include "GpuDescriptors.hpp"

// Vulkan hpp must be available when this header is included.
#include <vulkan/vulkan.hpp>

namespace zs::gpu::vk_map {

  // =========================================================================
  // Format
  // =========================================================================
  constexpr ::vk::Format toVk(Format f) {
    switch (f) {
      case Format::Undefined:        return ::vk::Format::eUndefined;
      // 8-bit
      case Format::R8Unorm:          return ::vk::Format::eR8Unorm;
      case Format::R8Snorm:          return ::vk::Format::eR8Snorm;
      case Format::R8Uint:           return ::vk::Format::eR8Uint;
      case Format::R8Sint:           return ::vk::Format::eR8Sint;
      case Format::RG8Unorm:         return ::vk::Format::eR8G8Unorm;
      case Format::RG8Snorm:         return ::vk::Format::eR8G8Snorm;
      case Format::RG8Uint:          return ::vk::Format::eR8G8Uint;
      case Format::RG8Sint:          return ::vk::Format::eR8G8Sint;
      case Format::RGBA8Unorm:       return ::vk::Format::eR8G8B8A8Unorm;
      case Format::RGBA8UnormSrgb:   return ::vk::Format::eR8G8B8A8Srgb;
      case Format::RGBA8Snorm:       return ::vk::Format::eR8G8B8A8Snorm;
      case Format::RGBA8Uint:        return ::vk::Format::eR8G8B8A8Uint;
      case Format::RGBA8Sint:        return ::vk::Format::eR8G8B8A8Sint;
      case Format::BGRA8Unorm:       return ::vk::Format::eB8G8R8A8Unorm;
      case Format::BGRA8UnormSrgb:   return ::vk::Format::eB8G8R8A8Srgb;
      // 16-bit
      case Format::R16Uint:          return ::vk::Format::eR16Uint;
      case Format::R16Sint:          return ::vk::Format::eR16Sint;
      case Format::R16Float:         return ::vk::Format::eR16Sfloat;
      case Format::RG16Uint:         return ::vk::Format::eR16G16Uint;
      case Format::RG16Sint:         return ::vk::Format::eR16G16Sint;
      case Format::RG16Float:        return ::vk::Format::eR16G16Sfloat;
      case Format::RGBA16Uint:       return ::vk::Format::eR16G16B16A16Uint;
      case Format::RGBA16Sint:       return ::vk::Format::eR16G16B16A16Sint;
      case Format::RGBA16Float:      return ::vk::Format::eR16G16B16A16Sfloat;
      // 32-bit
      case Format::R32Uint:          return ::vk::Format::eR32Uint;
      case Format::R32Sint:          return ::vk::Format::eR32Sint;
      case Format::R32Float:         return ::vk::Format::eR32Sfloat;
      case Format::RG32Uint:         return ::vk::Format::eR32G32Uint;
      case Format::RG32Sint:         return ::vk::Format::eR32G32Sint;
      case Format::RG32Float:        return ::vk::Format::eR32G32Sfloat;
      case Format::RGB32Float:       return ::vk::Format::eR32G32B32Sfloat;
      case Format::RGBA32Uint:       return ::vk::Format::eR32G32B32A32Uint;
      case Format::RGBA32Sint:       return ::vk::Format::eR32G32B32A32Sint;
      case Format::RGBA32Float:      return ::vk::Format::eR32G32B32A32Sfloat;
      // Packed
      case Format::RGB10A2Unorm:     return ::vk::Format::eA2B10G10R10UnormPack32;
      case Format::RG11B10Float:     return ::vk::Format::eB10G11R11UfloatPack32;
      case Format::RGB9E5Float:      return ::vk::Format::eE5B9G9R9UfloatPack32;
      // Depth
      case Format::D16Unorm:         return ::vk::Format::eD16Unorm;
      case Format::D24UnormS8Uint:   return ::vk::Format::eD24UnormS8Uint;
      case Format::D32Float:         return ::vk::Format::eD32Sfloat;
      case Format::D32FloatS8Uint:   return ::vk::Format::eD32SfloatS8Uint;
      // BC
      case Format::BC1RGBAUnorm:     return ::vk::Format::eBc1RgbaUnormBlock;
      case Format::BC1RGBAUnormSrgb: return ::vk::Format::eBc1RgbaSrgbBlock;
      case Format::BC2RGBAUnorm:     return ::vk::Format::eBc2UnormBlock;
      case Format::BC2RGBAUnormSrgb: return ::vk::Format::eBc2SrgbBlock;
      case Format::BC3RGBAUnorm:     return ::vk::Format::eBc3UnormBlock;
      case Format::BC3RGBAUnormSrgb: return ::vk::Format::eBc3SrgbBlock;
      case Format::BC4RUnorm:        return ::vk::Format::eBc4UnormBlock;
      case Format::BC4RSnorm:        return ::vk::Format::eBc4SnormBlock;
      case Format::BC5RGUnorm:       return ::vk::Format::eBc5UnormBlock;
      case Format::BC5RGSnorm:       return ::vk::Format::eBc5SnormBlock;
      case Format::BC6HRGBUfloat:    return ::vk::Format::eBc6HUfloatBlock;
      case Format::BC6HRGBFloat:     return ::vk::Format::eBc6HSfloatBlock;
      case Format::BC7RGBAUnorm:     return ::vk::Format::eBc7UnormBlock;
      case Format::BC7RGBAUnormSrgb: return ::vk::Format::eBc7SrgbBlock;
      default:                       return ::vk::Format::eUndefined;
    }
  }

  constexpr Format fromVk(::vk::Format f) {
    switch (f) {
      case ::vk::Format::eR8Unorm:              return Format::R8Unorm;
      case ::vk::Format::eR8Snorm:              return Format::R8Snorm;
      case ::vk::Format::eR8Uint:               return Format::R8Uint;
      case ::vk::Format::eR8Sint:               return Format::R8Sint;
      case ::vk::Format::eR8G8Unorm:            return Format::RG8Unorm;
      case ::vk::Format::eR8G8Snorm:            return Format::RG8Snorm;
      case ::vk::Format::eR8G8Uint:             return Format::RG8Uint;
      case ::vk::Format::eR8G8Sint:             return Format::RG8Sint;
      case ::vk::Format::eR8G8B8A8Unorm:        return Format::RGBA8Unorm;
      case ::vk::Format::eR8G8B8A8Srgb:         return Format::RGBA8UnormSrgb;
      case ::vk::Format::eR8G8B8A8Snorm:        return Format::RGBA8Snorm;
      case ::vk::Format::eR8G8B8A8Uint:         return Format::RGBA8Uint;
      case ::vk::Format::eR8G8B8A8Sint:         return Format::RGBA8Sint;
      case ::vk::Format::eB8G8R8A8Unorm:        return Format::BGRA8Unorm;
      case ::vk::Format::eB8G8R8A8Srgb:         return Format::BGRA8UnormSrgb;
      case ::vk::Format::eR16Uint:              return Format::R16Uint;
      case ::vk::Format::eR16Sint:              return Format::R16Sint;
      case ::vk::Format::eR16Sfloat:            return Format::R16Float;
      case ::vk::Format::eR16G16Uint:           return Format::RG16Uint;
      case ::vk::Format::eR16G16Sint:           return Format::RG16Sint;
      case ::vk::Format::eR16G16Sfloat:         return Format::RG16Float;
      case ::vk::Format::eR16G16B16A16Uint:     return Format::RGBA16Uint;
      case ::vk::Format::eR16G16B16A16Sint:     return Format::RGBA16Sint;
      case ::vk::Format::eR16G16B16A16Sfloat:   return Format::RGBA16Float;
      case ::vk::Format::eR32Uint:              return Format::R32Uint;
      case ::vk::Format::eR32Sint:              return Format::R32Sint;
      case ::vk::Format::eR32Sfloat:            return Format::R32Float;
      case ::vk::Format::eR32G32Uint:           return Format::RG32Uint;
      case ::vk::Format::eR32G32Sint:           return Format::RG32Sint;
      case ::vk::Format::eR32G32Sfloat:         return Format::RG32Float;
      case ::vk::Format::eR32G32B32Sfloat:      return Format::RGB32Float;
      case ::vk::Format::eR32G32B32A32Uint:     return Format::RGBA32Uint;
      case ::vk::Format::eR32G32B32A32Sint:     return Format::RGBA32Sint;
      case ::vk::Format::eR32G32B32A32Sfloat:   return Format::RGBA32Float;
      case ::vk::Format::eA2B10G10R10UnormPack32: return Format::RGB10A2Unorm;
      case ::vk::Format::eB10G11R11UfloatPack32:  return Format::RG11B10Float;
      case ::vk::Format::eE5B9G9R9UfloatPack32:   return Format::RGB9E5Float;
      case ::vk::Format::eD16Unorm:             return Format::D16Unorm;
      case ::vk::Format::eD24UnormS8Uint:        return Format::D24UnormS8Uint;
      case ::vk::Format::eD32Sfloat:            return Format::D32Float;
      case ::vk::Format::eD32SfloatS8Uint:      return Format::D32FloatS8Uint;
      case ::vk::Format::eBc1RgbaUnormBlock:     return Format::BC1RGBAUnorm;
      case ::vk::Format::eBc1RgbaSrgbBlock:      return Format::BC1RGBAUnormSrgb;
      case ::vk::Format::eBc2UnormBlock:         return Format::BC2RGBAUnorm;
      case ::vk::Format::eBc2SrgbBlock:          return Format::BC2RGBAUnormSrgb;
      case ::vk::Format::eBc3UnormBlock:         return Format::BC3RGBAUnorm;
      case ::vk::Format::eBc3SrgbBlock:          return Format::BC3RGBAUnormSrgb;
      case ::vk::Format::eBc4UnormBlock:         return Format::BC4RUnorm;
      case ::vk::Format::eBc4SnormBlock:         return Format::BC4RSnorm;
      case ::vk::Format::eBc5UnormBlock:         return Format::BC5RGUnorm;
      case ::vk::Format::eBc5SnormBlock:         return Format::BC5RGSnorm;
      case ::vk::Format::eBc6HUfloatBlock:       return Format::BC6HRGBUfloat;
      case ::vk::Format::eBc6HSfloatBlock:       return Format::BC6HRGBFloat;
      case ::vk::Format::eBc7UnormBlock:         return Format::BC7RGBAUnorm;
      case ::vk::Format::eBc7SrgbBlock:          return Format::BC7RGBAUnormSrgb;
      default:                                    return Format::Undefined;
    }
  }

  // =========================================================================
  // Vertex format
  // =========================================================================
  constexpr ::vk::Format toVk(VertexFormat f) {
    switch (f) {
      case VertexFormat::Float:       return ::vk::Format::eR32Sfloat;
      case VertexFormat::Float2:      return ::vk::Format::eR32G32Sfloat;
      case VertexFormat::Float3:      return ::vk::Format::eR32G32B32Sfloat;
      case VertexFormat::Float4:      return ::vk::Format::eR32G32B32A32Sfloat;
      case VertexFormat::Int:         return ::vk::Format::eR32Sint;
      case VertexFormat::Int2:        return ::vk::Format::eR32G32Sint;
      case VertexFormat::Int3:        return ::vk::Format::eR32G32B32Sint;
      case VertexFormat::Int4:        return ::vk::Format::eR32G32B32A32Sint;
      case VertexFormat::Uint:        return ::vk::Format::eR32Uint;
      case VertexFormat::Uint2:       return ::vk::Format::eR32G32Uint;
      case VertexFormat::Uint3:       return ::vk::Format::eR32G32B32Uint;
      case VertexFormat::Uint4:       return ::vk::Format::eR32G32B32A32Uint;
      case VertexFormat::Short2:      return ::vk::Format::eR16G16Sint;
      case VertexFormat::Short4:      return ::vk::Format::eR16G16B16A16Sint;
      case VertexFormat::UShort2:     return ::vk::Format::eR16G16Uint;
      case VertexFormat::UShort4:     return ::vk::Format::eR16G16B16A16Uint;
      case VertexFormat::Short2Norm:  return ::vk::Format::eR16G16Snorm;
      case VertexFormat::Short4Norm:  return ::vk::Format::eR16G16B16A16Snorm;
      case VertexFormat::Half2:       return ::vk::Format::eR16G16Sfloat;
      case VertexFormat::Half4:       return ::vk::Format::eR16G16B16A16Sfloat;
      case VertexFormat::UChar4:      return ::vk::Format::eR8G8B8A8Uint;
      case VertexFormat::UChar4Norm:  return ::vk::Format::eR8G8B8A8Unorm;
      default:                        return ::vk::Format::eUndefined;
    }
  }

  // =========================================================================
  // Topology
  // =========================================================================
  constexpr ::vk::PrimitiveTopology toVk(Topology t) {
    switch (t) {
      case Topology::PointList:     return ::vk::PrimitiveTopology::ePointList;
      case Topology::LineList:      return ::vk::PrimitiveTopology::eLineList;
      case Topology::LineStrip:     return ::vk::PrimitiveTopology::eLineStrip;
      case Topology::TriangleList:  return ::vk::PrimitiveTopology::eTriangleList;
      case Topology::TriangleStrip: return ::vk::PrimitiveTopology::eTriangleStrip;
      default:                      return ::vk::PrimitiveTopology::eTriangleList;
    }
  }

  // =========================================================================
  // Blend
  // =========================================================================
  constexpr ::vk::BlendFactor toVk(BlendFactor f) {
    switch (f) {
      case BlendFactor::Zero:              return ::vk::BlendFactor::eZero;
      case BlendFactor::One:               return ::vk::BlendFactor::eOne;
      case BlendFactor::SrcColor:          return ::vk::BlendFactor::eSrcColor;
      case BlendFactor::OneMinusSrcColor:  return ::vk::BlendFactor::eOneMinusSrcColor;
      case BlendFactor::SrcAlpha:          return ::vk::BlendFactor::eSrcAlpha;
      case BlendFactor::OneMinusSrcAlpha:  return ::vk::BlendFactor::eOneMinusSrcAlpha;
      case BlendFactor::DstColor:          return ::vk::BlendFactor::eDstColor;
      case BlendFactor::OneMinusDstColor:  return ::vk::BlendFactor::eOneMinusDstColor;
      case BlendFactor::DstAlpha:          return ::vk::BlendFactor::eDstAlpha;
      case BlendFactor::OneMinusDstAlpha:  return ::vk::BlendFactor::eOneMinusDstAlpha;
      case BlendFactor::SrcAlphaSaturated: return ::vk::BlendFactor::eSrcAlphaSaturate;
      case BlendFactor::BlendColor:        return ::vk::BlendFactor::eConstantColor;
      case BlendFactor::OneMinusBlendColor:return ::vk::BlendFactor::eOneMinusConstantColor;
      case BlendFactor::Src1Color:         return ::vk::BlendFactor::eSrc1Color;
      case BlendFactor::OneMinusSrc1Color: return ::vk::BlendFactor::eOneMinusSrc1Color;
      case BlendFactor::Src1Alpha:         return ::vk::BlendFactor::eSrc1Alpha;
      case BlendFactor::OneMinusSrc1Alpha: return ::vk::BlendFactor::eOneMinusSrc1Alpha;
      default:                             return ::vk::BlendFactor::eZero;
    }
  }

  constexpr ::vk::BlendOp toVk(BlendOp op) {
    switch (op) {
      case BlendOp::Add:             return ::vk::BlendOp::eAdd;
      case BlendOp::Subtract:        return ::vk::BlendOp::eSubtract;
      case BlendOp::ReverseSubtract: return ::vk::BlendOp::eReverseSubtract;
      case BlendOp::Min:             return ::vk::BlendOp::eMin;
      case BlendOp::Max:             return ::vk::BlendOp::eMax;
      default:                       return ::vk::BlendOp::eAdd;
    }
  }

  // =========================================================================
  // Color write mask
  // =========================================================================
  constexpr ::vk::ColorComponentFlags toVk(ColorWriteMask mask) {
    ::vk::ColorComponentFlags result{};
    if (static_cast<uint8_t>(mask & ColorWriteMask::Red))
      result |= ::vk::ColorComponentFlagBits::eR;
    if (static_cast<uint8_t>(mask & ColorWriteMask::Green))
      result |= ::vk::ColorComponentFlagBits::eG;
    if (static_cast<uint8_t>(mask & ColorWriteMask::Blue))
      result |= ::vk::ColorComponentFlagBits::eB;
    if (static_cast<uint8_t>(mask & ColorWriteMask::Alpha))
      result |= ::vk::ColorComponentFlagBits::eA;
    return result;
  }

  // =========================================================================
  // Compare op
  // =========================================================================
  constexpr ::vk::CompareOp toVk(CompareOp op) {
    switch (op) {
      case CompareOp::Never:          return ::vk::CompareOp::eNever;
      case CompareOp::Less:           return ::vk::CompareOp::eLess;
      case CompareOp::Equal:          return ::vk::CompareOp::eEqual;
      case CompareOp::LessOrEqual:    return ::vk::CompareOp::eLessOrEqual;
      case CompareOp::Greater:        return ::vk::CompareOp::eGreater;
      case CompareOp::NotEqual:       return ::vk::CompareOp::eNotEqual;
      case CompareOp::GreaterOrEqual: return ::vk::CompareOp::eGreaterOrEqual;
      case CompareOp::Always:         return ::vk::CompareOp::eAlways;
      default:                        return ::vk::CompareOp::eAlways;
    }
  }

  // =========================================================================
  // Stencil op
  // =========================================================================
  constexpr ::vk::StencilOp toVk(StencilOp op) {
    switch (op) {
      case StencilOp::Keep:           return ::vk::StencilOp::eKeep;
      case StencilOp::Zero:           return ::vk::StencilOp::eZero;
      case StencilOp::Replace:        return ::vk::StencilOp::eReplace;
      case StencilOp::IncrementClamp: return ::vk::StencilOp::eIncrementAndClamp;
      case StencilOp::DecrementClamp: return ::vk::StencilOp::eDecrementAndClamp;
      case StencilOp::Invert:         return ::vk::StencilOp::eInvert;
      case StencilOp::IncrementWrap:  return ::vk::StencilOp::eIncrementAndWrap;
      case StencilOp::DecrementWrap:  return ::vk::StencilOp::eDecrementAndWrap;
      default:                        return ::vk::StencilOp::eKeep;
    }
  }

  // =========================================================================
  // Rasterization
  // =========================================================================
  constexpr ::vk::CullModeFlags toVk(CullMode cm) {
    switch (cm) {
      case CullMode::None:  return ::vk::CullModeFlagBits::eNone;
      case CullMode::Front: return ::vk::CullModeFlagBits::eFront;
      case CullMode::Back:  return ::vk::CullModeFlagBits::eBack;
      default:              return ::vk::CullModeFlagBits::eNone;
    }
  }

  constexpr ::vk::FrontFace toVk(FrontFace ff) {
    switch (ff) {
      case FrontFace::CounterClockwise: return ::vk::FrontFace::eCounterClockwise;
      case FrontFace::Clockwise:        return ::vk::FrontFace::eClockwise;
      default:                          return ::vk::FrontFace::eCounterClockwise;
    }
  }

  constexpr ::vk::PolygonMode toVk(PolygonMode pm) {
    switch (pm) {
      case PolygonMode::Fill:  return ::vk::PolygonMode::eFill;
      case PolygonMode::Line:  return ::vk::PolygonMode::eLine;
      case PolygonMode::Point: return ::vk::PolygonMode::ePoint;
      default:                 return ::vk::PolygonMode::eFill;
    }
  }

  // =========================================================================
  // Sample count
  // =========================================================================
  constexpr ::vk::SampleCountFlagBits toVk(SampleCount sc) {
    switch (sc) {
      case SampleCount::x1:  return ::vk::SampleCountFlagBits::e1;
      case SampleCount::x2:  return ::vk::SampleCountFlagBits::e2;
      case SampleCount::x4:  return ::vk::SampleCountFlagBits::e4;
      case SampleCount::x8:  return ::vk::SampleCountFlagBits::e8;
      case SampleCount::x16: return ::vk::SampleCountFlagBits::e16;
      case SampleCount::x32: return ::vk::SampleCountFlagBits::e32;
      case SampleCount::x64: return ::vk::SampleCountFlagBits::e64;
      default:               return ::vk::SampleCountFlagBits::e1;
    }
  }

  // =========================================================================
  // Sampler
  // =========================================================================
  constexpr ::vk::Filter toVk(FilterMode fm) {
    switch (fm) {
      case FilterMode::Nearest: return ::vk::Filter::eNearest;
      case FilterMode::Linear:  return ::vk::Filter::eLinear;
      default:                  return ::vk::Filter::eLinear;
    }
  }

  constexpr ::vk::SamplerMipmapMode toVk(MipmapMode mm) {
    switch (mm) {
      case MipmapMode::Nearest: return ::vk::SamplerMipmapMode::eNearest;
      case MipmapMode::Linear:  return ::vk::SamplerMipmapMode::eLinear;
      default:                  return ::vk::SamplerMipmapMode::eLinear;
    }
  }

  constexpr ::vk::SamplerAddressMode toVk(AddressMode am) {
    switch (am) {
      case AddressMode::Repeat:         return ::vk::SamplerAddressMode::eRepeat;
      case AddressMode::MirroredRepeat: return ::vk::SamplerAddressMode::eMirroredRepeat;
      case AddressMode::ClampToEdge:    return ::vk::SamplerAddressMode::eClampToEdge;
      case AddressMode::ClampToBorder:  return ::vk::SamplerAddressMode::eClampToBorder;
      default:                          return ::vk::SamplerAddressMode::eRepeat;
    }
  }

  constexpr ::vk::BorderColor toVk(BorderColor bc) {
    switch (bc) {
      case BorderColor::TransparentBlack: return ::vk::BorderColor::eFloatTransparentBlack;
      case BorderColor::OpaqueBlack:      return ::vk::BorderColor::eFloatOpaqueBlack;
      case BorderColor::OpaqueWhite:      return ::vk::BorderColor::eFloatOpaqueWhite;
      default:                            return ::vk::BorderColor::eFloatTransparentBlack;
    }
  }

  // =========================================================================
  // Shader stage
  // =========================================================================
  constexpr ::vk::ShaderStageFlags toVk(ShaderStage s) {
    ::vk::ShaderStageFlags result{};
    if (static_cast<uint32_t>(s & ShaderStage::Vertex))
      result |= ::vk::ShaderStageFlagBits::eVertex;
    if (static_cast<uint32_t>(s & ShaderStage::Fragment))
      result |= ::vk::ShaderStageFlagBits::eFragment;
    if (static_cast<uint32_t>(s & ShaderStage::Compute))
      result |= ::vk::ShaderStageFlagBits::eCompute;
    return result;
  }

  constexpr ::vk::ShaderStageFlagBits toVkSingle(ShaderStage s) {
    switch (s) {
      case ShaderStage::Vertex:   return ::vk::ShaderStageFlagBits::eVertex;
      case ShaderStage::Fragment: return ::vk::ShaderStageFlagBits::eFragment;
      case ShaderStage::Compute:  return ::vk::ShaderStageFlagBits::eCompute;
      default:                    return ::vk::ShaderStageFlagBits::eVertex;
    }
  }

  // =========================================================================
  // Binding type -> Vulkan descriptor type
  // =========================================================================
  constexpr ::vk::DescriptorType toVk(BindingType bt) {
    switch (bt) {
      case BindingType::UniformBuffer:         return ::vk::DescriptorType::eUniformBuffer;
      case BindingType::StorageBuffer:         return ::vk::DescriptorType::eStorageBuffer;
      case BindingType::StorageBufferReadOnly: return ::vk::DescriptorType::eStorageBuffer;
      case BindingType::SampledTexture:        return ::vk::DescriptorType::eSampledImage;
      case BindingType::StorageTexture:        return ::vk::DescriptorType::eStorageImage;
      case BindingType::Sampler:               return ::vk::DescriptorType::eSampler;
      case BindingType::ComparisonSampler:     return ::vk::DescriptorType::eSampler;
      default:                                 return ::vk::DescriptorType::eUniformBuffer;
    }
  }

  // =========================================================================
  // Load / store ops
  // =========================================================================
  constexpr ::vk::AttachmentLoadOp toVk(LoadOp op) {
    switch (op) {
      case LoadOp::Load:     return ::vk::AttachmentLoadOp::eLoad;
      case LoadOp::Clear:    return ::vk::AttachmentLoadOp::eClear;
      case LoadOp::DontCare: return ::vk::AttachmentLoadOp::eDontCare;
      default:               return ::vk::AttachmentLoadOp::eDontCare;
    }
  }

  constexpr ::vk::AttachmentStoreOp toVk(StoreOp op) {
    switch (op) {
      case StoreOp::Store:    return ::vk::AttachmentStoreOp::eStore;
      case StoreOp::DontCare: return ::vk::AttachmentStoreOp::eDontCare;
      default:                return ::vk::AttachmentStoreOp::eStore;
    }
  }

  // =========================================================================
  // Buffer usage -> Vulkan buffer usage flags
  // =========================================================================
  constexpr ::vk::BufferUsageFlags toVk(BufferUsage u) {
    ::vk::BufferUsageFlags result{};
    if (static_cast<uint32_t>(u & BufferUsage::Vertex))
      result |= ::vk::BufferUsageFlagBits::eVertexBuffer;
    if (static_cast<uint32_t>(u & BufferUsage::Index))
      result |= ::vk::BufferUsageFlagBits::eIndexBuffer;
    if (static_cast<uint32_t>(u & BufferUsage::Uniform))
      result |= ::vk::BufferUsageFlagBits::eUniformBuffer;
    if (static_cast<uint32_t>(u & BufferUsage::Storage))
      result |= ::vk::BufferUsageFlagBits::eStorageBuffer;
    if (static_cast<uint32_t>(u & BufferUsage::Indirect))
      result |= ::vk::BufferUsageFlagBits::eIndirectBuffer;
    if (static_cast<uint32_t>(u & BufferUsage::CopySrc))
      result |= ::vk::BufferUsageFlagBits::eTransferSrc;
    if (static_cast<uint32_t>(u & BufferUsage::CopyDst))
      result |= ::vk::BufferUsageFlagBits::eTransferDst;
    return result;
  }

  // =========================================================================
  // Texture usage -> Vulkan image usage flags
  // =========================================================================
  constexpr ::vk::ImageUsageFlags toVk(TextureUsage u) {
    ::vk::ImageUsageFlags result{};
    if (static_cast<uint32_t>(u & TextureUsage::Sampled))
      result |= ::vk::ImageUsageFlagBits::eSampled;
    if (static_cast<uint32_t>(u & TextureUsage::Storage))
      result |= ::vk::ImageUsageFlagBits::eStorage;
    if (static_cast<uint32_t>(u & TextureUsage::ColorAttachment))
      result |= ::vk::ImageUsageFlagBits::eColorAttachment;
    if (static_cast<uint32_t>(u & TextureUsage::DepthStencil))
      result |= ::vk::ImageUsageFlagBits::eDepthStencilAttachment;
    if (static_cast<uint32_t>(u & TextureUsage::CopySrc))
      result |= ::vk::ImageUsageFlagBits::eTransferSrc;
    if (static_cast<uint32_t>(u & TextureUsage::CopyDst))
      result |= ::vk::ImageUsageFlagBits::eTransferDst;
    if (static_cast<uint32_t>(u & TextureUsage::InputAttachment))
      result |= ::vk::ImageUsageFlagBits::eInputAttachment;
    return result;
  }

  // =========================================================================
  // Index format
  // =========================================================================
  constexpr ::vk::IndexType toVk(IndexFormat f) {
    switch (f) {
      case IndexFormat::Uint16: return ::vk::IndexType::eUint16;
      case IndexFormat::Uint32: return ::vk::IndexType::eUint32;
      default:                  return ::vk::IndexType::eUint32;
    }
  }

}  // namespace zs::gpu::vk_map
