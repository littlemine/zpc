/// @file VulkanDeferredRenderer.cpp
/// @brief Headless Vulkan deferred renderer with shadow mapping, SSAO, bloom,
///        tone mapping, and FXAA.
///
/// Eight-pass deferred rendering:
///   1. Shadow map pass (depth-only render from light POV)
///   2. G-buffer pass (rasterization with MRT) — writes position, normal, albedo
///   3. SSAO pass (compute) — generates raw ambient occlusion from G-buffer
///   4. SSAO blur pass (compute) — bilateral blur for edge-preserving AO smoothing
///   5. Lighting pass (compute) — reads G-buffer + shadow map + AO, writes HDR
///   6. Bloom pass (compute) — threshold + separable Gaussian blur on half-res
///   7. Tone mapping pass (compute) — ACES filmic + exposure + bloom composite + gamma
///   8. FXAA pass (compute) — luminance-based edge-detecting anti-aliasing
///
/// Shadow mapping features:
///   - Orthographic projection for directional lights
///   - Depth bias (constant + slope-scaled) in the shadow pipeline
///   - 5x5 PCF (Percentage-Closer Filtering) for soft shadow edges
///   - Normal offset bias to reduce shadow acne
///
/// SSAO features:
///   - 32-sample hemisphere kernel oriented along surface normal
///   - Per-pixel random rotation (tiled hash noise) to break banding
///   - View-space depth comparison with range check
///   - Edge-aware bilateral blur (5x5) that respects depth discontinuities
///   - AO factor modulates ambient lighting in the final pass
///
/// Post-processing features:
///   - HDR lighting output (R16G16B16A16Sfloat) for proper bloom extraction
///   - Bloom: bright-pass threshold → separable Gaussian blur (half-res)
///   - Tone mapping: ACES filmic curve with configurable exposure
///   - Bloom composite: additive blend of bloom into tonemapped image
///   - FXAA: luminance-based edge detection with sub-pixel anti-aliasing
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

// -- Shadow map vertex shader (depth-only) --
static const char* k_shadow_vert_glsl = R"(
#version 450

layout(push_constant) uniform PushConstants {
  mat4 light_mvp;
} pc;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;
layout(location = 3) in vec2 inUV;

void main() {
  gl_Position = pc.light_mvp * vec4(inPosition, 1.0);
}
)";

// -- Shadow map fragment shader (no-op, required by pipeline builder) --
static const char* k_shadow_frag_glsl = R"(
#version 450
void main() {}
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

// -- SSAO compute shader --
// Generates a screen-space ambient occlusion factor for each pixel.
// Uses a hemisphere of random samples oriented along the surface normal.
// Samples are checked against the depth buffer (via position G-buffer)
// in view space.  A 4x4 random rotation is tiled across the screen
// to break banding, and the output is a single-channel R8 texture.
static const char* k_ssao_comp_glsl = R"(
#version 450

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

// G-buffer inputs
layout(set = 0, binding = 0) uniform sampler2D gPosition;  // world-space position
layout(set = 0, binding = 1) uniform sampler2D gNormal;    // world-space normal

// Output AO texture (single channel)
layout(set = 0, binding = 2, r8) uniform writeonly image2D aoImage;

// Push constants
layout(push_constant) uniform PushConstants {
  mat4 view;          // camera view matrix (world -> view)
  mat4 projection;    // camera projection matrix (view -> clip)
  uint width;
  uint height;
  float radius;       // sample hemisphere radius (world space)
  float bias;         // depth bias to prevent self-occlusion
  float intensity;    // AO intensity multiplier
  float _pad0;
} pc;

// Pseudo-random hash for sample generation and noise.
// We embed a fixed set of hemisphere samples and use a tiled noise
// pattern derived from pixel coordinates to rotate them.

// 32 hemisphere sample offsets (pre-computed, distributed on unit hemisphere)
// These are encoded as constants rather than using a buffer.
const int NUM_SAMPLES = 32;

vec3 hemisphereKernel[NUM_SAMPLES] = vec3[](
  vec3( 0.045,  0.024,  0.034),
  vec3(-0.025,  0.060,  0.010),
  vec3( 0.080, -0.012,  0.065),
  vec3(-0.035,  0.082,  0.025),
  vec3( 0.002,  0.011,  0.098),
  vec3( 0.110, -0.030,  0.080),
  vec3(-0.065,  0.045,  0.120),
  vec3( 0.028, -0.095,  0.055),
  vec3( 0.150,  0.060,  0.020),
  vec3(-0.048,  0.130,  0.095),
  vec3( 0.085, -0.110,  0.065),
  vec3(-0.180,  0.035,  0.100),
  vec3( 0.030,  0.200,  0.040),
  vec3( 0.140, -0.060,  0.180),
  vec3(-0.095,  0.015,  0.220),
  vec3( 0.060, -0.170,  0.160),
  vec3( 0.250,  0.080,  0.100),
  vec3(-0.120,  0.210,  0.120),
  vec3( 0.065, -0.230,  0.190),
  vec3(-0.280,  0.060,  0.050),
  vec3( 0.180,  0.200,  0.220),
  vec3(-0.050,  0.320,  0.130),
  vec3( 0.300, -0.100,  0.060),
  vec3(-0.150,  0.050,  0.350),
  vec3( 0.100,  0.400,  0.090),
  vec3( 0.350, -0.150,  0.250),
  vec3(-0.200,  0.300,  0.200),
  vec3( 0.050, -0.380,  0.300),
  vec3( 0.420,  0.100,  0.150),
  vec3(-0.350,  0.250,  0.100),
  vec3( 0.150, -0.100,  0.480),
  vec3(-0.100,  0.450,  0.250)
);

// Hash function for per-pixel random rotation
float hash(vec2 p) {
  return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
}

void main() {
  ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
  if (pixel.x >= int(pc.width) || pixel.y >= int(pc.height)) return;

  vec2 uv = (vec2(pixel) + 0.5) / vec2(pc.width, pc.height);

  // Read world-space position and normal from G-buffer
  vec3 worldPos = texture(gPosition, uv).xyz;
  vec3 worldNormal = texture(gNormal, uv).xyz;

  // Check for background (no geometry)
  if (length(worldNormal) < 0.01) {
    imageStore(aoImage, pixel, vec4(1.0));  // no occlusion for background
    return;
  }
  worldNormal = normalize(worldNormal);

  // Transform to view space for depth comparison
  vec3 viewPos = (pc.view * vec4(worldPos, 1.0)).xyz;
  vec3 viewNormal = normalize((pc.view * vec4(worldNormal, 0.0)).xyz);

  // Per-pixel random rotation angle (tiled 4x4 noise pattern)
  float noiseAngle = hash(vec2(pixel) * 0.25) * 6.2831853;
  float cosA = cos(noiseAngle);
  float sinA = sin(noiseAngle);

  // Build TBN matrix from view-space normal with random rotation
  // Create tangent from any non-parallel vector
  vec3 tangent;
  if (abs(viewNormal.y) < 0.999) {
    tangent = normalize(cross(viewNormal, vec3(0.0, 1.0, 0.0)));
  } else {
    tangent = normalize(cross(viewNormal, vec3(1.0, 0.0, 0.0)));
  }
  vec3 bitangent = cross(viewNormal, tangent);

  // Apply random rotation around normal
  vec3 rotTangent = tangent * cosA + bitangent * sinA;
  vec3 rotBitangent = -tangent * sinA + bitangent * cosA;

  mat3 TBN = mat3(rotTangent, rotBitangent, viewNormal);

  // Sample hemisphere and check occlusion
  float occlusion = 0.0;

  for (int i = 0; i < NUM_SAMPLES; i++) {
    // Orient sample in view space via TBN
    vec3 sampleOffset = TBN * hemisphereKernel[i];
    vec3 samplePos = viewPos + sampleOffset * pc.radius;

    // Project sample to screen space
    vec4 clipPos = pc.projection * vec4(samplePos, 1.0);
    vec2 sampleUV = (clipPos.xy / clipPos.w) * 0.5 + 0.5;

    // Bounds check
    if (sampleUV.x < 0.0 || sampleUV.x > 1.0 ||
        sampleUV.y < 0.0 || sampleUV.y > 1.0) {
      continue;
    }

    // Read the actual position at the sample's screen location
    vec3 actualWorldPos = texture(gPosition, sampleUV).xyz;
    vec3 actualViewPos = (pc.view * vec4(actualWorldPos, 1.0)).xyz;

    // Range check: only count occlusion from nearby geometry
    float rangeCheck = smoothstep(0.0, 1.0,
        pc.radius / max(abs(viewPos.z - actualViewPos.z), 0.001));

    // Occlusion test: is the actual surface closer to camera than sample?
    // (In view space with RH convention, more negative z = further away)
    occlusion += (actualViewPos.z >= samplePos.z + pc.bias ? 1.0 : 0.0) * rangeCheck;
  }

  float ao = 1.0 - (occlusion / float(NUM_SAMPLES)) * pc.intensity;
  ao = clamp(ao, 0.0, 1.0);

  imageStore(aoImage, pixel, vec4(ao, ao, ao, 1.0));
}
)";

// -- SSAO blur compute shader --
// Edge-preserving bilateral blur (4x4 kernel) that respects depth
// discontinuities to keep AO sharp at object boundaries.
static const char* k_ssao_blur_comp_glsl = R"(
#version 450

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

// Input raw AO
layout(set = 0, binding = 0) uniform sampler2D aoInput;

// Input position G-buffer (for edge-aware filtering)
layout(set = 0, binding = 1) uniform sampler2D gPosition;

// Output blurred AO
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

  vec2 uv = (vec2(pixel) + 0.5) / vec2(pc.width, pc.height);
  vec2 texelSize = 1.0 / vec2(pc.width, pc.height);

  float centerAO = texture(aoInput, uv).r;
  vec3 centerPos = texture(gPosition, uv).xyz;

  // If background, pass through
  if (length(texture(gPosition, uv).xyz) < 0.001 &&
      abs(centerAO - 1.0) < 0.01) {
    imageStore(aoBlurred, pixel, vec4(1.0));
    return;
  }

  float totalAO = 0.0;
  float totalWeight = 0.0;

  const int blurRadius = 2;  // 5x5 bilateral blur

  for (int x = -blurRadius; x <= blurRadius; x++) {
    for (int y = -blurRadius; y <= blurRadius; y++) {
      vec2 offsetUV = uv + vec2(float(x), float(y)) * texelSize;

      // Bounds check
      if (offsetUV.x < 0.0 || offsetUV.x > 1.0 ||
          offsetUV.y < 0.0 || offsetUV.y > 1.0) {
        continue;
      }

      float sampleAO = texture(aoInput, offsetUV).r;
      vec3 samplePos = texture(gPosition, offsetUV).xyz;

      // Bilateral weight: reduce weight for samples across depth edges
      float posDiff = length(samplePos - centerPos);
      float depthWeight = exp(-posDiff * posDiff * 10.0);

      // Spatial weight (Gaussian-like)
      float spatialDist = float(x * x + y * y);
      float spatialWeight = exp(-spatialDist * 0.2);

      float weight = depthWeight * spatialWeight;
      totalAO += sampleAO * weight;
      totalWeight += weight;
    }
  }

  float blurredAO = (totalWeight > 0.001) ? totalAO / totalWeight : centerAO;
  imageStore(aoBlurred, pixel, vec4(blurredAO, blurredAO, blurredAO, 1.0));
}
)";

