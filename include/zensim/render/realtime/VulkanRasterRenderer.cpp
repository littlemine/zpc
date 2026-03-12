/// @file VulkanRasterRenderer.cpp
/// @brief Headless Vulkan offscreen rasteriser implementation.
///
/// Renders a RenderScene to an RGBA8 pixel buffer using zpc's Vulkan
/// infrastructure: VulkanContext, RenderPassBuilder, PipelineBuilder,
/// VkModel, and SingleUseCommandBuffer.
///
/// Conditionally compiled — requires ZS_ENABLE_VULKAN=1.

#include "zensim/render/realtime/RasterRenderer.hpp"

#include <cstdio>
#include <memory>

#if defined(ZS_ENABLE_VULKAN) && ZS_ENABLE_VULKAN

#include "zensim/vulkan/Vulkan.hpp"
#include "zensim/vulkan/VkContext.hpp"
#include "zensim/vulkan/VkModel.hpp"
#include "zensim/vulkan/VkCommand.hpp"
#include "zensim/vulkan/VkRenderPass.hpp"
#include "zensim/vulkan/VkPipeline.hpp"

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

// ---------------------------------------------------------------
// Embedded GLSL shaders (compiled at runtime via shaderc)
// ---------------------------------------------------------------

static const char* k_vert_glsl = R"(
#version 450

layout(push_constant) uniform PushConstants {
  mat4 mvp;
  vec4 material_color;   // rgb = base_color, a = alpha
  vec4 light_dir_ambient; // xyz = light direction (normalised), w = ambient
  vec4 light_color;      // rgb = light color * intensity, w = unused
} pc;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;
layout(location = 3) in vec2 inUV;

layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec3 fragColor;

void main() {
  gl_Position = pc.mvp * vec4(inPosition, 1.0);
  fragNormal  = inNormal;
  // Blend vertex color with material base_color.
  // If the material color is the default grey (0.8), vertex colors
  // dominate.  For Cornell box scenes the material color is the
  // actual surface colour and vertex colors match.
  fragColor   = pc.material_color.rgb;
}
)";

static const char* k_frag_glsl = R"(
#version 450

layout(push_constant) uniform PushConstants {
  mat4 mvp;
  vec4 material_color;
  vec4 light_dir_ambient;
  vec4 light_color;
} pc;

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec3 fragColor;

layout(location = 0) out vec4 outColor;

void main() {
  vec3 lightDir = normalize(pc.light_dir_ambient.xyz);
  float ambient = pc.light_dir_ambient.w;
  vec3 lightCol = pc.light_color.rgb;

  float NdotL   = max(dot(normalize(fragNormal), -lightDir), 0.0);
  vec3  lit     = fragColor * (lightCol * NdotL + vec3(ambient));
  outColor      = vec4(lit, 1.0);
}
)";

// ---------------------------------------------------------------
// MVP helpers — compute view & projection from Camera struct
// ---------------------------------------------------------------

using mat4 = zs::vec<f32, 4, 4>;
using vec3 = zs::vec<f32, 3>;
using vec4 = zs::vec<f32, 4>;

/// Push constant layout matching the GLSL PushConstants struct.
/// Total: 64 (mat4) + 16 + 16 + 16 = 112 bytes.
struct RasterPushConstants {
  float mvp[16];
  float material_color[4];      // rgb = base_color, a = alpha
  float light_dir_ambient[4];   // xyz = light direction, w = ambient
  float light_color[4];         // rgb = light color * intensity, w = 0
};
static_assert(sizeof(RasterPushConstants) == 112,
              "RasterPushConstants must be 112 bytes");

