/// @file VulkanDeferredRenderer.cpp
/// @brief Headless Vulkan deferred renderer implementation.
///
/// Two-pass deferred rendering:
///   1. G-buffer pass (rasterization with MRT) — writes position, normal, albedo
///   2. Lighting pass (compute shader) — reads G-buffer, writes lit RGBA8 output
///
/// Conditionally compiled — requires ZS_ENABLE_VULKAN=1.

#include "zensim/render/realtime/RasterRenderer.hpp"

#include <cstdio>
#include <memory>

#if defined(ZS_ENABLE_VULKAN) && ZS_ENABLE_VULKAN

#include "zensim/vulkan/Vulkan.hpp"
#include "zensim/vulkan/VkBuffer.hpp"
#include "zensim/vulkan/VkCommand.hpp"
#include "zensim/vulkan/VkContext.hpp"
#include "zensim/vulkan/VkDescriptor.hpp"
#include "zensim/vulkan/VkImage.hpp"
#include "zensim/vulkan/VkModel.hpp"
#include "zensim/vulkan/VkPipeline.hpp"
#include "zensim/vulkan/VkRenderPass.hpp"
#include "zensim/vulkan/VkShader.hpp"

#include "zensim/render/RenderScene.hpp"
#include "zensim/render/RenderView.hpp"
#include "zensim/render/capture/RenderReadback.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace zs {
namespace render {

// =================================================================
// Embedded GLSL shaders
// =================================================================

// -- G-buffer vertex shader --
static const char* k_gbuffer_vert_glsl = R"(
#version 450

layout(push_constant) uniform PushConstants {
  mat4 mvp;
  mat4 model;
  vec4 material_color;  // rgb = base_color, a = alpha
} pc;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;
layout(location = 3) in vec2 inUV;

layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragWorldNormal;
layout(location = 2) out vec3 fragAlbedo;

void main() {
  vec4 worldPos = pc.model * vec4(inPosition, 1.0);
  fragWorldPos = worldPos.xyz;
  fragWorldNormal = mat3(pc.model) * inNormal;
  fragAlbedo = pc.material_color.rgb;
  gl_Position = pc.mvp * vec4(inPosition, 1.0);
}
)";

// -- G-buffer fragment shader (MRT output) --
static const char* k_gbuffer_frag_glsl = R"(
#version 450

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragWorldNormal;
layout(location = 2) in vec3 fragAlbedo;

// MRT outputs
layout(location = 0) out vec4 outPosition;  // world-space position
layout(location = 1) out vec4 outNormal;    // world-space normal
layout(location = 2) out vec4 outAlbedo;    // base color

void main() {
  outPosition = vec4(fragWorldPos, 1.0);
  outNormal = vec4(normalize(fragWorldNormal), 0.0);
  outAlbedo = vec4(fragAlbedo, 1.0);
}
)";

// -- Lighting compute shader --
static const char* k_lighting_comp_glsl = R"(
#version 450

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

// G-buffer inputs (combined image samplers)
layout(set = 0, binding = 0) uniform sampler2D gPosition;
layout(set = 0, binding = 1) uniform sampler2D gNormal;
layout(set = 0, binding = 2) uniform sampler2D gAlbedo;

// Output image
layout(set = 0, binding = 3, rgba8) uniform writeonly image2D outImage;

// Light SSBO
struct GPULight {
  vec4 position_type;     // xyz = position/direction, w = type (0=dir, 1=point)
  vec4 color_intensity;   // rgb = color, w = intensity
};
layout(std430, set = 0, binding = 4) readonly buffer Lights {
  GPULight lights[];
} lightBuf;

// Push constants
layout(push_constant) uniform PushConstants {
  vec4 camera_pos_numLights;  // xyz = camera position, w = numLights (as float)
  uint width;
  uint height;
  float ambient;
  float _pad;
} pc;

