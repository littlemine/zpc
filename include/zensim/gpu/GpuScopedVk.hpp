// Copyright (c) zs contributors. Licensed under the MIT License.
// GpuScopedVk.hpp - Transitional RAII wrapper bridging Owner<T> → gpu::ScopedHandle<Tag>.
//
// ScopedVk<T> wraps an Owner<T> internally but lives in the gpu:: namespace,
// signaling that the member participates in the gpu:: abstraction migration.
// All existing .get() call sites (returning T&) continue working unchanged.
//
// Migration path for each member:
//   Owner<Image>          → gpu::ScopedVk<Image>          → gpu::ScopedTexture
//   Owner<Buffer>         → gpu::ScopedVk<Buffer>         → gpu::ScopedBuffer
//   Owner<Pipeline>       → gpu::ScopedVk<Pipeline>       → gpu::ScopedRenderPipeline
//   Owner<ImageSampler>   → gpu::ScopedVk<ImageSampler>   → gpu::ScopedSampler
//   Owner<RenderPass>     → gpu::ScopedVk<RenderPass>     → (internal to VkDevice)
//   Owner<Framebuffer>    → gpu::ScopedVk<Framebuffer>    → (internal to VkDevice)
//   Owner<Fence>          → gpu::ScopedVk<Fence>          → gpu::ScopedFence (TBD)

#pragma once

#include "zensim/ZpcImplPattern.hpp"  // Owner<T>

namespace zs::gpu {

  /// Transitional RAII wrapper: same semantics as Owner<T> but in gpu:: namespace.
  /// Move-only, .get() returns T&, .reset() destroys, destructor cleans up.
  /// Accepts assignment from Owner<T> and T directly for seamless adoption.
  template <typename T>
  class ScopedVk {
  public:
    using value_type = T;

    // --- Construction ---
    ScopedVk() noexcept = default;
    ScopedVk(nullowner_t) noexcept : owner_{} {}

    /// Adopt an existing Owner<T> (move).
    ScopedVk(Owner<T>&& owner) noexcept(noexcept(Owner<T>(zs::move(owner))))
        : owner_(zs::move(owner)) {}

    // --- Move-only ---
    ScopedVk(ScopedVk&& o) noexcept(noexcept(Owner<T>(zs::move(o.owner_))))
        : owner_(zs::move(o.owner_)) {}

    ScopedVk& operator=(ScopedVk&& o) noexcept(
        noexcept(zs::declval<Owner<T>&>() = zs::move(o.owner_))) {
      if (this != &o) owner_ = zs::move(o.owner_);
      return *this;
    }

    ScopedVk(const ScopedVk&) = delete;
    ScopedVk& operator=(const ScopedVk&) = delete;

    // --- Destruction ---
    ~ScopedVk() = default;  // Owner<T> dtor handles cleanup

    // --- Assignment from Owner<T> (for ctx.createXxx() return values) ---
    ScopedVk& operator=(Owner<T>&& o) noexcept(
        noexcept(zs::declval<Owner<T>&>() = zs::move(o))) {
      owner_ = zs::move(o);
      return *this;
    }

    // --- Assignment from T directly (for ctx.framebuffer() etc.) ---
    ScopedVk& operator=(T&& value) noexcept(
        noexcept(zs::declval<Owner<T>&>() = zs::move(value))) {
      owner_ = zs::move(value);
      return *this;
    }

    // --- Access (same interface as Owner<T>) ---
    T& get() noexcept { return owner_.get(); }
    const T& get() const noexcept { return owner_.get(); }

    T* operator->() noexcept { return &owner_.get(); }
    const T* operator->() const noexcept { return &owner_.get(); }

    T& operator*() & noexcept { return owner_.get(); }
    const T& operator*() const& noexcept { return owner_.get(); }

    // Implicit conversion to T& (matches Owner<T> behavior)
    operator T&() noexcept { return owner_.get(); }
    operator const T&() const noexcept { return owner_.get(); }

    // --- State ---
    explicit operator bool() const noexcept { return static_cast<bool>(owner_); }
    bool has_value() const noexcept { return owner_.has_value(); }

    // --- Reset / emplace ---
    void reset() noexcept(noexcept(owner_.reset())) { owner_.reset(); }

    template <typename... Args>
    T& emplace(Args&&... args) {
      return owner_.emplace(zs::forward<Args>(args)...);
    }

    // --- Underlying Owner access (escape hatch during migration) ---
    Owner<T>& underlying() noexcept { return owner_; }
    const Owner<T>& underlying() const noexcept { return owner_; }

  private:
    Owner<T> owner_;
  };

}  // namespace zs::gpu
