// Copyright (c) zs contributors. Licensed under the MIT License.
// GpuTestVk.hpp - Integration test for gpu::VkDevice.
//
// Usage: Call gpu::test::runVkDeviceTests(ctx) with a valid VulkanContext.
// Returns true if all tests pass. Prints results to stderr.
//
// This header is self-contained (no additional .cpp needed).

#pragma once

#include "GpuDeviceVk.hpp"

#include <cstdio>
#include <cstring>

namespace zs::gpu::test {

  struct TestResult {
    int passed = 0;
    int failed = 0;
    int total  = 0;
  };

  inline void check(TestResult& r, bool cond, const char* name) {
    r.total++;
    if (cond) {
      r.passed++;
      fprintf(stderr, "  [PASS] %s\n", name);
    } else {
      r.failed++;
      fprintf(stderr, "  [FAIL] %s\n", name);
    }
  }

  /// Run all gpu::VkDevice integration tests.
  /// Requires a fully initialized VulkanContext with graphics queue.
  inline bool runVkDeviceTests(VulkanContext& ctx) {
    TestResult r;
    fprintf(stderr, "=== gpu::VkDevice Integration Tests ===\n");

    VkDevice dev(ctx);

    // -- Info tests --
    check(r, dev.backendName() == "Vulkan", "backendName() == Vulkan");
    check(r, !dev.deviceName().empty(), "deviceName() is not empty");
    fprintf(stderr, "  Device: %.*s\n",
            (int)dev.deviceName().size(), dev.deviceName().data());

    // -- Buffer creation --
    {
      auto buf = dev.createBuffer({
          .size = 256,
          .usage = BufferUsage::Uniform | BufferUsage::CopyDst | BufferUsage::MapWrite,
          .label = "test_uniform_buffer"
      });
      check(r, static_cast<bool>(buf), "createBuffer (uniform, 256B)");

      auto* rec = dev.getBuffer(buf);
      check(r, rec != nullptr, "getBuffer returns non-null record");
      if (rec) {
        check(r, rec->size == 256, "buffer size == 256");
      }

      // Map and write
      void* mapped = dev.mapBuffer(buf, 0, 0);
      check(r, mapped != nullptr, "mapBuffer returns non-null");
      if (mapped) {
        float testData[4] = {1.0f, 2.0f, 3.0f, 4.0f};
        std::memcpy(mapped, testData, sizeof(testData));
      }
      dev.unmapBuffer(buf);

      // writeBuffer
      float writeData[4] = {5.0f, 6.0f, 7.0f, 8.0f};
      dev.writeBuffer(buf, 0, writeData, sizeof(writeData));

      dev.destroyBuffer(buf);
      check(r, dev.getBuffer(buf) == nullptr, "destroyBuffer clears slot");
    }

    // -- Texture creation --
    {
      auto tex = dev.createTexture({
          .dimension = TextureDimension::e2D,
          .format = Format::RGBA8Unorm,
          .width = 64,
          .height = 64,
          .usage = TextureUsage::Sampled | TextureUsage::ColorAttachment,
          .label = "test_texture"
      });
      check(r, static_cast<bool>(tex), "createTexture (64x64 RGBA8)");

      auto* texRec = dev.getTexture(tex);
      check(r, texRec != nullptr, "getTexture returns non-null record");
      if (texRec) {
        check(r, texRec->extent.width == 64, "texture width == 64");
        check(r, texRec->extent.height == 64, "texture height == 64");
        check(r, texRec->defaultView != VK_NULL_HANDLE,
               "texture has default view");
      }

      // Create a custom texture view
      auto view = dev.createTextureView(tex, {
          .format = Format::RGBA8Unorm,
          .dimension = TextureViewDimension::e2D,
      });
      check(r, static_cast<bool>(view), "createTextureView");

      dev.destroyTextureView(view);
      dev.destroyTexture(tex);
      check(r, dev.getTexture(tex) == nullptr, "destroyTexture clears slot");
    }

    // -- Depth texture --
    {
      auto depthTex = dev.createTexture({
          .dimension = TextureDimension::e2D,
          .format = Format::D32Float,
          .width = 128,
          .height = 128,
          .usage = TextureUsage::ColorAttachment,
          .label = "test_depth"
      });
      check(r, static_cast<bool>(depthTex), "createTexture (128x128 D32Float)");
      dev.destroyTexture(depthTex);
    }

    // -- Sampler creation --
    {
      auto sampler = dev.createSampler({
          .magFilter = FilterMode::Linear,
          .minFilter = FilterMode::Linear,
          .addressU = AddressMode::ClampToEdge,
          .addressV = AddressMode::ClampToEdge,
          .label = "test_sampler"
      });
      check(r, static_cast<bool>(sampler), "createSampler (linear, clamp)");
      dev.destroySampler(sampler);
    }

    // -- Bind group layout --
    {
      auto layout = dev.createBindGroupLayout({
          .entries = {
              {0, ShaderStage::Vertex | ShaderStage::Fragment,
               BindingType::UniformBuffer},
              {1, ShaderStage::Fragment,
               BindingType::SampledTexture},
              {2, ShaderStage::Fragment,
               BindingType::Sampler},
          },
          .label = "test_bind_group_layout"
      });
      check(r, static_cast<bool>(layout), "createBindGroupLayout (3 entries)");

      auto* layoutRec = dev.getBindGroupLayout(layout);
      check(r, layoutRec != nullptr, "getBindGroupLayout non-null");
      if (layoutRec) {
        check(r, layoutRec->entries.size() == 3, "layout has 3 entries");
        check(r, layoutRec->vkBindings.size() == 3, "layout has 3 vk bindings");
      }

      // Create a bind group with actual resources
      auto buf = dev.createBuffer({
          .size = 64,
          .usage = BufferUsage::Uniform | BufferUsage::MapWrite,
      });
      auto tex = dev.createTexture({
          .format = Format::RGBA8Unorm,
          .width = 16, .height = 16,
          .usage = TextureUsage::Sampled,
      });
      auto view = dev.createTextureView(tex, {});
      auto sampler = dev.createSampler({});

      auto bindGroup = dev.createBindGroup({
          .layout = layout,
          .buffers = {{0, buf, 0, 64}},
          .textures = {{1, view}},
          .samplers = {{2, sampler}},
          .label = "test_bind_group"
      });
      check(r, static_cast<bool>(bindGroup), "createBindGroup");

      // Cleanup
      dev.destroyBindGroup(bindGroup);
      dev.destroySampler(sampler);
      dev.destroyTextureView(view);
      dev.destroyTexture(tex);
      dev.destroyBuffer(buf);
      dev.destroyBindGroupLayout(layout);
    }

    // -- Convenience methods --
    {
      auto tex = dev.createTexture2D(32, 32);
      check(r, static_cast<bool>(tex), "createTexture2D convenience");
      dev.destroyTexture(tex);

      auto buf = dev.createMappableBuffer(128);
      check(r, static_cast<bool>(buf), "createMappableBuffer convenience");
      dev.destroyBuffer(buf);
    }

    // -- Slot reuse --
    {
      auto b1 = dev.createBuffer({.size = 16, .usage = BufferUsage::Uniform});
      auto b2 = dev.createBuffer({.size = 32, .usage = BufferUsage::Uniform});
      auto id1 = b1.id;
      dev.destroyBuffer(b1);
      auto b3 = dev.createBuffer({.size = 64, .usage = BufferUsage::Uniform});
      check(r, b3.id == id1, "slot reuse after destroy");
      dev.destroyBuffer(b2);
      dev.destroyBuffer(b3);
    }

    // -- Dynamic rendering toggle --
    {
      check(r, !dev.isDynamicRenderingEnabled(),
             "dynamic rendering disabled by default");
      dev.enableDynamicRendering(true);
      check(r, dev.isDynamicRenderingEnabled(),
             "dynamic rendering enabled after toggle");
      dev.enableDynamicRendering(false);
    }

    // -- waitIdle --
    dev.waitIdle();
    check(r, true, "waitIdle does not crash");

    // -- Summary --
    fprintf(stderr, "\n=== Results: %d/%d passed",
            r.passed, r.total);
    if (r.failed > 0) fprintf(stderr, " (%d FAILED)", r.failed);
    fprintf(stderr, " ===\n");

    return r.failed == 0;
  }

}  // namespace zs::gpu::test