/// Right-handed look-at view matrix (row-major storage, accessed via (row, col)).
static mat4 buildViewMatrix(const Camera& cam) {
  // Forward = normalize(target - position)
  vec3 f;
  f(0) = cam.target(0) - cam.position(0);
  f(1) = cam.target(1) - cam.position(1);
  f(2) = cam.target(2) - cam.position(2);
  f32 flen = std::sqrt(f(0)*f(0) + f(1)*f(1) + f(2)*f(2));
  if (flen < 1e-12f) flen = 1.f;
  f(0) /= flen; f(1) /= flen; f(2) /= flen;

  // Right = normalize(forward x up)
  vec3 r;
  r(0) = f(1)*cam.up(2) - f(2)*cam.up(1);
  r(1) = f(2)*cam.up(0) - f(0)*cam.up(2);
  r(2) = f(0)*cam.up(1) - f(1)*cam.up(0);
  f32 rlen = std::sqrt(r(0)*r(0) + r(1)*r(1) + r(2)*r(2));
  if (rlen < 1e-12f) rlen = 1.f;
  r(0) /= rlen; r(1) /= rlen; r(2) /= rlen;

  // Actual up = right x forward
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
    P(1,1) = -1.f / tanHalf;  // Vulkan Y-flip
    P(2,2) = cam.far_plane / (cam.near_plane - cam.far_plane);
    P(2,3) = (cam.near_plane * cam.far_plane) / (cam.near_plane - cam.far_plane);
    P(3,2) = -1.f;
  } else {
    // Orthographic
    f32 h = cam.ortho_size;
    f32 w = h * cam.aspect_ratio;
    P(0,0) =  1.f / w;
    P(1,1) = -1.f / h;  // Vulkan Y-flip
    P(2,2) = 1.f / (cam.near_plane - cam.far_plane);
    P(2,3) = cam.near_plane / (cam.near_plane - cam.far_plane);
    P(3,3) = 1.f;
  }
  return P;
}

// ---------------------------------------------------------------
// VulkanRasterRenderer implementation
// ---------------------------------------------------------------

class VulkanRasterRenderer final : public IRasterRenderer {
public:
  VulkanRasterRenderer() = default;
  ~VulkanRasterRenderer() override { shutdown(); }

  bool init() override;
  RasterResult render(const RenderFrameRequest& request, uint32_t view_index) override;
  void shutdown() override;
  const char* name() const noexcept override { return "VulkanRasterRenderer"; }

private:
  VulkanContext* ctx_{nullptr};
  bool initialised_{false};

  // Persistent GPU objects (created once in init()).
  std::unique_ptr<RenderPass> render_pass_;
  std::unique_ptr<Pipeline> pipeline_;
  std::unique_ptr<ShaderModule> vert_shader_;
  std::unique_ptr<ShaderModule> frag_shader_;
};

// ---------------------------------------------------------------
// init
// ---------------------------------------------------------------