// -- Lighting compute shader (with shadow mapping + PCF + SSAO) --
static const char* k_lighting_comp_glsl = R"(
#version 450

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

// G-buffer inputs (combined image samplers)
layout(set = 0, binding = 0) uniform sampler2D gPosition;
layout(set = 0, binding = 1) uniform sampler2D gNormal;
layout(set = 0, binding = 2) uniform sampler2D gAlbedo;

// Output HDR image (R16G16B16A16Sfloat — no tone mapping here)
layout(set = 0, binding = 3, rgba16f) uniform writeonly image2D outImage;

// Light SSBO
struct GPULight {
  vec4 position_type;     // xyz = position/direction, w = type (0=dir, 1=point, 2=area)
  vec4 color_intensity;   // rgb = color, w = intensity
};
layout(std430, set = 0, binding = 4) readonly buffer Lights {
  GPULight lights[];
} lightBuf;

// Shadow map (depth texture, comparison sampler)
layout(set = 0, binding = 5) uniform sampler2D shadowMap;

// SSAO occlusion texture (single channel, blurred)
layout(set = 0, binding = 6) uniform sampler2D aoMap;

// Push constants
layout(push_constant) uniform PushConstants {
  vec4 camera_pos_numLights;  // xyz = camera position, w = numLights (as float)
  uint width;
  uint height;
  float ambient;
  float shadow_intensity;     // 0 = no shadow, 1 = full shadow
  mat4 light_vp;              // light view-projection matrix for shadow mapping
} pc;

/// 5x5 PCF shadow sampling with Poisson-like offsets for soft shadows.
float sampleShadow(vec3 worldPos) {
  // Transform world position to light clip space
  vec4 lightClip = pc.light_vp * vec4(worldPos, 1.0);
  vec3 lightNDC = lightClip.xyz / lightClip.w;

  // Convert from NDC [-1,1] to shadow map UV [0,1]
  // Note: Vulkan depth is already [0,1], but X/Y need mapping
  vec2 shadowUV = lightNDC.xy * 0.5 + 0.5;
  float currentDepth = lightNDC.z;

  // If outside the shadow map, no shadow
  if (shadowUV.x < 0.0 || shadowUV.x > 1.0 ||
      shadowUV.y < 0.0 || shadowUV.y > 1.0 ||
      currentDepth < 0.0 || currentDepth > 1.0) {
    return 1.0;  // fully lit
  }

  // PCF 5x5 kernel for soft shadows
  float shadow = 0.0;
  vec2 texelSize = 1.0 / vec2(textureSize(shadowMap, 0));
  const float bias = 0.002;  // small constant bias (depth bias is also in the pipeline)

  const int pcfRadius = 2;  // 5x5 kernel
  float sampleCount = 0.0;

  for (int x = -pcfRadius; x <= pcfRadius; ++x) {
    for (int y = -pcfRadius; y <= pcfRadius; ++y) {
      vec2 offset = vec2(float(x), float(y)) * texelSize;
      float shadowDepth = texture(shadowMap, shadowUV + offset).r;
      shadow += (currentDepth - bias > shadowDepth) ? 0.0 : 1.0;
      sampleCount += 1.0;
    }
  }

  return shadow / sampleCount;
}

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

  // Shadow factor (1.0 = fully lit, 0.0 = fully shadowed)
  float shadowFactor = mix(1.0, sampleShadow(position), pc.shadow_intensity);

  // SSAO factor (1.0 = no occlusion, 0.0 = fully occluded)
  float aoFactor = texture(aoMap, uv).r;

  // Hemisphere ambient: surfaces facing up get more indirect light
  // (simulates diffuse interreflection from floor/ceiling).
  // In a closed box like Cornell, surfaces get significant indirect
  // illumination from all directions — use a generous base.
  float hemiBlend = normal.y * 0.5 + 0.5;  // remap [-1,1] -> [0,1]
  float hemiAmbient = mix(pc.ambient * 0.6, pc.ambient * 1.4, hemiBlend);

  // Apply SSAO to ambient — darkens crevices and corners
  hemiAmbient *= aoFactor;

  // Accumulate lighting (Blinn-Phong with wrap lighting)
  vec3 totalLight = vec3(hemiAmbient);
  uint numLights = uint(pc.camera_pos_numLights.w);

  // Wrap factor: allows light to "wrap" around surfaces, providing
  // soft illumination even when NdotL would normally be zero.
  // A moderate wrap factor approximates the broad illumination from an
  // area light source (like the Cornell Box ceiling panel).
  const float wrapFactor = 0.35;

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
      // Point or area light — treat as point source from position
      vec3 toLight = light.position_type.xyz - position;
      float dist = length(toLight);
      lightDir = toLight / max(dist, 0.001);
      // Inverse-square attenuation with minimum distance clamp
      // to prevent singularity at the light source.
      float clampDist = max(dist, 0.5);
      attenuation = 1.0 / (clampDist * clampDist);
    }

    // Wrap lighting: (NdotL + wrap) / (1 + wrap)
    // Provides soft illumination even for surfaces nearly perpendicular
    // to the light direction (e.g., vertical walls under a ceiling light).
    float rawNdotL = dot(normal, lightDir);
    float wrappedNdotL = max((rawNdotL + wrapFactor) / (1.0 + wrapFactor), 0.0);

    // Specular (Blinn-Phong) — use unwrapped NdotL for specular gate
    vec3 halfVec = normalize(lightDir + viewDir);
    float NdotH = max(dot(normal, halfVec), 0.0);
    float specular = (rawNdotL > 0.0) ? pow(NdotH, 32.0) * 0.3 : 0.0;

    // Apply shadow to diffuse + specular (not ambient)
    totalLight += (wrappedNdotL + specular) * lightColor * intensity * attenuation * shadowFactor;
  }

  vec3 color = albedo * totalLight;

  // Output HDR linear color — tone mapping and gamma are applied in
  // the dedicated post-processing passes (bloom → tonemap → FXAA).
  imageStore(outImage, pixel, vec4(color, 1.0));
}
)";

// -- Bloom threshold + downsample compute shader --
// Extracts bright pixels above a luminance threshold and writes them to
// a half-resolution image for subsequent Gaussian blur.
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
  float threshold;    // luminance threshold for bloom (default 1.0)
  float softKnee;     // soft threshold knee width (default 0.5)
  float _pad0;
  float _pad1;
} pc;

void main() {
  ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
  if (pixel.x >= int(pc.dstWidth) || pixel.y >= int(pc.dstHeight)) return;

  // Sample the HDR image at the center of the corresponding 2x2 block
  vec2 uv = (vec2(pixel) * 2.0 + 1.0) / vec2(pc.srcWidth, pc.srcHeight);
  vec3 color = texture(hdrInput, uv).rgb;

  // Luminance-based soft threshold
  float lum = dot(color, vec3(0.2126, 0.7152, 0.0722));
  float knee = pc.threshold * pc.softKnee;
  float soft = lum - pc.threshold + knee;
  soft = clamp(soft, 0.0, 2.0 * knee);
  soft = soft * soft / (4.0 * knee + 0.00001);
  float contribution = max(soft, lum - pc.threshold) / max(lum, 0.00001);
  contribution = max(contribution, 0.0);

  imageStore(bloomOutput, pixel, vec4(color * contribution, 1.0));
}
)";

// -- Bloom Gaussian blur compute shader (separable, one direction) --
// Applies a 13-tap Gaussian blur in either horizontal or vertical direction.
// Direction is controlled via push constants.
static const char* k_bloom_blur_comp_glsl = R"(
#version 450

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(set = 0, binding = 0) uniform sampler2D blurInput;
layout(set = 0, binding = 1, rgba16f) uniform writeonly image2D blurOutput;

layout(push_constant) uniform PushConstants {
  uint width;
  uint height;
  float dirX;   // 1.0 for horizontal, 0.0 for vertical
  float dirY;   // 0.0 for horizontal, 1.0 for vertical
} pc;

// 13-tap Gaussian weights (sigma ~= 4.0)
const int KERNEL_SIZE = 13;
const float weights[KERNEL_SIZE] = float[](
  0.0162, 0.0338, 0.0613, 0.0966, 0.1323,
  0.1573, 0.1627,
  0.1573, 0.1323, 0.0966, 0.0613, 0.0338, 0.0162
);

void main() {
  ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
  if (pixel.x >= int(pc.width) || pixel.y >= int(pc.height)) return;

  vec2 texelSize = 1.0 / vec2(pc.width, pc.height);
  vec2 uv = (vec2(pixel) + 0.5) * texelSize;
  vec2 dir = vec2(pc.dirX, pc.dirY) * texelSize;

  vec3 result = vec3(0.0);
  int halfSize = KERNEL_SIZE / 2;

  for (int i = 0; i < KERNEL_SIZE; i++) {
    vec2 offset = dir * float(i - halfSize);
    result += texture(blurInput, uv + offset).rgb * weights[i];
  }

  imageStore(blurOutput, pixel, vec4(result, 1.0));
}
)";

// -- Tone mapping compute shader (ACES filmic + exposure + bloom composite + gamma) --
// Reads the HDR lit image and bloom texture, composites them, applies ACES
// tone mapping with configurable exposure, then gamma correction to sRGB.
// Outputs R8G8B8A8Unorm for readback or FXAA input.
static const char* k_tonemap_comp_glsl = R"(
#version 450

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(set = 0, binding = 0) uniform sampler2D hdrInput;    // HDR lit image
layout(set = 0, binding = 1) uniform sampler2D bloomInput;  // bloom blur result (half-res)
layout(set = 0, binding = 2, rgba8) uniform writeonly image2D ldrOutput;

layout(push_constant) uniform PushConstants {
  uint width;
  uint height;
  float exposure;        // exposure multiplier (default 1.0)
  float bloomStrength;   // bloom blend factor (default 0.04)
} pc;

// ACES filmic tone mapping (Narkowicz 2015 approximation)
vec3 acesTonemap(vec3 x) {
  const float a = 2.51;
  const float b = 0.03;
  const float c = 2.43;
  const float d = 0.59;
  const float e = 0.14;
  return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main() {
  ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
  if (pixel.x >= int(pc.width) || pixel.y >= int(pc.height)) return;

  vec2 uv = (vec2(pixel) + 0.5) / vec2(pc.width, pc.height);

  // Read HDR scene color
  vec3 hdr = texture(hdrInput, uv).rgb;

  // Read bloom (sampled from half-res with bilinear filtering)
  vec3 bloom = texture(bloomInput, uv).rgb;

  // Composite bloom
  vec3 color = hdr + bloom * pc.bloomStrength;

  // Apply exposure
  color *= pc.exposure;

  // ACES filmic tone mapping
  color = acesTonemap(color);

  // Gamma correction (linear -> sRGB)
  color = pow(color, vec3(1.0 / 2.2));

  imageStore(ldrOutput, pixel, vec4(color, 1.0));
}
)";

// -- FXAA compute shader --
// Fast approximate anti-aliasing (FXAA 3.11 quality 12 approximation).
// Operates on LDR image, uses luminance-based edge detection and
// sub-pixel blending to smooth jagged edges.
static const char* k_fxaa_comp_glsl = R"(
#version 450

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(set = 0, binding = 0) uniform sampler2D ldrInput;
layout(set = 0, binding = 1, rgba8) uniform writeonly image2D fxaaOutput;

