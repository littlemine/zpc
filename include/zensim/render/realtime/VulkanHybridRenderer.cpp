/// @file VulkanHybridRenderer.cpp
/// @brief Headless Vulkan hybrid raster + ray-tracing renderer.
///
/// Combines rasterization (for G-buffer) with hardware ray tracing
/// (for shadows and reflections). Uses the VK_KHR_ray_tracing_pipeline
/// extension with BLAS/TLAS acceleration structures.
///
/// Pipeline:
///   1. G-buffer pass (rasterization with MRT) — writes position, normal, albedo
///   2. Build BLAS/TLAS from scene geometry
///   3. RT shadows + reflections pass (ray tracing pipeline)
///   4. SSAO pass (compute)
///   5. SSAO blur pass (compute)
///   6. Lighting pass (compute) — reads G-buffer + RT results + AO
///   7. Bloom pass (compute) — threshold + separable Gaussian blur at half-res
///   8. Tone mapping pass (compute) — ACES filmic + bloom composite + gamma
///   9. FXAA pass (compute) — luminance-based anti-aliasing
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

#include <algorithm>
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

// -- G-buffer vertex shader (same as deferred) --
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

// -- G-buffer fragment shader (same as deferred) --
static const char* k_gbuffer_frag_glsl = R"(
#version 450

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragWorldNormal;
layout(location = 2) in vec3 fragAlbedo;

layout(location = 0) out vec4 outPosition;
layout(location = 1) out vec4 outNormal;
layout(location = 2) out vec4 outAlbedo;

void main() {
  outPosition = vec4(fragWorldPos, 1.0);
  outNormal   = vec4(normalize(fragWorldNormal), 0.0);
  outAlbedo   = vec4(fragAlbedo, 1.0);
}
)";

// -- Ray generation shader --
// Reads G-buffer position/normal, traces shadow ray to light and
// reflection ray for mirror surfaces.  Outputs 2-channel image:
//   R = shadow factor (1.0 = lit, 0.0 = shadowed)
//   G = reflection hit (1.0 = hit, 0.0 = miss)
//   B,A = reflection color R,G (packed)
// Plus a second image for reflection color.
static const char* k_raygen_glsl = R"(
#version 460
#extension GL_EXT_ray_tracing : require

layout(set = 0, binding = 0) uniform accelerationStructureEXT topLevelAS;
layout(set = 0, binding = 1) uniform sampler2D gPosition;
layout(set = 0, binding = 2) uniform sampler2D gNormal;
layout(set = 0, binding = 3) uniform sampler2D gAlbedo;
layout(set = 0, binding = 4, rgba16f) uniform writeonly image2D rtShadowReflect;

layout(push_constant) uniform PushConstants {
  vec4 lightDir_intensity;     // xyz = light direction (toward light), w = intensity
  vec4 cameraPos_pad;          // xyz = camera position, w = unused
  uint width;
  uint height;
  float shadowBias;            // ray origin offset along normal
  float reflectivity;          // global reflectivity factor
} pc;

layout(location = 0) rayPayloadEXT float shadowPayload;
layout(location = 1) rayPayloadEXT vec4 reflectPayload;  // rgb = color, a = hit

void main() {
  ivec2 pixel = ivec2(gl_LaunchIDEXT.xy);
  if (pixel.x >= int(pc.width) || pixel.y >= int(pc.height)) return;

  vec2 texelSize = 1.0 / vec2(pc.width, pc.height);
  vec2 uv = (vec2(pixel) + 0.5) * texelSize;

  vec3 worldPos = texture(gPosition, uv).xyz;
  vec3 worldNormal = normalize(texture(gNormal, uv).xyz);
  vec3 albedo = texture(gAlbedo, uv).rgb;

  // Check if this pixel has valid geometry (position != 0)
  float posLen = dot(worldPos, worldPos);
  if (posLen < 1e-6) {
    imageStore(rtShadowReflect, pixel, vec4(1.0, 0.0, 0.0, 0.0));
    return;
  }

  // ---- Shadow ray ----
  vec3 lightDir = normalize(pc.lightDir_intensity.xyz);
  vec3 shadowOrigin = worldPos + worldNormal * pc.shadowBias;

  shadowPayload = 0.0;  // 0 = shadowed (will be set to 1 by miss shader)
  traceRayEXT(topLevelAS,
    gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT | gl_RayFlagsSkipClosestHitShaderEXT,
    0xFF,           // cull mask
    0,              // sbt offset (shadow hit group)
    0,              // sbt stride
    0,              // miss index (shadow miss)
    shadowOrigin,   // origin
    0.001,          // tmin
    lightDir,       // direction
    100.0,          // tmax
    0               // payload location
  );
  float shadow = shadowPayload;  // 1.0 = lit, 0.0 = shadowed

  // ---- Reflection ray ----
  vec3 viewDir = normalize(worldPos - pc.cameraPos_pad.xyz);
  vec3 reflDir = reflect(viewDir, worldNormal);
  vec3 reflOrigin = worldPos + worldNormal * pc.shadowBias;

  reflectPayload = vec4(0.0);  // no hit
  traceRayEXT(topLevelAS,
    gl_RayFlagsOpaqueEXT,
    0xFF,
    1,              // sbt offset (reflection hit group)
    0,              // sbt stride
    1,              // miss index (reflection miss)
    reflOrigin,
    0.001,
    reflDir,
    100.0,
    1               // payload location
  );

  imageStore(rtShadowReflect, pixel,
    vec4(shadow, reflectPayload.a, reflectPayload.rg));
}
)";

// -- Shadow miss shader --
// If shadow ray misses, the point is lit.
static const char* k_shadow_miss_glsl = R"(
#version 460
#extension GL_EXT_ray_tracing : require

layout(location = 0) rayPayloadInEXT float shadowPayload;

void main() {
  shadowPayload = 1.0;  // lit — no occluder found
}
)";

// -- Reflection miss shader --
// Returns a simple sky/environment color on miss.
static const char* k_reflect_miss_glsl = R"(
#version 460
#extension GL_EXT_ray_tracing : require

layout(location = 1) rayPayloadInEXT vec4 reflectPayload;

void main() {
  // Sky gradient for misses
  vec3 dir = normalize(gl_WorldRayDirectionEXT);
  float t = 0.5 * (dir.y + 1.0);
  vec3 sky = mix(vec3(0.8, 0.85, 0.9), vec3(0.3, 0.5, 0.8), t);
  reflectPayload = vec4(sky, 0.5);  // a < 1 means partial reflection (sky)
}
)";

// -- Reflection closest-hit shader --
// Returns the albedo of the hit surface (simple diffuse reflection).
// Uses gl_ObjectToWorldEXT and hit barycentrics for basic shading.
static const char* k_reflect_chit_glsl = R"(
#version 460
#extension GL_EXT_ray_tracing : require

layout(location = 1) rayPayloadInEXT vec4 reflectPayload;

hitAttributeEXT vec2 attribs;

void main() {
  // Barycentrics
  vec3 bary = vec3(1.0 - attribs.x - attribs.y, attribs.x, attribs.y);

  // Approximate normal from ray direction and hit distance.
  // Without per-vertex data, we use the world-space ray direction
  // reflected through the geometric hit plane.
  // For a simple approximation, shade based on how glancing the hit is.
  vec3 rayDir = normalize(gl_WorldRayDirectionEXT);
  float hitT = gl_HitTEXT;

  // Use a combination of barycentric coords for subtle color variation
  // and a facing ratio for basic shading
  float facingRatio = abs(dot(rayDir, vec3(0.0, 1.0, 0.0)));

  // Neutral gray with slight variation from barycentrics
  vec3 color = vec3(0.55 + bary.x * 0.1, 0.55 + bary.y * 0.1, 0.6);

  // Simple distance-based attenuation
  float atten = 1.0 / (1.0 + hitT * 0.1);
  color *= mix(0.5, 1.0, atten);

  reflectPayload = vec4(color, 1.0);  // a = 1 means solid hit
}
)";

// -- Lighting compute shader (hybrid version) --
// Reads G-buffer + RT shadow/reflection results + AO.
// No shadow map needed — shadows come from ray tracing.
static const char* k_hybrid_lighting_comp_glsl = R"(
#version 450

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(set = 0, binding = 0) uniform sampler2D gPosition;
layout(set = 0, binding = 1) uniform sampler2D gNormal;
layout(set = 0, binding = 2) uniform sampler2D gAlbedo;
layout(set = 0, binding = 3, rgba16f) uniform writeonly image2D outImage;
layout(set = 0, binding = 4) buffer Lights {
  vec4 lights[];  // Each light: [pos.xyz, type] [color.rgb, intensity]
};
layout(set = 0, binding = 5) uniform sampler2D rtShadowReflect;  // RT results
layout(set = 0, binding = 6) uniform sampler2D aoMap;

layout(push_constant) uniform PushConstants {
  vec4 camera_pos_numLights;  // xyz = camera pos, w = numLights
  uint width;
  uint height;
  float ambient;
  float reflectionStrength;   // how much reflection contributes
} pc;

void main() {
  ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
  if (pixel.x >= int(pc.width) || pixel.y >= int(pc.height)) return;

  vec2 texelSize = 1.0 / vec2(pc.width, pc.height);
  vec2 uv = (vec2(pixel) + 0.5) * texelSize;

  vec3 fragPos = texture(gPosition, uv).xyz;
  vec3 fragNormal = normalize(texture(gNormal, uv).xyz);
  vec3 albedo = texture(gAlbedo, uv).rgb;

  // Check for empty pixels (background)
  float posLen = dot(fragPos, fragPos);
  if (posLen < 1e-6) {
    imageStore(outImage, pixel, vec4(0.0, 0.0, 0.0, 1.0));
    return;
  }

  // Read RT results
  vec4 rtData = texture(rtShadowReflect, uv);
  float rtShadow = rtData.r;       // 1.0 = lit, 0.0 = shadowed
  float reflHit = rtData.g;        // reflection alpha
  vec2 reflColorRG = rtData.ba;    // packed reflection R,G

  // Read AO
  float ao = texture(aoMap, uv).r;

  // Camera direction
  vec3 viewDir = normalize(pc.camera_pos_numLights.xyz - fragPos);
  int numLights = int(pc.camera_pos_numLights.w);

  // Hemisphere ambient with AO
  float hemisphereBlend = fragNormal.y * 0.5 + 0.5;
  vec3 groundColor = vec3(0.08, 0.06, 0.04);
  vec3 skyColor = vec3(0.15, 0.18, 0.25);
  vec3 ambientColor = mix(groundColor, skyColor, hemisphereBlend) * pc.ambient;
  vec3 color = albedo * ambientColor * ao;

  // Lighting with wrap factor
  float wrapFactor = 0.35;

  for (int i = 0; i < numLights; ++i) {
    vec4 posType = lights[i * 2 + 0];
    vec4 colInt  = lights[i * 2 + 1];

    vec3 lightColor = colInt.rgb * colInt.a;
    vec3 L;
    float attenuation = 1.0;

    if (posType.w < 0.5) {
      // Directional light
      L = normalize(-posType.xyz);
    } else {
      // Point light
      vec3 toLight = posType.xyz - fragPos;
      float dist = length(toLight);
      L = toLight / max(dist, 0.001);
      attenuation = 1.0 / (1.0 + dist * dist);
    }

    // Wrap diffuse
    float rawNdotL = dot(fragNormal, L);
    float NdotL = (rawNdotL + wrapFactor) / (1.0 + wrapFactor);
    NdotL = max(NdotL, 0.0);

    // Apply RT shadow (replaces shadow map)
    float shadow = rtShadow;

    // Diffuse
    vec3 diffuse = albedo * lightColor * NdotL * shadow * attenuation;

    // Specular (Blinn-Phong)
    vec3 H = normalize(L + viewDir);
    float spec = 0.0;
    if (rawNdotL > 0.0) {
      spec = pow(max(dot(fragNormal, H), 0.0), 64.0) * shadow * attenuation;
    }

    color += diffuse + lightColor * spec * 0.3;
  }

  // Add reflection contribution
  // Reconstruct approximate reflection color
  vec3 reflColor = vec3(reflColorRG, 0.5);  // approximate B channel
  color = mix(color, reflColor, reflHit * pc.reflectionStrength);

  imageStore(outImage, pixel, vec4(color, 1.0));
}
)";

// -- SSAO compute shader (same as deferred) --
static const char* k_ssao_comp_glsl = R"(
#version 450

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(set = 0, binding = 0) uniform sampler2D gPosition;
layout(set = 0, binding = 1) uniform sampler2D gNormal;
layout(set = 0, binding = 2, r8) uniform writeonly image2D aoImage;

layout(push_constant) uniform PushConstants {
  mat4 view;
  mat4 projection;
  uint width;
  uint height;
  float radius;
  float bias;
  float intensity;
  float _pad0;
} pc;

// Hash-based noise for per-pixel random rotation
float hash(vec2 p) {
  return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

vec3 randomHemisphereDir(vec2 seed, vec3 normal) {
  float u = hash(seed);
  float v = hash(seed + vec2(17.3, 59.1));
  float theta = acos(sqrt(1.0 - u));
  float phi = 6.28318530718 * v;
  vec3 dir = vec3(sin(theta) * cos(phi), sin(theta) * sin(phi), cos(theta));
  return dot(dir, normal) < 0.0 ? -dir : dir;
}

void main() {
  ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
  if (pixel.x >= int(pc.width) || pixel.y >= int(pc.height)) return;

  vec2 texelSize = 1.0 / vec2(pc.width, pc.height);
  vec2 uv = (vec2(pixel) + 0.5) * texelSize;

  vec3 fragPos = texture(gPosition, uv).xyz;
  vec3 fragNormal = normalize(texture(gNormal, uv).xyz);

  float posLen = dot(fragPos, fragPos);
  if (posLen < 1e-6) {
    imageStore(aoImage, pixel, vec4(1.0));
    return;
  }

  // Transform to view space
  vec3 viewPos = (pc.view * vec4(fragPos, 1.0)).xyz;

  const int NUM_SAMPLES = 32;
  float occlusion = 0.0;

  for (int i = 0; i < NUM_SAMPLES; ++i) {
    vec2 seed = vec2(pixel) + vec2(float(i) * 7.13, float(i) * 3.71);
    vec3 sampleDir = randomHemisphereDir(seed, fragNormal);

    vec3 samplePos = fragPos + sampleDir * pc.radius;
    vec4 projected = pc.projection * pc.view * vec4(samplePos, 1.0);
    projected.xy /= projected.w;
    vec2 sampleUV = projected.xy * 0.5 + 0.5;
    sampleUV.y = 1.0 - sampleUV.y;

    if (sampleUV.x < 0.0 || sampleUV.x > 1.0 || sampleUV.y < 0.0 || sampleUV.y > 1.0) continue;

    vec3 storedPos = texture(gPosition, sampleUV).xyz;
    vec3 storedViewPos = (pc.view * vec4(storedPos, 1.0)).xyz;
    vec3 sampleViewPos = (pc.view * vec4(samplePos, 1.0)).xyz;

    float rangeCheck = smoothstep(0.0, 1.0, pc.radius / max(abs(storedViewPos.z - viewPos.z), 0.001));
    occlusion += (storedViewPos.z > sampleViewPos.z + pc.bias ? 1.0 : 0.0) * rangeCheck;
  }

  float ao = 1.0 - (occlusion / float(NUM_SAMPLES)) * pc.intensity;
  ao = clamp(ao, 0.0, 1.0);

  imageStore(aoImage, pixel, vec4(ao));
}
)";