void main() {
  ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
  if (pixel.x >= int(pc.width) || pixel.y >= int(pc.height)) return;

  // Sample G-buffer
  vec2 uv = (vec2(pixel) + 0.5) / vec2(pc.width, pc.height);
  vec3 position = texture(gPosition, uv).xyz;
  vec3 normal = texture(gNormal, uv).xyz;
  vec3 albedo = texture(gAlbedo, uv).rgb;

  // Check for background (no geometry hit — normal is zero)
  float normalLen = length(normal);
  if (normalLen < 0.01) {
    // Background: dark grey
    imageStore(outImage, pixel, vec4(0.1, 0.1, 0.1, 1.0));
    return;
  }
  normal = normalize(normal);

  // Camera direction for specular
  vec3 viewDir = normalize(pc.camera_pos_numLights.xyz - position);

  // Accumulate lighting (Blinn-Phong)
  vec3 totalLight = vec3(pc.ambient);
  uint numLights = uint(pc.camera_pos_numLights.w);

  for (uint i = 0u; i < numLights; i++) {
    GPULight light = lightBuf.lights[i];
    uint lightType = uint(light.position_type.w);
    vec3 lightColor = light.color_intensity.rgb;
    float intensity = light.color_intensity.w;

    vec3 lightDir;
    float attenuation = 1.0;

    if (lightType == 0u) {
      // Directional light
      lightDir = normalize(-light.position_type.xyz);
    } else {
      // Point light
      vec3 toLight = light.position_type.xyz - position;
      float dist = length(toLight);
      lightDir = toLight / max(dist, 0.001);
      attenuation = 1.0 / (1.0 + dist * dist * 0.01);
    }

    // Diffuse (Lambert)
    float NdotL = max(dot(normal, lightDir), 0.0);

    // Specular (Blinn-Phong)
    vec3 halfVec = normalize(lightDir + viewDir);
    float NdotH = max(dot(normal, halfVec), 0.0);
    float specular = pow(NdotH, 32.0) * 0.3;

    totalLight += (NdotL + specular) * lightColor * intensity * attenuation;
  }

  vec3 color = albedo * totalLight;

  // Reinhard tone mapping
  color = color / (vec3(1.0) + color);

  // Gamma correction
  color = pow(color, vec3(1.0 / 2.2));

  imageStore(outImage, pixel, vec4(color, 1.0));
}
)";

// =================================================================
// MVP helpers (same as forward renderer)
// =================================================================

using mat4 = zs::vec<f32, 4, 4>;
using vec3 = zs::vec<f32, 3>;
using vec4 = zs::vec<f32, 4>;

/// G-buffer push constants: MVP (64) + model (64) + material_color (16) = 144 bytes.
struct GBufferPushConstants {
  float mvp[16];
  float model[16];
  float material_color[4];  // rgb = base_color, a = alpha
};
static_assert(sizeof(GBufferPushConstants) == 144,
              "GBufferPushConstants must be 144 bytes");

/// Lighting compute push constants: 32 bytes.
struct LightingPushConstants {
  float camera_pos_numLights[4];  // xyz = cam pos, w = numLights
  uint32_t width;
  uint32_t height;
  float ambient;
  float _pad;
};
static_assert(sizeof(LightingPushConstants) == 32,
              "LightingPushConstants must be 32 bytes");

/// GPU-packed light for the lighting SSBO (32 bytes).
struct GPULight {
  float position_type[4];   // xyz = position/direction, w = type
  float color_intensity[4]; // rgb = color, w = intensity
};
static_assert(sizeof(GPULight) == 32, "GPULight must be 32 bytes");

/// Right-handed look-at view matrix (row-major storage, accessed via (row, col)).
static mat4 buildViewMatrix(const Camera& cam) {
  vec3 f;
  f(0) = cam.target(0) - cam.position(0);
  f(1) = cam.target(1) - cam.position(1);
  f(2) = cam.target(2) - cam.position(2);
  f32 flen = std::sqrt(f(0)*f(0) + f(1)*f(1) + f(2)*f(2));
  if (flen < 1e-12f) flen = 1.f;
  f(0) /= flen; f(1) /= flen; f(2) /= flen;

  vec3 r;
  r(0) = f(1)*cam.up(2) - f(2)*cam.up(1);
  r(1) = f(2)*cam.up(0) - f(0)*cam.up(2);
  r(2) = f(0)*cam.up(1) - f(1)*cam.up(0);
  f32 rlen = std::sqrt(r(0)*r(0) + r(1)*r(1) + r(2)*r(2));
  if (rlen < 1e-12f) rlen = 1.f;
  r(0) /= rlen; r(1) /= rlen; r(2) /= rlen;

  vec3 u;
  u(0) = r(1)*f(2) - r(2)*f(1);
  u(1) = r(2)*f(0) - r(0)*f(2);
  u(2) = r(0)*f(1) - r(1)*f(0);

  mat4 V = mat4::identity();
  V(0,0) =  r(0); V(0,1) =  r(1); V(0,2) =  r(2); V(0,3) = -(r(0)*cam.position(0) + r(1)*cam.position(1) + r(2)*cam.position(2));
  V(1,0) =  u(0); V(1,1) =  u(1); V(1,2) =  u(2); V(1,3) = -(u(0)*cam.position(0) + u(1)*cam.position(1) + u(2)*cam.position(2));
  V(2,0) = -f(0); V(2,1) = -f(1); V(2,2) = -f(2); V(2,3) =  (f(0)*cam.position(0) + f(1)*cam.position(1) + f(2)*cam.position(2));
  V(3,0) = 0.f;   V(3,1) = 0.f;   V(3,2) = 0.f;   V(3,3) = 1.f;
  return V;
}