layout(push_constant) uniform PushConstants {
  uint width;
  uint height;
  float edgeThresholdMin;   // minimum edge threshold (default 0.0312)
  float edgeThreshold;      // relative edge threshold (default 0.125)
} pc;

float luminance(vec3 c) {
  return dot(c, vec3(0.299, 0.587, 0.114));
}

void main() {
  ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
  if (pixel.x >= int(pc.width) || pixel.y >= int(pc.height)) return;

  vec2 texelSize = 1.0 / vec2(pc.width, pc.height);
  vec2 uv = (vec2(pixel) + 0.5) * texelSize;

  // Sample center and 4-neighbours
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

  // Early exit: skip FXAA for areas with low contrast
  if (lumRange < max(pc.edgeThresholdMin, lumMax * pc.edgeThreshold)) {
    imageStore(fxaaOutput, pixel, vec4(rgbM, 1.0));
    return;
  }

  // Sample 4 diagonal neighbours
  vec3 rgbNW = texture(ldrInput, uv + vec2(-1.0, -1.0) * texelSize).rgb;
  vec3 rgbNE = texture(ldrInput, uv + vec2( 1.0, -1.0) * texelSize).rgb;
  vec3 rgbSW = texture(ldrInput, uv + vec2(-1.0,  1.0) * texelSize).rgb;
  vec3 rgbSE = texture(ldrInput, uv + vec2( 1.0,  1.0) * texelSize).rgb;

  float lumNW = luminance(rgbNW);
  float lumNE = luminance(rgbNE);
  float lumSW = luminance(rgbSW);
  float lumSE = luminance(rgbSE);

  // Compute edge direction
  float edgeH = abs(-2.0 * lumW + lumNW + lumSW) +
                abs(-2.0 * lumM + lumN  + lumS ) * 2.0 +
                abs(-2.0 * lumE + lumNE + lumSE);
  float edgeV = abs(-2.0 * lumN + lumNW + lumNE) +
                abs(-2.0 * lumM + lumW  + lumE ) * 2.0 +
                abs(-2.0 * lumS + lumSW + lumSE);
  bool isHorizontal = (edgeH >= edgeV);

  // Choose edge endpoints
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

  // Shift UV to edge
  vec2 currentUV = uv;
  if (isHorizontal) {
    currentUV.y += stepLength * 0.5;
  } else {
    currentUV.x += stepLength * 0.5;
  }

  // Walk along the edge in both directions
  vec2 edgeStep = isHorizontal ? vec2(texelSize.x, 0.0) : vec2(0.0, texelSize.y);

  vec2 uv1 = currentUV - edgeStep;
  vec2 uv2 = currentUV + edgeStep;

  float lumEnd1 = luminance(texture(ldrInput, uv1).rgb) - localAvg;
  float lumEnd2 = luminance(texture(ldrInput, uv2).rgb) - localAvg;
  bool reached1 = abs(lumEnd1) >= grad1 * 0.5;
  bool reached2 = abs(lumEnd2) >= grad2 * 0.5;

  // Walk up to 8 steps in each direction
  for (int i = 0; i < 8 && !(reached1 && reached2); i++) {
    if (!reached1) {
      uv1 -= edgeStep;
      lumEnd1 = luminance(texture(ldrInput, uv1).rgb) - localAvg;
      reached1 = abs(lumEnd1) >= grad1 * 0.5;
    }
    if (!reached2) {
      uv2 += edgeStep;
      lumEnd2 = luminance(texture(ldrInput, uv2).rgb) - localAvg;
      reached2 = abs(lumEnd2) >= grad2 * 0.5;
    }
  }

  // Compute blend factor based on distance to edge endpoints
  float dist1 = isHorizontal ? (uv.x - uv1.x) : (uv.y - uv1.y);
  float dist2 = isHorizontal ? (uv2.x - uv.x) : (uv2.y - uv.y);
  float minDist = min(dist1, dist2);
  float edgeLength = dist1 + dist2;
  float pixelOffset = -minDist / edgeLength + 0.5;

  // Sub-pixel aliasing factor
  float subPixelLum = (lumN + lumS + lumW + lumE) * 0.25;
  float subPixelDiff = abs(subPixelLum - lumM);
  float subPixelFactor = clamp(subPixelDiff / lumRange, 0.0, 1.0);
  subPixelFactor = smoothstep(0.0, 1.0, subPixelFactor);
  subPixelFactor = subPixelFactor * subPixelFactor * 0.75;

  float blendFactor = max(pixelOffset, subPixelFactor);

  // Final blend: sample offset along the edge normal
  vec2 finalUV = uv;
  if (isHorizontal) {
    finalUV.y += stepLength * blendFactor;
  } else {
    finalUV.x += stepLength * blendFactor;
  }

  vec3 result = texture(ldrInput, finalUV).rgb;
  imageStore(fxaaOutput, pixel, vec4(result, 1.0));
}
)";

// =================================================================
// MVP helpers (same as forward renderer)
// =================================================================

using mat4 = zs::vec<f32, 4, 4>;
using vec3 = zs::vec<f32, 3>;
using vec4 = zs::vec<f32, 4>;

/// Shadow pass push constants: light MVP (64 bytes).
struct ShadowPushConstants {
  float light_mvp[16];
};
static_assert(sizeof(ShadowPushConstants) == 64,
              "ShadowPushConstants must be 64 bytes");

/// SSAO push constants: view (64) + projection (64) + width/height/radius/bias/intensity/pad (24) = 152 bytes.
struct SSAOPushConstants {
  float view[16];         // camera view matrix (column-major for GLSL)
  float projection[16];   // camera projection matrix (column-major for GLSL)
  uint32_t width;
  uint32_t height;
  float radius;           // sample hemisphere radius
  float bias;             // depth comparison bias
  float intensity;        // AO intensity multiplier
  float _pad0;
};
static_assert(sizeof(SSAOPushConstants) == 152,
              "SSAOPushConstants must be 152 bytes");

/// SSAO blur push constants: width/height + padding = 16 bytes.
struct SSAOBlurPushConstants {
  uint32_t width;
  uint32_t height;
  float _pad0;
  float _pad1;
};
static_assert(sizeof(SSAOBlurPushConstants) == 16,
              "SSAOBlurPushConstants must be 16 bytes");

/// G-buffer push constants: MVP (64) + model (64) + material_color (16) = 144 bytes.
struct GBufferPushConstants {
  float mvp[16];
  float model[16];
  float material_color[4];  // rgb = base_color, a = alpha
};
static_assert(sizeof(GBufferPushConstants) == 144,
              "GBufferPushConstants must be 144 bytes");

/// Lighting compute push constants: 96 bytes.
/// camera_pos_numLights (16) + width/height/ambient/shadow_intensity (16) + light_vp (64)
struct LightingPushConstants {
  float camera_pos_numLights[4];  // xyz = cam pos, w = numLights
  uint32_t width;
  uint32_t height;
  float ambient;
  float shadow_intensity;         // 0 = no shadow, 1 = full shadow
  float light_vp[16];            // light view-projection matrix (column-major)
};
static_assert(sizeof(LightingPushConstants) == 96,
              "LightingPushConstants must be 96 bytes");

/// Bloom threshold push constants: 32 bytes.
struct BloomThresholdPushConstants {
  uint32_t srcWidth;
  uint32_t srcHeight;
  uint32_t dstWidth;
  uint32_t dstHeight;
  float threshold;      // luminance threshold (default 1.0)
  float softKnee;       // knee width (default 0.5)
  float _pad0;
  float _pad1;
};
static_assert(sizeof(BloomThresholdPushConstants) == 32,
              "BloomThresholdPushConstants must be 32 bytes");

/// Bloom blur push constants: 16 bytes.
struct BloomBlurPushConstants {
  uint32_t width;
  uint32_t height;
  float dirX;   // 1.0 horizontal, 0.0 vertical
  float dirY;   // 0.0 horizontal, 1.0 vertical
};
static_assert(sizeof(BloomBlurPushConstants) == 16,
              "BloomBlurPushConstants must be 16 bytes");

/// Tone mapping push constants: 16 bytes.
struct TonemapPushConstants {
  uint32_t width;
  uint32_t height;
  float exposure;        // exposure multiplier (default 1.0)
  float bloomStrength;   // bloom blend factor (default 0.04)
};
static_assert(sizeof(TonemapPushConstants) == 16,
              "TonemapPushConstants must be 16 bytes");

/// FXAA push constants: 16 bytes.
struct FXAAPushConstants {
  uint32_t width;
  uint32_t height;
  float edgeThresholdMin;  // minimum edge threshold (default 0.0312)
  float edgeThreshold;     // relative edge threshold (default 0.125)
};
static_assert(sizeof(FXAAPushConstants) == 16,
              "FXAAPushConstants must be 16 bytes");

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