// -- SSAO blur compute shader (same as deferred) --
static const char* k_ssao_blur_comp_glsl = R"(
#version 450

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(set = 0, binding = 0) uniform sampler2D aoInput;
layout(set = 0, binding = 1) uniform sampler2D gPosition;
layout(set = 0, binding = 2, r8) uniform writeonly image2D aoBlurred;

layout(push_constant) uniform PushConstants {
  uint width;
  uint height;
  float _pad0;
  float _pad1;
} pc;

void main() {
  ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
  if (pixel.x >= int(pc.width) || pixel.y >= int(pc.height)) return;

  vec2 texelSize = 1.0 / vec2(pc.width, pc.height);
  vec2 uv = (vec2(pixel) + 0.5) * texelSize;

  vec3 centerPos = texture(gPosition, uv).xyz;
  float centerDepth = length(centerPos);

  float total = 0.0;
  float weight = 0.0;

  for (int dy = -2; dy <= 2; ++dy) {
    for (int dx = -2; dx <= 2; ++dx) {
      vec2 offset = vec2(float(dx), float(dy)) * texelSize;
      vec2 sampleUV = uv + offset;

      vec3 samplePos = texture(gPosition, sampleUV).xyz;
      float sampleDepth = length(samplePos);

      float depthDiff = abs(centerDepth - sampleDepth);
      float w = exp(-depthDiff * 10.0);

      total += texture(aoInput, sampleUV).r * w;
      weight += w;
    }
  }

  float ao = weight > 0.0 ? total / weight : texture(aoInput, uv).r;
  imageStore(aoBlurred, pixel, vec4(ao));
}
)";

// -- Bloom threshold compute shader (same as deferred) --
static const char* k_bloom_threshold_comp_glsl = R"(
#version 450

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(set = 0, binding = 0) uniform sampler2D hdrInput;
layout(set = 0, binding = 1, rgba16f) uniform writeonly image2D bloomOutput;

layout(push_constant) uniform PushConstants {
  uint srcWidth;
  uint srcHeight;
  uint dstWidth;
  uint dstHeight;
  float threshold;
  float softKnee;
  float _pad0;
  float _pad1;
} pc;

void main() {
  ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
  if (pixel.x >= int(pc.dstWidth) || pixel.y >= int(pc.dstHeight)) return;

  vec2 uv = (vec2(pixel) + 0.5) / vec2(pc.dstWidth, pc.dstHeight);
  vec3 color = texture(hdrInput, uv).rgb;

  float brightness = dot(color, vec3(0.2126, 0.7152, 0.0722));
  float knee = pc.threshold * pc.softKnee;
  float soft = brightness - pc.threshold + knee;
  soft = clamp(soft, 0.0, 2.0 * knee);
  soft = soft * soft / (4.0 * knee + 1e-4);

  float contribution = max(soft, brightness - pc.threshold) / max(brightness, 1e-4);
  contribution = max(contribution, 0.0);

  imageStore(bloomOutput, pixel, vec4(color * contribution, 1.0));
}
)";

// -- Bloom blur compute shader (same as deferred) --
static const char* k_bloom_blur_comp_glsl = R"(
#version 450

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(set = 0, binding = 0) uniform sampler2D blurInput;
layout(set = 0, binding = 1, rgba16f) uniform writeonly image2D blurOutput;

layout(push_constant) uniform PushConstants {
  uint width;
  uint height;
  float dirX;
  float dirY;
} pc;

void main() {
  ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
  if (pixel.x >= int(pc.width) || pixel.y >= int(pc.height)) return;

  vec2 texelSize = 1.0 / vec2(pc.width, pc.height);
  vec2 uv = (vec2(pixel) + 0.5) * texelSize;
  vec2 dir = vec2(pc.dirX, pc.dirY) * texelSize;

  // 13-tap Gaussian (sigma ~ 4.0)
  const float weights[7] = float[](
    0.0702, 0.1311, 0.1907, 0.2160, 0.1907, 0.1311, 0.0702
  );

  vec3 result = vec3(0.0);
  for (int i = -6; i <= 6; ++i) {
    float w = weights[abs(i) < 7 ? abs(i) : 6];
    result += texture(blurInput, uv + dir * float(i)).rgb * w;
  }

  imageStore(blurOutput, pixel, vec4(result, 1.0));
}
)";

// -- Tone mapping compute shader (same as deferred) --
static const char* k_tonemap_comp_glsl = R"(
#version 450

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(set = 0, binding = 0) uniform sampler2D hdrInput;
layout(set = 0, binding = 1) uniform sampler2D bloomInput;
layout(set = 0, binding = 2, rgba8) uniform writeonly image2D ldrOutput;

layout(push_constant) uniform PushConstants {
  uint width;
  uint height;
  float exposure;
  float bloomStrength;
} pc;

vec3 ACESFilm(vec3 x) {
  float a = 2.51;
  float b = 0.03;
  float c = 2.43;
  float d = 0.59;
  float e = 0.14;
  return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main() {
  ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
  if (pixel.x >= int(pc.width) || pixel.y >= int(pc.height)) return;

  vec2 texelSize = 1.0 / vec2(pc.width, pc.height);
  vec2 uv = (vec2(pixel) + 0.5) * texelSize;

  vec3 hdr = texture(hdrInput, uv).rgb;
  vec3 bloom = texture(bloomInput, uv).rgb;

  vec3 color = hdr + bloom * pc.bloomStrength;
  color *= pc.exposure;
  color = ACESFilm(color);
  color = pow(color, vec3(1.0 / 2.2));

  imageStore(ldrOutput, pixel, vec4(color, 1.0));
}
)";

// -- FXAA compute shader (same as deferred) --
static const char* k_fxaa_comp_glsl = R"(
#version 450

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(set = 0, binding = 0) uniform sampler2D ldrInput;
layout(set = 0, binding = 1, rgba8) uniform writeonly image2D fxaaOutput;

layout(push_constant) uniform PushConstants {
  uint width;
  uint height;
  float edgeThresholdMin;
  float edgeThreshold;
} pc;

float luminance(vec3 c) {
  return dot(c, vec3(0.299, 0.587, 0.114));
}

void main() {
  ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
  if (pixel.x >= int(pc.width) || pixel.y >= int(pc.height)) return;

  vec2 texelSize = 1.0 / vec2(pc.width, pc.height);
  vec2 uv = (vec2(pixel) + 0.5) * texelSize;

  vec3 rgbM  = texture(ldrInput, uv).rgb;
  vec3 rgbN  = texture(ldrInput, uv + vec2( 0.0, -1.0) * texelSize).rgb;
  vec3 rgbS  = texture(ldrInput, uv + vec2( 0.0,  1.0) * texelSize).rgb;
  vec3 rgbW  = texture(ldrInput, uv + vec2(-1.0,  0.0) * texelSize).rgb;
  vec3 rgbE  = texture(ldrInput, uv + vec2( 1.0,  0.0) * texelSize).rgb;

  float lumM = luminance(rgbM);
  float lumN = luminance(rgbN);
  float lumS = luminance(rgbS);
  float lumW = luminance(rgbW);
  float lumE = luminance(rgbE);

  float lumMin = min(lumM, min(min(lumN, lumS), min(lumW, lumE)));
  float lumMax = max(lumM, max(max(lumN, lumS), max(lumW, lumE)));
  float lumRange = lumMax - lumMin;

  if (lumRange < max(pc.edgeThresholdMin, lumMax * pc.edgeThreshold)) {
    imageStore(fxaaOutput, pixel, vec4(rgbM, 1.0));
    return;
  }

  vec3 rgbNW = texture(ldrInput, uv + vec2(-1.0, -1.0) * texelSize).rgb;
  vec3 rgbNE = texture(ldrInput, uv + vec2( 1.0, -1.0) * texelSize).rgb;
  vec3 rgbSW = texture(ldrInput, uv + vec2(-1.0,  1.0) * texelSize).rgb;
  vec3 rgbSE = texture(ldrInput, uv + vec2( 1.0,  1.0) * texelSize).rgb;

  float lumNW = luminance(rgbNW);
  float lumNE = luminance(rgbNE);
  float lumSW = luminance(rgbSW);
  float lumSE = luminance(rgbSE);

  float edgeH = abs(-2.0 * lumW + lumNW + lumSW) +
                abs(-2.0 * lumM + lumN  + lumS ) * 2.0 +
                abs(-2.0 * lumE + lumNE + lumSE);
  float edgeV = abs(-2.0 * lumN + lumNW + lumNE) +
                abs(-2.0 * lumM + lumW  + lumE ) * 2.0 +
                abs(-2.0 * lumS + lumSW + lumSE);
  bool isHorizontal = (edgeH >= edgeV);

  float lum1 = isHorizontal ? lumN : lumW;
  float lum2 = isHorizontal ? lumS : lumE;
  float grad1 = abs(lum1 - lumM);
  float grad2 = abs(lum2 - lumM);

  float stepLength = isHorizontal ? texelSize.y : texelSize.x;
  float localAvg;

  if (grad1 >= grad2) {
    stepLength = -stepLength;
    localAvg = 0.5 * (lum1 + lumM);
  } else {
    localAvg = 0.5 * (lum2 + lumM);
  }

  vec2 currentUV = uv;
  if (isHorizontal) {
    currentUV.y += stepLength * 0.5;
  } else {
    currentUV.x += stepLength * 0.5;
  }

  vec2 edgeStep = isHorizontal ? vec2(texelSize.x, 0.0) : vec2(0.0, texelSize.y);

  vec2 uv1 = currentUV - edgeStep;
  vec2 uv2 = currentUV + edgeStep;

  float lumEnd1 = luminance(texture(ldrInput, uv1).rgb) - localAvg;
  float lumEnd2 = luminance(texture(ldrInput, uv2).rgb) - localAvg;

  bool reached1 = abs(lumEnd1) >= lumRange * 0.25;
  bool reached2 = abs(lumEnd2) >= lumRange * 0.25;

  const int MAX_STEPS = 12;
  for (int i = 0; i < MAX_STEPS && !(reached1 && reached2); ++i) {
    if (!reached1) {
      uv1 -= edgeStep;
      lumEnd1 = luminance(texture(ldrInput, uv1).rgb) - localAvg;
      reached1 = abs(lumEnd1) >= lumRange * 0.25;
    }
    if (!reached2) {
      uv2 += edgeStep;
      lumEnd2 = luminance(texture(ldrInput, uv2).rgb) - localAvg;
      reached2 = abs(lumEnd2) >= lumRange * 0.25;
    }
  }

  float dist1 = isHorizontal ? (uv.x - uv1.x) : (uv.y - uv1.y);
  float dist2 = isHorizontal ? (uv2.x - uv.x) : (uv2.y - uv.y);
  float distMin = min(dist1, dist2);
  float totalDist = dist1 + dist2;

  float pixelOffset = -distMin / totalDist + 0.5;

  bool isLumCenter = (lumM - localAvg) < 0.0;
  bool correctVariation1 = (lumEnd1 < 0.0) != isLumCenter;
  bool correctVariation2 = (lumEnd2 < 0.0) != isLumCenter;

  if (dist1 < dist2 && !correctVariation1) pixelOffset = 0.0;
  if (dist2 < dist1 && !correctVariation2) pixelOffset = 0.0;

  float subPixelLum = (lumN + lumS + lumW + lumE) * 0.25;
  float subPixelDelta = clamp(abs(subPixelLum - lumM) / lumRange, 0.0, 1.0);
  float subPixelFactor = subPixelDelta * subPixelDelta * 0.75;
  pixelOffset = max(pixelOffset, subPixelFactor);

  vec2 finalUV = uv;
  if (isHorizontal) {
    finalUV.y += pixelOffset * stepLength;
  } else {
    finalUV.x += pixelOffset * stepLength;
  }

  vec3 finalColor = texture(ldrInput, finalUV).rgb;
  imageStore(fxaaOutput, pixel, vec4(finalColor, 1.0));
}
)";

// =================================================================
// Type aliases
// =================================================================

using mat4 = zs::vec<f32, 4, 4>;
using vec3 = zs::vec<f32, 3>;
using vec4 = zs::vec<f32, 4>;
using TriMesh = zs::Mesh<float, 3, u32, 3>;

// =================================================================
// Push constant structs
// =================================================================

/// G-buffer push constants: MVP (64) + model (64) + material_color (16) = 144 bytes.
struct GBufferPushConstants {
  float mvp[16];
  float model[16];
  float material_color[4];
};
static_assert(sizeof(GBufferPushConstants) == 144,
              "GBufferPushConstants must be 144 bytes");

/// SSAO push constants: 152 bytes.
struct SSAOPushConstants {
  float view[16];
  float projection[16];
  uint32_t width;
  uint32_t height;
  float radius;
  float bias;
  float intensity;
  float _pad0;
};
static_assert(sizeof(SSAOPushConstants) == 152,
              "SSAOPushConstants must be 152 bytes");

/// SSAO blur push constants: 16 bytes.
struct SSAOBlurPushConstants {
  uint32_t width;
  uint32_t height;
  float _pad0;
  float _pad1;
};
static_assert(sizeof(SSAOBlurPushConstants) == 16,
              "SSAOBlurPushConstants must be 16 bytes");