/// Vulkan-convention perspective projection: depth [0,1], Y flipped.
static mat4 buildProjectionMatrix(const Camera& cam) {
  mat4 P = mat4::zeros();
  if (cam.projection == ProjectionType::Perspective) {
    f32 tanHalf = std::tan(cam.fov_y_radians * 0.5f);
    P(0,0) = 1.f / (cam.aspect_ratio * tanHalf);
    P(1,1) = -1.f / tanHalf;
    P(2,2) = cam.far_plane / (cam.near_plane - cam.far_plane);
    P(2,3) = (cam.near_plane * cam.far_plane) / (cam.near_plane - cam.far_plane);
    P(3,2) = -1.f;
  } else {
    f32 h = cam.ortho_size;
    f32 w = h * cam.aspect_ratio;
    P(0,0) =  1.f / w;
    P(1,1) = -1.f / h;
    P(2,2) = 1.f / (cam.near_plane - cam.far_plane);
    P(2,3) = cam.near_plane / (cam.near_plane - cam.far_plane);
    P(3,3) = 1.f;
  }
  return P;
}

// =================================================================
// VulkanDeferredRenderer implementation
// =================================================================

class VulkanDeferredRenderer final : public IRasterRenderer {
public:
  VulkanDeferredRenderer() = default;
  ~VulkanDeferredRenderer() override { shutdown(); }

  bool init() override;
  RasterResult render(const RenderFrameRequest& request, uint32_t view_index) override;
  void shutdown() override;
  const char* name() const noexcept override { return "VulkanDeferredRenderer"; }

private:
  VulkanContext* ctx_{nullptr};
  bool initialised_{false};

  // G-buffer pass objects
  std::unique_ptr<RenderPass> gbuffer_pass_;
  std::unique_ptr<Pipeline> gbuffer_pipeline_;
  std::unique_ptr<ShaderModule> gbuffer_vert_shader_;
  std::unique_ptr<ShaderModule> gbuffer_frag_shader_;

  // Lighting pass objects
  std::unique_ptr<ShaderModule> lighting_comp_shader_;
  std::unique_ptr<Pipeline> lighting_pipeline_;
};

// ---------------------------------------------------------------
// init
// ---------------------------------------------------------------

