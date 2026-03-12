/// @file VulkanPathTracer.cpp
/// @brief Vulkan compute-based path tracer implementation.
///
/// Contains:
///   - Embedded GLSL compute shader (path tracing with BVH traversal)
///   - VulkanPathTracer class implementing IPathTracer
///   - createCornellBox() procedural scene factory
///
/// Conditionally compiled — requires ZS_ENABLE_VULKAN=1.

#include "zensim/render/offline/PathTracer.hpp"
#include "zensim/render/offline/BVHBuilder.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

#if defined(ZS_ENABLE_VULKAN) && ZS_ENABLE_VULKAN

#include "zensim/vulkan/Vulkan.hpp"
#include "zensim/vulkan/VkBuffer.hpp"
#include "zensim/vulkan/VkCommand.hpp"
#include "zensim/vulkan/VkContext.hpp"
#include "zensim/vulkan/VkDescriptor.hpp"
#include "zensim/vulkan/VkImage.hpp"
#include "zensim/vulkan/VkPipeline.hpp"
#include "zensim/vulkan/VkShader.hpp"

namespace zs {
namespace render {

// =================================================================
// Embedded GLSL compute shader — path tracer
// =================================================================

static const char* k_pathtrace_comp_glsl = R"(
#version 450

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

// --- Output image ---
layout(set = 0, binding = 0, rgba32f) uniform image2D outImage;

// --- BVH nodes ---
struct BVHNode {
  float min_x, min_y, min_z;
  uint left_first;
  float max_x, max_y, max_z;
  uint count;
};
layout(std430, set = 0, binding = 1) readonly buffer BVHNodes {
  BVHNode nodes[];
} bvh;

// --- Packed vertices ---
struct PackedVertex {
  vec4 pos;   // xyz = position, w = 0
  vec4 norm;  // xyz = normal, w = 0
};
layout(std430, set = 0, binding = 2) readonly buffer Vertices {
  PackedVertex verts[];
} vertices;

// --- Packed triangles ---
struct PackedTriangle {
  uint i0, i1, i2;
  uint material_id;
};
layout(std430, set = 0, binding = 3) readonly buffer Triangles {
  PackedTriangle tris[];
} triangles;

// --- Materials ---
struct GPUMaterial {
  vec4 albedo;    // rgb + pad
  vec4 emission;  // rgb + intensity
  vec4 params;    // x=roughness, y=ior, z=type(uint as float), w=pad
};
layout(std430, set = 0, binding = 4) readonly buffer Materials {
  GPUMaterial mats[];
} materials;

// --- Push constants ---
layout(push_constant) uniform PushConstants {
  vec4 cam_origin;
  vec4 cam_forward;
  vec4 cam_right;    // scaled by half-width
  vec4 cam_up;       // scaled by half-height
  uint width;
  uint height;
  uint frame;
  uint max_bounces;
  uint spp;
  uint num_triangles;
  uint _pad0;
  uint _pad1;
} pc;

// --- Triangle index indirection (stored after BVH nodes) ---
// Actually, tri_indices are baked into the BVH: leaf nodes reference
// ranges in the original triangle array (reordered by BVH builder).
// The GPU shader accesses triangles via bvh.nodes[n].left_first as
// the start index into the triangles buffer.

// =================================================================
// PCG random number generator
// =================================================================

uint pcg_state;

void pcg_init(uint seed) {
  pcg_state = seed * 747796405u + 2891336453u;
  pcg_state = pcg_state * 747796405u + 2891336453u;
}

uint pcg_next() {
  uint state = pcg_state;
  pcg_state = state * 747796405u + 2891336453u;
  uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
  return (word >> 22u) ^ word;
}

float pcg_float() {
  return float(pcg_next()) / 4294967296.0;
}

// =================================================================
// Ray-AABB intersection (slab method)
// =================================================================

bool intersectAABB(vec3 origin, vec3 invDir, vec3 bmin, vec3 bmax, float tmax) {
  vec3 t1 = (bmin - origin) * invDir;
  vec3 t2 = (bmax - origin) * invDir;
  vec3 tmin_v = min(t1, t2);
  vec3 tmax_v = max(t1, t2);
  float tenter = max(max(tmin_v.x, tmin_v.y), tmin_v.z);
  float texit  = min(min(tmax_v.x, tmax_v.y), tmax_v.z);
  return tenter <= texit && texit >= 0.0 && tenter < tmax;
}

// =================================================================
// Moller-Trumbore ray-triangle intersection
// =================================================================

struct HitInfo {
  float t;
  float u, v;
  uint tri_idx;
  bool hit;
};

HitInfo intersectTriangle(vec3 origin, vec3 dir, uint tri_idx) {
  HitInfo h;
  h.hit = false;
  h.t = 1e30;

  PackedTriangle tri = triangles.tris[tri_idx];
  vec3 v0 = vertices.verts[tri.i0].pos.xyz;
  vec3 v1 = vertices.verts[tri.i1].pos.xyz;
  vec3 v2 = vertices.verts[tri.i2].pos.xyz;

  vec3 e1 = v1 - v0;
  vec3 e2 = v2 - v0;
  vec3 pvec = cross(dir, e2);
  float det = dot(e1, pvec);

  if (abs(det) < 1e-8) return h;

  float inv_det = 1.0 / det;
  vec3 tvec = origin - v0;
  float u = dot(tvec, pvec) * inv_det;
  if (u < 0.0 || u > 1.0) return h;

  vec3 qvec = cross(tvec, e1);
  float v = dot(dir, qvec) * inv_det;
  if (v < 0.0 || u + v > 1.0) return h;

  float t = dot(e2, qvec) * inv_det;
  if (t < 0.001) return h;

  h.t = t;
  h.u = u;
  h.v = v;
  h.tri_idx = tri_idx;
  h.hit = true;
  return h;
}

// =================================================================
// BVH traversal
// =================================================================

HitInfo traverseBVH(vec3 origin, vec3 dir) {
  HitInfo closest;
  closest.hit = false;
  closest.t = 1e30;

  vec3 invDir = 1.0 / dir;

  // Explicit stack for traversal
  uint stack[64];
  int sp = 0;
  stack[sp++] = 0u;  // root node

  while (sp > 0) {
    uint nodeIdx = stack[--sp];
    BVHNode node = bvh.nodes[nodeIdx];

    vec3 bmin = vec3(node.min_x, node.min_y, node.min_z);
    vec3 bmax = vec3(node.max_x, node.max_y, node.max_z);

    if (!intersectAABB(origin, invDir, bmin, bmax, closest.t))
      continue;

    if (node.count > 0u) {
      // Leaf node: test all triangles
      for (uint i = 0u; i < node.count; i++) {
        HitInfo h = intersectTriangle(origin, dir, node.left_first + i);
        if (h.hit && h.t < closest.t) {
          closest = h;
        }
      }
    } else {
      // Internal node: push children
      // left_first = left child index, right child = left_first + 1
      uint left  = node.left_first;
      uint right = node.left_first + 1u;

      // Push far child first so near child is processed first
      // Simple heuristic: compare box centroids along ray direction
      BVHNode leftNode  = bvh.nodes[left];
      BVHNode rightNode = bvh.nodes[right];
      float leftMid  = (leftNode.min_x + leftNode.max_x + leftNode.min_y + leftNode.max_y + leftNode.min_z + leftNode.max_z);
      float rightMid = (rightNode.min_x + rightNode.max_x + rightNode.min_y + rightNode.max_y + rightNode.min_z + rightNode.max_z);
      float dl = dot(dir, vec3(leftNode.min_x + leftNode.max_x, leftNode.min_y + leftNode.max_y, leftNode.min_z + leftNode.max_z));
      float dr = dot(dir, vec3(rightNode.min_x + rightNode.max_x, rightNode.min_y + rightNode.max_y, rightNode.min_z + rightNode.max_z));

      if (dl < dr) {
        // left is nearer: push right first, then left
        if (sp < 63) stack[sp++] = right;
        if (sp < 63) stack[sp++] = left;
      } else {
        if (sp < 63) stack[sp++] = left;
        if (sp < 63) stack[sp++] = right;
      }
    }
  }

  return closest;
}

// =================================================================
// Cosine-weighted hemisphere sampling
// =================================================================

vec3 cosineHemisphere(vec3 normal) {
  float u1 = pcg_float();
  float u2 = pcg_float();
  float r = sqrt(u1);
  float theta = 2.0 * 3.14159265359 * u2;
  float x = r * cos(theta);
  float y = r * sin(theta);
  float z = sqrt(max(0.0, 1.0 - u1));

  // Build orthonormal basis from normal
  vec3 up = abs(normal.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
  vec3 tangent = normalize(cross(up, normal));
  vec3 bitangent = cross(normal, tangent);

  return normalize(tangent * x + bitangent * y + normal * z);
}

// =================================================================
// Perfect specular reflection
// =================================================================

vec3 reflectDir(vec3 incident, vec3 normal) {
  return incident - 2.0 * dot(incident, normal) * normal;
}

// =================================================================
// Get interpolated normal at hit point
// =================================================================

vec3 getHitNormal(HitInfo h) {
  PackedTriangle tri = triangles.tris[h.tri_idx];
  vec3 n0 = vertices.verts[tri.i0].norm.xyz;
  vec3 n1 = vertices.verts[tri.i1].norm.xyz;
  vec3 n2 = vertices.verts[tri.i2].norm.xyz;
  float w = 1.0 - h.u - h.v;
  return normalize(n0 * w + n1 * h.u + n2 * h.v);
}

// =================================================================
// Path tracing kernel
// =================================================================

vec3 tracePath(vec3 origin, vec3 dir) {
  vec3 throughput = vec3(1.0);
  vec3 radiance = vec3(0.0);

  for (uint bounce = 0u; bounce < pc.max_bounces; bounce++) {
    HitInfo hit = traverseBVH(origin, dir);

    if (!hit.hit) {
      // Miss: small ambient sky contribution
      radiance += throughput * vec3(0.01, 0.01, 0.02);
      break;
    }

    // Get material
    PackedTriangle tri = triangles.tris[hit.tri_idx];
    GPUMaterial mat = materials.mats[tri.material_id];
    uint matType = uint(mat.params.z);
    vec3 albedo = mat.albedo.rgb;
    vec3 emission = mat.emission.rgb;

    // Add emission
    radiance += throughput * emission;

    // Emissive materials don't bounce
    if (matType == 3u) break;

    // Get hit point and normal
    vec3 hitPos = origin + dir * hit.t;
    vec3 normal = getHitNormal(hit);

    // Ensure normal faces the incoming ray
    if (dot(normal, dir) > 0.0) normal = -normal;

    if (matType == 0u) {
      // Diffuse (Lambertian)
      dir = cosineHemisphere(normal);
      throughput *= albedo;
      // No explicit 1/pi * pi = 1 factor for cosine sampling
    } else if (matType == 1u) {
      // Mirror
      dir = reflectDir(dir, normal);
      throughput *= albedo;
    } else {
      // Default to diffuse for unknown types
      dir = cosineHemisphere(normal);
      throughput *= albedo;
    }

    origin = hitPos + normal * 0.001;

    // Russian roulette after 3 bounces
    if (bounce >= 3u) {
      float p = max(throughput.x, max(throughput.y, throughput.z));
      if (pcg_float() > p) break;
      throughput /= p;
    }
  }

  return radiance;
}

// =================================================================
// Main compute entry
// =================================================================

void main() {
  ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
  if (pixel.x >= int(pc.width) || pixel.y >= int(pc.height)) return;

  // Initialize RNG with pixel-unique seed
  uint seed = uint(pixel.x) + uint(pixel.y) * pc.width + pc.frame * pc.width * pc.height;
  pcg_init(seed);

  vec3 color = vec3(0.0);

  for (uint s = 0u; s < pc.spp; s++) {
    // Jittered pixel position
    float u = (float(pixel.x) + pcg_float()) / float(pc.width)  * 2.0 - 1.0;
    float v = (float(pixel.y) + pcg_float()) / float(pc.height) * 2.0 - 1.0;

    // Generate ray
    vec3 origin = pc.cam_origin.xyz;
    vec3 dir = normalize(pc.cam_forward.xyz + pc.cam_right.xyz * u + pc.cam_up.xyz * v);

    color += tracePath(origin, dir);
  }

  color /= float(pc.spp);

  // Progressive accumulation: blend with previous frames
  if (pc.frame > 0u) {
    vec4 prev = imageLoad(outImage, pixel);
    float weight = 1.0 / float(pc.frame + 1u);
    color = mix(prev.rgb, color, weight);
  }

  imageStore(outImage, pixel, vec4(color, 1.0));
}
)";

// =================================================================
// VulkanPathTracer implementation
// =================================================================

class VulkanPathTracer final : public IPathTracer {
public:
  VulkanPathTracer() = default;
  ~VulkanPathTracer() override { shutdown(); }