/// RT push constants: 32 bytes.
struct RTPushConstants {
  float lightDir_intensity[4];   // xyz = light direction (toward light), w = intensity
  float cameraPos_pad[4];        // xyz = camera position, w = unused
  uint32_t width;
  uint32_t height;
  float shadowBias;
  float reflectivity;
};
static_assert(sizeof(RTPushConstants) == 48,
              "RTPushConstants must be 48 bytes");

/// Hybrid lighting push constants: 32 bytes.
struct HybridLightingPushConstants {
  float camera_pos_numLights[4];
  uint32_t width;
  uint32_t height;
  float ambient;
  float reflectionStrength;
};
static_assert(sizeof(HybridLightingPushConstants) == 32,
              "HybridLightingPushConstants must be 32 bytes");

/// Bloom threshold push constants: 32 bytes.
struct BloomThresholdPushConstants {
  uint32_t srcWidth;
  uint32_t srcHeight;
  uint32_t dstWidth;
  uint32_t dstHeight;
  float threshold;
  float softKnee;
  float _pad0;
  float _pad1;
};
static_assert(sizeof(BloomThresholdPushConstants) == 32,
              "BloomThresholdPushConstants must be 32 bytes");

/// Bloom blur push constants: 16 bytes.
struct BloomBlurPushConstants {
  uint32_t width;
  uint32_t height;
  float dirX;
  float dirY;
};
static_assert(sizeof(BloomBlurPushConstants) == 16,
              "BloomBlurPushConstants must be 16 bytes");

/// Tone mapping push constants: 16 bytes.
struct TonemapPushConstants {
  uint32_t width;
  uint32_t height;
  float exposure;
  float bloomStrength;
};
static_assert(sizeof(TonemapPushConstants) == 16,
              "TonemapPushConstants must be 16 bytes");

/// FXAA push constants: 16 bytes.
struct FXAAPushConstants {
  uint32_t width;
  uint32_t height;
  float edgeThresholdMin;
  float edgeThreshold;
};
static_assert(sizeof(FXAAPushConstants) == 16,
              "FXAAPushConstants must be 16 bytes");

/// GPU-packed light (32 bytes).
struct GPULight {
  float position_type[4];
  float color_intensity[4];
};
static_assert(sizeof(GPULight) == 32, "GPULight must be 32 bytes");

// =================================================================
// Helper functions
// =================================================================

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

static void computeSceneAABB(const RenderScene& scene,
                              vec3& outMin, vec3& outMax) {
  outMin(0) =  1e30f; outMin(1) =  1e30f; outMin(2) =  1e30f;
  outMax(0) = -1e30f; outMax(1) = -1e30f; outMax(2) = -1e30f;

  for (const auto& inst : scene.instances()) {
    if (!inst.visible) continue;
    const TriMesh* mesh = scene.findMeshData(inst.mesh);
    if (!mesh || mesh->nodes.empty()) continue;

    const auto& M = inst.transform.matrix;
    for (const auto& node : mesh->nodes) {
      f32 wx = M(0,0)*node[0] + M(0,1)*node[1] + M(0,2)*node[2] + M(0,3);
      f32 wy = M(1,0)*node[0] + M(1,1)*node[1] + M(1,2)*node[2] + M(1,3);
      f32 wz = M(2,0)*node[0] + M(2,1)*node[1] + M(2,2)*node[2] + M(2,3);

      if (wx < outMin(0)) outMin(0) = wx;
      if (wy < outMin(1)) outMin(1) = wy;
      if (wz < outMin(2)) outMin(2) = wz;
      if (wx > outMax(0)) outMax(0) = wx;
      if (wy > outMax(1)) outMax(1) = wy;
      if (wz > outMax(2)) outMax(2) = wz;
    }
  }

  if (outMin(0) > outMax(0)) {
    outMin(0) = -1.f; outMin(1) = -1.f; outMin(2) = -1.f;
    outMax(0) =  1.f; outMax(1) =  1.f; outMax(2) =  1.f;
  }
}

// =================================================================
// Hybrid renderer class
// =================================================================

class VulkanHybridRenderer final : public IRasterRenderer {
public:
  VulkanHybridRenderer() = default;
  ~VulkanHybridRenderer() override { shutdown(); }

  bool init() override;
  RasterResult render(const RenderFrameRequest& request, uint32_t view_index) override;
  void shutdown() override;
  const char* name() const noexcept override { return "VulkanHybridRenderer"; }

private:
  VulkanContext* ctx_{nullptr};
  bool initialised_{false};

  // G-buffer pass
  std::unique_ptr<RenderPass> gbuffer_pass_;
  std::unique_ptr<Pipeline> gbuffer_pipeline_;
  std::unique_ptr<ShaderModule> gbuffer_vert_shader_;
  std::unique_ptr<ShaderModule> gbuffer_frag_shader_;

  // RT shaders (compiled but pipeline built per-frame due to descriptor set layout)
  std::unique_ptr<ShaderModule> raygen_shader_;
  std::unique_ptr<ShaderModule> shadow_miss_shader_;
  std::unique_ptr<ShaderModule> reflect_miss_shader_;
  std::unique_ptr<ShaderModule> reflect_chit_shader_;

  // Lighting pass (hybrid)
  std::unique_ptr<ShaderModule> lighting_comp_shader_;
  std::unique_ptr<Pipeline> lighting_pipeline_;

  // SSAO
  std::unique_ptr<ShaderModule> ssao_comp_shader_;
  std::unique_ptr<Pipeline> ssao_pipeline_;
  std::unique_ptr<ShaderModule> ssao_blur_comp_shader_;
  std::unique_ptr<Pipeline> ssao_blur_pipeline_;

  // Post-processing
  std::unique_ptr<ShaderModule> bloom_threshold_comp_shader_;
  std::unique_ptr<Pipeline> bloom_threshold_pipeline_;
  std::unique_ptr<ShaderModule> bloom_blur_comp_shader_;
  std::unique_ptr<Pipeline> bloom_blur_pipeline_;
  std::unique_ptr<ShaderModule> tonemap_comp_shader_;
  std::unique_ptr<Pipeline> tonemap_pipeline_;
  std::unique_ptr<ShaderModule> fxaa_comp_shader_;
  std::unique_ptr<Pipeline> fxaa_pipeline_;

  // RT pipeline (built once during init)
  vk::Pipeline rt_pipeline_{VK_NULL_HANDLE};
  vk::PipelineLayout rt_pipeline_layout_{VK_NULL_HANDLE};
  std::unique_ptr<DescriptorSetLayout> rt_ds_layout_owned_;  // RAII owner
  vk::DescriptorSetLayout rt_ds_layout_{VK_NULL_HANDLE};     // cached raw handle

  // Shader binding table
  std::unique_ptr<Buffer> sbt_buffer_;
  vk::StridedDeviceAddressRegionKHR sbt_raygen_{};
  vk::StridedDeviceAddressRegionKHR sbt_miss_{};
  vk::StridedDeviceAddressRegionKHR sbt_hit_{};
  vk::StridedDeviceAddressRegionKHR sbt_callable_{};

  // Helper: build BLAS from vertex/index data
  struct BLASInfo {
    vk::AccelerationStructureKHR handle{VK_NULL_HANDLE};
    std::unique_ptr<Buffer> buffer;
  };

  struct TLASInfo {
    vk::AccelerationStructureKHR handle{VK_NULL_HANDLE};
    std::unique_ptr<Buffer> buffer;
  };

  // Build acceleration structures
  BLASInfo buildBLAS(const Buffer& vertexBuf, uint32_t vertexCount,
                     const Buffer& indexBuf, uint32_t indexCount,
                     uint32_t vertexStride);
  TLASInfo buildTLAS(const std::vector<vk::AccelerationStructureInstanceKHR>& instances);
  void destroyBLAS(BLASInfo& blas);
  void destroyTLAS(TLASInfo& tlas);

  // Build RT pipeline and SBT
  bool buildRTPipeline();
  void destroyRTPipeline();
};

// ---------------------------------------------------------------
// buildBLAS
// ---------------------------------------------------------------

VulkanHybridRenderer::BLASInfo VulkanHybridRenderer::buildBLAS(
    const Buffer& vertexBuf, uint32_t vertexCount,
    const Buffer& indexBuf, uint32_t indexCount,
    uint32_t vertexStride) {
  BLASInfo result;

  // Get device addresses
  vk::BufferDeviceAddressInfo vertAddrInfo;
  vertAddrInfo.buffer = static_cast<vk::Buffer>(vertexBuf);
  auto vertAddr = ctx_->getDevice().getBufferAddress(vertAddrInfo, ctx_->dispatcher);

  vk::BufferDeviceAddressInfo idxAddrInfo;
  idxAddrInfo.buffer = static_cast<vk::Buffer>(indexBuf);
  auto idxAddr = ctx_->getDevice().getBufferAddress(idxAddrInfo, ctx_->dispatcher);

  // Triangle geometry description
  vk::AccelerationStructureGeometryTrianglesDataKHR triangles;
  triangles.vertexFormat = vk::Format::eR32G32B32Sfloat;
  triangles.vertexData.deviceAddress = vertAddr;
  triangles.vertexStride = vertexStride;
  triangles.maxVertex = vertexCount - 1;
  triangles.indexType = vk::IndexType::eUint32;
  triangles.indexData.deviceAddress = idxAddr;

  vk::AccelerationStructureGeometryKHR geometry;
  geometry.geometryType = vk::GeometryTypeKHR::eTriangles;
  geometry.geometry.triangles = triangles;
  geometry.flags = vk::GeometryFlagBitsKHR::eOpaque;

  // Build info
  vk::AccelerationStructureBuildGeometryInfoKHR buildInfo;
  buildInfo.type = vk::AccelerationStructureTypeKHR::eBottomLevel;
  buildInfo.flags = vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace;
  buildInfo.mode = vk::BuildAccelerationStructureModeKHR::eBuild;
  buildInfo.geometryCount = 1;
  buildInfo.pGeometries = &geometry;

  uint32_t primitiveCount = indexCount / 3;

  // Query sizes
  vk::AccelerationStructureBuildSizesInfoKHR sizeInfo;
  ctx_->getDevice().getAccelerationStructureBuildSizesKHR(
      vk::AccelerationStructureBuildTypeKHR::eDevice,
      &buildInfo, &primitiveCount, &sizeInfo, ctx_->dispatcher);

  // Create AS buffer
  result.buffer = std::make_unique<Buffer>(ctx_->createBuffer(
      sizeInfo.accelerationStructureSize,
      vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR
      | vk::BufferUsageFlagBits::eShaderDeviceAddress));

  // Create acceleration structure
  vk::AccelerationStructureCreateInfoKHR asCI;
  asCI.buffer = **result.buffer;
  asCI.size = sizeInfo.accelerationStructureSize;
  asCI.type = vk::AccelerationStructureTypeKHR::eBottomLevel;

  result.handle = ctx_->getDevice().createAccelerationStructureKHR(
      asCI, nullptr, ctx_->dispatcher);

  // Create scratch buffer
  auto scratchBuf = ctx_->createBuffer(
      sizeInfo.buildScratchSize,
      vk::BufferUsageFlagBits::eStorageBuffer
      | vk::BufferUsageFlagBits::eShaderDeviceAddress);

  vk::BufferDeviceAddressInfo scratchAddrInfo;
  scratchAddrInfo.buffer = *scratchBuf;
  auto scratchAddr = ctx_->getDevice().getBufferAddress(scratchAddrInfo, ctx_->dispatcher);

  // Build
  buildInfo.dstAccelerationStructure = result.handle;
  buildInfo.scratchData.deviceAddress = scratchAddr;

  vk::AccelerationStructureBuildRangeInfoKHR rangeInfo;
  rangeInfo.primitiveCount = primitiveCount;
  rangeInfo.primitiveOffset = 0;
  rangeInfo.firstVertex = 0;
  rangeInfo.transformOffset = 0;
  const auto* pRangeInfo = &rangeInfo;

  {
    SingleUseCommandBuffer cmd(*ctx_, vk_queue_e::graphics);
    vk::CommandBuffer cb = *cmd;
    cb.buildAccelerationStructuresKHR(1, &buildInfo, &pRangeInfo, ctx_->dispatcher);
  }

  return result;
}

// ---------------------------------------------------------------
// buildTLAS
// ---------------------------------------------------------------