/// Build an orthographic light view-projection matrix for shadow mapping.
///
/// For directional/area lights, we compute a view matrix looking from
/// the light toward the scene center, then wrap the scene's bounding
/// box in an ortho frustum.  For point lights, we approximate with a
/// directional projection from the point light position.
///
/// @param light     The scene light producing shadows.
/// @param sceneMin  Scene AABB minimum (world space).
/// @param sceneMax  Scene AABB maximum (world space).
/// @return          Column-major-convention VP matrix (row-major storage).
static mat4 buildLightVP(const Light& light,
                         const vec3& sceneMin,
                         const vec3& sceneMax) {
  // Scene center and radius
  vec3 center;
  center(0) = (sceneMin(0) + sceneMax(0)) * 0.5f;
  center(1) = (sceneMin(1) + sceneMax(1)) * 0.5f;
  center(2) = (sceneMin(2) + sceneMax(2)) * 0.5f;

  vec3 extent;
  extent(0) = (sceneMax(0) - sceneMin(0)) * 0.5f;
  extent(1) = (sceneMax(1) - sceneMin(1)) * 0.5f;
  extent(2) = (sceneMax(2) - sceneMin(2)) * 0.5f;

  f32 radius = std::sqrt(extent(0)*extent(0) + extent(1)*extent(1) + extent(2)*extent(2));

  // Light direction (always normalised)
  vec3 lightDir;
  if (light.type == LightType::Directional || light.type == LightType::Area) {
    f32 len = std::sqrt(light.direction(0)*light.direction(0) +
                        light.direction(1)*light.direction(1) +
                        light.direction(2)*light.direction(2));
    if (len < 1e-6f) len = 1.f;
    lightDir(0) = light.direction(0) / len;
    lightDir(1) = light.direction(1) / len;
    lightDir(2) = light.direction(2) / len;
  } else {
    // Point light — direction from light to scene center
    lightDir(0) = center(0) - light.position(0);
    lightDir(1) = center(1) - light.position(1);
    lightDir(2) = center(2) - light.position(2);
    f32 len = std::sqrt(lightDir(0)*lightDir(0) + lightDir(1)*lightDir(1) + lightDir(2)*lightDir(2));
    if (len < 1e-6f) len = 1.f;
    lightDir(0) /= len; lightDir(1) /= len; lightDir(2) /= len;
  }

  // Light position: for area lights, use the actual light position.
  // For directional lights, pull back along -lightDir from scene center.
  // For point lights, use the actual light position.
  vec3 lightPos;
  if (light.type == LightType::Area || light.type == LightType::Point) {
    lightPos(0) = light.position(0);
    lightPos(1) = light.position(1);
    lightPos(2) = light.position(2);
  } else {
    // Directional light — synthesise position outside scene
    lightPos(0) = center(0) - lightDir(0) * radius * 2.f;
    lightPos(1) = center(1) - lightDir(1) * radius * 2.f;
    lightPos(2) = center(2) - lightDir(2) * radius * 2.f;
  }

  // Build light view matrix (look-at)
  vec3 f; // forward = lightDir
  f(0) = lightDir(0); f(1) = lightDir(1); f(2) = lightDir(2);

  // Choose an up vector that isn't collinear with f
  vec3 worldUp;
  if (std::abs(f(1)) > 0.99f) {
    worldUp(0) = 1.f; worldUp(1) = 0.f; worldUp(2) = 0.f;
  } else {
    worldUp(0) = 0.f; worldUp(1) = 1.f; worldUp(2) = 0.f;
  }

  vec3 r; // right = cross(f, worldUp)
  r(0) = f(1)*worldUp(2) - f(2)*worldUp(1);
  r(1) = f(2)*worldUp(0) - f(0)*worldUp(2);
  r(2) = f(0)*worldUp(1) - f(1)*worldUp(0);
  f32 rlen = std::sqrt(r(0)*r(0) + r(1)*r(1) + r(2)*r(2));
  if (rlen < 1e-12f) rlen = 1.f;
  r(0) /= rlen; r(1) /= rlen; r(2) /= rlen;

  vec3 u; // up = cross(r, f)
  u(0) = r(1)*f(2) - r(2)*f(1);
  u(1) = r(2)*f(0) - r(0)*f(2);
  u(2) = r(0)*f(1) - r(1)*f(0);

  mat4 V = mat4::identity();
  V(0,0) =  r(0); V(0,1) =  r(1); V(0,2) =  r(2);
  V(0,3) = -(r(0)*lightPos(0) + r(1)*lightPos(1) + r(2)*lightPos(2));
  V(1,0) =  u(0); V(1,1) =  u(1); V(1,2) =  u(2);
  V(1,3) = -(u(0)*lightPos(0) + u(1)*lightPos(1) + u(2)*lightPos(2));
  V(2,0) =  f(0); V(2,1) =  f(1); V(2,2) =  f(2);
  V(2,3) = -(f(0)*lightPos(0) + f(1)*lightPos(1) + f(2)*lightPos(2));
  V(3,0) = 0.f; V(3,1) = 0.f; V(3,2) = 0.f; V(3,3) = 1.f;

  // Orthographic projection: Vulkan-convention (depth [0,1], Y not flipped for shadow map)
  // Size the frustum to enclose the scene bounding sphere
  f32 halfSize = radius * 1.1f;  // 10% margin
  f32 nearP = 0.f;
  f32 farP  = radius * 4.f;

  mat4 P = mat4::zeros();
  P(0,0) = 1.f / halfSize;
  P(1,1) = 1.f / halfSize;  // No Y flip for shadow map (we flip in UV space)
  P(2,2) = 1.f / (farP - nearP);
  P(2,3) = -nearP / (farP - nearP);
  P(3,3) = 1.f;

  return P * V;
}