  bool init() override;
  void uploadScene(const PathTracerScene& scene) override;
  PathTraceResult render(const Camera& camera, const PathTracerConfig& config) override;
  void shutdown() override;
  const char* name() const noexcept override { return "VulkanPathTracer"; }

private:
  VulkanContext* ctx_{nullptr};
  bool initialised_{false};

  // Shader + pipeline (created in init)
  std::unique_ptr<ShaderModule> comp_shader_;
  std::unique_ptr<Pipeline> pipeline_;

  // Scene GPU buffers (created in uploadScene)
  std::unique_ptr<Buffer> bvh_buf_;
  std::unique_ptr<Buffer> vertex_buf_;
  std::unique_ptr<Buffer> triangle_buf_;
  std::unique_ptr<Buffer> material_buf_;

  // BVH tri_indices reordering: we need to store the reordered triangle
  // array so the GPU buffer matches the BVH's index references.
  uint32_t num_triangles_{0};
};

// ---------------------------------------------------------------
// init
// ---------------------------------------------------------------

bool VulkanPathTracer::init() {
  if (initialised_) return true;

  try {
    ctx_ = &Vulkan::context();
  } catch (const std::exception& e) {
    std::fprintf(stderr, "[VulkanPathTracer] failed to acquire VulkanContext: %s\n", e.what());
    return false;
  }

  // Compile compute shader
  try {
    comp_shader_ = std::make_unique<ShaderModule>(
        ctx_->createShaderModuleFromGlsl(k_pathtrace_comp_glsl,
                                         vk::ShaderStageFlagBits::eCompute,
                                         "pathtrace_comp"));
  } catch (const std::exception& e) {
    std::fprintf(stderr, "[VulkanPathTracer] shader compilation failed: %s\n", e.what());
    return false;
  }

  // Create compute pipeline from the shader module.
  // The Pipeline(ShaderModule, pushConstantSize) constructor auto-reflects
  // descriptor set layouts from SPIRV and creates the pipeline layout.
  try {
    pipeline_ = std::make_unique<Pipeline>(
        *comp_shader_, static_cast<u32>(sizeof(PathTracerPushConstants)));
  } catch (const std::exception& e) {
    std::fprintf(stderr, "[VulkanPathTracer] pipeline creation failed: %s\n", e.what());
    return false;
  }

  initialised_ = true;
  auto devProps = ctx_->getPhysicalDevice().getProperties(ctx_->dispatcher);
  std::printf("[VulkanPathTracer] initialised on %s\n",
              devProps.deviceName.data());
  return true;
}

// ---------------------------------------------------------------
// uploadScene
// ---------------------------------------------------------------

void VulkanPathTracer::uploadScene(const PathTracerScene& scene) {
  if (!initialised_ || !ctx_) {
    std::fprintf(stderr, "[VulkanPathTracer] not initialised, cannot upload scene\n");
    return;
  }

  // 1. Build BVH on CPU
  FlatBVH bvh = buildBVH(scene.positions, scene.triangles);
  std::printf("[VulkanPathTracer] BVH built: %zu nodes, %zu tri_indices\n",
              bvh.nodes.size(), bvh.tri_indices.size());

  // 2. Pack vertices
  std::vector<PackedVertex> packedVerts(scene.positions.size());
  for (size_t i = 0; i < scene.positions.size(); ++i) {
    packedVerts[i].px = scene.positions[i][0];
    packedVerts[i].py = scene.positions[i][1];
    packedVerts[i].pz = scene.positions[i][2];
    packedVerts[i].pw = 0.0f;
    if (i < scene.normals.size()) {
      packedVerts[i].nx = scene.normals[i][0];
      packedVerts[i].ny = scene.normals[i][1];
      packedVerts[i].nz = scene.normals[i][2];
    } else {
      packedVerts[i].nx = 0.0f;
      packedVerts[i].ny = 1.0f;
      packedVerts[i].nz = 0.0f;
    }
    packedVerts[i].nw = 0.0f;
  }

  // 3. Pack triangles — REORDERED by BVH tri_indices
  //    The BVH leaf nodes reference ranges [left_first, left_first+count)
  //    into the reordered index array. We reorder the triangle data itself
  //    so the GPU can index directly: tris[bvh_leaf.left_first + i].
  num_triangles_ = static_cast<uint32_t>(scene.triangles.size());
  std::vector<PackedTriangle> packedTris(num_triangles_);
  for (uint32_t i = 0; i < num_triangles_; ++i) {
    uint32_t origIdx = bvh.tri_indices[i];
    const auto& tri = scene.triangles[origIdx];
    packedTris[i].i0 = tri[0];
    packedTris[i].i1 = tri[1];
    packedTris[i].i2 = tri[2];
    packedTris[i].material_id = origIdx < scene.material_ids.size()
                                    ? scene.material_ids[origIdx]
                                    : 0;
  }

  // 4. Pack materials
  std::vector<RenderMaterialGPU> gpuMats(scene.materials.size());
  for (size_t i = 0; i < scene.materials.size(); ++i)
    gpuMats[i] = scene.materials[i].toGPU();

  // 5. Upload to GPU via staging buffers
  auto uploadBuffer = [&](const void* data, vk::DeviceSize size,
                          vk::BufferUsageFlags usage) -> std::unique_ptr<Buffer> {
    auto gpu = std::make_unique<Buffer>(
        ctx_->createBuffer(size, usage | vk::BufferUsageFlagBits::eTransferDst,
                           vk::MemoryPropertyFlagBits::eDeviceLocal));

    auto staging = ctx_->createStagingBuffer(size);
    staging.map();
    std::memcpy(staging.mappedAddress(), data, static_cast<size_t>(size));
    staging.unmap();

    {
      SingleUseCommandBuffer cmd(*ctx_, vk_queue_e::compute);
      vk::CommandBuffer cb = *cmd;
      vk::BufferCopy region{0, 0, size};
      cb.copyBuffer(*staging, **gpu, 1, &region, ctx_->dispatcher);
    }

    return gpu;
  };

  bvh_buf_ = uploadBuffer(bvh.nodes.data(),
                           bvh.nodes.size() * sizeof(BVHNodeGPU),
                           vk::BufferUsageFlagBits::eStorageBuffer);

  vertex_buf_ = uploadBuffer(packedVerts.data(),
                              packedVerts.size() * sizeof(PackedVertex),
                              vk::BufferUsageFlagBits::eStorageBuffer);

  triangle_buf_ = uploadBuffer(packedTris.data(),
                                packedTris.size() * sizeof(PackedTriangle),
                                vk::BufferUsageFlagBits::eStorageBuffer);

  material_buf_ = uploadBuffer(gpuMats.data(),
                                gpuMats.size() * sizeof(RenderMaterialGPU),
                                vk::BufferUsageFlagBits::eStorageBuffer);

  std::printf("[VulkanPathTracer] scene uploaded: %u tris, %zu verts, %zu mats\n",
              num_triangles_, scene.positions.size(), scene.materials.size());
}

// ---------------------------------------------------------------
// render
// ---------------------------------------------------------------

PathTraceResult VulkanPathTracer::render(const Camera& camera,
                                         const PathTracerConfig& config) {
  PathTraceResult result;
  result.width = config.width;
  result.height = config.height;

  if (!initialised_ || !ctx_) {
    result.error = "not initialised";
    return result;
  }
  if (!bvh_buf_ || !vertex_buf_ || !triangle_buf_ || !material_buf_) {
    result.error = "no scene uploaded";
    return result;
  }

  auto t0 = std::chrono::high_resolution_clock::now();

  const uint32_t width = config.width;
  const uint32_t height = config.height;
  const vk::Extent2D extent{width, height};

  // -- Create output storage image (RGBA32F) --
  auto outImage = ctx_->create2DImage(
      extent, vk::Format::eR32G32B32A32Sfloat,
      vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc,
      vk::MemoryPropertyFlagBits::eDeviceLocal,
      /*mipmaps=*/false, /*createView=*/true, /*enableTransfer=*/true);

  // -- Transition image to eGeneral for compute shader writes --
  {
    SingleUseCommandBuffer cmd(*ctx_, vk_queue_e::compute);
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

  // -- Allocate and write descriptor set --
  auto& dsLayout = comp_shader_->layout(0);
  vk::DescriptorSet ds;
  ctx_->acquireSet(dsLayout, ds);

  // Image descriptor
  vk::DescriptorImageInfo imageInfo;
  imageInfo.imageView = outImage.view();
  imageInfo.imageLayout = vk::ImageLayout::eGeneral;

  // Buffer descriptors
  auto bvhInfo = bvh_buf_->descriptorInfo();
  auto vertInfo = vertex_buf_->descriptorInfo();
  auto triInfo = triangle_buf_->descriptorInfo();
  auto matInfo = material_buf_->descriptorInfo();

  ctx_->writeDescriptorSet(imageInfo, ds, vk::DescriptorType::eStorageImage, 0);
  ctx_->writeDescriptorSet(bvhInfo, ds, vk::DescriptorType::eStorageBuffer, 1);
  ctx_->writeDescriptorSet(vertInfo, ds, vk::DescriptorType::eStorageBuffer, 2);
  ctx_->writeDescriptorSet(triInfo, ds, vk::DescriptorType::eStorageBuffer, 3);
  ctx_->writeDescriptorSet(matInfo, ds, vk::DescriptorType::eStorageBuffer, 4);

  // -- Compute camera vectors --
  // Forward = normalize(target - position)
  float fx = camera.target(0) - camera.position(0);
  float fy = camera.target(1) - camera.position(1);
  float fz = camera.target(2) - camera.position(2);
  float flen = std::sqrt(fx * fx + fy * fy + fz * fz);
  if (flen < 1e-12f) flen = 1.0f;
  fx /= flen; fy /= flen; fz /= flen;

  // Right = normalize(forward x up)
  float rx = fy * camera.up(2) - fz * camera.up(1);
  float ry = fz * camera.up(0) - fx * camera.up(2);
  float rz = fx * camera.up(1) - fy * camera.up(0);
  float rlen = std::sqrt(rx * rx + ry * ry + rz * rz);
  if (rlen < 1e-12f) rlen = 1.0f;
  rx /= rlen; ry /= rlen; rz /= rlen;

  // Actual up = right x forward
  float ux = ry * fz - rz * fy;
  float uy = rz * fx - rx * fz;
  float uz = rx * fy - ry * fx;

  // Scale right and up by tan(fov/2) to match sensor dimensions
  float half_h = std::tan(camera.fov_y_radians * 0.5f);
  float half_w = half_h * static_cast<float>(width) / static_cast<float>(height);

  // -- Dispatch compute shader (multiple frames for progressive accumulation) --
  // With spp samples per dispatch, we can do multiple dispatches for progressive
  // accumulation if desired. For now, single dispatch with all spp.
  uint32_t groups_x = (width + 15) / 16;
  uint32_t groups_y = (height + 15) / 16;

  // We dispatch once with all spp in a single pass.
  // For progressive accumulation across multiple dispatches, set frame > 0.
  {
    SingleUseCommandBuffer cmd(*ctx_, vk_queue_e::compute);
    vk::CommandBuffer cb = *cmd;

    cb.bindPipeline(vk::PipelineBindPoint::eCompute, **pipeline_, ctx_->dispatcher);
    cb.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                          static_cast<vk::PipelineLayout>(*pipeline_),
                          0, 1, &ds, 0, nullptr, ctx_->dispatcher);

    PathTracerPushConstants pc{};
    pc.cam_origin[0] = camera.position(0);
    pc.cam_origin[1] = camera.position(1);
    pc.cam_origin[2] = camera.position(2);
    pc.cam_origin[3] = 0.0f;
    pc.cam_forward[0] = fx;
    pc.cam_forward[1] = fy;
    pc.cam_forward[2] = fz;
    pc.cam_forward[3] = 0.0f;
    pc.cam_right[0] = rx * half_w;
    pc.cam_right[1] = ry * half_w;
    pc.cam_right[2] = rz * half_w;
    pc.cam_right[3] = 0.0f;
    pc.cam_up[0] = ux * half_h;
    pc.cam_up[1] = uy * half_h;
    pc.cam_up[2] = uz * half_h;
    pc.cam_up[3] = 0.0f;
    pc.width = width;
    pc.height = height;
    pc.frame = 0;
    pc.max_bounces = config.max_bounces;
    pc.spp = config.samples_per_pixel;
    pc.num_triangles = num_triangles_;
    pc._pad0 = 0;
    pc._pad1 = 0;

    cb.pushConstants(
        static_cast<vk::PipelineLayout>(*pipeline_),
        vk::ShaderStageFlagBits::eCompute,
        0, static_cast<uint32_t>(sizeof(PathTracerPushConstants)),
        &pc, ctx_->dispatcher);

    cb.dispatch(groups_x, groups_y, 1, ctx_->dispatcher);
  }

  // -- Transition image for transfer read --
  {
    SingleUseCommandBuffer cmd(*ctx_, vk_queue_e::compute);
    vk::CommandBuffer cb = *cmd;

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

  // -- Readback to CPU --
  const vk::DeviceSize floatPixelBytes =
      static_cast<vk::DeviceSize>(width) * height * 4 * sizeof(float);
  auto staging = ctx_->createStagingBuffer(
      floatPixelBytes, vk::BufferUsageFlagBits::eTransferDst);

  {
    SingleUseCommandBuffer cmd(*ctx_, vk_queue_e::compute);
    vk::CommandBuffer cb = *cmd;

    vk::BufferImageCopy region;
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource = vk::ImageSubresourceLayers{
        vk::ImageAspectFlagBits::eColor, 0, 0, 1};
    region.imageOffset = vk::Offset3D{0, 0, 0};
    region.imageExtent = vk::Extent3D{width, height, 1};

    cb.copyImageToBuffer(
        static_cast<vk::Image>(outImage),
        vk::ImageLayout::eTransferSrcOptimal,
        *staging, 1, &region, ctx_->dispatcher);
  }

  // -- Convert RGBA32F to RGBA8 --
  staging.map();
  const float* floatData = static_cast<const float*>(staging.mappedAddress());
  result.pixels.resize(static_cast<size_t>(width) * height * 4);

  for (uint32_t i = 0; i < width * height; ++i) {
    for (int c = 0; c < 3; ++c) {
      // Tone map: simple Reinhard + gamma
      float v = floatData[i * 4 + c];
      v = v / (1.0f + v);  // Reinhard
      v = std::pow(v, 1.0f / 2.2f);  // Gamma
      v = std::min(std::max(v, 0.0f), 1.0f);
      result.pixels[i * 4 + c] = static_cast<uint8_t>(v * 255.0f + 0.5f);
    }
    result.pixels[i * 4 + 3] = 255;  // Alpha
  }
  staging.unmap();

  auto t1 = std::chrono::high_resolution_clock::now();
  result.render_time_us = static_cast<double>(
      std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count());

  result.success = true;
  std::printf("[VulkanPathTracer] rendered %ux%u, %u spp, %u bounces in %.1f ms\n",
              width, height, config.samples_per_pixel, config.max_bounces,
              result.render_time_us / 1000.0);

  return result;
}

// ---------------------------------------------------------------
// shutdown
// ---------------------------------------------------------------

void VulkanPathTracer::shutdown() {
  if (!initialised_) return;
  if (ctx_) ctx_->sync();

  material_buf_.reset();
  triangle_buf_.reset();
  vertex_buf_.reset();
  bvh_buf_.reset();
  pipeline_.reset();
  comp_shader_.reset();

  initialised_ = false;
  std::printf("[VulkanPathTracer] shut down\n");
}

// =================================================================
// Factory
// =================================================================

std::unique_ptr<IPathTracer> createVulkanPathTracer() {
  return std::make_unique<VulkanPathTracer>();
}

// =================================================================
// createCornellBox — procedural Cornell Box scene
// =================================================================

PathTracerScene createCornellBox() {
  PathTracerScene scene;

  // --- Materials ---
  // 0: White diffuse (floor, ceiling, back wall)
  scene.materials.push_back(RenderMaterial::diffuse(0.73f, 0.73f, 0.73f));
  // 1: Red diffuse (left wall)
  scene.materials.push_back(RenderMaterial::diffuse(0.65f, 0.05f, 0.05f));
  // 2: Green diffuse (right wall)
  scene.materials.push_back(RenderMaterial::diffuse(0.12f, 0.45f, 0.15f));
  // 3: Light (ceiling light)
  scene.materials.push_back(RenderMaterial::emissive(1.0f, 1.0f, 1.0f, 15.0f));
  // 4: White diffuse for boxes
  scene.materials.push_back(RenderMaterial::diffuse(0.73f, 0.73f, 0.73f));

  // --- Geometry ---
  // Cornell box spans [0, 555] in all axes (classic dimensions).
  // We'll scale to [0, 5.55] for nicer numbers.
  const float S = 5.55f;

  // Helper: add a quad (two triangles) given 4 corners and a material
  auto addQuad = [&](std::array<float, 3> v0, std::array<float, 3> v1,
                     std::array<float, 3> v2, std::array<float, 3> v3,
                     uint32_t matId) {
    uint32_t base = static_cast<uint32_t>(scene.positions.size());

    // Compute face normal from first triangle
    float e1x = v1[0] - v0[0], e1y = v1[1] - v0[1], e1z = v1[2] - v0[2];
    float e2x = v2[0] - v0[0], e2y = v2[1] - v0[1], e2z = v2[2] - v0[2];
    float nx = e1y * e2z - e1z * e2y;
    float ny = e1z * e2x - e1x * e2z;
    float nz = e1x * e2y - e1y * e2x;
    float nlen = std::sqrt(nx * nx + ny * ny + nz * nz);
    if (nlen > 1e-8f) { nx /= nlen; ny /= nlen; nz /= nlen; }
    std::array<float, 3> normal{nx, ny, nz};

    scene.positions.push_back(v0);
    scene.positions.push_back(v1);
    scene.positions.push_back(v2);
    scene.positions.push_back(v3);
    scene.normals.push_back(normal);
    scene.normals.push_back(normal);
    scene.normals.push_back(normal);
    scene.normals.push_back(normal);

    scene.triangles.push_back({base, base + 1, base + 2});
    scene.material_ids.push_back(matId);
    scene.triangles.push_back({base, base + 2, base + 3});
    scene.material_ids.push_back(matId);
  };

  // Floor (y = 0)
  addQuad({0, 0, 0}, {S, 0, 0}, {S, 0, S}, {0, 0, S}, 0);

  // Ceiling (y = S)
  addQuad({0, S, S}, {S, S, S}, {S, S, 0}, {0, S, 0}, 0);

  // Back wall (z = S)
  addQuad({0, 0, S}, {S, 0, S}, {S, S, S}, {0, S, S}, 0);

  // Left wall (x = 0) — RED
  addQuad({0, 0, 0}, {0, 0, S}, {0, S, S}, {0, S, 0}, 1);

  // Right wall (x = S) — GREEN
  addQuad({S, 0, S}, {S, 0, 0}, {S, S, 0}, {S, S, S}, 2);

  // Ceiling light (small quad, slightly below ceiling)
  {
    float lx0 = S * 0.35f, lx1 = S * 0.65f;
    float lz0 = S * 0.35f, lz1 = S * 0.65f;
    float ly = S - 0.01f;
    addQuad({lx0, ly, lz0}, {lx1, ly, lz0}, {lx1, ly, lz1}, {lx0, ly, lz1}, 3);
  }

  // --- Tall box (rotated ~15 degrees) ---
  {
    // Box center approximately at (3.68, 1.65, 3.51), height 3.30
    float cx = 3.68f, cz = 3.51f;
    float hw = 0.83f;  // half-width
    float h = 3.30f;
    float angle = 15.0f * 3.14159265f / 180.0f;
    float cosA = std::cos(angle), sinA = std::sin(angle);

    auto rotPt = [&](float dx, float dz, float y) -> std::array<float, 3> {
      return {cx + dx * cosA - dz * sinA, y, cz + dx * sinA + dz * cosA};
    };

    // 4 corners at y=0 and y=h
    auto b0 = rotPt(-hw, -hw, 0.0f);
    auto b1 = rotPt(hw, -hw, 0.0f);
    auto b2 = rotPt(hw, hw, 0.0f);
    auto b3 = rotPt(-hw, hw, 0.0f);
    auto t0 = rotPt(-hw, -hw, h);
    auto t1 = rotPt(hw, -hw, h);
    auto t2 = rotPt(hw, hw, h);
    auto t3 = rotPt(-hw, hw, h);

    // Top
    addQuad(t0, t1, t2, t3, 4);
    // Front
    addQuad(b0, b1, t1, t0, 4);
    // Right
    addQuad(b1, b2, t2, t1, 4);
    // Back
    addQuad(b2, b3, t3, t2, 4);
    // Left
    addQuad(b3, b0, t0, t3, 4);
  }

  // --- Short box (rotated ~-18 degrees) ---
  {
    float cx = 1.86f, cz = 1.69f;
    float hw = 0.83f;
    float h = 1.65f;
    float angle = -18.0f * 3.14159265f / 180.0f;
    float cosA = std::cos(angle), sinA = std::sin(angle);

    auto rotPt = [&](float dx, float dz, float y) -> std::array<float, 3> {
      return {cx + dx * cosA - dz * sinA, y, cz + dx * sinA + dz * cosA};
    };

    auto b0 = rotPt(-hw, -hw, 0.0f);
    auto b1 = rotPt(hw, -hw, 0.0f);
    auto b2 = rotPt(hw, hw, 0.0f);
    auto b3 = rotPt(-hw, hw, 0.0f);
    auto t0 = rotPt(-hw, -hw, h);
    auto t1 = rotPt(hw, -hw, h);
    auto t2 = rotPt(hw, hw, h);
    auto t3 = rotPt(-hw, hw, h);

    // Top
    addQuad(t0, t1, t2, t3, 4);
    // Front
    addQuad(b0, b1, t1, t0, 4);
    // Right
    addQuad(b1, b2, t2, t1, 4);
    // Back
    addQuad(b2, b3, t3, t2, 4);
    // Left
    addQuad(b3, b0, t0, t3, 4);
  }

  std::printf("[createCornellBox] %zu triangles, %zu vertices, %zu materials\n",
              scene.triangles.size(), scene.positions.size(), scene.materials.size());

  return scene;
}

}  // namespace render
}  // namespace zs

#else  // !ZS_ENABLE_VULKAN

namespace zs {
namespace render {

std::unique_ptr<IPathTracer> createVulkanPathTracer() {
  std::fprintf(stderr, "[VulkanPathTracer] Vulkan not enabled (ZS_ENABLE_VULKAN=0)\n");
  return nullptr;
}

PathTracerScene createCornellBox() {
  PathTracerScene scene;
  std::fprintf(stderr, "[createCornellBox] Vulkan not enabled, returning empty scene\n");
  return scene;
}

}  // namespace render
}  // namespace zs

#endif  // ZS_ENABLE_VULKAN