VulkanHybridRenderer::TLASInfo VulkanHybridRenderer::buildTLAS(
    const std::vector<vk::AccelerationStructureInstanceKHR>& instances) {
  TLASInfo result;

  // Upload instances to device
  vk::DeviceSize instancesSize = instances.size() * sizeof(vk::AccelerationStructureInstanceKHR);

  auto instanceBuf = ctx_->createBuffer(
      instancesSize,
      vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR
      | vk::BufferUsageFlagBits::eShaderDeviceAddress
      | vk::BufferUsageFlagBits::eTransferDst);

  {
    auto staging = ctx_->createStagingBuffer(instancesSize);
    staging.map();
    std::memcpy(staging.mappedAddress(), instances.data(), static_cast<size_t>(instancesSize));
    staging.unmap();

    SingleUseCommandBuffer cmd(*ctx_, vk_queue_e::graphics);
    vk::CommandBuffer cb = *cmd;
    vk::BufferCopy region{0, 0, instancesSize};
    cb.copyBuffer(*staging, *instanceBuf, 1, &region, ctx_->dispatcher);
  }

  vk::BufferDeviceAddressInfo instanceAddrInfo;
  instanceAddrInfo.buffer = *instanceBuf;
  auto instanceAddr = ctx_->getDevice().getBufferAddress(instanceAddrInfo, ctx_->dispatcher);

  // Instance geometry
  vk::AccelerationStructureGeometryInstancesDataKHR instancesData;
  instancesData.arrayOfPointers = VK_FALSE;
  instancesData.data.deviceAddress = instanceAddr;

  vk::AccelerationStructureGeometryKHR geometry;
  geometry.geometryType = vk::GeometryTypeKHR::eInstances;
  geometry.geometry.instances = instancesData;

  // Build info
  vk::AccelerationStructureBuildGeometryInfoKHR buildInfo;
  buildInfo.type = vk::AccelerationStructureTypeKHR::eTopLevel;
  buildInfo.flags = vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace;
  buildInfo.mode = vk::BuildAccelerationStructureModeKHR::eBuild;
  buildInfo.geometryCount = 1;
  buildInfo.pGeometries = &geometry;

  uint32_t primitiveCount = static_cast<uint32_t>(instances.size());

  vk::AccelerationStructureBuildSizesInfoKHR sizeInfo;
  ctx_->getDevice().getAccelerationStructureBuildSizesKHR(
      vk::AccelerationStructureBuildTypeKHR::eDevice,
      &buildInfo, &primitiveCount, &sizeInfo, ctx_->dispatcher);

  result.buffer = std::make_unique<Buffer>(ctx_->createBuffer(
      sizeInfo.accelerationStructureSize,
      vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR
      | vk::BufferUsageFlagBits::eShaderDeviceAddress));

  vk::AccelerationStructureCreateInfoKHR asCI;
  asCI.buffer = **result.buffer;
  asCI.size = sizeInfo.accelerationStructureSize;
  asCI.type = vk::AccelerationStructureTypeKHR::eTopLevel;

  result.handle = ctx_->getDevice().createAccelerationStructureKHR(
      asCI, nullptr, ctx_->dispatcher);

  auto scratchBuf = ctx_->createBuffer(
      sizeInfo.buildScratchSize,
      vk::BufferUsageFlagBits::eStorageBuffer
      | vk::BufferUsageFlagBits::eShaderDeviceAddress);

  vk::BufferDeviceAddressInfo scratchAddrInfo;
  scratchAddrInfo.buffer = *scratchBuf;
  auto scratchAddr = ctx_->getDevice().getBufferAddress(scratchAddrInfo, ctx_->dispatcher);

  buildInfo.dstAccelerationStructure = result.handle;
  buildInfo.scratchData.deviceAddress = scratchAddr;

  vk::AccelerationStructureBuildRangeInfoKHR rangeInfo;
  rangeInfo.primitiveCount = primitiveCount;
  rangeInfo.primitiveOffset = 0;
  rangeInfo.firstVertex = 0;
  rangeInfo.transformOffset = 0;
  const auto* pRangeInfo = &rangeInfo;

  {
    SingleUseCommandBuffer cmd(*ctx_, vk_queue_e::graphics);
    vk::CommandBuffer cb = *cmd;
    cb.buildAccelerationStructuresKHR(1, &buildInfo, &pRangeInfo, ctx_->dispatcher);
  }

  return result;
}

void VulkanHybridRenderer::destroyBLAS(BLASInfo& blas) {
  if (blas.handle != VK_NULL_HANDLE) {
    ctx_->getDevice().destroyAccelerationStructureKHR(blas.handle, nullptr, ctx_->dispatcher);
    blas.handle = VK_NULL_HANDLE;
  }
  blas.buffer.reset();
}

void VulkanHybridRenderer::destroyTLAS(TLASInfo& tlas) {
  if (tlas.handle != VK_NULL_HANDLE) {
    ctx_->getDevice().destroyAccelerationStructureKHR(tlas.handle, nullptr, ctx_->dispatcher);
    tlas.handle = VK_NULL_HANDLE;
  }
  tlas.buffer.reset();
}

// ---------------------------------------------------------------
// buildRTPipeline
// ---------------------------------------------------------------

bool VulkanHybridRenderer::buildRTPipeline() {
  // Create descriptor set layout for RT pass
  // Binding 0: TLAS (acceleration structure)
  // Binding 1: gPosition (sampled image)
  // Binding 2: gNormal (sampled image)
  // Binding 3: gAlbedo (sampled image)
  // Binding 4: RT output (storage image)
  auto dsLayoutBuilder = ctx_->setlayout();
  dsLayoutBuilder.addBinding(0, vk::DescriptorType::eAccelerationStructureKHR,
                             vk::ShaderStageFlagBits::eRaygenKHR);
  dsLayoutBuilder.addBinding(1, vk::DescriptorType::eCombinedImageSampler,
                             vk::ShaderStageFlagBits::eRaygenKHR);
  dsLayoutBuilder.addBinding(2, vk::DescriptorType::eCombinedImageSampler,
                             vk::ShaderStageFlagBits::eRaygenKHR);
  dsLayoutBuilder.addBinding(3, vk::DescriptorType::eCombinedImageSampler,
                             vk::ShaderStageFlagBits::eRaygenKHR);
  dsLayoutBuilder.addBinding(4, vk::DescriptorType::eStorageImage,
                             vk::ShaderStageFlagBits::eRaygenKHR);

  auto dsLayout = dsLayoutBuilder.build();
  rt_ds_layout_owned_ = std::make_unique<DescriptorSetLayout>(std::move(dsLayout));
  rt_ds_layout_ = static_cast<vk::DescriptorSetLayout>(*rt_ds_layout_owned_);

  // Pipeline layout with push constants
  vk::PushConstantRange pcRange;
  pcRange.stageFlags = vk::ShaderStageFlagBits::eRaygenKHR;
  pcRange.offset = 0;
  pcRange.size = static_cast<uint32_t>(sizeof(RTPushConstants));

  vk::PipelineLayoutCreateInfo layoutCI;
  layoutCI.setLayoutCount = 1;
  layoutCI.pSetLayouts = &rt_ds_layout_;
  layoutCI.pushConstantRangeCount = 1;
  layoutCI.pPushConstantRanges = &pcRange;

  rt_pipeline_layout_ = ctx_->getDevice().createPipelineLayout(
      layoutCI, nullptr, ctx_->dispatcher);

  // Shader stages
  std::vector<vk::PipelineShaderStageCreateInfo> stages(4);
  stages[0].stage = vk::ShaderStageFlagBits::eRaygenKHR;
  stages[0].module = **raygen_shader_;
  stages[0].pName = "main";
  stages[1].stage = vk::ShaderStageFlagBits::eMissKHR;
  stages[1].module = **shadow_miss_shader_;
  stages[1].pName = "main";
  stages[2].stage = vk::ShaderStageFlagBits::eMissKHR;
  stages[2].module = **reflect_miss_shader_;
  stages[2].pName = "main";
  stages[3].stage = vk::ShaderStageFlagBits::eClosestHitKHR;
  stages[3].module = **reflect_chit_shader_;
  stages[3].pName = "main";

  // Shader groups
  std::vector<vk::RayTracingShaderGroupCreateInfoKHR> groups(4);

  // Group 0: raygen
  groups[0].type = vk::RayTracingShaderGroupTypeKHR::eGeneral;
  groups[0].generalShader = 0;
  groups[0].closestHitShader = VK_SHADER_UNUSED_KHR;
  groups[0].anyHitShader = VK_SHADER_UNUSED_KHR;
  groups[0].intersectionShader = VK_SHADER_UNUSED_KHR;

  // Group 1: shadow miss
  groups[1].type = vk::RayTracingShaderGroupTypeKHR::eGeneral;
  groups[1].generalShader = 1;
  groups[1].closestHitShader = VK_SHADER_UNUSED_KHR;
  groups[1].anyHitShader = VK_SHADER_UNUSED_KHR;
  groups[1].intersectionShader = VK_SHADER_UNUSED_KHR;

  // Group 2: reflection miss
  groups[2].type = vk::RayTracingShaderGroupTypeKHR::eGeneral;
  groups[2].generalShader = 2;
  groups[2].closestHitShader = VK_SHADER_UNUSED_KHR;
  groups[2].anyHitShader = VK_SHADER_UNUSED_KHR;
  groups[2].intersectionShader = VK_SHADER_UNUSED_KHR;

  // Group 3: reflection closest hit
  groups[3].type = vk::RayTracingShaderGroupTypeKHR::eTrianglesHitGroup;
  groups[3].generalShader = VK_SHADER_UNUSED_KHR;
  groups[3].closestHitShader = 3;
  groups[3].anyHitShader = VK_SHADER_UNUSED_KHR;
  groups[3].intersectionShader = VK_SHADER_UNUSED_KHR;

  vk::RayTracingPipelineCreateInfoKHR rtPipelineCI;
  rtPipelineCI.stageCount = static_cast<uint32_t>(stages.size());
  rtPipelineCI.pStages = stages.data();
  rtPipelineCI.groupCount = static_cast<uint32_t>(groups.size());
  rtPipelineCI.pGroups = groups.data();
  rtPipelineCI.maxPipelineRayRecursionDepth = 1;
  rtPipelineCI.layout = rt_pipeline_layout_;

  auto pipeResult = ctx_->getDevice().createRayTracingPipelineKHR(
      VK_NULL_HANDLE, VK_NULL_HANDLE, rtPipelineCI, nullptr, ctx_->dispatcher);

  if (pipeResult.result != vk::Result::eSuccess) {
    std::fprintf(stderr, "[VulkanHybridRenderer] RT pipeline creation failed\n");
    return false;
  }
  rt_pipeline_ = pipeResult.value;

  // Build Shader Binding Table
  uint32_t handleSize = ctx_->shaderGroupHandleSize();
  uint32_t handleAlignment = ctx_->shaderGroupHandleAlignment();
  uint32_t baseAlignment = ctx_->shaderGroupBaseAlignment();

  // Round up handle size to alignment
  uint32_t handleSizeAligned = (handleSize + handleAlignment - 1) & ~(handleAlignment - 1);

  // SBT layout: raygen (1 entry) | miss (2 entries) | hit (1 entry)
  // Each region must be aligned to baseAlignment
  uint32_t raygenSize = (handleSizeAligned + baseAlignment - 1) & ~(baseAlignment - 1);
  uint32_t missSize = ((handleSizeAligned * 2) + baseAlignment - 1) & ~(baseAlignment - 1);
  uint32_t hitSize = (handleSizeAligned + baseAlignment - 1) & ~(baseAlignment - 1);
  uint32_t sbtSize = raygenSize + missSize + hitSize;

  // Get shader group handles
  uint32_t totalGroups = static_cast<uint32_t>(groups.size());
  std::vector<uint8_t> handles(handleSize * totalGroups);
  auto handleResult = ctx_->getDevice().getRayTracingShaderGroupHandlesKHR(
      rt_pipeline_, 0, totalGroups, handles.size(), handles.data(), ctx_->dispatcher);
  if (handleResult != vk::Result::eSuccess) {
    std::fprintf(stderr, "[VulkanHybridRenderer] failed to get RT shader group handles\n");
    return false;
  }

  // Create SBT buffer
  sbt_buffer_ = std::make_unique<Buffer>(ctx_->createBuffer(
      sbtSize,
      vk::BufferUsageFlagBits::eShaderBindingTableKHR
      | vk::BufferUsageFlagBits::eShaderDeviceAddress
      | vk::BufferUsageFlagBits::eTransferDst,
      vk::MemoryPropertyFlagBits::eDeviceLocal));

  // Fill SBT via staging buffer
  {
    auto staging = ctx_->createStagingBuffer(sbtSize);
    staging.map();
    auto* data = static_cast<uint8_t*>(staging.mappedAddress());
    std::memset(data, 0, sbtSize);

    // Group 0: raygen
    std::memcpy(data, handles.data() + 0 * handleSize, handleSize);
    // Group 1: shadow miss
    std::memcpy(data + raygenSize, handles.data() + 1 * handleSize, handleSize);
    // Group 2: reflection miss
    std::memcpy(data + raygenSize + handleSizeAligned, handles.data() + 2 * handleSize, handleSize);
    // Group 3: reflection hit
    std::memcpy(data + raygenSize + missSize, handles.data() + 3 * handleSize, handleSize);

    staging.unmap();

    SingleUseCommandBuffer cmd(*ctx_, vk_queue_e::graphics);
    vk::CommandBuffer cb = *cmd;
    vk::BufferCopy region{0, 0, sbtSize};
    cb.copyBuffer(*staging, **sbt_buffer_, 1, &region, ctx_->dispatcher);
  }

  // Set up SBT regions
  vk::BufferDeviceAddressInfo sbtAddrInfo;
  sbtAddrInfo.buffer = **sbt_buffer_;
  auto sbtAddr = ctx_->getDevice().getBufferAddress(sbtAddrInfo, ctx_->dispatcher);

  sbt_raygen_.deviceAddress = sbtAddr;
  sbt_raygen_.stride = handleSizeAligned;
  sbt_raygen_.size = raygenSize;

  sbt_miss_.deviceAddress = sbtAddr + raygenSize;
  sbt_miss_.stride = handleSizeAligned;
  sbt_miss_.size = missSize;

  sbt_hit_.deviceAddress = sbtAddr + raygenSize + missSize;
  sbt_hit_.stride = handleSizeAligned;
  sbt_hit_.size = hitSize;

  sbt_callable_ = vk::StridedDeviceAddressRegionKHR{};  // no callable shaders

  return true;
}

void VulkanHybridRenderer::destroyRTPipeline() {
  if (rt_pipeline_ != VK_NULL_HANDLE) {
    ctx_->getDevice().destroyPipeline(rt_pipeline_, nullptr, ctx_->dispatcher);
    rt_pipeline_ = VK_NULL_HANDLE;
  }
  if (rt_pipeline_layout_ != VK_NULL_HANDLE) {
    ctx_->getDevice().destroyPipelineLayout(rt_pipeline_layout_, nullptr, ctx_->dispatcher);
    rt_pipeline_layout_ = VK_NULL_HANDLE;
  }
  // DescriptorSetLayout is RAII-managed — just reset the unique_ptr
  rt_ds_layout_owned_.reset();
  rt_ds_layout_ = VK_NULL_HANDLE;
  sbt_buffer_.reset();
}

// ---------------------------------------------------------------
// init
// ---------------------------------------------------------------