/// Compute the scene AABB from all visible instances.
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
      // Transform vertex to world space
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

  // Fallback if scene is empty
  if (outMin(0) > outMax(0)) {
    outMin(0) = -1.f; outMin(1) = -1.f; outMin(2) = -1.f;
    outMax(0) =  1.f; outMax(1) =  1.f; outMax(2) =  1.f;
  }
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

  // Shadow map constants
  static constexpr uint32_t kShadowMapSize = 2048;

  // Shadow map pass objects
  std::unique_ptr<RenderPass> shadow_pass_;
  std::unique_ptr<Pipeline> shadow_pipeline_;
  std::unique_ptr<ShaderModule> shadow_vert_shader_;
  std::unique_ptr<ShaderModule> shadow_frag_shader_;

  // G-buffer pass objects
  std::unique_ptr<RenderPass> gbuffer_pass_;
  std::unique_ptr<Pipeline> gbuffer_pipeline_;
  std::unique_ptr<ShaderModule> gbuffer_vert_shader_;
  std::unique_ptr<ShaderModule> gbuffer_frag_shader_;

  // Lighting pass objects
  std::unique_ptr<ShaderModule> lighting_comp_shader_;
  std::unique_ptr<Pipeline> lighting_pipeline_;

  // SSAO pass objects
  std::unique_ptr<ShaderModule> ssao_comp_shader_;
  std::unique_ptr<Pipeline> ssao_pipeline_;
  std::unique_ptr<ShaderModule> ssao_blur_comp_shader_;
  std::unique_ptr<Pipeline> ssao_blur_pipeline_;

  // Bloom pass objects
  std::unique_ptr<ShaderModule> bloom_threshold_comp_shader_;
  std::unique_ptr<Pipeline> bloom_threshold_pipeline_;
  std::unique_ptr<ShaderModule> bloom_blur_comp_shader_;
  std::unique_ptr<Pipeline> bloom_blur_pipeline_;

  // Tone mapping pass objects
  std::unique_ptr<ShaderModule> tonemap_comp_shader_;
  std::unique_ptr<Pipeline> tonemap_pipeline_;

  // FXAA pass objects
  std::unique_ptr<ShaderModule> fxaa_comp_shader_;
  std::unique_ptr<Pipeline> fxaa_pipeline_;
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
  uint32_t maxPcSize = devProps.limits.maxPushConstantsSize;
  uint32_t neededPcSize = static_cast<uint32_t>(
      std::max({sizeof(GBufferPushConstants), sizeof(SSAOPushConstants),
                sizeof(LightingPushConstants), sizeof(BloomThresholdPushConstants),
                sizeof(BloomBlurPushConstants), sizeof(TonemapPushConstants),
                sizeof(FXAAPushConstants)}));
  if (maxPcSize < neededPcSize) {
    std::fprintf(stderr, "[VulkanDeferredRenderer] device only supports %u bytes push constants, need %u\n",
                 maxPcSize, neededPcSize);
    return false;
  }

  // -- Compile shaders --
  try {
    // Shadow pass shaders
    shadow_vert_shader_ = std::make_unique<ShaderModule>(
        ctx_->createShaderModuleFromGlsl(k_shadow_vert_glsl,
                                          vk::ShaderStageFlagBits::eVertex,
                                          "shadow_depth_vert"));
    shadow_frag_shader_ = std::make_unique<ShaderModule>(
        ctx_->createShaderModuleFromGlsl(k_shadow_frag_glsl,
                                          vk::ShaderStageFlagBits::eFragment,
                                          "shadow_depth_frag"));
    // G-buffer shaders
    gbuffer_vert_shader_ = std::make_unique<ShaderModule>(
        ctx_->createShaderModuleFromGlsl(k_gbuffer_vert_glsl,
                                          vk::ShaderStageFlagBits::eVertex,
                                          "deferred_gbuffer_vert"));
    gbuffer_frag_shader_ = std::make_unique<ShaderModule>(
        ctx_->createShaderModuleFromGlsl(k_gbuffer_frag_glsl,
                                          vk::ShaderStageFlagBits::eFragment,
                                          "deferred_gbuffer_frag"));
    // Lighting compute shader
    lighting_comp_shader_ = std::make_unique<ShaderModule>(
        ctx_->createShaderModuleFromGlsl(k_lighting_comp_glsl,
                                          vk::ShaderStageFlagBits::eCompute,
                                          "deferred_lighting_comp"));
    // SSAO compute shader
    ssao_comp_shader_ = std::make_unique<ShaderModule>(
        ctx_->createShaderModuleFromGlsl(k_ssao_comp_glsl,
                                          vk::ShaderStageFlagBits::eCompute,
                                          "ssao_comp"));
    // SSAO blur compute shader
    ssao_blur_comp_shader_ = std::make_unique<ShaderModule>(
        ctx_->createShaderModuleFromGlsl(k_ssao_blur_comp_glsl,
                                          vk::ShaderStageFlagBits::eCompute,
                                          "ssao_blur_comp"));
    // Bloom threshold compute shader
    bloom_threshold_comp_shader_ = std::make_unique<ShaderModule>(
        ctx_->createShaderModuleFromGlsl(k_bloom_threshold_comp_glsl,
                                          vk::ShaderStageFlagBits::eCompute,
                                          "bloom_threshold_comp"));
    // Bloom blur compute shader (used twice: horizontal + vertical)
    bloom_blur_comp_shader_ = std::make_unique<ShaderModule>(
        ctx_->createShaderModuleFromGlsl(k_bloom_blur_comp_glsl,
                                          vk::ShaderStageFlagBits::eCompute,
                                          "bloom_blur_comp"));
    // Tone mapping compute shader
    tonemap_comp_shader_ = std::make_unique<ShaderModule>(
        ctx_->createShaderModuleFromGlsl(k_tonemap_comp_glsl,
                                          vk::ShaderStageFlagBits::eCompute,
                                          "tonemap_comp"));
    // FXAA compute shader
    fxaa_comp_shader_ = std::make_unique<ShaderModule>(
        ctx_->createShaderModuleFromGlsl(k_fxaa_comp_glsl,
                                          vk::ShaderStageFlagBits::eCompute,
                                          "fxaa_comp"));
  } catch (const std::exception& e) {
    std::fprintf(stderr, "[VulkanDeferredRenderer] shader compilation failed: %s\n", e.what());
    return false;
  }

  // -- Build shadow map render pass (depth-only) --
  // Single depth attachment, transitions to eShaderReadOnlyOptimal for sampling.
  // Use the generic addAttachment() to control finalLayout, and let auto-build
  // create the subpass and dependencies automatically.
  try {
    shadow_pass_ = std::make_unique<RenderPass>(
        ctx_->renderpass()
            .addAttachment(vk::Format::eD32Sfloat,
                           vk::ImageLayout::eUndefined,
                           vk::ImageLayout::eShaderReadOnlyOptimal,
                           /*clear=*/true)
            .build());
  } catch (const std::exception& e) {
    std::fprintf(stderr, "[VulkanDeferredRenderer] shadow render pass creation failed: %s\n", e.what());
    return false;
  }

  // -- Build shadow map pipeline (depth-only, with depth bias) --
  try {
    auto bindings   = VkModel::get_binding_descriptions(VkModel::draw_category_e::tri);
    auto attributes = VkModel::get_attribute_descriptions(VkModel::draw_category_e::tri);

    vk::PushConstantRange shadowPcRange{};
    shadowPcRange.stageFlags = vk::ShaderStageFlagBits::eVertex;
    shadowPcRange.offset = 0;
    shadowPcRange.size = static_cast<uint32_t>(sizeof(ShadowPushConstants));

    shadow_pipeline_ = std::make_unique<Pipeline>(
        ctx_->pipeline()
            .setShader(vk::ShaderStageFlagBits::eVertex, **shadow_vert_shader_, "main")
            .setShader(vk::ShaderStageFlagBits::eFragment, **shadow_frag_shader_, "main")
            .setRenderPass(*shadow_pass_, /*subpass=*/0)
            .setBindingDescriptions(bindings)
            .setAttributeDescriptions(attributes)
            .setTopology(vk::PrimitiveTopology::eTriangleList)
            .setCullMode(vk::CullModeFlagBits::eNone)  // No culling — works for enclosed scenes
            .setFrontFace(vk::FrontFace::eCounterClockwise)
            .setDepthTestEnable(true)
            .setDepthWriteEnable(true)
            .setDepthCompareOp(vk::CompareOp::eLessOrEqual)
            .enableDepthBias(1.25f, 1.75f)  // Constant + slope-scaled bias
            .setPushConstantRange(shadowPcRange)
            .build());
  } catch (const std::exception& e) {
    std::fprintf(stderr, "[VulkanDeferredRenderer] shadow pipeline creation failed: %s\n", e.what());
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
            .setCullMode(vk::CullModeFlagBits::eNone)  // No culling — needed for enclosed scenes (e.g. Cornell Box walls)
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

  // -- Build SSAO compute pipeline --
  try {
    ssao_pipeline_ = std::make_unique<Pipeline>(
        *ssao_comp_shader_,
        static_cast<u32>(sizeof(SSAOPushConstants)));
  } catch (const std::exception& e) {
    std::fprintf(stderr, "[VulkanDeferredRenderer] SSAO pipeline creation failed: %s\n", e.what());
    return false;
  }

  // -- Build SSAO blur compute pipeline --
  try {
    ssao_blur_pipeline_ = std::make_unique<Pipeline>(
        *ssao_blur_comp_shader_,
        static_cast<u32>(sizeof(SSAOBlurPushConstants)));
  } catch (const std::exception& e) {
    std::fprintf(stderr, "[VulkanDeferredRenderer] SSAO blur pipeline creation failed: %s\n", e.what());
    return false;
  }

  // -- Build bloom threshold compute pipeline --
  try {
    bloom_threshold_pipeline_ = std::make_unique<Pipeline>(
        *bloom_threshold_comp_shader_,
        static_cast<u32>(sizeof(BloomThresholdPushConstants)));
  } catch (const std::exception& e) {
    std::fprintf(stderr, "[VulkanDeferredRenderer] bloom threshold pipeline creation failed: %s\n", e.what());
    return false;
  }

  // -- Build bloom blur compute pipeline --
  try {
    bloom_blur_pipeline_ = std::make_unique<Pipeline>(
        *bloom_blur_comp_shader_,
        static_cast<u32>(sizeof(BloomBlurPushConstants)));
  } catch (const std::exception& e) {
    std::fprintf(stderr, "[VulkanDeferredRenderer] bloom blur pipeline creation failed: %s\n", e.what());
    return false;
  }

  // -- Build tone mapping compute pipeline --
  try {
    tonemap_pipeline_ = std::make_unique<Pipeline>(
        *tonemap_comp_shader_,
        static_cast<u32>(sizeof(TonemapPushConstants)));
  } catch (const std::exception& e) {
    std::fprintf(stderr, "[VulkanDeferredRenderer] tonemap pipeline creation failed: %s\n", e.what());
    return false;
  }

  // -- Build FXAA compute pipeline --
  try {
    fxaa_pipeline_ = std::make_unique<Pipeline>(
        *fxaa_comp_shader_,
        static_cast<u32>(sizeof(FXAAPushConstants)));
  } catch (const std::exception& e) {
    std::fprintf(stderr, "[VulkanDeferredRenderer] FXAA pipeline creation failed: %s\n", e.what());
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

  // HDR lighting output (R16G16B16A16Sfloat — storage for lighting write + sampled for bloom/tonemap read)
  auto hdrImage = ctx_->create2DImage(
      extent, vk::Format::eR16G16B16A16Sfloat,
      vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled,
      vk::MemoryPropertyFlagBits::eDeviceLocal,
      /*mipmaps=*/false, /*createView=*/true, /*enableTransfer=*/false);

  // SSAO raw output (R8Unorm — single channel AO factor)
  auto ssaoRawImage = ctx_->create2DImage(
      extent, vk::Format::eR8Unorm,
      vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled,
      vk::MemoryPropertyFlagBits::eDeviceLocal,
      /*mipmaps=*/false, /*createView=*/true, /*enableTransfer=*/false);

  // SSAO blurred output (R8Unorm — single channel AO factor, blurred)
  auto ssaoBlurImage = ctx_->create2DImage(
      extent, vk::Format::eR8Unorm,
      vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled,
      vk::MemoryPropertyFlagBits::eDeviceLocal,
      /*mipmaps=*/false, /*createView=*/true, /*enableTransfer=*/false);

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
    bool    cast_shadow;
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
    item.cast_shadow = inst.cast_shadow;
    item.model = VkModel(*ctx_, *meshData);
    draws.push_back(std::move(item));
  }

  if (draws.empty()) {
    result.error = "no drawable instances in scene";
    return result;
  }

  // =================================================================
  // Shadow map: compute light VP and find shadow-casting light
  // =================================================================

  vec3 sceneMin, sceneMax;
  computeSceneAABB(scene, sceneMin, sceneMax);

  // Find the first shadow-casting light (prefer directional/area)
  const Light* shadowLight = nullptr;
  for (const auto& light : scene.lights()) {
    if (light.cast_shadow) {
      shadowLight = &light;
      break;
    }
  }

  // Synthesize a default directional light for shadow if no shadow-casting light
  Light defaultShadowLight;
  if (!shadowLight) {
    defaultShadowLight.type = LightType::Directional;
    defaultShadowLight.direction = {-0.5774f, -0.5774f, -0.5774f};
    defaultShadowLight.color = {1.f, 1.f, 1.f};
    defaultShadowLight.intensity = 1.f;
    defaultShadowLight.cast_shadow = true;
    shadowLight = &defaultShadowLight;
  }

  mat4 lightVP = buildLightVP(*shadowLight, sceneMin, sceneMax);

  // =================================================================
  // Pass 0: Shadow map (depth-only from light POV)
  // =================================================================

  const vk::Extent2D shadowExtent{kShadowMapSize, kShadowMapSize};

  // Create shadow depth image (depth attachment + sampled for lighting pass)
  auto shadowDepthImage = ctx_->create2DImage(
      shadowExtent, vk::Format::eD32Sfloat,
      vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled,
      vk::MemoryPropertyFlagBits::eDeviceLocal,
      /*mipmaps=*/false, /*createView=*/true, /*enableTransfer=*/false);

  // Create framebuffer for shadow pass (depth-only)
  std::vector<vk::ImageView> shadowFbViews{
      static_cast<vk::ImageView>(shadowDepthImage)};
  auto shadowFramebuffer = ctx_->createFramebuffer(
      shadowFbViews, shadowExtent, **shadow_pass_);

  // Render shadow map
  {
    SingleUseCommandBuffer cmd(*ctx_, vk_queue_e::graphics);
    vk::CommandBuffer cb = *cmd;

    vk::ClearValue clearDepth;
    clearDepth.depthStencil = vk::ClearDepthStencilValue{1.f, 0};

    vk::RenderPassBeginInfo rpBegin;
    rpBegin.renderPass = **shadow_pass_;
    rpBegin.framebuffer = *shadowFramebuffer;
    rpBegin.renderArea = vk::Rect2D{{0, 0}, shadowExtent};
    rpBegin.clearValueCount = 1;
    rpBegin.pClearValues = &clearDepth;

    cb.beginRenderPass(rpBegin, vk::SubpassContents::eInline, ctx_->dispatcher);

    cb.bindPipeline(vk::PipelineBindPoint::eGraphics,
                    **shadow_pipeline_, ctx_->dispatcher);

    vk::Viewport vp{0.f, 0.f,
                     static_cast<float>(kShadowMapSize),
                     static_cast<float>(kShadowMapSize),
                     0.f, 1.f};
    cb.setViewport(0, 1, &vp, ctx_->dispatcher);
    vk::Rect2D scissor{{0, 0}, shadowExtent};
    cb.setScissor(0, 1, &scissor, ctx_->dispatcher);

    for (auto& item : draws) {
      // Skip instances that shouldn't cast shadows (room enclosure, lights).
      if (!item.cast_shadow) continue;
      const Material* mat = scene.findMaterial(item.material_id);
      if (mat && mat->surface_type == SurfaceType::Emissive) continue;

      ShadowPushConstants spc{};

      // Compute light MVP for this instance
      mat4 lightMVP = lightVP * item.model_mat;

      // Transpose to column-major for GLSL
      for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
          spc.light_mvp[j * 4 + i] = lightMVP(i, j);

      cb.pushConstants(
          static_cast<vk::PipelineLayout>(*shadow_pipeline_),
          vk::ShaderStageFlagBits::eVertex,
          0, static_cast<uint32_t>(sizeof(ShadowPushConstants)),
          &spc, ctx_->dispatcher);

      item.model.bind(cb);
      item.model.draw(cb);
    }

    cb.endRenderPass(ctx_->dispatcher);
    // Render pass auto-transitions depth to eShaderReadOnlyOptimal via finalLayout.
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
  // Pass 2: SSAO (compute) — generate raw AO texture
  // =================================================================

  // Transition SSAO raw image to eGeneral for compute writes
  {
    SingleUseCommandBuffer cmd(*ctx_, vk_queue_e::graphics);
    vk::CommandBuffer cb = *cmd;

    vk::ImageMemoryBarrier barrier;
    barrier.oldLayout = vk::ImageLayout::eUndefined;
    barrier.newLayout = vk::ImageLayout::eGeneral;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = static_cast<vk::Image>(ssaoRawImage);
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

  // Create nearest-neighbor sampler for SSAO G-buffer reads
  vk::SamplerCreateInfo ssaoSamplerInfo{};
  ssaoSamplerInfo.magFilter = vk::Filter::eNearest;
  ssaoSamplerInfo.minFilter = vk::Filter::eNearest;
  ssaoSamplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
  ssaoSamplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
  ssaoSamplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
  auto ssaoSampler = ctx_->createSampler(ssaoSamplerInfo);

  // SSAO descriptor set
  {
    auto& ssaoDsLayout = ssao_comp_shader_->layout(0);
    vk::DescriptorSet ssaoDs;
    ctx_->acquireSet(ssaoDsLayout, ssaoDs);

    // Binding 0: gPosition (combined image sampler)
    vk::DescriptorImageInfo ssaoPosInfo;
    ssaoPosInfo.sampler = *ssaoSampler;
    ssaoPosInfo.imageView = gPosImage.view();
    ssaoPosInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    ctx_->writeDescriptorSet(ssaoPosInfo, ssaoDs, vk::DescriptorType::eCombinedImageSampler, 0);

    // Binding 1: gNormal (combined image sampler)
    vk::DescriptorImageInfo ssaoNormInfo;
    ssaoNormInfo.sampler = *ssaoSampler;
    ssaoNormInfo.imageView = gNormImage.view();
    ssaoNormInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    ctx_->writeDescriptorSet(ssaoNormInfo, ssaoDs, vk::DescriptorType::eCombinedImageSampler, 1);

    // Binding 2: AO output (storage image)
    vk::DescriptorImageInfo ssaoOutInfo;
    ssaoOutInfo.imageView = ssaoRawImage.view();
    ssaoOutInfo.imageLayout = vk::ImageLayout::eGeneral;
    ctx_->writeDescriptorSet(ssaoOutInfo, ssaoDs, vk::DescriptorType::eStorageImage, 2);

    // Dispatch SSAO compute
    SingleUseCommandBuffer cmd(*ctx_, vk_queue_e::graphics);
    vk::CommandBuffer cb = *cmd;

    cb.bindPipeline(vk::PipelineBindPoint::eCompute, **ssao_pipeline_, ctx_->dispatcher);
    cb.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                          static_cast<vk::PipelineLayout>(*ssao_pipeline_),
                          0, 1, &ssaoDs, 0, nullptr, ctx_->dispatcher);

    SSAOPushConstants ssaoPc{};
    // View matrix (transpose to column-major for GLSL)
    for (int i = 0; i < 4; ++i)
      for (int j = 0; j < 4; ++j)
        ssaoPc.view[j * 4 + i] = V(i, j);
    // Projection matrix (transpose to column-major for GLSL)
    for (int i = 0; i < 4; ++i)
      for (int j = 0; j < 4; ++j)
        ssaoPc.projection[j * 4 + i] = P(i, j);
    ssaoPc.width = width;
    ssaoPc.height = height;
    ssaoPc.radius = 0.5f;      // world-space hemisphere radius
    ssaoPc.bias = 0.025f;       // depth comparison bias
    ssaoPc.intensity = 1.5f;    // AO intensity multiplier
    ssaoPc._pad0 = 0.f;

    cb.pushConstants(
        static_cast<vk::PipelineLayout>(*ssao_pipeline_),
        vk::ShaderStageFlagBits::eCompute,
        0, static_cast<uint32_t>(sizeof(SSAOPushConstants)),
        &ssaoPc, ctx_->dispatcher);

    uint32_t groups_x = (width + 15) / 16;
    uint32_t groups_y = (height + 15) / 16;
    cb.dispatch(groups_x, groups_y, 1, ctx_->dispatcher);

    // Barrier: compute write -> shader read for blur pass
    vk::ImageMemoryBarrier aoBarrier;
    aoBarrier.oldLayout = vk::ImageLayout::eGeneral;
    aoBarrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    aoBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    aoBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    aoBarrier.image = static_cast<vk::Image>(ssaoRawImage);
    aoBarrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    aoBarrier.subresourceRange.baseMipLevel = 0;
    aoBarrier.subresourceRange.levelCount = 1;
    aoBarrier.subresourceRange.baseArrayLayer = 0;
    aoBarrier.subresourceRange.layerCount = 1;
    aoBarrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
    aoBarrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

    cb.pipelineBarrier(
        vk::PipelineStageFlagBits::eComputeShader,
        vk::PipelineStageFlagBits::eComputeShader,
        {}, 0, nullptr, 0, nullptr, 1, &aoBarrier,
        ctx_->dispatcher);
  }

  // =================================================================
  // Pass 3: SSAO Blur (compute) — bilateral blur the raw AO
  // =================================================================

  // Transition SSAO blur image to eGeneral for compute writes
  {
    SingleUseCommandBuffer cmd(*ctx_, vk_queue_e::graphics);
    vk::CommandBuffer cb = *cmd;

    vk::ImageMemoryBarrier barrier;
    barrier.oldLayout = vk::ImageLayout::eUndefined;
    barrier.newLayout = vk::ImageLayout::eGeneral;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = static_cast<vk::Image>(ssaoBlurImage);
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

  // SSAO blur descriptor set
  {
    auto& blurDsLayout = ssao_blur_comp_shader_->layout(0);
    vk::DescriptorSet blurDs;
    ctx_->acquireSet(blurDsLayout, blurDs);

    // Binding 0: raw AO (combined image sampler)
    vk::DescriptorImageInfo aoRawInfo;
    aoRawInfo.sampler = *ssaoSampler;
    aoRawInfo.imageView = ssaoRawImage.view();
    aoRawInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    ctx_->writeDescriptorSet(aoRawInfo, blurDs, vk::DescriptorType::eCombinedImageSampler, 0);

    // Binding 1: gPosition for edge-aware filtering (combined image sampler)
    vk::DescriptorImageInfo blurPosInfo;
    blurPosInfo.sampler = *ssaoSampler;
    blurPosInfo.imageView = gPosImage.view();
    blurPosInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    ctx_->writeDescriptorSet(blurPosInfo, blurDs, vk::DescriptorType::eCombinedImageSampler, 1);

    // Binding 2: blurred AO output (storage image)
    vk::DescriptorImageInfo blurOutInfo;
    blurOutInfo.imageView = ssaoBlurImage.view();
    blurOutInfo.imageLayout = vk::ImageLayout::eGeneral;
    ctx_->writeDescriptorSet(blurOutInfo, blurDs, vk::DescriptorType::eStorageImage, 2);

    // Dispatch blur compute
    SingleUseCommandBuffer cmd(*ctx_, vk_queue_e::graphics);
    vk::CommandBuffer cb = *cmd;

    cb.bindPipeline(vk::PipelineBindPoint::eCompute, **ssao_blur_pipeline_, ctx_->dispatcher);
    cb.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                          static_cast<vk::PipelineLayout>(*ssao_blur_pipeline_),
                          0, 1, &blurDs, 0, nullptr, ctx_->dispatcher);

    SSAOBlurPushConstants blurPc{};
    blurPc.width = width;
    blurPc.height = height;
    blurPc._pad0 = 0.f;
    blurPc._pad1 = 0.f;

    cb.pushConstants(
        static_cast<vk::PipelineLayout>(*ssao_blur_pipeline_),
        vk::ShaderStageFlagBits::eCompute,
        0, static_cast<uint32_t>(sizeof(SSAOBlurPushConstants)),
        &blurPc, ctx_->dispatcher);

    uint32_t groups_x = (width + 15) / 16;
    uint32_t groups_y = (height + 15) / 16;
    cb.dispatch(groups_x, groups_y, 1, ctx_->dispatcher);

    // Barrier: blur write -> shader read for lighting pass
    vk::ImageMemoryBarrier blurBarrier;
    blurBarrier.oldLayout = vk::ImageLayout::eGeneral;
    blurBarrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    blurBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    blurBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    blurBarrier.image = static_cast<vk::Image>(ssaoBlurImage);
    blurBarrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    blurBarrier.subresourceRange.baseMipLevel = 0;
    blurBarrier.subresourceRange.levelCount = 1;
    blurBarrier.subresourceRange.baseArrayLayer = 0;
    blurBarrier.subresourceRange.layerCount = 1;
    blurBarrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
    blurBarrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

    cb.pipelineBarrier(
        vk::PipelineStageFlagBits::eComputeShader,
        vk::PipelineStageFlagBits::eComputeShader,
        {}, 0, nullptr, 0, nullptr, 1, &blurBarrier,
        ctx_->dispatcher);
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
  // Pass 2: Lighting (compute) with shadow mapping
  // =================================================================

  // Create a nearest-neighbor sampler for G-buffer reads
  vk::SamplerCreateInfo samplerInfo{};
  samplerInfo.magFilter = vk::Filter::eNearest;
  samplerInfo.minFilter = vk::Filter::eNearest;
  samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
  samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
  samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
  auto sampler = ctx_->createSampler(samplerInfo);

  // Shadow map sampler (nearest, clamp-to-border with max depth)
  vk::SamplerCreateInfo shadowSamplerInfo{};
  shadowSamplerInfo.magFilter = vk::Filter::eNearest;
  shadowSamplerInfo.minFilter = vk::Filter::eNearest;
  shadowSamplerInfo.addressModeU = vk::SamplerAddressMode::eClampToBorder;
  shadowSamplerInfo.addressModeV = vk::SamplerAddressMode::eClampToBorder;
  shadowSamplerInfo.addressModeW = vk::SamplerAddressMode::eClampToBorder;
  shadowSamplerInfo.borderColor = vk::BorderColor::eFloatOpaqueWhite;  // depth=1.0 = no shadow
  auto shadowSampler = ctx_->createSampler(shadowSamplerInfo);

  // Transition HDR image to eGeneral for compute writes
  {
    SingleUseCommandBuffer cmd(*ctx_, vk_queue_e::graphics);
    vk::CommandBuffer cb = *cmd;

    vk::ImageMemoryBarrier barrier;
    barrier.oldLayout = vk::ImageLayout::eUndefined;
    barrier.newLayout = vk::ImageLayout::eGeneral;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = static_cast<vk::Image>(hdrImage);
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
  outImageInfo.imageView = hdrImage.view();
  outImageInfo.imageLayout = vk::ImageLayout::eGeneral;

  // Light buffer
  auto lightBufInfo = lightBuf.descriptorInfo();

  ctx_->writeDescriptorSet(gPosInfo, ds, vk::DescriptorType::eCombinedImageSampler, 0);
  ctx_->writeDescriptorSet(gNormInfo, ds, vk::DescriptorType::eCombinedImageSampler, 1);
  ctx_->writeDescriptorSet(gAlbedoInfo, ds, vk::DescriptorType::eCombinedImageSampler, 2);
  ctx_->writeDescriptorSet(outImageInfo, ds, vk::DescriptorType::eStorageImage, 3);
  ctx_->writeDescriptorSet(lightBufInfo, ds, vk::DescriptorType::eStorageBuffer, 4);

  // Shadow map (combined image sampler at binding 5)
  vk::DescriptorImageInfo shadowMapInfo;
  shadowMapInfo.sampler = *shadowSampler;
  shadowMapInfo.imageView = shadowDepthImage.view();
  shadowMapInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
  ctx_->writeDescriptorSet(shadowMapInfo, ds, vk::DescriptorType::eCombinedImageSampler, 5);

  // SSAO blurred AO texture (combined image sampler at binding 6)
  vk::DescriptorImageInfo aoMapInfo;
  aoMapInfo.sampler = *sampler;  // use same nearest-neighbor sampler as G-buffer
  aoMapInfo.imageView = ssaoBlurImage.view();
  aoMapInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
  ctx_->writeDescriptorSet(aoMapInfo, ds, vk::DescriptorType::eCombinedImageSampler, 6);

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
    lpc.shadow_intensity = 1.0f;  // Full shadow strength

    // Light VP matrix (transpose row-major to column-major for GLSL)
    for (int i = 0; i < 4; ++i)
      for (int j = 0; j < 4; ++j)
        lpc.light_vp[j * 4 + i] = lightVP(i, j);

    cb.pushConstants(
        static_cast<vk::PipelineLayout>(*lighting_pipeline_),
        vk::ShaderStageFlagBits::eCompute,
        0, static_cast<uint32_t>(sizeof(LightingPushConstants)),
        &lpc, ctx_->dispatcher);

    uint32_t groups_x = (width + 15) / 16;
    uint32_t groups_y = (height + 15) / 16;
    cb.dispatch(groups_x, groups_y, 1, ctx_->dispatcher);

    // Barrier: lighting compute write -> shader read for bloom/tonemap
    vk::ImageMemoryBarrier barrier;
    barrier.oldLayout = vk::ImageLayout::eGeneral;
    barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = static_cast<vk::Image>(hdrImage);
    barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
    barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

    cb.pipelineBarrier(
        vk::PipelineStageFlagBits::eComputeShader,
        vk::PipelineStageFlagBits::eComputeShader,
        {}, 0, nullptr, 0, nullptr, 1, &barrier,
        ctx_->dispatcher);
  }

  // =================================================================
  // Pass 5: Bloom (compute) — threshold + separable Gaussian blur
  // =================================================================

  // Bloom images at half resolution
  const uint32_t bloomWidth  = std::max(width / 2, 1u);
  const uint32_t bloomHeight = std::max(height / 2, 1u);
  const vk::Extent2D bloomExtent{bloomWidth, bloomHeight};

  // Bloom image A (threshold output / V-blur output)
  auto bloomImageA = ctx_->create2DImage(
      bloomExtent, vk::Format::eR16G16B16A16Sfloat,
      vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled,
      vk::MemoryPropertyFlagBits::eDeviceLocal,
      /*mipmaps=*/false, /*createView=*/true, /*enableTransfer=*/false);

  // Bloom image B (H-blur output — ping-pong target)
  auto bloomImageB = ctx_->create2DImage(
      bloomExtent, vk::Format::eR16G16B16A16Sfloat,
      vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled,
      vk::MemoryPropertyFlagBits::eDeviceLocal,
      /*mipmaps=*/false, /*createView=*/true, /*enableTransfer=*/false);

  // Transition bloom images to eGeneral
  {
    SingleUseCommandBuffer cmd(*ctx_, vk_queue_e::graphics);
    vk::CommandBuffer cb = *cmd;

    std::array<vk::ImageMemoryBarrier, 2> barriers;
    for (int i = 0; i < 2; ++i) {
      barriers[i].oldLayout = vk::ImageLayout::eUndefined;
      barriers[i].newLayout = vk::ImageLayout::eGeneral;
      barriers[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      barriers[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      barriers[i].image = (i == 0)
          ? static_cast<vk::Image>(bloomImageA)
          : static_cast<vk::Image>(bloomImageB);
      barriers[i].subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
      barriers[i].subresourceRange.baseMipLevel = 0;
      barriers[i].subresourceRange.levelCount = 1;
      barriers[i].subresourceRange.baseArrayLayer = 0;
      barriers[i].subresourceRange.layerCount = 1;
      barriers[i].srcAccessMask = {};
      barriers[i].dstAccessMask = vk::AccessFlagBits::eShaderWrite;
    }

    cb.pipelineBarrier(
        vk::PipelineStageFlagBits::eTopOfPipe,
        vk::PipelineStageFlagBits::eComputeShader,
        {}, 0, nullptr, 0, nullptr,
        static_cast<uint32_t>(barriers.size()), barriers.data(),
        ctx_->dispatcher);
  }

  // Linear sampler for bloom (bilinear upsampling from half-res)
  vk::SamplerCreateInfo linearSamplerInfo{};
  linearSamplerInfo.magFilter = vk::Filter::eLinear;
  linearSamplerInfo.minFilter = vk::Filter::eLinear;
  linearSamplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
  linearSamplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
  linearSamplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
  auto linearSampler = ctx_->createSampler(linearSamplerInfo);

  // Pass 5a: Bloom threshold — extract bright pixels + downsample to half-res
  {
    auto& btDsLayout = bloom_threshold_comp_shader_->layout(0);
    vk::DescriptorSet btDs;
    ctx_->acquireSet(btDsLayout, btDs);

    // Binding 0: HDR input (combined image sampler)
    vk::DescriptorImageInfo hdrSampledInfo;
    hdrSampledInfo.sampler = *linearSampler;
    hdrSampledInfo.imageView = hdrImage.view();
    hdrSampledInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    ctx_->writeDescriptorSet(hdrSampledInfo, btDs, vk::DescriptorType::eCombinedImageSampler, 0);

    // Binding 1: bloom output (storage image)
    vk::DescriptorImageInfo bloomOutInfo;
    bloomOutInfo.imageView = bloomImageA.view();
    bloomOutInfo.imageLayout = vk::ImageLayout::eGeneral;
    ctx_->writeDescriptorSet(bloomOutInfo, btDs, vk::DescriptorType::eStorageImage, 1);

    SingleUseCommandBuffer cmd(*ctx_, vk_queue_e::graphics);
    vk::CommandBuffer cb = *cmd;

    cb.bindPipeline(vk::PipelineBindPoint::eCompute, **bloom_threshold_pipeline_, ctx_->dispatcher);
    cb.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                          static_cast<vk::PipelineLayout>(*bloom_threshold_pipeline_),
                          0, 1, &btDs, 0, nullptr, ctx_->dispatcher);

    BloomThresholdPushConstants btPc{};
    btPc.srcWidth  = width;
    btPc.srcHeight = height;
    btPc.dstWidth  = bloomWidth;
    btPc.dstHeight = bloomHeight;
    btPc.threshold = 1.0f;
    btPc.softKnee  = 0.5f;
    btPc._pad0 = 0.f;
    btPc._pad1 = 0.f;

    cb.pushConstants(
        static_cast<vk::PipelineLayout>(*bloom_threshold_pipeline_),
        vk::ShaderStageFlagBits::eCompute,
        0, static_cast<uint32_t>(sizeof(BloomThresholdPushConstants)),
        &btPc, ctx_->dispatcher);

    uint32_t bg_x = (bloomWidth + 15) / 16;
    uint32_t bg_y = (bloomHeight + 15) / 16;
    cb.dispatch(bg_x, bg_y, 1, ctx_->dispatcher);

    // Barrier: bloomImageA write -> read for H-blur
    vk::ImageMemoryBarrier bBarrier;
    bBarrier.oldLayout = vk::ImageLayout::eGeneral;
    bBarrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    bBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bBarrier.image = static_cast<vk::Image>(bloomImageA);
    bBarrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    bBarrier.subresourceRange.baseMipLevel = 0;
    bBarrier.subresourceRange.levelCount = 1;
    bBarrier.subresourceRange.baseArrayLayer = 0;
    bBarrier.subresourceRange.layerCount = 1;
    bBarrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
    bBarrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

    cb.pipelineBarrier(
        vk::PipelineStageFlagBits::eComputeShader,
        vk::PipelineStageFlagBits::eComputeShader,
        {}, 0, nullptr, 0, nullptr, 1, &bBarrier,
        ctx_->dispatcher);
  }

  // Pass 5b: Bloom horizontal blur (A -> B)
  {
    auto& blurDsLayout = bloom_blur_comp_shader_->layout(0);
    vk::DescriptorSet hblurDs;
    ctx_->acquireSet(blurDsLayout, hblurDs);

    // Binding 0: bloomImageA (read)
    vk::DescriptorImageInfo bloomAInfo;
    bloomAInfo.sampler = *linearSampler;
    bloomAInfo.imageView = bloomImageA.view();
    bloomAInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    ctx_->writeDescriptorSet(bloomAInfo, hblurDs, vk::DescriptorType::eCombinedImageSampler, 0);

    // Binding 1: bloomImageB (write)
    vk::DescriptorImageInfo bloomBInfo;
    bloomBInfo.imageView = bloomImageB.view();
    bloomBInfo.imageLayout = vk::ImageLayout::eGeneral;
    ctx_->writeDescriptorSet(bloomBInfo, hblurDs, vk::DescriptorType::eStorageImage, 1);

    SingleUseCommandBuffer cmd(*ctx_, vk_queue_e::graphics);
    vk::CommandBuffer cb = *cmd;

    cb.bindPipeline(vk::PipelineBindPoint::eCompute, **bloom_blur_pipeline_, ctx_->dispatcher);
    cb.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                          static_cast<vk::PipelineLayout>(*bloom_blur_pipeline_),
                          0, 1, &hblurDs, 0, nullptr, ctx_->dispatcher);

    BloomBlurPushConstants hblurPc{};
    hblurPc.width  = bloomWidth;
    hblurPc.height = bloomHeight;
    hblurPc.dirX   = 1.0f;  // horizontal
    hblurPc.dirY   = 0.0f;

    cb.pushConstants(
        static_cast<vk::PipelineLayout>(*bloom_blur_pipeline_),
        vk::ShaderStageFlagBits::eCompute,
        0, static_cast<uint32_t>(sizeof(BloomBlurPushConstants)),
        &hblurPc, ctx_->dispatcher);

    uint32_t bg_x = (bloomWidth + 15) / 16;
    uint32_t bg_y = (bloomHeight + 15) / 16;
    cb.dispatch(bg_x, bg_y, 1, ctx_->dispatcher);

    // Barrier: bloomImageB write -> read for V-blur
    vk::ImageMemoryBarrier bBarrier;
    bBarrier.oldLayout = vk::ImageLayout::eGeneral;
    bBarrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    bBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bBarrier.image = static_cast<vk::Image>(bloomImageB);
    bBarrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    bBarrier.subresourceRange.baseMipLevel = 0;
    bBarrier.subresourceRange.levelCount = 1;
    bBarrier.subresourceRange.baseArrayLayer = 0;
    bBarrier.subresourceRange.layerCount = 1;
    bBarrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
    bBarrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

    cb.pipelineBarrier(
        vk::PipelineStageFlagBits::eComputeShader,
        vk::PipelineStageFlagBits::eComputeShader,
        {}, 0, nullptr, 0, nullptr, 1, &bBarrier,
        ctx_->dispatcher);
  }

  // Pass 5c: Bloom vertical blur (B -> A, ping-pong back)
  {
    // Transition bloomImageA back to eGeneral for writing
    {
      SingleUseCommandBuffer cmd(*ctx_, vk_queue_e::graphics);
      vk::CommandBuffer cb = *cmd;

      vk::ImageMemoryBarrier barrier;
      barrier.oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
      barrier.newLayout = vk::ImageLayout::eGeneral;
      barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      barrier.image = static_cast<vk::Image>(bloomImageA);
      barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
      barrier.subresourceRange.baseMipLevel = 0;
      barrier.subresourceRange.levelCount = 1;
      barrier.subresourceRange.baseArrayLayer = 0;
      barrier.subresourceRange.layerCount = 1;
      barrier.srcAccessMask = vk::AccessFlagBits::eShaderRead;
      barrier.dstAccessMask = vk::AccessFlagBits::eShaderWrite;

      cb.pipelineBarrier(
          vk::PipelineStageFlagBits::eComputeShader,
          vk::PipelineStageFlagBits::eComputeShader,
          {}, 0, nullptr, 0, nullptr, 1, &barrier,
          ctx_->dispatcher);
    }

    auto& blurDsLayout = bloom_blur_comp_shader_->layout(0);
    vk::DescriptorSet vblurDs;
    ctx_->acquireSet(blurDsLayout, vblurDs);

    // Binding 0: bloomImageB (read)
    vk::DescriptorImageInfo bloomBReadInfo;
    bloomBReadInfo.sampler = *linearSampler;
    bloomBReadInfo.imageView = bloomImageB.view();
    bloomBReadInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    ctx_->writeDescriptorSet(bloomBReadInfo, vblurDs, vk::DescriptorType::eCombinedImageSampler, 0);

    // Binding 1: bloomImageA (write)
    vk::DescriptorImageInfo bloomAWriteInfo;
    bloomAWriteInfo.imageView = bloomImageA.view();
    bloomAWriteInfo.imageLayout = vk::ImageLayout::eGeneral;
    ctx_->writeDescriptorSet(bloomAWriteInfo, vblurDs, vk::DescriptorType::eStorageImage, 1);

    SingleUseCommandBuffer cmd(*ctx_, vk_queue_e::graphics);
    vk::CommandBuffer cb = *cmd;

    cb.bindPipeline(vk::PipelineBindPoint::eCompute, **bloom_blur_pipeline_, ctx_->dispatcher);
    cb.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                          static_cast<vk::PipelineLayout>(*bloom_blur_pipeline_),
                          0, 1, &vblurDs, 0, nullptr, ctx_->dispatcher);

    BloomBlurPushConstants vblurPc{};
    vblurPc.width  = bloomWidth;
    vblurPc.height = bloomHeight;
    vblurPc.dirX   = 0.0f;
    vblurPc.dirY   = 1.0f;  // vertical

    cb.pushConstants(
        static_cast<vk::PipelineLayout>(*bloom_blur_pipeline_),
        vk::ShaderStageFlagBits::eCompute,
        0, static_cast<uint32_t>(sizeof(BloomBlurPushConstants)),
        &vblurPc, ctx_->dispatcher);

    uint32_t bg_x = (bloomWidth + 15) / 16;
    uint32_t bg_y = (bloomHeight + 15) / 16;
    cb.dispatch(bg_x, bg_y, 1, ctx_->dispatcher);

    // Barrier: bloomImageA write -> read for tonemap
    vk::ImageMemoryBarrier bBarrier;
    bBarrier.oldLayout = vk::ImageLayout::eGeneral;
    bBarrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    bBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bBarrier.image = static_cast<vk::Image>(bloomImageA);
    bBarrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    bBarrier.subresourceRange.baseMipLevel = 0;
    bBarrier.subresourceRange.levelCount = 1;
    bBarrier.subresourceRange.baseArrayLayer = 0;
    bBarrier.subresourceRange.layerCount = 1;
    bBarrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
    bBarrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

    cb.pipelineBarrier(
        vk::PipelineStageFlagBits::eComputeShader,
        vk::PipelineStageFlagBits::eComputeShader,
        {}, 0, nullptr, 0, nullptr, 1, &bBarrier,
        ctx_->dispatcher);
  }

  // =================================================================
  // Pass 6: Tone mapping (compute) — ACES filmic + bloom composite
  // =================================================================

  // LDR tone-mapped image (storage + sampled for FXAA to read)
  auto ldrImage = ctx_->create2DImage(
      extent, vk::Format::eR8G8B8A8Unorm,
      vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled,
      vk::MemoryPropertyFlagBits::eDeviceLocal,
      /*mipmaps=*/false, /*createView=*/true, /*enableTransfer=*/false);

  // Transition LDR image to eGeneral
  {
    SingleUseCommandBuffer cmd(*ctx_, vk_queue_e::graphics);
    vk::CommandBuffer cb = *cmd;

    vk::ImageMemoryBarrier barrier;
    barrier.oldLayout = vk::ImageLayout::eUndefined;
    barrier.newLayout = vk::ImageLayout::eGeneral;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = static_cast<vk::Image>(ldrImage);
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

  // Tonemap descriptor set
  {
    auto& tmDsLayout = tonemap_comp_shader_->layout(0);
    vk::DescriptorSet tmDs;
    ctx_->acquireSet(tmDsLayout, tmDs);

    // Binding 0: HDR input (combined image sampler)
    vk::DescriptorImageInfo tmHdrInfo;
    tmHdrInfo.sampler = *linearSampler;
    tmHdrInfo.imageView = hdrImage.view();
    tmHdrInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    ctx_->writeDescriptorSet(tmHdrInfo, tmDs, vk::DescriptorType::eCombinedImageSampler, 0);

    // Binding 1: bloom input (combined image sampler, half-res with bilinear)
    vk::DescriptorImageInfo tmBloomInfo;
    tmBloomInfo.sampler = *linearSampler;
    tmBloomInfo.imageView = bloomImageA.view();
    tmBloomInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    ctx_->writeDescriptorSet(tmBloomInfo, tmDs, vk::DescriptorType::eCombinedImageSampler, 1);

    // Binding 2: LDR output (storage image)
    vk::DescriptorImageInfo tmOutInfo;
    tmOutInfo.imageView = ldrImage.view();
    tmOutInfo.imageLayout = vk::ImageLayout::eGeneral;
    ctx_->writeDescriptorSet(tmOutInfo, tmDs, vk::DescriptorType::eStorageImage, 2);

    SingleUseCommandBuffer cmd(*ctx_, vk_queue_e::graphics);
    vk::CommandBuffer cb = *cmd;

    cb.bindPipeline(vk::PipelineBindPoint::eCompute, **tonemap_pipeline_, ctx_->dispatcher);
    cb.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                          static_cast<vk::PipelineLayout>(*tonemap_pipeline_),
                          0, 1, &tmDs, 0, nullptr, ctx_->dispatcher);

    TonemapPushConstants tmPc{};
    tmPc.width = width;
    tmPc.height = height;
    tmPc.exposure = 1.0f;
    tmPc.bloomStrength = 0.04f;

    cb.pushConstants(
        static_cast<vk::PipelineLayout>(*tonemap_pipeline_),
        vk::ShaderStageFlagBits::eCompute,
        0, static_cast<uint32_t>(sizeof(TonemapPushConstants)),
        &tmPc, ctx_->dispatcher);

    uint32_t tg_x = (width + 15) / 16;
    uint32_t tg_y = (height + 15) / 16;
    cb.dispatch(tg_x, tg_y, 1, ctx_->dispatcher);

    // Barrier: LDR write -> read for FXAA
    vk::ImageMemoryBarrier ldrBarrier;
    ldrBarrier.oldLayout = vk::ImageLayout::eGeneral;
    ldrBarrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    ldrBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ldrBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ldrBarrier.image = static_cast<vk::Image>(ldrImage);
    ldrBarrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    ldrBarrier.subresourceRange.baseMipLevel = 0;
    ldrBarrier.subresourceRange.levelCount = 1;
    ldrBarrier.subresourceRange.baseArrayLayer = 0;
    ldrBarrier.subresourceRange.layerCount = 1;
    ldrBarrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
    ldrBarrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

    cb.pipelineBarrier(
        vk::PipelineStageFlagBits::eComputeShader,
        vk::PipelineStageFlagBits::eComputeShader,
        {}, 0, nullptr, 0, nullptr, 1, &ldrBarrier,
        ctx_->dispatcher);
  }

  // =================================================================
  // Pass 7: FXAA (compute) — luminance-based anti-aliasing
  // =================================================================

  // Final output image (FXAA result — storage + transfer src for readback)
  auto fxaaImage = ctx_->create2DImage(
      extent, vk::Format::eR8G8B8A8Unorm,
      vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc,
      vk::MemoryPropertyFlagBits::eDeviceLocal,
      /*mipmaps=*/false, /*createView=*/true, /*enableTransfer=*/true);

  // Transition FXAA image to eGeneral
  {
    SingleUseCommandBuffer cmd(*ctx_, vk_queue_e::graphics);
    vk::CommandBuffer cb = *cmd;

    vk::ImageMemoryBarrier barrier;
    barrier.oldLayout = vk::ImageLayout::eUndefined;
    barrier.newLayout = vk::ImageLayout::eGeneral;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = static_cast<vk::Image>(fxaaImage);
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

  // FXAA descriptor set
  {
    auto& fxaaDsLayout = fxaa_comp_shader_->layout(0);
    vk::DescriptorSet fxaaDs;
    ctx_->acquireSet(fxaaDsLayout, fxaaDs);

    // Binding 0: LDR input (combined image sampler)
    vk::DescriptorImageInfo fxaaInInfo;
    fxaaInInfo.sampler = *linearSampler;
    fxaaInInfo.imageView = ldrImage.view();
    fxaaInInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    ctx_->writeDescriptorSet(fxaaInInfo, fxaaDs, vk::DescriptorType::eCombinedImageSampler, 0);

    // Binding 1: FXAA output (storage image)
    vk::DescriptorImageInfo fxaaOutInfo;
    fxaaOutInfo.imageView = fxaaImage.view();
    fxaaOutInfo.imageLayout = vk::ImageLayout::eGeneral;
    ctx_->writeDescriptorSet(fxaaOutInfo, fxaaDs, vk::DescriptorType::eStorageImage, 1);

    SingleUseCommandBuffer cmd(*ctx_, vk_queue_e::graphics);
    vk::CommandBuffer cb = *cmd;

    cb.bindPipeline(vk::PipelineBindPoint::eCompute, **fxaa_pipeline_, ctx_->dispatcher);
    cb.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                          static_cast<vk::PipelineLayout>(*fxaa_pipeline_),
                          0, 1, &fxaaDs, 0, nullptr, ctx_->dispatcher);

    FXAAPushConstants fxaaPc{};
    fxaaPc.width = width;
    fxaaPc.height = height;
    fxaaPc.edgeThresholdMin = 0.0312f;
    fxaaPc.edgeThreshold = 0.125f;

    cb.pushConstants(
        static_cast<vk::PipelineLayout>(*fxaa_pipeline_),
        vk::ShaderStageFlagBits::eCompute,
        0, static_cast<uint32_t>(sizeof(FXAAPushConstants)),
        &fxaaPc, ctx_->dispatcher);

    uint32_t fg_x = (width + 15) / 16;
    uint32_t fg_y = (height + 15) / 16;
    cb.dispatch(fg_x, fg_y, 1, ctx_->dispatcher);

    // Barrier: FXAA write -> transfer read for readback
    vk::ImageMemoryBarrier fxaaBarrier;
    fxaaBarrier.oldLayout = vk::ImageLayout::eGeneral;
    fxaaBarrier.newLayout = vk::ImageLayout::eTransferSrcOptimal;
    fxaaBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    fxaaBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    fxaaBarrier.image = static_cast<vk::Image>(fxaaImage);
    fxaaBarrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    fxaaBarrier.subresourceRange.baseMipLevel = 0;
    fxaaBarrier.subresourceRange.levelCount = 1;
    fxaaBarrier.subresourceRange.baseArrayLayer = 0;
    fxaaBarrier.subresourceRange.layerCount = 1;
    fxaaBarrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
    fxaaBarrier.dstAccessMask = vk::AccessFlagBits::eTransferRead;

    cb.pipelineBarrier(
        vk::PipelineStageFlagBits::eComputeShader,
        vk::PipelineStageFlagBits::eTransfer,
        {}, 0, nullptr, 0, nullptr, 1, &fxaaBarrier,
        ctx_->dispatcher);
  }

  // =================================================================
  // Readback pixels to CPU (from FXAA output)
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
        static_cast<vk::Image>(fxaaImage),
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

  fxaa_pipeline_.reset();
  fxaa_comp_shader_.reset();
  tonemap_pipeline_.reset();
  tonemap_comp_shader_.reset();
  bloom_blur_pipeline_.reset();
  bloom_blur_comp_shader_.reset();
  bloom_threshold_pipeline_.reset();
  bloom_threshold_comp_shader_.reset();
  lighting_pipeline_.reset();
  lighting_comp_shader_.reset();
  ssao_blur_pipeline_.reset();
  ssao_blur_comp_shader_.reset();
  ssao_pipeline_.reset();
  ssao_comp_shader_.reset();
  gbuffer_pipeline_.reset();
  gbuffer_vert_shader_.reset();
  gbuffer_frag_shader_.reset();
  gbuffer_pass_.reset();
  shadow_pipeline_.reset();
  shadow_vert_shader_.reset();
  shadow_frag_shader_.reset();
  shadow_pass_.reset();

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