bool VulkanRasterRenderer::init() {
  if (initialised_) return true;

  try {
    ctx_ = &Vulkan::context();
  } catch (const std::exception& e) {
    std::fprintf(stderr, "[VulkanRasterRenderer] failed to acquire VulkanContext: %s\n", e.what());
    return false;
  }

  // Compile shaders from GLSL strings.
  try {
    vert_shader_ = std::make_unique<ShaderModule>(
        ctx_->createShaderModuleFromGlsl(k_vert_glsl,
                                         vk::ShaderStageFlagBits::eVertex,
                                         "render_vert"));
    frag_shader_ = std::make_unique<ShaderModule>(
        ctx_->createShaderModuleFromGlsl(k_frag_glsl,
                                         vk::ShaderStageFlagBits::eFragment,
                                         "render_frag"));
  } catch (const std::exception& e) {
    std::fprintf(stderr, "[VulkanRasterRenderer] shader compilation failed: %s\n", e.what());
    return false;
  }

  // Build render pass: color (RGBA8, clear→transfer-src) + depth (D32F, clear).
  try {
    render_pass_ = std::make_unique<RenderPass>(
        ctx_->renderpass()
            .addAttachment(vk::Format::eR8G8B8A8Unorm,
                           vk::ImageLayout::eUndefined,
                           vk::ImageLayout::eTransferSrcOptimal,
                           /*clear=*/true)
            .addDepthAttachment(vk::Format::eD32Sfloat, /*clear=*/true)
            .build());
  } catch (const std::exception& e) {
    std::fprintf(stderr, "[VulkanRasterRenderer] render pass creation failed: %s\n", e.what());
    return false;
  }

  // Build graphics pipeline.
  try {
    auto bindings   = VkModel::get_binding_descriptions(VkModel::draw_category_e::tri);
    auto attributes = VkModel::get_attribute_descriptions(VkModel::draw_category_e::tri);

    vk::PushConstantRange pcRange{};
    pcRange.stageFlags = vk::ShaderStageFlagBits::eVertex
                       | vk::ShaderStageFlagBits::eFragment;
    pcRange.offset     = 0;
    pcRange.size       = static_cast<uint32_t>(sizeof(RasterPushConstants));

    pipeline_ = std::make_unique<Pipeline>(
        ctx_->pipeline()
            .setShader(vk::ShaderStageFlagBits::eVertex, **vert_shader_, "main")
            .setShader(vk::ShaderStageFlagBits::eFragment, **frag_shader_, "main")
            .setRenderPass(*render_pass_, /*subpass=*/0)
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
    std::fprintf(stderr, "[VulkanRasterRenderer] pipeline creation failed: %s\n", e.what());
    return false;
  }

  initialised_ = true;
  std::printf("[VulkanRasterRenderer] initialised (headless offscreen)\n");
  return true;
}

// ---------------------------------------------------------------
// render
// ---------------------------------------------------------------

RasterResult VulkanRasterRenderer::render(const RenderFrameRequest& request,
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

  // -- Create transient offscreen images ----------------------------
  auto colorImage = ctx_->create2DImage(
      extent, vk::Format::eR8G8B8A8Unorm,
      vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc,
      vk::MemoryPropertyFlagBits::eDeviceLocal,
      /*mipmaps=*/false, /*createView=*/true, /*enableTransfer=*/true);

  auto depthImage = ctx_->create2DImage(
      extent, vk::Format::eD32Sfloat,
      vk::ImageUsageFlagBits::eDepthStencilAttachment,
      vk::MemoryPropertyFlagBits::eDeviceLocal,
      /*mipmaps=*/false, /*createView=*/true, /*enableTransfer=*/false);

  // -- Create framebuffer -------------------------------------------
  std::vector<vk::ImageView> fbViews{
      static_cast<vk::ImageView>(colorImage),
      static_cast<vk::ImageView>(depthImage)};
  auto framebuffer = ctx_->createFramebuffer(fbViews, extent, **render_pass_);

  // -- Compute MVP --------------------------------------------------
  mat4 V   = buildViewMatrix(view.camera);
  mat4 P   = buildProjectionMatrix(view.camera);
  mat4 MVP = P * V;  // model is identity for now (per-instance transform later)

  // -- Upload meshes and record draw commands -----------------------
  // Collect VkModels for all visible instances.
  const auto& scene = *request.scene;
  struct DrawItem {
    VkModel model;
    mat4    mvp;
    MaterialId material_id;
  };
  std::vector<DrawItem> draws;

  for (const auto& inst : scene.instances()) {
    if (!inst.visible) continue;

    const TriMesh* meshData = scene.findMeshData(inst.mesh);
    if (!meshData || meshData->nodes.empty()) continue;

    DrawItem item;
    // Compute per-instance MVP.
    item.mvp = MVP * inst.transform.matrix;
    item.material_id = inst.material;
    // Upload mesh to GPU via VkModel.
    item.model = VkModel(*ctx_, *meshData);
    draws.push_back(std::move(item));
  }

  if (draws.empty()) {
    result.error = "no drawable instances in scene";
    return result;
  }

  // -- Record and submit command buffer -----------------------------
  {
    SingleUseCommandBuffer cmd(*ctx_, vk_queue_e::graphics);
    vk::CommandBuffer cb = *cmd;

    std::array<vk::ClearValue, 2> clearValues;
    clearValues[0].color = vk::ClearColorValue{std::array<float,4>{{0.1f, 0.1f, 0.1f, 1.f}}};
    clearValues[1].depthStencil = vk::ClearDepthStencilValue{1.f, 0};

    vk::RenderPassBeginInfo rpBegin;
    rpBegin.renderPass  = **render_pass_;
    rpBegin.framebuffer = *framebuffer;
    rpBegin.renderArea  = vk::Rect2D{{0, 0}, extent};
    rpBegin.clearValueCount = static_cast<uint32_t>(clearValues.size());
    rpBegin.pClearValues    = clearValues.data();

    cb.beginRenderPass(rpBegin, vk::SubpassContents::eInline, ctx_->dispatcher);

    cb.bindPipeline(vk::PipelineBindPoint::eGraphics,
                    **pipeline_, ctx_->dispatcher);

    // Set viewport and scissor dynamically (although we could bake them).
    vk::Viewport vp{0.f, 0.f,
                     static_cast<float>(width),
                     static_cast<float>(height),
                     0.f, 1.f};
    cb.setViewport(0, 1, &vp, ctx_->dispatcher);
    vk::Rect2D scissor{{0,0}, extent};
    cb.setScissor(0, 1, &scissor, ctx_->dispatcher);

    for (auto& item : draws) {
      // Build push constants.
      RasterPushConstants pc{};

      // MVP (transpose to column-major for GLSL).
      for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
          pc.mvp[j * 4 + i] = item.mvp(i, j);

      // Material base_color from the scene palette.
      const Material* mat = scene.findMaterial(item.material_id);
      if (mat) {
        pc.material_color[0] = mat->base_color(0);
        pc.material_color[1] = mat->base_color(1);
        pc.material_color[2] = mat->base_color(2);
        pc.material_color[3] = mat->base_color(3);
      } else {
        // Fallback: neutral grey.
        pc.material_color[0] = 0.8f;
        pc.material_color[1] = 0.8f;
        pc.material_color[2] = 0.8f;
        pc.material_color[3] = 1.0f;
      }

      // Light: use the first light from the scene, or a default.
      if (!scene.lights().empty()) {
        const auto& light = scene.lights()[0];
        pc.light_dir_ambient[0] = light.direction(0);
        pc.light_dir_ambient[1] = light.direction(1);
        pc.light_dir_ambient[2] = light.direction(2);
        pc.light_dir_ambient[3] = 0.15f;  // ambient factor
        float li = light.intensity;
        // Clamp intensity for rasteriser to avoid blowout.
        if (li > 3.0f) li = 3.0f;
        pc.light_color[0] = light.color(0) * li;
        pc.light_color[1] = light.color(1) * li;
        pc.light_color[2] = light.color(2) * li;
        pc.light_color[3] = 0.0f;
      } else {
        // Default: directional sun from upper-left-front.
        pc.light_dir_ambient[0] = -0.5774f;
        pc.light_dir_ambient[1] = -0.5774f;
        pc.light_dir_ambient[2] = -0.5774f;
        pc.light_dir_ambient[3] = 0.15f;
        pc.light_color[0] = 1.0f;
        pc.light_color[1] = 1.0f;
        pc.light_color[2] = 1.0f;
        pc.light_color[3] = 0.0f;
      }

      cb.pushConstants(
          static_cast<vk::PipelineLayout>(*pipeline_),
          vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
          0, static_cast<uint32_t>(sizeof(RasterPushConstants)),
          &pc, ctx_->dispatcher);

      item.model.bind(cb);
      item.model.draw(cb);
    }

    cb.endRenderPass(ctx_->dispatcher);

    // -- Transition color image for transfer read -------------------
    // The render pass already transitions to eTransferSrcOptimal via
    // finalLayout, so no extra barrier is needed.

    // cmd auto-submits and waits on destruction.
  }

  // -- Readback pixels to CPU ---------------------------------------
  const vk::DeviceSize pixelBytes = static_cast<vk::DeviceSize>(width) * height * 4;
  auto staging = ctx_->createStagingBuffer(
      pixelBytes, vk::BufferUsageFlagBits::eTransferDst);

  {
    SingleUseCommandBuffer cmd(*ctx_, vk_queue_e::graphics);
    vk::CommandBuffer cb = *cmd;

    vk::BufferImageCopy region;
    region.bufferOffset      = 0;
    region.bufferRowLength   = 0;  // tightly packed
    region.bufferImageHeight = 0;
    region.imageSubresource  = vk::ImageSubresourceLayers{
        vk::ImageAspectFlagBits::eColor, 0, 0, 1};
    region.imageOffset = vk::Offset3D{0, 0, 0};
    region.imageExtent = vk::Extent3D{width, height, 1};

    cb.copyImageToBuffer(
        static_cast<vk::Image>(colorImage),
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
  return result;
}

// ---------------------------------------------------------------
// shutdown
// ---------------------------------------------------------------

void VulkanRasterRenderer::shutdown() {
  if (!initialised_) return;
  if (ctx_) ctx_->sync();

  pipeline_.reset();
  vert_shader_.reset();
  frag_shader_.reset();
  render_pass_.reset();

  initialised_ = false;
  std::printf("[VulkanRasterRenderer] shut down\n");
}

// ---------------------------------------------------------------
// Factory
// ---------------------------------------------------------------

std::unique_ptr<IRasterRenderer> createVulkanRasterRenderer() {
  return std::make_unique<VulkanRasterRenderer>();
}

}  // namespace render
}  // namespace zs

#else  // !ZS_ENABLE_VULKAN

namespace zs {
namespace render {

std::unique_ptr<IRasterRenderer> createVulkanRasterRenderer() {
  std::fprintf(stderr, "[VulkanRasterRenderer] Vulkan not enabled (ZS_ENABLE_VULKAN=0)\n");
  return nullptr;
}

}  // namespace render
}  // namespace zs

#endif  // ZS_ENABLE_VULKAN