bool VulkanDeferredRenderer::init() {
  if (initialised_) return true;

  try {
    ctx_ = &Vulkan::context();
  } catch (const std::exception& e) {
    std::fprintf(stderr, "[VulkanDeferredRenderer] failed to acquire VulkanContext: %s\n", e.what());
    return false;
  }

  // Check push constant size limit.
  auto devProps = ctx_->getPhysicalDevice().getProperties(ctx_->dispatcher);
  if (devProps.limits.maxPushConstantsSize < sizeof(GBufferPushConstants)) {
    std::fprintf(stderr, "[VulkanDeferredRenderer] device only supports %u bytes push constants, need %zu\n",
                 devProps.limits.maxPushConstantsSize,
                 sizeof(GBufferPushConstants));
    return false;
  }

  // -- Compile shaders --
  try {
    gbuffer_vert_shader_ = std::make_unique<ShaderModule>(
        ctx_->createShaderModuleFromGlsl(k_gbuffer_vert_glsl,
                                          vk::ShaderStageFlagBits::eVertex,
                                          "deferred_gbuffer_vert"));
    gbuffer_frag_shader_ = std::make_unique<ShaderModule>(
        ctx_->createShaderModuleFromGlsl(k_gbuffer_frag_glsl,
                                          vk::ShaderStageFlagBits::eFragment,
                                          "deferred_gbuffer_frag"));
    lighting_comp_shader_ = std::make_unique<ShaderModule>(
        ctx_->createShaderModuleFromGlsl(k_lighting_comp_glsl,
                                          vk::ShaderStageFlagBits::eCompute,
                                          "deferred_lighting_comp"));
  } catch (const std::exception& e) {
    std::fprintf(stderr, "[VulkanDeferredRenderer] shader compilation failed: %s\n", e.what());
    return false;
  }

  // -- Build G-buffer render pass --
  // 3 color attachments (position, normal, albedo) + 1 depth
  try {
    gbuffer_pass_ = std::make_unique<RenderPass>(
        ctx_->renderpass()
            // Attachment 0: Position (R16G16B16A16Sfloat)
            .addAttachment(vk::Format::eR16G16B16A16Sfloat,
                           vk::ImageLayout::eUndefined,
                           vk::ImageLayout::eShaderReadOnlyOptimal,
                           /*clear=*/true)
            // Attachment 1: Normal (R16G16B16A16Sfloat)
            .addAttachment(vk::Format::eR16G16B16A16Sfloat,
                           vk::ImageLayout::eUndefined,
                           vk::ImageLayout::eShaderReadOnlyOptimal,
                           /*clear=*/true)
            // Attachment 2: Albedo (R8G8B8A8Unorm)
            .addAttachment(vk::Format::eR8G8B8A8Unorm,
                           vk::ImageLayout::eUndefined,
                           vk::ImageLayout::eShaderReadOnlyOptimal,
                           /*clear=*/true)
            // Attachment 3: Depth (D32Sfloat)
            .addDepthAttachment(vk::Format::eD32Sfloat, /*clear=*/true)
            .build());
  } catch (const std::exception& e) {
    std::fprintf(stderr, "[VulkanDeferredRenderer] G-buffer render pass creation failed: %s\n", e.what());
    return false;
  }

  // -- Build G-buffer graphics pipeline --
  try {
    auto bindings   = VkModel::get_binding_descriptions(VkModel::draw_category_e::tri);
    auto attributes = VkModel::get_attribute_descriptions(VkModel::draw_category_e::tri);

    vk::PushConstantRange pcRange{};
    pcRange.stageFlags = vk::ShaderStageFlagBits::eVertex;
    pcRange.offset     = 0;
    pcRange.size       = static_cast<uint32_t>(sizeof(GBufferPushConstants));

    gbuffer_pipeline_ = std::make_unique<Pipeline>(
        ctx_->pipeline()
            .setShader(vk::ShaderStageFlagBits::eVertex, **gbuffer_vert_shader_, "main")
            .setShader(vk::ShaderStageFlagBits::eFragment, **gbuffer_frag_shader_, "main")
            .setRenderPass(*gbuffer_pass_, /*subpass=*/0)
            // Disable blending on all 3 G-buffer color attachments
            // (must be after setRenderPass which auto-sizes blend attachments)
            .setBlendEnable(false, 0)
            .setBlendEnable(false, 1)
            .setBlendEnable(false, 2)
            .setBindingDescriptions(bindings)
            .setAttributeDescriptions(attributes)
            .setTopology(vk::PrimitiveTopology::eTriangleList)
            .setCullMode(vk::CullModeFlagBits::eBack)
            .setFrontFace(vk::FrontFace::eCounterClockwise)
            .setDepthTestEnable(true)
            .setDepthWriteEnable(true)
            .setDepthCompareOp(vk::CompareOp::eLessOrEqual)
            .setPushConstantRange(pcRange)
            .build());
  } catch (const std::exception& e) {
    std::fprintf(stderr, "[VulkanDeferredRenderer] G-buffer pipeline creation failed: %s\n", e.what());
    return false;
  }

  // -- Build lighting compute pipeline --
  try {
    lighting_pipeline_ = std::make_unique<Pipeline>(
        *lighting_comp_shader_,
        static_cast<u32>(sizeof(LightingPushConstants)));
  } catch (const std::exception& e) {
    std::fprintf(stderr, "[VulkanDeferredRenderer] lighting pipeline creation failed: %s\n", e.what());
    return false;
  }

  initialised_ = true;
  std::printf("[VulkanDeferredRenderer] initialised on %s\n",
              devProps.deviceName.data());
  return true;
}