bool VulkanHybridRenderer::init() {
  if (initialised_) return true;

  try {
    ctx_ = &Vulkan::context();
  } catch (const std::exception& e) {
    std::fprintf(stderr, "[VulkanHybridRenderer] failed to acquire VulkanContext: %s\n", e.what());
    return false;
  }

  // Check ray tracing support
  if (!ctx_->supportRayTracing()) {
    std::fprintf(stderr, "[VulkanHybridRenderer] ray tracing not supported on this device\n");
    return false;
  }

  // Check push constant size limit
  auto devProps = ctx_->getPhysicalDevice().getProperties(ctx_->dispatcher);
  uint32_t maxPcSize = devProps.limits.maxPushConstantsSize;
  uint32_t neededPcSize = static_cast<uint32_t>(
      std::max({sizeof(GBufferPushConstants), sizeof(SSAOPushConstants),
                sizeof(HybridLightingPushConstants), sizeof(RTPushConstants),
                sizeof(BloomThresholdPushConstants), sizeof(BloomBlurPushConstants),
                sizeof(TonemapPushConstants), sizeof(FXAAPushConstants)}));
  if (maxPcSize < neededPcSize) {
    std::fprintf(stderr, "[VulkanHybridRenderer] device only supports %u bytes push constants, need %u\n",
                 maxPcSize, neededPcSize);
    return false;
  }

  // -- Compile shaders --
  try {
    // G-buffer shaders
    gbuffer_vert_shader_ = std::make_unique<ShaderModule>(
        ctx_->createShaderModuleFromGlsl(k_gbuffer_vert_glsl,
                                          vk::ShaderStageFlagBits::eVertex,
                                          "hybrid_gbuffer_vert"));
    gbuffer_frag_shader_ = std::make_unique<ShaderModule>(
        ctx_->createShaderModuleFromGlsl(k_gbuffer_frag_glsl,
                                          vk::ShaderStageFlagBits::eFragment,
                                          "hybrid_gbuffer_frag"));

    // RT shaders
    raygen_shader_ = std::make_unique<ShaderModule>(
        ctx_->createShaderModuleFromGlsl(k_raygen_glsl,
                                          vk::ShaderStageFlagBits::eRaygenKHR,
                                          "hybrid_raygen"));
    shadow_miss_shader_ = std::make_unique<ShaderModule>(
        ctx_->createShaderModuleFromGlsl(k_shadow_miss_glsl,
                                          vk::ShaderStageFlagBits::eMissKHR,
                                          "hybrid_shadow_miss"));
    reflect_miss_shader_ = std::make_unique<ShaderModule>(
        ctx_->createShaderModuleFromGlsl(k_reflect_miss_glsl,
                                          vk::ShaderStageFlagBits::eMissKHR,
                                          "hybrid_reflect_miss"));
    reflect_chit_shader_ = std::make_unique<ShaderModule>(
        ctx_->createShaderModuleFromGlsl(k_reflect_chit_glsl,
                                          vk::ShaderStageFlagBits::eClosestHitKHR,
                                          "hybrid_reflect_chit"));

    // Lighting compute
    lighting_comp_shader_ = std::make_unique<ShaderModule>(
        ctx_->createShaderModuleFromGlsl(k_hybrid_lighting_comp_glsl,
                                          vk::ShaderStageFlagBits::eCompute,
                                          "hybrid_lighting_comp"));

    // SSAO shaders
    ssao_comp_shader_ = std::make_unique<ShaderModule>(
        ctx_->createShaderModuleFromGlsl(k_ssao_comp_glsl,
                                          vk::ShaderStageFlagBits::eCompute,
                                          "hybrid_ssao_comp"));
    ssao_blur_comp_shader_ = std::make_unique<ShaderModule>(
        ctx_->createShaderModuleFromGlsl(k_ssao_blur_comp_glsl,
                                          vk::ShaderStageFlagBits::eCompute,
                                          "hybrid_ssao_blur_comp"));

    // Post-processing shaders
    bloom_threshold_comp_shader_ = std::make_unique<ShaderModule>(
        ctx_->createShaderModuleFromGlsl(k_bloom_threshold_comp_glsl,
                                          vk::ShaderStageFlagBits::eCompute,
                                          "hybrid_bloom_threshold_comp"));
    bloom_blur_comp_shader_ = std::make_unique<ShaderModule>(
        ctx_->createShaderModuleFromGlsl(k_bloom_blur_comp_glsl,
                                          vk::ShaderStageFlagBits::eCompute,
                                          "hybrid_bloom_blur_comp"));
    tonemap_comp_shader_ = std::make_unique<ShaderModule>(
        ctx_->createShaderModuleFromGlsl(k_tonemap_comp_glsl,
                                          vk::ShaderStageFlagBits::eCompute,
                                          "hybrid_tonemap_comp"));
    fxaa_comp_shader_ = std::make_unique<ShaderModule>(
        ctx_->createShaderModuleFromGlsl(k_fxaa_comp_glsl,
                                          vk::ShaderStageFlagBits::eCompute,
                                          "hybrid_fxaa_comp"));
  } catch (const std::exception& e) {
    std::fprintf(stderr, "[VulkanHybridRenderer] shader compilation failed: %s\n", e.what());
    return false;
  }

  // -- Build G-buffer render pass (3 color + 1 depth) --
  try {
    gbuffer_pass_ = std::make_unique<RenderPass>(
        ctx_->renderpass()
            .addAttachment(vk::Format::eR16G16B16A16Sfloat,
                           vk::ImageLayout::eUndefined,
                           vk::ImageLayout::eShaderReadOnlyOptimal, true)
            .addAttachment(vk::Format::eR16G16B16A16Sfloat,
                           vk::ImageLayout::eUndefined,
                           vk::ImageLayout::eShaderReadOnlyOptimal, true)
            .addAttachment(vk::Format::eR8G8B8A8Unorm,
                           vk::ImageLayout::eUndefined,
                           vk::ImageLayout::eShaderReadOnlyOptimal, true)
            .addDepthAttachment(vk::Format::eD32Sfloat, true)
            .build());
  } catch (const std::exception& e) {
    std::fprintf(stderr, "[VulkanHybridRenderer] G-buffer render pass creation failed: %s\n", e.what());
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
            .setRenderPass(*gbuffer_pass_, 0)
            .setBlendEnable(false, 0)
            .setBlendEnable(false, 1)
            .setBlendEnable(false, 2)
            .setBindingDescriptions(bindings)
            .setAttributeDescriptions(attributes)
            .setTopology(vk::PrimitiveTopology::eTriangleList)
            .setCullMode(vk::CullModeFlagBits::eNone)
            .setFrontFace(vk::FrontFace::eCounterClockwise)
            .setDepthTestEnable(true)
            .setDepthWriteEnable(true)
            .setDepthCompareOp(vk::CompareOp::eLessOrEqual)
            .setPushConstantRange(pcRange)
            .build());
  } catch (const std::exception& e) {
    std::fprintf(stderr, "[VulkanHybridRenderer] G-buffer pipeline creation failed: %s\n", e.what());
    return false;
  }

  // -- Build RT pipeline and SBT --
  if (!buildRTPipeline()) {
    return false;
  }

  // -- Build compute pipelines --
  try {
    lighting_pipeline_ = std::make_unique<Pipeline>(
        *lighting_comp_shader_,
        static_cast<u32>(sizeof(HybridLightingPushConstants)));

    ssao_pipeline_ = std::make_unique<Pipeline>(
        *ssao_comp_shader_, static_cast<u32>(sizeof(SSAOPushConstants)));

    ssao_blur_pipeline_ = std::make_unique<Pipeline>(
        *ssao_blur_comp_shader_, static_cast<u32>(sizeof(SSAOBlurPushConstants)));

    bloom_threshold_pipeline_ = std::make_unique<Pipeline>(
        *bloom_threshold_comp_shader_, static_cast<u32>(sizeof(BloomThresholdPushConstants)));

    bloom_blur_pipeline_ = std::make_unique<Pipeline>(
        *bloom_blur_comp_shader_, static_cast<u32>(sizeof(BloomBlurPushConstants)));

    tonemap_pipeline_ = std::make_unique<Pipeline>(
        *tonemap_comp_shader_, static_cast<u32>(sizeof(TonemapPushConstants)));

    fxaa_pipeline_ = std::make_unique<Pipeline>(
        *fxaa_comp_shader_, static_cast<u32>(sizeof(FXAAPushConstants)));
  } catch (const std::exception& e) {
    std::fprintf(stderr, "[VulkanHybridRenderer] compute pipeline creation failed: %s\n", e.what());
    return false;
  }

  initialised_ = true;
  std::printf("[VulkanHybridRenderer] initialised on %s (RT: handleSize=%u, maxRecursion=%u)\n",
              devProps.deviceName.data(),
              ctx_->shaderGroupHandleSize(),
              ctx_->maxRayRecursionDepth());
  return true;
}

// ---------------------------------------------------------------
// render
// ---------------------------------------------------------------

RasterResult VulkanHybridRenderer::render(const RenderFrameRequest& request,
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

  auto gPosImage = ctx_->create2DImage(
      extent, vk::Format::eR16G16B16A16Sfloat,
      vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
      vk::MemoryPropertyFlagBits::eDeviceLocal, false, true, false);

  auto gNormImage = ctx_->create2DImage(
      extent, vk::Format::eR16G16B16A16Sfloat,
      vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
      vk::MemoryPropertyFlagBits::eDeviceLocal, false, true, false);

  auto gAlbedoImage = ctx_->create2DImage(
      extent, vk::Format::eR8G8B8A8Unorm,
      vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
      vk::MemoryPropertyFlagBits::eDeviceLocal, false, true, false);

  auto depthImage = ctx_->create2DImage(
      extent, vk::Format::eD32Sfloat,
      vk::ImageUsageFlagBits::eDepthStencilAttachment,
      vk::MemoryPropertyFlagBits::eDeviceLocal, false, true, false);

  // RT shadow/reflect output
  auto rtResultImage = ctx_->create2DImage(
      extent, vk::Format::eR16G16B16A16Sfloat,
      vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled,
      vk::MemoryPropertyFlagBits::eDeviceLocal, false, true, false);

  // HDR lighting output
  auto hdrImage = ctx_->create2DImage(
      extent, vk::Format::eR16G16B16A16Sfloat,
      vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled,
      vk::MemoryPropertyFlagBits::eDeviceLocal, false, true, false);

  // SSAO images
  auto ssaoRawImage = ctx_->create2DImage(
      extent, vk::Format::eR8Unorm,
      vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled,
      vk::MemoryPropertyFlagBits::eDeviceLocal, false, true, false);

  auto ssaoBlurImage = ctx_->create2DImage(
      extent, vk::Format::eR8Unorm,
      vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled,
      vk::MemoryPropertyFlagBits::eDeviceLocal, false, true, false);

  // G-buffer framebuffer
  std::vector<vk::ImageView> fbViews{
      static_cast<vk::ImageView>(gPosImage),
      static_cast<vk::ImageView>(gNormImage),
      static_cast<vk::ImageView>(gAlbedoImage),
      static_cast<vk::ImageView>(depthImage)};
  auto framebuffer = ctx_->createFramebuffer(fbViews, extent, **gbuffer_pass_);

  // Compute matrices
  mat4 V = buildViewMatrix(view.camera);
  mat4 P = buildProjectionMatrix(view.camera);

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

  // We also need raw geometry for BLAS construction
  // Collect all triangles in world space for a single BLAS
  std::vector<float> allPositions;   // x,y,z per vertex
  std::vector<uint32_t> allIndices;
  uint32_t vertexOffset = 0;

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

    // Collect world-space positions for BLAS
    for (const auto& node : meshData->nodes) {
      f32 wx = M(0,0)*node[0] + M(0,1)*node[1] + M(0,2)*node[2] + M(0,3);
      f32 wy = M(1,0)*node[0] + M(1,1)*node[1] + M(1,2)*node[2] + M(1,3);
      f32 wz = M(2,0)*node[0] + M(2,1)*node[1] + M(2,2)*node[2] + M(2,3);
      allPositions.push_back(wx);
      allPositions.push_back(wy);
      allPositions.push_back(wz);
    }

    // Collect indices (offset by vertex count)
    for (const auto& elem : meshData->elems) {
      allIndices.push_back(vertexOffset + elem[0]);
      allIndices.push_back(vertexOffset + elem[1]);
      allIndices.push_back(vertexOffset + elem[2]);
    }
    vertexOffset += static_cast<uint32_t>(meshData->nodes.size());
  }

  if (draws.empty()) {
    result.error = "no drawable instances in scene";
    return result;
  }

  // =================================================================
  // Pass 0: G-buffer (rasterization)
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
    cb.bindPipeline(vk::PipelineBindPoint::eGraphics, **gbuffer_pipeline_, ctx_->dispatcher);

    vk::Viewport vp{0.f, 0.f, static_cast<float>(width), static_cast<float>(height), 0.f, 1.f};
    cb.setViewport(0, 1, &vp, ctx_->dispatcher);
    vk::Rect2D scissor{{0,0}, extent};
    cb.setScissor(0, 1, &scissor, ctx_->dispatcher);

    for (auto& item : draws) {
      GBufferPushConstants pc{};
      for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
          pc.mvp[j * 4 + i] = item.mvp(i, j);
      for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
          pc.model[j * 4 + i] = item.model_mat(i, j);

      const Material* mat = scene.findMaterial(item.material_id);
      if (mat) {
        pc.material_color[0] = mat->base_color(0);
        pc.material_color[1] = mat->base_color(1);
        pc.material_color[2] = mat->base_color(2);
        pc.material_color[3] = mat->base_color(3);
      } else {
        pc.material_color[0] = 0.8f; pc.material_color[1] = 0.8f;
        pc.material_color[2] = 0.8f; pc.material_color[3] = 1.0f;
      }

      cb.pushConstants(static_cast<vk::PipelineLayout>(*gbuffer_pipeline_),
                       vk::ShaderStageFlagBits::eVertex, 0,
                       static_cast<uint32_t>(sizeof(GBufferPushConstants)),
                       &pc, ctx_->dispatcher);
      item.model.bind(cb);
      item.model.draw(cb);
    }

    cb.endRenderPass(ctx_->dispatcher);
  }

  // =================================================================
  // Pass 1: Build BLAS/TLAS
  // =================================================================

  // Upload positions to device buffer
  vk::DeviceSize posBufSize = allPositions.size() * sizeof(float);
  auto posBuf = ctx_->createBuffer(
      posBufSize,
      vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR
      | vk::BufferUsageFlagBits::eShaderDeviceAddress
      | vk::BufferUsageFlagBits::eTransferDst);
  {
    auto staging = ctx_->createStagingBuffer(posBufSize);
    staging.map();
    std::memcpy(staging.mappedAddress(), allPositions.data(), static_cast<size_t>(posBufSize));
    staging.unmap();
    SingleUseCommandBuffer cmd(*ctx_, vk_queue_e::graphics);
    vk::CommandBuffer cb = *cmd;
    vk::BufferCopy region{0, 0, posBufSize};
    cb.copyBuffer(*staging, *posBuf, 1, &region, ctx_->dispatcher);
  }

  // Upload indices to device buffer
  vk::DeviceSize idxBufSize = allIndices.size() * sizeof(uint32_t);
  auto idxBuf = ctx_->createBuffer(
      idxBufSize,
      vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR
      | vk::BufferUsageFlagBits::eShaderDeviceAddress
      | vk::BufferUsageFlagBits::eTransferDst);
  {
    auto staging = ctx_->createStagingBuffer(idxBufSize);
    staging.map();
    std::memcpy(staging.mappedAddress(), allIndices.data(), static_cast<size_t>(idxBufSize));
    staging.unmap();
    SingleUseCommandBuffer cmd(*ctx_, vk_queue_e::graphics);
    vk::CommandBuffer cb = *cmd;
    vk::BufferCopy region{0, 0, idxBufSize};
    cb.copyBuffer(*staging, *idxBuf, 1, &region, ctx_->dispatcher);
  }

  // Build BLAS (single BLAS for all geometry)
  auto blas = buildBLAS(posBuf, vertexOffset, idxBuf,
                        static_cast<uint32_t>(allIndices.size()),
                        3 * sizeof(float));

  // Build TLAS (single instance pointing to our BLAS)
  vk::AccelerationStructureDeviceAddressInfoKHR blasAddrInfo;
  blasAddrInfo.accelerationStructure = blas.handle;
  auto blasAddr = ctx_->getDevice().getAccelerationStructureAddressKHR(
      blasAddrInfo, ctx_->dispatcher);

  vk::AccelerationStructureInstanceKHR instance{};
  // Identity transform (geometry is already in world space)
  instance.transform.matrix[0][0] = 1.0f;
  instance.transform.matrix[1][1] = 1.0f;
  instance.transform.matrix[2][2] = 1.0f;
  instance.instanceCustomIndex = 0;
  instance.mask = 0xFF;
  instance.instanceShaderBindingTableRecordOffset = 0;
  instance.flags = static_cast<VkGeometryInstanceFlagsKHR>(
      vk::GeometryInstanceFlagBitsKHR::eTriangleFacingCullDisable);
  instance.accelerationStructureReference = blasAddr;

  std::vector<vk::AccelerationStructureInstanceKHR> instances{instance};
  auto tlas = buildTLAS(instances);

  // =================================================================
  // Pass 2: RT shadows + reflections
  // =================================================================

  // Transition RT result image to eGeneral for ray tracing writes
  {
    SingleUseCommandBuffer cmd(*ctx_, vk_queue_e::graphics);
    vk::CommandBuffer cb = *cmd;

    vk::ImageMemoryBarrier barrier;
    barrier.oldLayout = vk::ImageLayout::eUndefined;
    barrier.newLayout = vk::ImageLayout::eGeneral;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = static_cast<vk::Image>(rtResultImage);
    barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = {};
    barrier.dstAccessMask = vk::AccessFlagBits::eShaderWrite;

    cb.pipelineBarrier(
        vk::PipelineStageFlagBits::eTopOfPipe,
        vk::PipelineStageFlagBits::eRayTracingShaderKHR,
        {}, 0, nullptr, 0, nullptr, 1, &barrier, ctx_->dispatcher);
  }

  // Create sampler for G-buffer reads in RT pass
  vk::SamplerCreateInfo rtSamplerInfo{};
  rtSamplerInfo.magFilter = vk::Filter::eNearest;
  rtSamplerInfo.minFilter = vk::Filter::eNearest;
  rtSamplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
  rtSamplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
  rtSamplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
  auto rtSampler = ctx_->createSampler(rtSamplerInfo);

  // Write RT descriptor set
  {
    vk::DescriptorSet rtDs;
    ctx_->acquireSet(rt_ds_layout_, rtDs);

    // Binding 0: TLAS
    vk::WriteDescriptorSetAccelerationStructureKHR asWrite;
    asWrite.accelerationStructureCount = 1;
    asWrite.pAccelerationStructures = &tlas.handle;

    vk::WriteDescriptorSet tlasWrite;
    tlasWrite.dstSet = rtDs;
    tlasWrite.dstBinding = 0;
    tlasWrite.descriptorCount = 1;
    tlasWrite.descriptorType = vk::DescriptorType::eAccelerationStructureKHR;
    tlasWrite.pNext = &asWrite;
    ctx_->getDevice().updateDescriptorSets(1, &tlasWrite, 0, nullptr, ctx_->dispatcher);

    // Binding 1: gPosition
    vk::DescriptorImageInfo gPosInfo;
    gPosInfo.sampler = *rtSampler;
    gPosInfo.imageView = gPosImage.view();
    gPosInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    ctx_->writeDescriptorSet(gPosInfo, rtDs, vk::DescriptorType::eCombinedImageSampler, 1);

    // Binding 2: gNormal
    vk::DescriptorImageInfo gNormInfo;
    gNormInfo.sampler = *rtSampler;
    gNormInfo.imageView = gNormImage.view();
    gNormInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    ctx_->writeDescriptorSet(gNormInfo, rtDs, vk::DescriptorType::eCombinedImageSampler, 2);

    // Binding 3: gAlbedo
    vk::DescriptorImageInfo gAlbedoInfo;
    gAlbedoInfo.sampler = *rtSampler;
    gAlbedoInfo.imageView = gAlbedoImage.view();
    gAlbedoInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    ctx_->writeDescriptorSet(gAlbedoInfo, rtDs, vk::DescriptorType::eCombinedImageSampler, 3);

    // Binding 4: RT output
    vk::DescriptorImageInfo rtOutInfo;
    rtOutInfo.imageView = rtResultImage.view();
    rtOutInfo.imageLayout = vk::ImageLayout::eGeneral;
    ctx_->writeDescriptorSet(rtOutInfo, rtDs, vk::DescriptorType::eStorageImage, 4);

    // Find light direction
    vec3 sceneMin, sceneMax;
    computeSceneAABB(scene, sceneMin, sceneMax);

    vec3 lightDir;
    float lightIntensity = 1.0f;
    if (!scene.lights().empty()) {
      const auto& light = scene.lights()[0];
      if (light.type == LightType::Directional || light.type == LightType::Area) {
        lightDir(0) = -light.direction(0);
        lightDir(1) = -light.direction(1);
        lightDir(2) = -light.direction(2);
      } else {
        // Point light — direction from scene center toward light
        vec3 center;
        center(0) = (sceneMin(0) + sceneMax(0)) * 0.5f;
        center(1) = (sceneMin(1) + sceneMax(1)) * 0.5f;
        center(2) = (sceneMin(2) + sceneMax(2)) * 0.5f;
        lightDir(0) = light.position(0) - center(0);
        lightDir(1) = light.position(1) - center(1);
        lightDir(2) = light.position(2) - center(2);
      }
      f32 len = std::sqrt(lightDir(0)*lightDir(0) + lightDir(1)*lightDir(1) + lightDir(2)*lightDir(2));
      if (len > 1e-6f) { lightDir(0) /= len; lightDir(1) /= len; lightDir(2) /= len; }
      lightIntensity = scene.lights()[0].intensity;
    } else {
      lightDir(0) = 0.5774f; lightDir(1) = 0.5774f; lightDir(2) = 0.5774f;
    }

    // Dispatch ray tracing
    SingleUseCommandBuffer cmd(*ctx_, vk_queue_e::graphics);
    vk::CommandBuffer cb = *cmd;

    cb.bindPipeline(vk::PipelineBindPoint::eRayTracingKHR, rt_pipeline_, ctx_->dispatcher);
    cb.bindDescriptorSets(vk::PipelineBindPoint::eRayTracingKHR,
                          rt_pipeline_layout_, 0, 1, &rtDs, 0, nullptr, ctx_->dispatcher);

    RTPushConstants rtPc{};
    rtPc.lightDir_intensity[0] = lightDir(0);
    rtPc.lightDir_intensity[1] = lightDir(1);
    rtPc.lightDir_intensity[2] = lightDir(2);
    rtPc.lightDir_intensity[3] = lightIntensity;
    rtPc.cameraPos_pad[0] = view.camera.position(0);
    rtPc.cameraPos_pad[1] = view.camera.position(1);
    rtPc.cameraPos_pad[2] = view.camera.position(2);
    rtPc.cameraPos_pad[3] = 0.f;
    rtPc.width = width;
    rtPc.height = height;
    rtPc.shadowBias = 0.01f;
    rtPc.reflectivity = 0.3f;

    cb.pushConstants(rt_pipeline_layout_,
                     vk::ShaderStageFlagBits::eRaygenKHR, 0,
                     static_cast<uint32_t>(sizeof(RTPushConstants)),
                     &rtPc, ctx_->dispatcher);

    cb.traceRaysKHR(sbt_raygen_, sbt_miss_, sbt_hit_, sbt_callable_,
                    width, height, 1, ctx_->dispatcher);

    // Barrier: RT write -> shader read
    vk::ImageMemoryBarrier rtBarrier;
    rtBarrier.oldLayout = vk::ImageLayout::eGeneral;
    rtBarrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    rtBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    rtBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    rtBarrier.image = static_cast<vk::Image>(rtResultImage);
    rtBarrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    rtBarrier.subresourceRange.baseMipLevel = 0;
    rtBarrier.subresourceRange.levelCount = 1;
    rtBarrier.subresourceRange.baseArrayLayer = 0;
    rtBarrier.subresourceRange.layerCount = 1;
    rtBarrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
    rtBarrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

    cb.pipelineBarrier(
        vk::PipelineStageFlagBits::eRayTracingShaderKHR,
        vk::PipelineStageFlagBits::eComputeShader,
        {}, 0, nullptr, 0, nullptr, 1, &rtBarrier, ctx_->dispatcher);
  }

  // =================================================================
  // Pass 3: SSAO
  // =================================================================

  vk::SamplerCreateInfo samplerInfo{};
  samplerInfo.magFilter = vk::Filter::eNearest;
  samplerInfo.minFilter = vk::Filter::eNearest;
  samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
  samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
  samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
  auto sampler = ctx_->createSampler(samplerInfo);

  // Transition SSAO raw to eGeneral
  {
    SingleUseCommandBuffer cmd(*ctx_, vk_queue_e::graphics);
    vk::CommandBuffer cb = *cmd;
    vk::ImageMemoryBarrier barrier;
    barrier.oldLayout = vk::ImageLayout::eUndefined;
    barrier.newLayout = vk::ImageLayout::eGeneral;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = static_cast<vk::Image>(ssaoRawImage);
    barrier.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
    barrier.srcAccessMask = {};
    barrier.dstAccessMask = vk::AccessFlagBits::eShaderWrite;
    cb.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,
                       vk::PipelineStageFlagBits::eComputeShader,
                       {}, 0, nullptr, 0, nullptr, 1, &barrier, ctx_->dispatcher);
  }

  {
    auto& ssaoDsLayout = ssao_comp_shader_->layout(0);
    vk::DescriptorSet ssaoDs;
    ctx_->acquireSet(ssaoDsLayout, ssaoDs);

    vk::DescriptorImageInfo posInfo;
    posInfo.sampler = *sampler; posInfo.imageView = gPosImage.view();
    posInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    ctx_->writeDescriptorSet(posInfo, ssaoDs, vk::DescriptorType::eCombinedImageSampler, 0);

    vk::DescriptorImageInfo normInfo;
    normInfo.sampler = *sampler; normInfo.imageView = gNormImage.view();
    normInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    ctx_->writeDescriptorSet(normInfo, ssaoDs, vk::DescriptorType::eCombinedImageSampler, 1);

    vk::DescriptorImageInfo aoOutInfo;
    aoOutInfo.imageView = ssaoRawImage.view();
    aoOutInfo.imageLayout = vk::ImageLayout::eGeneral;
    ctx_->writeDescriptorSet(aoOutInfo, ssaoDs, vk::DescriptorType::eStorageImage, 2);

    SingleUseCommandBuffer cmd(*ctx_, vk_queue_e::graphics);
    vk::CommandBuffer cb = *cmd;
    cb.bindPipeline(vk::PipelineBindPoint::eCompute, **ssao_pipeline_, ctx_->dispatcher);
    cb.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                          static_cast<vk::PipelineLayout>(*ssao_pipeline_),
                          0, 1, &ssaoDs, 0, nullptr, ctx_->dispatcher);

    SSAOPushConstants ssaoPc{};
    for (int i = 0; i < 4; ++i)
      for (int j = 0; j < 4; ++j)
        ssaoPc.view[j * 4 + i] = V(i, j);
    for (int i = 0; i < 4; ++i)
      for (int j = 0; j < 4; ++j)
        ssaoPc.projection[j * 4 + i] = P(i, j);
    ssaoPc.width = width; ssaoPc.height = height;
    ssaoPc.radius = 0.5f; ssaoPc.bias = 0.025f; ssaoPc.intensity = 1.5f; ssaoPc._pad0 = 0.f;

    cb.pushConstants(static_cast<vk::PipelineLayout>(*ssao_pipeline_),
                     vk::ShaderStageFlagBits::eCompute, 0,
                     static_cast<uint32_t>(sizeof(SSAOPushConstants)), &ssaoPc, ctx_->dispatcher);

    uint32_t gx = (width + 15) / 16, gy = (height + 15) / 16;
    cb.dispatch(gx, gy, 1, ctx_->dispatcher);

    // Barrier: SSAO write -> read
    vk::ImageMemoryBarrier aoBarrier;
    aoBarrier.oldLayout = vk::ImageLayout::eGeneral;
    aoBarrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    aoBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    aoBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    aoBarrier.image = static_cast<vk::Image>(ssaoRawImage);
    aoBarrier.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
    aoBarrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
    aoBarrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
    cb.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                       vk::PipelineStageFlagBits::eComputeShader,
                       {}, 0, nullptr, 0, nullptr, 1, &aoBarrier, ctx_->dispatcher);
  }

  // =================================================================
  // Pass 4: SSAO blur
  // =================================================================

  {
    SingleUseCommandBuffer cmd(*ctx_, vk_queue_e::graphics);
    vk::CommandBuffer cb = *cmd;
    vk::ImageMemoryBarrier barrier;
    barrier.oldLayout = vk::ImageLayout::eUndefined;
    barrier.newLayout = vk::ImageLayout::eGeneral;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = static_cast<vk::Image>(ssaoBlurImage);
    barrier.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
    barrier.srcAccessMask = {};
    barrier.dstAccessMask = vk::AccessFlagBits::eShaderWrite;
    cb.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,
                       vk::PipelineStageFlagBits::eComputeShader,
                       {}, 0, nullptr, 0, nullptr, 1, &barrier, ctx_->dispatcher);
  }

  {
    auto& blurDsLayout = ssao_blur_comp_shader_->layout(0);
    vk::DescriptorSet blurDs;
    ctx_->acquireSet(blurDsLayout, blurDs);

    vk::DescriptorImageInfo aoRawInfo;
    aoRawInfo.sampler = *sampler; aoRawInfo.imageView = ssaoRawImage.view();
    aoRawInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    ctx_->writeDescriptorSet(aoRawInfo, blurDs, vk::DescriptorType::eCombinedImageSampler, 0);

    vk::DescriptorImageInfo blurPosInfo;
    blurPosInfo.sampler = *sampler; blurPosInfo.imageView = gPosImage.view();
    blurPosInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    ctx_->writeDescriptorSet(blurPosInfo, blurDs, vk::DescriptorType::eCombinedImageSampler, 1);

    vk::DescriptorImageInfo blurOutInfo;
    blurOutInfo.imageView = ssaoBlurImage.view();
    blurOutInfo.imageLayout = vk::ImageLayout::eGeneral;
    ctx_->writeDescriptorSet(blurOutInfo, blurDs, vk::DescriptorType::eStorageImage, 2);

    SingleUseCommandBuffer cmd(*ctx_, vk_queue_e::graphics);
    vk::CommandBuffer cb = *cmd;
    cb.bindPipeline(vk::PipelineBindPoint::eCompute, **ssao_blur_pipeline_, ctx_->dispatcher);
    cb.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                          static_cast<vk::PipelineLayout>(*ssao_blur_pipeline_),
                          0, 1, &blurDs, 0, nullptr, ctx_->dispatcher);

    SSAOBlurPushConstants blurPc{};
    blurPc.width = width; blurPc.height = height; blurPc._pad0 = 0.f; blurPc._pad1 = 0.f;
    cb.pushConstants(static_cast<vk::PipelineLayout>(*ssao_blur_pipeline_),
                     vk::ShaderStageFlagBits::eCompute, 0,
                     static_cast<uint32_t>(sizeof(SSAOBlurPushConstants)), &blurPc, ctx_->dispatcher);

    uint32_t gx = (width + 15) / 16, gy = (height + 15) / 16;
    cb.dispatch(gx, gy, 1, ctx_->dispatcher);

    vk::ImageMemoryBarrier blurBarrier;
    blurBarrier.oldLayout = vk::ImageLayout::eGeneral;
    blurBarrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    blurBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    blurBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    blurBarrier.image = static_cast<vk::Image>(ssaoBlurImage);
    blurBarrier.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
    blurBarrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
    blurBarrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
    cb.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                       vk::PipelineStageFlagBits::eComputeShader,
                       {}, 0, nullptr, 0, nullptr, 1, &blurBarrier, ctx_->dispatcher);
  }

  // =================================================================
  // Upload lights
  // =================================================================

  std::vector<GPULight> gpuLights;
  for (const auto& light : scene.lights()) {
    GPULight gl{};
    if (light.type == LightType::Directional) {
      gl.position_type[0] = light.direction(0);
      gl.position_type[1] = light.direction(1);
      gl.position_type[2] = light.direction(2);
      gl.position_type[3] = 0.f;
    } else {
      gl.position_type[0] = light.position(0);
      gl.position_type[1] = light.position(1);
      gl.position_type[2] = light.position(2);
      gl.position_type[3] = 1.f;
    }
    gl.color_intensity[0] = light.color(0);
    gl.color_intensity[1] = light.color(1);
    gl.color_intensity[2] = light.color(2);
    gl.color_intensity[3] = light.intensity;
    gpuLights.push_back(gl);
  }
  if (gpuLights.empty()) {
    GPULight def{};
    def.position_type[0] = -0.5774f; def.position_type[1] = -0.5774f;
    def.position_type[2] = -0.5774f; def.position_type[3] = 0.f;
    def.color_intensity[0] = 1.0f; def.color_intensity[1] = 1.0f;
    def.color_intensity[2] = 1.0f; def.color_intensity[3] = 1.0f;
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
    std::memcpy(staging.mappedAddress(), gpuLights.data(), static_cast<size_t>(lightBufSize));
    staging.unmap();
    SingleUseCommandBuffer cmd(*ctx_, vk_queue_e::graphics);
    vk::CommandBuffer cb = *cmd;
    vk::BufferCopy region{0, 0, lightBufSize};
    cb.copyBuffer(*staging, *lightBuf, 1, &region, ctx_->dispatcher);
  }

  // =================================================================
  // Pass 5: Lighting (compute, hybrid)
  // =================================================================

  // Transition HDR to eGeneral
  {
    SingleUseCommandBuffer cmd(*ctx_, vk_queue_e::graphics);
    vk::CommandBuffer cb = *cmd;
    vk::ImageMemoryBarrier barrier;
    barrier.oldLayout = vk::ImageLayout::eUndefined;
    barrier.newLayout = vk::ImageLayout::eGeneral;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = static_cast<vk::Image>(hdrImage);
    barrier.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
    barrier.srcAccessMask = {};
    barrier.dstAccessMask = vk::AccessFlagBits::eShaderWrite;
    cb.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,
                       vk::PipelineStageFlagBits::eComputeShader,
                       {}, 0, nullptr, 0, nullptr, 1, &barrier, ctx_->dispatcher);
  }

  {
    auto& dsLayout = lighting_comp_shader_->layout(0);
    vk::DescriptorSet ds;
    ctx_->acquireSet(dsLayout, ds);

    vk::DescriptorImageInfo gPosInfo;
    gPosInfo.sampler = *sampler; gPosInfo.imageView = gPosImage.view();
    gPosInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    ctx_->writeDescriptorSet(gPosInfo, ds, vk::DescriptorType::eCombinedImageSampler, 0);

    vk::DescriptorImageInfo gNormInfo;
    gNormInfo.sampler = *sampler; gNormInfo.imageView = gNormImage.view();
    gNormInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    ctx_->writeDescriptorSet(gNormInfo, ds, vk::DescriptorType::eCombinedImageSampler, 1);

    vk::DescriptorImageInfo gAlbedoInfo;
    gAlbedoInfo.sampler = *sampler; gAlbedoInfo.imageView = gAlbedoImage.view();
    gAlbedoInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    ctx_->writeDescriptorSet(gAlbedoInfo, ds, vk::DescriptorType::eCombinedImageSampler, 2);

    vk::DescriptorImageInfo hdrOutInfo;
    hdrOutInfo.imageView = hdrImage.view();
    hdrOutInfo.imageLayout = vk::ImageLayout::eGeneral;
    ctx_->writeDescriptorSet(hdrOutInfo, ds, vk::DescriptorType::eStorageImage, 3);

    auto lightBufInfo = lightBuf.descriptorInfo();
    ctx_->writeDescriptorSet(lightBufInfo, ds, vk::DescriptorType::eStorageBuffer, 4);

    vk::DescriptorImageInfo rtInfo;
    rtInfo.sampler = *sampler; rtInfo.imageView = rtResultImage.view();
    rtInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    ctx_->writeDescriptorSet(rtInfo, ds, vk::DescriptorType::eCombinedImageSampler, 5);

    vk::DescriptorImageInfo aoInfo;
    aoInfo.sampler = *sampler; aoInfo.imageView = ssaoBlurImage.view();
    aoInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    ctx_->writeDescriptorSet(aoInfo, ds, vk::DescriptorType::eCombinedImageSampler, 6);

    SingleUseCommandBuffer cmd(*ctx_, vk_queue_e::graphics);
    vk::CommandBuffer cb = *cmd;
    cb.bindPipeline(vk::PipelineBindPoint::eCompute, **lighting_pipeline_, ctx_->dispatcher);
    cb.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                          static_cast<vk::PipelineLayout>(*lighting_pipeline_),
                          0, 1, &ds, 0, nullptr, ctx_->dispatcher);

    HybridLightingPushConstants lpc{};
    lpc.camera_pos_numLights[0] = view.camera.position(0);
    lpc.camera_pos_numLights[1] = view.camera.position(1);
    lpc.camera_pos_numLights[2] = view.camera.position(2);
    lpc.camera_pos_numLights[3] = static_cast<float>(gpuLights.size());
    lpc.width = width; lpc.height = height;
    lpc.ambient = 0.15f;
    lpc.reflectionStrength = 0.15f;

    cb.pushConstants(static_cast<vk::PipelineLayout>(*lighting_pipeline_),
                     vk::ShaderStageFlagBits::eCompute, 0,
                     static_cast<uint32_t>(sizeof(HybridLightingPushConstants)),
                     &lpc, ctx_->dispatcher);

    uint32_t gx = (width + 15) / 16, gy = (height + 15) / 16;
    cb.dispatch(gx, gy, 1, ctx_->dispatcher);

    // Barrier: HDR write -> read
    vk::ImageMemoryBarrier hdrBarrier;
    hdrBarrier.oldLayout = vk::ImageLayout::eGeneral;
    hdrBarrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    hdrBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    hdrBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    hdrBarrier.image = static_cast<vk::Image>(hdrImage);
    hdrBarrier.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
    hdrBarrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
    hdrBarrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
    cb.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                       vk::PipelineStageFlagBits::eComputeShader,
                       {}, 0, nullptr, 0, nullptr, 1, &hdrBarrier, ctx_->dispatcher);
  }

  // =================================================================
  // Pass 6-8: Bloom + Tonemap + FXAA (same as deferred)
  // =================================================================

  const uint32_t bloomWidth  = std::max(width / 2, 1u);
  const uint32_t bloomHeight = std::max(height / 2, 1u);
  const vk::Extent2D bloomExtent{bloomWidth, bloomHeight};

  auto bloomImageA = ctx_->create2DImage(bloomExtent, vk::Format::eR16G16B16A16Sfloat,
      vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled,
      vk::MemoryPropertyFlagBits::eDeviceLocal, false, true, false);
  auto bloomImageB = ctx_->create2DImage(bloomExtent, vk::Format::eR16G16B16A16Sfloat,
      vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled,
      vk::MemoryPropertyFlagBits::eDeviceLocal, false, true, false);

  // Transition bloom images
  {
    SingleUseCommandBuffer cmd(*ctx_, vk_queue_e::graphics);
    vk::CommandBuffer cb = *cmd;
    std::array<vk::ImageMemoryBarrier, 2> barriers;
    for (int i = 0; i < 2; ++i) {
      barriers[i].oldLayout = vk::ImageLayout::eUndefined;
      barriers[i].newLayout = vk::ImageLayout::eGeneral;
      barriers[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      barriers[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      barriers[i].image = (i == 0) ? static_cast<vk::Image>(bloomImageA) : static_cast<vk::Image>(bloomImageB);
      barriers[i].subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
      barriers[i].srcAccessMask = {};
      barriers[i].dstAccessMask = vk::AccessFlagBits::eShaderWrite;
    }
    cb.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eComputeShader,
                       {}, 0, nullptr, 0, nullptr, 2, barriers.data(), ctx_->dispatcher);
  }

  vk::SamplerCreateInfo linearSamplerInfo{};
  linearSamplerInfo.magFilter = vk::Filter::eLinear;
  linearSamplerInfo.minFilter = vk::Filter::eLinear;
  linearSamplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
  linearSamplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
  linearSamplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
  auto linearSampler = ctx_->createSampler(linearSamplerInfo);

  // Bloom threshold
  {
    auto& btDsLayout = bloom_threshold_comp_shader_->layout(0);
    vk::DescriptorSet btDs;
    ctx_->acquireSet(btDsLayout, btDs);
    vk::DescriptorImageInfo hdrSampledInfo;
    hdrSampledInfo.sampler = *linearSampler; hdrSampledInfo.imageView = hdrImage.view();
    hdrSampledInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    ctx_->writeDescriptorSet(hdrSampledInfo, btDs, vk::DescriptorType::eCombinedImageSampler, 0);
    vk::DescriptorImageInfo bloomOutInfo;
    bloomOutInfo.imageView = bloomImageA.view(); bloomOutInfo.imageLayout = vk::ImageLayout::eGeneral;
    ctx_->writeDescriptorSet(bloomOutInfo, btDs, vk::DescriptorType::eStorageImage, 1);

    SingleUseCommandBuffer cmd(*ctx_, vk_queue_e::graphics);
    vk::CommandBuffer cb = *cmd;
    cb.bindPipeline(vk::PipelineBindPoint::eCompute, **bloom_threshold_pipeline_, ctx_->dispatcher);
    cb.bindDescriptorSets(vk::PipelineBindPoint::eCompute, static_cast<vk::PipelineLayout>(*bloom_threshold_pipeline_),
                          0, 1, &btDs, 0, nullptr, ctx_->dispatcher);
    BloomThresholdPushConstants btPc{}; btPc.srcWidth = width; btPc.srcHeight = height;
    btPc.dstWidth = bloomWidth; btPc.dstHeight = bloomHeight;
    btPc.threshold = 1.0f; btPc.softKnee = 0.5f;
    cb.pushConstants(static_cast<vk::PipelineLayout>(*bloom_threshold_pipeline_),
                     vk::ShaderStageFlagBits::eCompute, 0, static_cast<uint32_t>(sizeof(btPc)), &btPc, ctx_->dispatcher);
    cb.dispatch((bloomWidth+15)/16, (bloomHeight+15)/16, 1, ctx_->dispatcher);

    vk::ImageMemoryBarrier b; b.oldLayout = vk::ImageLayout::eGeneral;
    b.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = static_cast<vk::Image>(bloomImageA);
    b.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
    b.srcAccessMask = vk::AccessFlagBits::eShaderWrite; b.dstAccessMask = vk::AccessFlagBits::eShaderRead;
    cb.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader,
                       {}, 0, nullptr, 0, nullptr, 1, &b, ctx_->dispatcher);
  }

  // Bloom H-blur (A -> B)
  {
    auto& blurDsLayout = bloom_blur_comp_shader_->layout(0);
    vk::DescriptorSet hDs; ctx_->acquireSet(blurDsLayout, hDs);
    vk::DescriptorImageInfo aInfo; aInfo.sampler = *linearSampler; aInfo.imageView = bloomImageA.view();
    aInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    ctx_->writeDescriptorSet(aInfo, hDs, vk::DescriptorType::eCombinedImageSampler, 0);
    vk::DescriptorImageInfo bInfo; bInfo.imageView = bloomImageB.view(); bInfo.imageLayout = vk::ImageLayout::eGeneral;
    ctx_->writeDescriptorSet(bInfo, hDs, vk::DescriptorType::eStorageImage, 1);

    SingleUseCommandBuffer cmd(*ctx_, vk_queue_e::graphics); vk::CommandBuffer cb = *cmd;
    cb.bindPipeline(vk::PipelineBindPoint::eCompute, **bloom_blur_pipeline_, ctx_->dispatcher);
    cb.bindDescriptorSets(vk::PipelineBindPoint::eCompute, static_cast<vk::PipelineLayout>(*bloom_blur_pipeline_),
                          0, 1, &hDs, 0, nullptr, ctx_->dispatcher);
    BloomBlurPushConstants hp{}; hp.width = bloomWidth; hp.height = bloomHeight; hp.dirX = 1.f; hp.dirY = 0.f;
    cb.pushConstants(static_cast<vk::PipelineLayout>(*bloom_blur_pipeline_), vk::ShaderStageFlagBits::eCompute,
                     0, static_cast<uint32_t>(sizeof(hp)), &hp, ctx_->dispatcher);
    cb.dispatch((bloomWidth+15)/16, (bloomHeight+15)/16, 1, ctx_->dispatcher);

    vk::ImageMemoryBarrier b; b.oldLayout = vk::ImageLayout::eGeneral;
    b.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = static_cast<vk::Image>(bloomImageB);
    b.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
    b.srcAccessMask = vk::AccessFlagBits::eShaderWrite; b.dstAccessMask = vk::AccessFlagBits::eShaderRead;
    cb.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader,
                       {}, 0, nullptr, 0, nullptr, 1, &b, ctx_->dispatcher);
  }

  // Bloom V-blur (B -> A)
  {
    { // Transition A back to eGeneral
      SingleUseCommandBuffer cmd(*ctx_, vk_queue_e::graphics); vk::CommandBuffer cb = *cmd;
      vk::ImageMemoryBarrier b; b.oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
      b.newLayout = vk::ImageLayout::eGeneral;
      b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      b.image = static_cast<vk::Image>(bloomImageA);
      b.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
      b.srcAccessMask = vk::AccessFlagBits::eShaderRead; b.dstAccessMask = vk::AccessFlagBits::eShaderWrite;
      cb.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader,
                         {}, 0, nullptr, 0, nullptr, 1, &b, ctx_->dispatcher);
    }

    auto& blurDsLayout = bloom_blur_comp_shader_->layout(0);
    vk::DescriptorSet vDs; ctx_->acquireSet(blurDsLayout, vDs);
    vk::DescriptorImageInfo bRead; bRead.sampler = *linearSampler; bRead.imageView = bloomImageB.view();
    bRead.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    ctx_->writeDescriptorSet(bRead, vDs, vk::DescriptorType::eCombinedImageSampler, 0);
    vk::DescriptorImageInfo aWrite; aWrite.imageView = bloomImageA.view(); aWrite.imageLayout = vk::ImageLayout::eGeneral;
    ctx_->writeDescriptorSet(aWrite, vDs, vk::DescriptorType::eStorageImage, 1);

    SingleUseCommandBuffer cmd(*ctx_, vk_queue_e::graphics); vk::CommandBuffer cb = *cmd;
    cb.bindPipeline(vk::PipelineBindPoint::eCompute, **bloom_blur_pipeline_, ctx_->dispatcher);
    cb.bindDescriptorSets(vk::PipelineBindPoint::eCompute, static_cast<vk::PipelineLayout>(*bloom_blur_pipeline_),
                          0, 1, &vDs, 0, nullptr, ctx_->dispatcher);
    BloomBlurPushConstants vp{}; vp.width = bloomWidth; vp.height = bloomHeight; vp.dirX = 0.f; vp.dirY = 1.f;
    cb.pushConstants(static_cast<vk::PipelineLayout>(*bloom_blur_pipeline_), vk::ShaderStageFlagBits::eCompute,
                     0, static_cast<uint32_t>(sizeof(vp)), &vp, ctx_->dispatcher);
    cb.dispatch((bloomWidth+15)/16, (bloomHeight+15)/16, 1, ctx_->dispatcher);

    vk::ImageMemoryBarrier b; b.oldLayout = vk::ImageLayout::eGeneral;
    b.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = static_cast<vk::Image>(bloomImageA);
    b.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
    b.srcAccessMask = vk::AccessFlagBits::eShaderWrite; b.dstAccessMask = vk::AccessFlagBits::eShaderRead;
    cb.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader,
                       {}, 0, nullptr, 0, nullptr, 1, &b, ctx_->dispatcher);
  }

  // Tone mapping
  auto ldrImage = ctx_->create2DImage(extent, vk::Format::eR8G8B8A8Unorm,
      vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled,
      vk::MemoryPropertyFlagBits::eDeviceLocal, false, true, false);
  {
    SingleUseCommandBuffer cmd(*ctx_, vk_queue_e::graphics); vk::CommandBuffer cb = *cmd;
    vk::ImageMemoryBarrier b; b.oldLayout = vk::ImageLayout::eUndefined;
    b.newLayout = vk::ImageLayout::eGeneral;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = static_cast<vk::Image>(ldrImage);
    b.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
    b.srcAccessMask = {}; b.dstAccessMask = vk::AccessFlagBits::eShaderWrite;
    cb.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eComputeShader,
                       {}, 0, nullptr, 0, nullptr, 1, &b, ctx_->dispatcher);
  }
  {
    auto& tmDsLayout = tonemap_comp_shader_->layout(0);
    vk::DescriptorSet tmDs; ctx_->acquireSet(tmDsLayout, tmDs);
    vk::DescriptorImageInfo hdrIn; hdrIn.sampler = *linearSampler; hdrIn.imageView = hdrImage.view();
    hdrIn.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    ctx_->writeDescriptorSet(hdrIn, tmDs, vk::DescriptorType::eCombinedImageSampler, 0);
    vk::DescriptorImageInfo bloomIn; bloomIn.sampler = *linearSampler; bloomIn.imageView = bloomImageA.view();
    bloomIn.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    ctx_->writeDescriptorSet(bloomIn, tmDs, vk::DescriptorType::eCombinedImageSampler, 1);
    vk::DescriptorImageInfo ldrOut; ldrOut.imageView = ldrImage.view(); ldrOut.imageLayout = vk::ImageLayout::eGeneral;
    ctx_->writeDescriptorSet(ldrOut, tmDs, vk::DescriptorType::eStorageImage, 2);

    SingleUseCommandBuffer cmd(*ctx_, vk_queue_e::graphics); vk::CommandBuffer cb = *cmd;
    cb.bindPipeline(vk::PipelineBindPoint::eCompute, **tonemap_pipeline_, ctx_->dispatcher);
    cb.bindDescriptorSets(vk::PipelineBindPoint::eCompute, static_cast<vk::PipelineLayout>(*tonemap_pipeline_),
                          0, 1, &tmDs, 0, nullptr, ctx_->dispatcher);
    TonemapPushConstants tp{}; tp.width = width; tp.height = height; tp.exposure = 1.0f; tp.bloomStrength = 0.04f;
    cb.pushConstants(static_cast<vk::PipelineLayout>(*tonemap_pipeline_), vk::ShaderStageFlagBits::eCompute,
                     0, static_cast<uint32_t>(sizeof(tp)), &tp, ctx_->dispatcher);
    cb.dispatch((width+15)/16, (height+15)/16, 1, ctx_->dispatcher);

    vk::ImageMemoryBarrier b; b.oldLayout = vk::ImageLayout::eGeneral;
    b.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = static_cast<vk::Image>(ldrImage);
    b.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
    b.srcAccessMask = vk::AccessFlagBits::eShaderWrite; b.dstAccessMask = vk::AccessFlagBits::eShaderRead;
    cb.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader,
                       {}, 0, nullptr, 0, nullptr, 1, &b, ctx_->dispatcher);
  }

  // FXAA
  auto fxaaImage = ctx_->create2DImage(extent, vk::Format::eR8G8B8A8Unorm,
      vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc,
      vk::MemoryPropertyFlagBits::eDeviceLocal, false, true, true);
  {
    SingleUseCommandBuffer cmd(*ctx_, vk_queue_e::graphics); vk::CommandBuffer cb = *cmd;
    vk::ImageMemoryBarrier b; b.oldLayout = vk::ImageLayout::eUndefined;
    b.newLayout = vk::ImageLayout::eGeneral;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = static_cast<vk::Image>(fxaaImage);
    b.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
    b.srcAccessMask = {}; b.dstAccessMask = vk::AccessFlagBits::eShaderWrite;
    cb.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eComputeShader,
                       {}, 0, nullptr, 0, nullptr, 1, &b, ctx_->dispatcher);
  }
  {
    auto& fxDsLayout = fxaa_comp_shader_->layout(0);
    vk::DescriptorSet fxDs; ctx_->acquireSet(fxDsLayout, fxDs);
    vk::DescriptorImageInfo ldrIn; ldrIn.sampler = *linearSampler; ldrIn.imageView = ldrImage.view();
    ldrIn.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    ctx_->writeDescriptorSet(ldrIn, fxDs, vk::DescriptorType::eCombinedImageSampler, 0);
    vk::DescriptorImageInfo fxOut; fxOut.imageView = fxaaImage.view(); fxOut.imageLayout = vk::ImageLayout::eGeneral;
    ctx_->writeDescriptorSet(fxOut, fxDs, vk::DescriptorType::eStorageImage, 1);

    SingleUseCommandBuffer cmd(*ctx_, vk_queue_e::graphics); vk::CommandBuffer cb = *cmd;
    cb.bindPipeline(vk::PipelineBindPoint::eCompute, **fxaa_pipeline_, ctx_->dispatcher);
    cb.bindDescriptorSets(vk::PipelineBindPoint::eCompute, static_cast<vk::PipelineLayout>(*fxaa_pipeline_),
                          0, 1, &fxDs, 0, nullptr, ctx_->dispatcher);
    FXAAPushConstants fp{}; fp.width = width; fp.height = height;
    fp.edgeThresholdMin = 0.0312f; fp.edgeThreshold = 0.125f;
    cb.pushConstants(static_cast<vk::PipelineLayout>(*fxaa_pipeline_), vk::ShaderStageFlagBits::eCompute,
                     0, static_cast<uint32_t>(sizeof(fp)), &fp, ctx_->dispatcher);
    cb.dispatch((width+15)/16, (height+15)/16, 1, ctx_->dispatcher);

    vk::ImageMemoryBarrier b; b.oldLayout = vk::ImageLayout::eGeneral;
    b.newLayout = vk::ImageLayout::eTransferSrcOptimal;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = static_cast<vk::Image>(fxaaImage);
    b.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
    b.srcAccessMask = vk::AccessFlagBits::eShaderWrite; b.dstAccessMask = vk::AccessFlagBits::eTransferRead;
    cb.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eTransfer,
                       {}, 0, nullptr, 0, nullptr, 1, &b, ctx_->dispatcher);
  }

  // =================================================================
  // Readback
  // =================================================================

  const vk::DeviceSize pixelBytes = static_cast<vk::DeviceSize>(width) * height * 4;
  auto staging = ctx_->createStagingBuffer(pixelBytes, vk::BufferUsageFlagBits::eTransferDst);
  {
    SingleUseCommandBuffer cmd(*ctx_, vk_queue_e::graphics);
    vk::CommandBuffer cb = *cmd;
    vk::BufferImageCopy region;
    region.bufferOffset = 0; region.bufferRowLength = 0; region.bufferImageHeight = 0;
    region.imageSubresource = vk::ImageSubresourceLayers{vk::ImageAspectFlagBits::eColor, 0, 0, 1};
    region.imageOffset = vk::Offset3D{0, 0, 0};
    region.imageExtent = vk::Extent3D{width, height, 1};
    cb.copyImageToBuffer(static_cast<vk::Image>(fxaaImage), vk::ImageLayout::eTransferSrcOptimal,
                         *staging, 1, &region, ctx_->dispatcher);
  }

  staging.map();
  result.color = createReadback(staging.mappedAddress(), width, height, 4, 1);
  staging.unmap();

  // Cleanup per-frame RT resources
  destroyBLAS(blas);
  destroyTLAS(tlas);

  auto t1 = std::chrono::high_resolution_clock::now();
  result.render_time_us = static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count());
  result.success = true;

  std::printf("[VulkanHybridRenderer] rendered %ux%u in %.1f ms (RT shadows + reflections)\n",
              width, height, result.render_time_us / 1000.0);
  return result;
}