// ---------------------------------------------------------------
// render
// ---------------------------------------------------------------

RasterResult VulkanDeferredRenderer::render(const RenderFrameRequest& request,
                                            uint32_t view_index) {
  RasterResult result;

  if (!initialised_ || !ctx_) {
    result.error = "renderer not initialised";
    return result;
  }
  if (!request.scene || request.scene->empty()) {
    result.error = "scene is null or empty";
    return result;
  }
  if (view_index >= request.views.size()) {
    result.error = "view_index out of range";
    return result;
  }

  const auto& view = request.views[view_index];
  const uint32_t width  = view.viewport.width;
  const uint32_t height = view.viewport.height;
  const vk::Extent2D extent{width, height};

  auto t0 = std::chrono::high_resolution_clock::now();

  // =================================================================
  // Create transient G-buffer images
  // =================================================================

  // Position (R16G16B16A16Sfloat) — color attachment + sampled
  auto gPosImage = ctx_->create2DImage(
      extent, vk::Format::eR16G16B16A16Sfloat,
      vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
      vk::MemoryPropertyFlagBits::eDeviceLocal,
      /*mipmaps=*/false, /*createView=*/true, /*enableTransfer=*/false);

  // Normal (R16G16B16A16Sfloat)
  auto gNormImage = ctx_->create2DImage(
      extent, vk::Format::eR16G16B16A16Sfloat,
      vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
      vk::MemoryPropertyFlagBits::eDeviceLocal,
      /*mipmaps=*/false, /*createView=*/true, /*enableTransfer=*/false);

  // Albedo (R8G8B8A8Unorm)
  auto gAlbedoImage = ctx_->create2DImage(
      extent, vk::Format::eR8G8B8A8Unorm,
      vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
      vk::MemoryPropertyFlagBits::eDeviceLocal,
      /*mipmaps=*/false, /*createView=*/true, /*enableTransfer=*/false);

  // Depth (D32Sfloat)
  auto depthImage = ctx_->create2DImage(
      extent, vk::Format::eD32Sfloat,
      vk::ImageUsageFlagBits::eDepthStencilAttachment,
      vk::MemoryPropertyFlagBits::eDeviceLocal,
      /*mipmaps=*/false, /*createView=*/true, /*enableTransfer=*/false);

  // Output image (R8G8B8A8Unorm, storage + transfer src)
  auto outImage = ctx_->create2DImage(
      extent, vk::Format::eR8G8B8A8Unorm,
      vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc,
      vk::MemoryPropertyFlagBits::eDeviceLocal,
      /*mipmaps=*/false, /*createView=*/true, /*enableTransfer=*/true);

  // =================================================================
  // Create framebuffer for G-buffer pass
  // =================================================================

  std::vector<vk::ImageView> fbViews{
      static_cast<vk::ImageView>(gPosImage),
      static_cast<vk::ImageView>(gNormImage),
      static_cast<vk::ImageView>(gAlbedoImage),
      static_cast<vk::ImageView>(depthImage)};
  auto framebuffer = ctx_->createFramebuffer(fbViews, extent, **gbuffer_pass_);

  // =================================================================
  // Compute matrices
  // =================================================================

  mat4 V   = buildViewMatrix(view.camera);
  mat4 P   = buildProjectionMatrix(view.camera);

  // =================================================================
  // Upload meshes and collect draw items
  // =================================================================

  const auto& scene = *request.scene;
  struct DrawItem {
    VkModel model;
    mat4    mvp;
    mat4    model_mat;
    MaterialId material_id;
  };
  std::vector<DrawItem> draws;

  for (const auto& inst : scene.instances()) {
    if (!inst.visible) continue;

    const TriMesh* meshData = scene.findMeshData(inst.mesh);
    if (!meshData || meshData->nodes.empty()) continue;

    DrawItem item;
    mat4 M = inst.transform.matrix;
    item.mvp = P * V * M;
    item.model_mat = M;
    item.material_id = inst.material;
    item.model = VkModel(*ctx_, *meshData);
    draws.push_back(std::move(item));
  }

  if (draws.empty()) {
    result.error = "no drawable instances in scene";
    return result;
  }

  // =================================================================
  // Pass 1: G-buffer
  // =================================================================

  {
    SingleUseCommandBuffer cmd(*ctx_, vk_queue_e::graphics);
    vk::CommandBuffer cb = *cmd;

    std::array<vk::ClearValue, 4> clearValues;
    clearValues[0].color = vk::ClearColorValue{std::array<float,4>{{0.f, 0.f, 0.f, 0.f}}};
    clearValues[1].color = vk::ClearColorValue{std::array<float,4>{{0.f, 0.f, 0.f, 0.f}}};
    clearValues[2].color = vk::ClearColorValue{std::array<float,4>{{0.f, 0.f, 0.f, 0.f}}};
    clearValues[3].depthStencil = vk::ClearDepthStencilValue{1.f, 0};

    vk::RenderPassBeginInfo rpBegin;
    rpBegin.renderPass  = **gbuffer_pass_;
    rpBegin.framebuffer = *framebuffer;
    rpBegin.renderArea  = vk::Rect2D{{0, 0}, extent};
    rpBegin.clearValueCount = static_cast<uint32_t>(clearValues.size());
    rpBegin.pClearValues    = clearValues.data();

    cb.beginRenderPass(rpBegin, vk::SubpassContents::eInline, ctx_->dispatcher);

    cb.bindPipeline(vk::PipelineBindPoint::eGraphics,
                    **gbuffer_pipeline_, ctx_->dispatcher);

    vk::Viewport vp{0.f, 0.f,
                     static_cast<float>(width),
                     static_cast<float>(height),
                     0.f, 1.f};
    cb.setViewport(0, 1, &vp, ctx_->dispatcher);
    vk::Rect2D scissor{{0,0}, extent};
    cb.setScissor(0, 1, &scissor, ctx_->dispatcher);

    for (auto& item : draws) {
      GBufferPushConstants pc{};

      // MVP (transpose to column-major for GLSL)
      for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
          pc.mvp[j * 4 + i] = item.mvp(i, j);

      // Model matrix (transpose to column-major for GLSL)
      for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
          pc.model[j * 4 + i] = item.model_mat(i, j);

      // Material base_color
      const Material* mat = scene.findMaterial(item.material_id);
      if (mat) {
        pc.material_color[0] = mat->base_color(0);
        pc.material_color[1] = mat->base_color(1);
        pc.material_color[2] = mat->base_color(2);
        pc.material_color[3] = mat->base_color(3);
      } else {
        pc.material_color[0] = 0.8f;
        pc.material_color[1] = 0.8f;
        pc.material_color[2] = 0.8f;
        pc.material_color[3] = 1.0f;
      }

      cb.pushConstants(
          static_cast<vk::PipelineLayout>(*gbuffer_pipeline_),
          vk::ShaderStageFlagBits::eVertex,
          0, static_cast<uint32_t>(sizeof(GBufferPushConstants)),
          &pc, ctx_->dispatcher);

      item.model.bind(cb);
      item.model.draw(cb);
    }

    cb.endRenderPass(ctx_->dispatcher);
    // Render pass auto-transitions G-buffer color attachments to
    // eShaderReadOnlyOptimal via their finalLayout setting.
  }

  // =================================================================
  // Upload lights to SSBO
  // =================================================================

  std::vector<GPULight> gpuLights;
  for (const auto& light : scene.lights()) {
    GPULight gl{};
    if (light.type == LightType::Directional) {
      gl.position_type[0] = light.direction(0);
      gl.position_type[1] = light.direction(1);
      gl.position_type[2] = light.direction(2);
      gl.position_type[3] = 0.f;  // type = directional
    } else {
      gl.position_type[0] = light.position(0);
      gl.position_type[1] = light.position(1);
      gl.position_type[2] = light.position(2);
      gl.position_type[3] = 1.f;  // type = point
    }
    gl.color_intensity[0] = light.color(0);
    gl.color_intensity[1] = light.color(1);
    gl.color_intensity[2] = light.color(2);
    gl.color_intensity[3] = light.intensity;
    gpuLights.push_back(gl);
  }

  // Add default directional light if scene has no lights
  if (gpuLights.empty()) {
    GPULight def{};
    def.position_type[0] = -0.5774f;
    def.position_type[1] = -0.5774f;
    def.position_type[2] = -0.5774f;
    def.position_type[3] = 0.f;
    def.color_intensity[0] = 1.0f;
    def.color_intensity[1] = 1.0f;
    def.color_intensity[2] = 1.0f;
    def.color_intensity[3] = 1.0f;
    gpuLights.push_back(def);
  }

  vk::DeviceSize lightBufSize = gpuLights.size() * sizeof(GPULight);
  auto lightBuf = ctx_->createBuffer(
      lightBufSize,
      vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
      vk::MemoryPropertyFlagBits::eDeviceLocal);

  {
    auto staging = ctx_->createStagingBuffer(lightBufSize);
    staging.map();
    std::memcpy(staging.mappedAddress(), gpuLights.data(),
                static_cast<size_t>(lightBufSize));
    staging.unmap();

    SingleUseCommandBuffer cmd(*ctx_, vk_queue_e::graphics);
    vk::CommandBuffer cb = *cmd;
    vk::BufferCopy region{0, 0, lightBufSize};
    cb.copyBuffer(*staging, *lightBuf, 1, &region, ctx_->dispatcher);
  }

  // =================================================================
  // Pass 2: Lighting (compute)
  // =================================================================

  // Create a nearest-neighbor sampler for G-buffer reads
  vk::SamplerCreateInfo samplerInfo{};
  samplerInfo.magFilter = vk::Filter::eNearest;
  samplerInfo.minFilter = vk::Filter::eNearest;
  samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
  samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
  samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
  auto sampler = ctx_->createSampler(samplerInfo);

  // Transition output image to eGeneral for compute writes
  {
    SingleUseCommandBuffer cmd(*ctx_, vk_queue_e::graphics);
    vk::CommandBuffer cb = *cmd;

    vk::ImageMemoryBarrier barrier;
    barrier.oldLayout = vk::ImageLayout::eUndefined;
    barrier.newLayout = vk::ImageLayout::eGeneral;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = static_cast<vk::Image>(outImage);
    barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = {};
    barrier.dstAccessMask = vk::AccessFlagBits::eShaderWrite;

    cb.pipelineBarrier(
        vk::PipelineStageFlagBits::eTopOfPipe,
        vk::PipelineStageFlagBits::eComputeShader,
        {}, 0, nullptr, 0, nullptr, 1, &barrier,
        ctx_->dispatcher);
  }

  // Allocate and write descriptor set for lighting pass
  auto& dsLayout = lighting_comp_shader_->layout(0);
  vk::DescriptorSet ds;
  ctx_->acquireSet(dsLayout, ds);

  // G-buffer combined image samplers
  vk::DescriptorImageInfo gPosInfo;
  gPosInfo.sampler = *sampler;
  gPosInfo.imageView = gPosImage.view();
  gPosInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

  vk::DescriptorImageInfo gNormInfo;
  gNormInfo.sampler = *sampler;
  gNormInfo.imageView = gNormImage.view();
  gNormInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

  vk::DescriptorImageInfo gAlbedoInfo;
  gAlbedoInfo.sampler = *sampler;
  gAlbedoInfo.imageView = gAlbedoImage.view();
  gAlbedoInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

  // Output image (storage)
  vk::DescriptorImageInfo outImageInfo;
  outImageInfo.imageView = outImage.view();
  outImageInfo.imageLayout = vk::ImageLayout::eGeneral;

  // Light buffer
  auto lightBufInfo = lightBuf.descriptorInfo();

  ctx_->writeDescriptorSet(gPosInfo, ds, vk::DescriptorType::eCombinedImageSampler, 0);
  ctx_->writeDescriptorSet(gNormInfo, ds, vk::DescriptorType::eCombinedImageSampler, 1);
  ctx_->writeDescriptorSet(gAlbedoInfo, ds, vk::DescriptorType::eCombinedImageSampler, 2);
  ctx_->writeDescriptorSet(outImageInfo, ds, vk::DescriptorType::eStorageImage, 3);
  ctx_->writeDescriptorSet(lightBufInfo, ds, vk::DescriptorType::eStorageBuffer, 4);

  // Dispatch compute
  {
    SingleUseCommandBuffer cmd(*ctx_, vk_queue_e::graphics);
    vk::CommandBuffer cb = *cmd;

    cb.bindPipeline(vk::PipelineBindPoint::eCompute, **lighting_pipeline_, ctx_->dispatcher);
    cb.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                          static_cast<vk::PipelineLayout>(*lighting_pipeline_),
                          0, 1, &ds, 0, nullptr, ctx_->dispatcher);

    LightingPushConstants lpc{};
    lpc.camera_pos_numLights[0] = view.camera.position(0);
    lpc.camera_pos_numLights[1] = view.camera.position(1);
    lpc.camera_pos_numLights[2] = view.camera.position(2);
    lpc.camera_pos_numLights[3] = static_cast<float>(gpuLights.size());
    lpc.width = width;
    lpc.height = height;
    lpc.ambient = 0.15f;
    lpc._pad = 0.f;

    cb.pushConstants(
        static_cast<vk::PipelineLayout>(*lighting_pipeline_),
        vk::ShaderStageFlagBits::eCompute,
        0, static_cast<uint32_t>(sizeof(LightingPushConstants)),
        &lpc, ctx_->dispatcher);

    uint32_t groups_x = (width + 15) / 16;
    uint32_t groups_y = (height + 15) / 16;
    cb.dispatch(groups_x, groups_y, 1, ctx_->dispatcher);

    // Barrier: compute write -> transfer read
    vk::ImageMemoryBarrier barrier;
    barrier.oldLayout = vk::ImageLayout::eGeneral;
    barrier.newLayout = vk::ImageLayout::eTransferSrcOptimal;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = static_cast<vk::Image>(outImage);
    barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
    barrier.dstAccessMask = vk::AccessFlagBits::eTransferRead;

    cb.pipelineBarrier(
        vk::PipelineStageFlagBits::eComputeShader,
        vk::PipelineStageFlagBits::eTransfer,
        {}, 0, nullptr, 0, nullptr, 1, &barrier,
        ctx_->dispatcher);
  }

  // =================================================================
  // Readback pixels to CPU
  // =================================================================

  const vk::DeviceSize pixelBytes = static_cast<vk::DeviceSize>(width) * height * 4;
  auto staging = ctx_->createStagingBuffer(
      pixelBytes, vk::BufferUsageFlagBits::eTransferDst);

  {
    SingleUseCommandBuffer cmd(*ctx_, vk_queue_e::graphics);
    vk::CommandBuffer cb = *cmd;

    vk::BufferImageCopy region;
    region.bufferOffset      = 0;
    region.bufferRowLength   = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource  = vk::ImageSubresourceLayers{
        vk::ImageAspectFlagBits::eColor, 0, 0, 1};
    region.imageOffset = vk::Offset3D{0, 0, 0};
    region.imageExtent = vk::Extent3D{width, height, 1};

    cb.copyImageToBuffer(
        static_cast<vk::Image>(outImage),
        vk::ImageLayout::eTransferSrcOptimal,
        *staging, 1, &region, ctx_->dispatcher);
  }

  // Map staging and build ReadbackBuffer.
  staging.map();
  result.color = createReadback(staging.mappedAddress(), width, height, /*channels=*/4,
                                /*bytes_per_channel=*/1);
  staging.unmap();

  auto t1 = std::chrono::high_resolution_clock::now();
  result.render_time_us = static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count());

  result.success = true;
  std::printf("[VulkanDeferredRenderer] rendered %ux%u in %.1f ms\n",
              width, height, result.render_time_us / 1000.0);

  return result;
}

// ---------------------------------------------------------------
// shutdown
// ---------------------------------------------------------------

void VulkanDeferredRenderer::shutdown() {
  if (!initialised_) return;
  if (ctx_) ctx_->sync();

  lighting_pipeline_.reset();
  lighting_comp_shader_.reset();
  gbuffer_pipeline_.reset();
  gbuffer_vert_shader_.reset();
  gbuffer_frag_shader_.reset();
  gbuffer_pass_.reset();

  initialised_ = false;
  std::printf("[VulkanDeferredRenderer] shut down\n");
}

// ---------------------------------------------------------------
// Factory
// ---------------------------------------------------------------

std::unique_ptr<IRasterRenderer> createVulkanDeferredRenderer() {
  return std::make_unique<VulkanDeferredRenderer>();
}

}  // namespace render
}  // namespace zs

#else  // !ZS_ENABLE_VULKAN

namespace zs {
namespace render {

std::unique_ptr<IRasterRenderer> createVulkanDeferredRenderer() {
  std::fprintf(stderr, "[VulkanDeferredRenderer] Vulkan not enabled (ZS_ENABLE_VULKAN=0)\n");
  return nullptr;
}

}  // namespace render
}  // namespace zs

#endif  // ZS_ENABLE_VULKAN