// ---------------------------------------------------------------
// shutdown
// ---------------------------------------------------------------

void VulkanHybridRenderer::shutdown() {
  if (!initialised_) return;
  if (ctx_) ctx_->sync();

  fxaa_pipeline_.reset(); fxaa_comp_shader_.reset();
  tonemap_pipeline_.reset(); tonemap_comp_shader_.reset();
  bloom_blur_pipeline_.reset(); bloom_blur_comp_shader_.reset();
  bloom_threshold_pipeline_.reset(); bloom_threshold_comp_shader_.reset();
  lighting_pipeline_.reset(); lighting_comp_shader_.reset();
  ssao_blur_pipeline_.reset(); ssao_blur_comp_shader_.reset();
  ssao_pipeline_.reset(); ssao_comp_shader_.reset();

  destroyRTPipeline();
  raygen_shader_.reset(); shadow_miss_shader_.reset();
  reflect_miss_shader_.reset(); reflect_chit_shader_.reset();

  gbuffer_pipeline_.reset();
  gbuffer_vert_shader_.reset(); gbuffer_frag_shader_.reset();
  gbuffer_pass_.reset();

  initialised_ = false;
  std::printf("[VulkanHybridRenderer] shut down\n");
}

// ---------------------------------------------------------------
// Factory
// ---------------------------------------------------------------

std::unique_ptr<IRasterRenderer> createVulkanHybridRenderer() {
  return std::make_unique<VulkanHybridRenderer>();
}

}  // namespace render
}  // namespace zs

#else  // !ZS_ENABLE_VULKAN

namespace zs {
namespace render {

std::unique_ptr<IRasterRenderer> createVulkanHybridRenderer() {
  std::fprintf(stderr, "[VulkanHybridRenderer] Vulkan not enabled (ZS_ENABLE_VULKAN=0)\n");
  return nullptr;
}

}  // namespace render
}  // namespace zs

#endif  // ZS_ENABLE_VULKAN
