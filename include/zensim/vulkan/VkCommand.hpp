#pragma once
#include "zensim/vulkan/VkContext.hpp"

namespace zs {

  struct ZPC_CORE_API VkCommand {
    using PoolFamily = ExecutionContext::PoolFamily;
    VkCommand(PoolFamily& poolFamily, vk::CommandBuffer cmd, vk_cmd_usage_e usage);
    VkCommand(VkCommand&& o) noexcept;
    ~VkCommand();

    void begin(const vk::CommandBufferBeginInfo& bi) { _cmd.begin(bi); }
    void begin() { _cmd.begin(vk::CommandBufferBeginInfo{usageFlag(), nullptr}); }
    void end() { _cmd.end(); }
    void waitStage(vk::PipelineStageFlags stageFlag) { _stages = {stageFlag}; }
    void wait(vk::Semaphore s) { _waitSemaphores.push_back(s); }
    void signal(vk::Semaphore s) { _signalSemaphores.push_back(s); }
    void submit(vk::Fence fence, bool resetFence = true, bool resetConfig = false);

    vk::CommandBufferUsageFlags usageFlag() const {
      vk::CommandBufferUsageFlags usageFlags{};
      if (_usage == vk_cmd_usage_e::single_use || _usage == vk_cmd_usage_e::reset)
        usageFlags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
      else
        usageFlags = vk::CommandBufferUsageFlagBits::eSimultaneousUse;
      return usageFlags;
    }

    const VulkanContext& ctx() const { return *_poolFamily.pctx; }
    VulkanContext& ctx() { return *_poolFamily.pctx; }
    vk::CommandPool getPool() const noexcept { return _poolFamily.cmdpool(_usage); }
    vk::Queue getQueue() const noexcept { return _poolFamily.queue; }

    vk::CommandBuffer operator*() const noexcept { return _cmd; }
    operator vk::CommandBuffer() const noexcept { return _cmd; }
    operator vk::CommandBuffer*() noexcept { return &_cmd; }
    operator const vk::CommandBuffer*() const noexcept { return &_cmd; }

#if 0
    void swap(VkCommand& r) noexcept {
      zs_swap(_cmd, r._cmd);
      zs_swap(_usage, r._usage);
      zs_swap(_stage, r._stage);
      zs_swap(_waitSemaphores, r._waitSemaphores);
      zs_swap(_signalSemaphores, r._signalSemaphores);
    }
#endif

  protected:
    friend struct VulkanContext;

    PoolFamily& _poolFamily;
    vk::CommandBuffer _cmd;
    vk_cmd_usage_e _usage;

    std::vector<vk::PipelineStageFlags> _stages;
    std::vector<vk::Semaphore> _waitSemaphores, _signalSemaphores;
  };

  struct ZPC_CORE_API Fence {
    Fence(VulkanContext& ctx, bool signaled = false) : _ctx{ctx} {
      vk::FenceCreateInfo ci{};
      if (signaled) ci.setFlags(vk::FenceCreateFlagBits::eSignaled);
      _fence = ctx.device.createFence(ci, nullptr, ctx.dispatcher);
    }

    Fence(Fence&& o) noexcept : _ctx{o._ctx}, _fence{o._fence} { o._fence = VK_NULL_HANDLE; }

    ~Fence() {
      _ctx.device.destroyFence(_fence, nullptr, _ctx.dispatcher);
      _fence = VK_NULL_HANDLE;
    }

    void wait() const;
    void reset() { _ctx.device.resetFences({_fence}); }

    vk::Fence operator*() const noexcept { return _fence; }
    operator vk::Fence() const noexcept { return _fence; }

  protected:
    friend struct VulkanContext;

    VulkanContext& _ctx;
    vk::Fence _fence;
  };

  /// @brief RAII wrapper for device-only vk::Event
  /// @note Used for fine-grained GPU-GPU synchronization within a single queue
  /// @note Device-only events cannot be signaled/reset from host but may be faster
  /// @note Created with VK_EVENT_CREATE_DEVICE_ONLY_BIT flag
  /// @example
  /// @code
  /// DeviceEvent event(ctx);
  /// // In command buffer:
  /// (*cmd).setEvent(event, vk::PipelineStageFlagBits::eComputeShader);
  /// // ... other commands ...
  /// (*cmd).waitEvents({event}, srcStage, dstStage, {}, {}, {});
  /// @endcode
  struct ZPC_CORE_API DeviceEvent {
    DeviceEvent() = delete;

    /// @brief Create a device-only event
    /// @param ctx VulkanContext to allocate from
    explicit DeviceEvent(VulkanContext& ctx)
        : ctx{ctx}, event{VK_NULL_HANDLE} {
      vk::EventCreateInfo createInfo{};
      createInfo.setFlags(vk::EventCreateFlagBits::eDeviceOnly);
      event = ctx.device.createEvent(createInfo, nullptr, ctx.dispatcher);
    }

    DeviceEvent(const DeviceEvent&) = delete;
    DeviceEvent& operator=(const DeviceEvent&) = delete;

    DeviceEvent(DeviceEvent&& o) noexcept : ctx{o.ctx}, event{o.event} {
      o.event = VK_NULL_HANDLE;
    }

    DeviceEvent& operator=(DeviceEvent&& o) {
      if (this != &o) {
        if (&ctx != &o.ctx)
          throw std::runtime_error("unable to move-assign device event due to ctx mismatch");
        reset();
        event = o.event;
        o.event = VK_NULL_HANDLE;
      }
      return *this;
    }

    ~DeviceEvent() { reset(); }

    /// @brief Release the event resource
    void reset() {
      if (event != VK_NULL_HANDLE) {
        ctx.device.destroyEvent(event, nullptr, ctx.dispatcher);
        event = VK_NULL_HANDLE;
      }
    }

    /// @brief Check if the event is valid
    bool isValid() const noexcept { return event != VK_NULL_HANDLE; }
    explicit operator bool() const noexcept { return isValid(); }

    /// @brief Access the underlying vk::Event handle
    vk::Event operator*() const { return event; }
    operator vk::Event() const { return event; }
    vk::Event get() const noexcept { return event; }

    VulkanContext& getContext() noexcept { return ctx; }
    const VulkanContext& getContext() const noexcept { return ctx; }

  protected:
    friend struct VulkanContext;

    VulkanContext& ctx;
    vk::Event event;
  };

  /// @brief RAII wrapper for host-manageable vk::Event
  /// @note Used for fine-grained GPU-GPU synchronization within a single queue
  /// @note Can be signaled/reset from both host and device
  /// @note Slightly slower than DeviceEvent but more flexible
  /// @example
  /// @code
  /// HostEvent event(ctx);
  /// // Signal from host:
  /// event.set();
  /// // Or in command buffer:
  /// (*cmd).setEvent(event, vk::PipelineStageFlagBits::eComputeShader);
  /// // Check status from host:
  /// if (event.isSignaled()) { ... }
  /// // Reset from host:
  /// event.resetEvent();
  /// @endcode
  struct ZPC_CORE_API HostEvent {
    HostEvent() = delete;

    /// @brief Create a host-manageable event
    /// @param ctx VulkanContext to allocate from
    explicit HostEvent(VulkanContext& ctx)
        : ctx{ctx}, event{VK_NULL_HANDLE} {
      vk::EventCreateInfo createInfo{};
      // No flags = host-manageable event
      event = ctx.device.createEvent(createInfo, nullptr, ctx.dispatcher);
    }

    HostEvent(const HostEvent&) = delete;
    HostEvent& operator=(const HostEvent&) = delete;

    HostEvent(HostEvent&& o) noexcept : ctx{o.ctx}, event{o.event} {
      o.event = VK_NULL_HANDLE;
    }

    HostEvent& operator=(HostEvent&& o) {
      if (this != &o) {
        if (&ctx != &o.ctx)
          throw std::runtime_error("unable to move-assign host event due to ctx mismatch");
        reset();
        event = o.event;
        o.event = VK_NULL_HANDLE;
      }
      return *this;
    }

    ~HostEvent() { reset(); }

    /// @brief Release the event resource
    void reset() {
      if (event != VK_NULL_HANDLE) {
        ctx.device.destroyEvent(event, nullptr, ctx.dispatcher);
        event = VK_NULL_HANDLE;
      }
    }

    /// @brief Check if the event is valid
    bool isValid() const noexcept { return event != VK_NULL_HANDLE; }
    explicit operator bool() const noexcept { return isValid(); }

    /// @brief Get the current status of the event from the host
    /// @return true if event is signaled, false if reset
    /// @throws std::runtime_error if event is invalid or query fails
    bool isSignaled() const {
      if (!isValid())
        throw std::runtime_error("cannot query status of invalid event");
      auto result = ctx.device.getEventStatus(event, ctx.dispatcher);
      if (result == vk::Result::eEventSet)
        return true;
      if (result == vk::Result::eEventReset)
        return false;
      throw std::runtime_error("failed to query event status");
    }

    /// @brief Signal the event from the host
    /// @throws std::runtime_error if event is invalid
    void set() {
      if (!isValid())
        throw std::runtime_error("cannot set invalid event");
      ctx.device.setEvent(event, ctx.dispatcher);
    }

    /// @brief Reset the event from the host
    /// @throws std::runtime_error if event is invalid
    void resetEvent() {
      if (!isValid())
        throw std::runtime_error("cannot reset invalid event");
      ctx.device.resetEvent(event, ctx.dispatcher);
    }

    /// @brief Access the underlying vk::Event handle
    vk::Event operator*() const { return event; }
    operator vk::Event() const { return event; }
    vk::Event get() const noexcept { return event; }

    VulkanContext& getContext() noexcept { return ctx; }
    const VulkanContext& getContext() const noexcept { return ctx; }

  protected:
    friend struct VulkanContext;

    VulkanContext& ctx;
    vk::Event event;
  };

  /// @brief RAII wrapper for vk::Semaphore (binary semaphore)
  /// @note Used for GPU-GPU synchronization between queue operations
  struct ZPC_CORE_API BinarySemaphore {
    BinarySemaphore() = delete;
    BinarySemaphore(VulkanContext& ctx, vk::Semaphore semaphore = VK_NULL_HANDLE)
        : ctx{ctx}, semaphore{semaphore} {}

    BinarySemaphore(const BinarySemaphore&) = delete;
    BinarySemaphore& operator=(const BinarySemaphore&) = delete;

    BinarySemaphore(BinarySemaphore&& o) noexcept : ctx{o.ctx}, semaphore{o.semaphore} {
      o.semaphore = VK_NULL_HANDLE;
    }

    BinarySemaphore& operator=(BinarySemaphore&& o) {
      if (this != &o) {
        if (&ctx != &o.ctx)
          throw std::runtime_error("unable to move-assign vk semaphore due to ctx mismatch");
        reset();
        semaphore = o.semaphore;
        o.semaphore = VK_NULL_HANDLE;
      }
      return *this;
    }

    ~BinarySemaphore() { reset(); }

    /// @brief Release the semaphore resource
    void reset() {
      if (semaphore != VK_NULL_HANDLE) {
        ctx.device.destroySemaphore(semaphore, nullptr, ctx.dispatcher);
        semaphore = VK_NULL_HANDLE;
      }
    }

    /// @brief Check if the semaphore is valid
    bool isValid() const noexcept { return semaphore != VK_NULL_HANDLE; }
    explicit operator bool() const noexcept { return isValid(); }

    /// @brief Access the underlying vk::Semaphore handle
    vk::Semaphore operator*() const { return semaphore; }
    operator vk::Semaphore() const { return semaphore; }
    vk::Semaphore get() const noexcept { return semaphore; }

    VulkanContext& getContext() noexcept { return ctx; }
    const VulkanContext& getContext() const noexcept { return ctx; }

  protected:
    friend struct VulkanContext;

    VulkanContext& ctx;
    vk::Semaphore semaphore;
  };

  /// @brief RAII wrapper for vk::Semaphore with timeline extension
  /// @note Requires VK_KHR_timeline_semaphore or Vulkan 1.2+
  /// @note Used for more flexible GPU-GPU and CPU-GPU synchronization
  struct ZPC_CORE_API TimelineSemaphore {
    TimelineSemaphore() = delete;
    TimelineSemaphore(VulkanContext& ctx, vk::Semaphore semaphore = VK_NULL_HANDLE,
                      u64 initialValue = 0)
        : ctx{ctx}, semaphore{semaphore}, currentValue{initialValue} {}

    TimelineSemaphore(const TimelineSemaphore&) = delete;
    TimelineSemaphore& operator=(const TimelineSemaphore&) = delete;

    TimelineSemaphore(TimelineSemaphore&& o) noexcept
        : ctx{o.ctx}, semaphore{o.semaphore}, currentValue{o.currentValue} {
      o.semaphore = VK_NULL_HANDLE;
      o.currentValue = 0;
    }

    TimelineSemaphore& operator=(TimelineSemaphore&& o) {
      if (this != &o) {
        if (&ctx != &o.ctx)
          throw std::runtime_error(
              "unable to move-assign vk timeline semaphore due to ctx mismatch");
        reset();
        semaphore = o.semaphore;
        currentValue = o.currentValue;
        o.semaphore = VK_NULL_HANDLE;
        o.currentValue = 0;
      }
      return *this;
    }

    ~TimelineSemaphore() { reset(); }

    /// @brief Release the semaphore resource
    void reset() {
      if (semaphore != VK_NULL_HANDLE) {
        ctx.device.destroySemaphore(semaphore, nullptr, ctx.dispatcher);
        semaphore = VK_NULL_HANDLE;
        currentValue = 0;
      }
    }

    /// @brief Check if the semaphore is valid
    bool isValid() const noexcept { return semaphore != VK_NULL_HANDLE; }
    explicit operator bool() const noexcept { return isValid(); }

    /// @brief Access the underlying vk::Semaphore handle
    vk::Semaphore operator*() const { return semaphore; }
    operator vk::Semaphore() const { return semaphore; }
    vk::Semaphore get() const noexcept { return semaphore; }

    /// @brief Get the current counter value from the GPU
    /// @throws std::runtime_error if semaphore is invalid
    u64 getCounterValue() const {
      if (!isValid())
        throw std::runtime_error("cannot get counter value from invalid timeline semaphore");
      return ctx.device.getSemaphoreCounterValue(semaphore, ctx.dispatcher);
    }

    /// @brief Wait on the host until the semaphore reaches the specified value
    /// @param value The value to wait for
    /// @param timeout Timeout in nanoseconds (default: max u64)
    /// @return true if wait succeeded, false on timeout
    /// @throws std::runtime_error if semaphore is invalid
    bool wait(u64 value, u64 timeout = std::numeric_limits<u64>::max()) const {
      if (!isValid())
        throw std::runtime_error("cannot wait on invalid timeline semaphore");
      vk::SemaphoreWaitInfo waitInfo{};
      waitInfo.setSemaphoreCount(1).setPSemaphores(&semaphore).setPValues(&value);
      auto result = ctx.device.waitSemaphores(waitInfo, timeout, ctx.dispatcher);
      return result == vk::Result::eSuccess;
    }

    /// @brief Signal the semaphore from the host with the specified value
    /// @param value The value to signal (must be greater than current)
    /// @throws std::runtime_error if semaphore is invalid
    void signal(u64 value) {
      if (!isValid())
        throw std::runtime_error("cannot signal invalid timeline semaphore");
      vk::SemaphoreSignalInfo signalInfo{};
      signalInfo.setSemaphore(semaphore).setValue(value);
      ctx.device.signalSemaphore(signalInfo, ctx.dispatcher);
      currentValue = value;
    }

    /// @brief Increment and signal with next value, returns the signaled value
    /// @throws std::runtime_error if semaphore is invalid
    u64 signalNext() {
      signal(++currentValue);
      return currentValue;
    }

    /// @brief Get the tracked current value (may differ from GPU counter if externally signaled)
    u64 getTrackedValue() const noexcept { return currentValue; }

    /// @brief Get submit info for waiting on this semaphore
    /// @throws std::runtime_error if semaphore is invalid
    vk::SemaphoreSubmitInfo waitSubmitInfo(
        u64 value, vk::PipelineStageFlags2 stageMask = vk::PipelineStageFlagBits2::eAllCommands)
        const {
      if (!isValid())
        throw std::runtime_error("cannot create wait submit info from invalid timeline semaphore");
      return vk::SemaphoreSubmitInfo{semaphore, value, stageMask, 0};
    }

    /// @brief Get submit info for signaling this semaphore
    /// @throws std::runtime_error if semaphore is invalid
    vk::SemaphoreSubmitInfo signalSubmitInfo(
        u64 value, vk::PipelineStageFlags2 stageMask = vk::PipelineStageFlagBits2::eAllCommands)
        const {
      if (!isValid())
        throw std::runtime_error(
            "cannot create signal submit info from invalid timeline semaphore");
      return vk::SemaphoreSubmitInfo{semaphore, value, stageMask, 0};
    }

    VulkanContext& getContext() noexcept { return ctx; }
    const VulkanContext& getContext() const noexcept { return ctx; }

  protected:
    friend struct VulkanContext;

    VulkanContext& ctx;
    vk::Semaphore semaphore;
    u64 currentValue;  // Tracked value for convenience
  };

  /// @brief RAII wrapper for single-use command buffer with automatic submission
  /// @note Allocates command buffer on construction, submits and waits on destruction
  /// @note Ideal for one-time operations like buffer/image transfers, transitions, etc.
  /// @example
  /// @code
  /// {
  ///   SingleUseCommandBuffer cmd(ctx);
  ///   (*cmd).copyBuffer(src, dst, region);
  /// } // automatically submits and waits here
  /// @endcode
  struct ZPC_CORE_API SingleUseCommandBuffer {
    SingleUseCommandBuffer() = delete;
    
    /// @brief Create a single-use command buffer and begin recording
    /// @param ctx VulkanContext to allocate from
    /// @param queueFamily Queue family to use (default: graphics)
    explicit SingleUseCommandBuffer(VulkanContext& ctx,
                                    vk_queue_e queueFamily = vk_queue_e::graphics)
        : ctx{ctx}, queueFamily{queueFamily}, cmd{VK_NULL_HANDLE}, submitted{false} {
      auto& pool = ctx.env().pools(queueFamily);
      cmd = pool.createCommandBuffer(vk::CommandBufferLevel::ePrimary, true, nullptr,
                                     vk_cmd_usage_e::single_use);
    }

    SingleUseCommandBuffer(const SingleUseCommandBuffer&) = delete;
    SingleUseCommandBuffer& operator=(const SingleUseCommandBuffer&) = delete;

    SingleUseCommandBuffer(SingleUseCommandBuffer&& o) noexcept
        : ctx{o.ctx}, queueFamily{o.queueFamily}, cmd{o.cmd}, submitted{o.submitted} {
      o.cmd = VK_NULL_HANDLE;
      o.submitted = true;  // Prevent double submission
    }

    SingleUseCommandBuffer& operator=(SingleUseCommandBuffer&& o) {
      if (this != &o) {
        if (&ctx != &o.ctx)
          throw std::runtime_error(
              "unable to move-assign single use command buffer due to ctx mismatch");
        submitAndWait();  // Submit existing if not already
        cmd = o.cmd;
        queueFamily = o.queueFamily;
        submitted = o.submitted;
        o.cmd = VK_NULL_HANDLE;
        o.submitted = true;
      }
      return *this;
    }

    /// @brief Destructor automatically submits and waits if not already done
    ~SingleUseCommandBuffer() { submitAndWait(); }

    /// @brief Manually end recording, submit, and wait for completion
    /// @note Called automatically in destructor, but can be called early if needed
    void submitAndWait() {
      if (submitted || cmd == VK_NULL_HANDLE) return;
      submitted = true;

      cmd.end();

      auto& pool = ctx.env().pools(queueFamily);
      vk::Fence fence = ctx.device.createFence(vk::FenceCreateInfo{}, nullptr, ctx.dispatcher);

      vk::SubmitInfo submitInfo{};
      submitInfo.setCommandBufferCount(1).setPCommandBuffers(&cmd);

      if (auto res = pool.queue.submit(1, &submitInfo, fence, ctx.dispatcher);
          res != vk::Result::eSuccess) {
        ctx.device.destroyFence(fence, nullptr, ctx.dispatcher);
        throw std::runtime_error("failed to submit single-use command buffer");
      }

      if (ctx.device.waitForFences(1, &fence, VK_TRUE, std::numeric_limits<u64>::max(),
                                   ctx.dispatcher)
          != vk::Result::eSuccess) {
        ctx.device.destroyFence(fence, nullptr, ctx.dispatcher);
        throw std::runtime_error("failed waiting for single-use command buffer fence");
      }

      ctx.device.destroyFence(fence, nullptr, ctx.dispatcher);
      ctx.device.freeCommandBuffers(pool.cmdpool(vk_cmd_usage_e::single_use), 1, &cmd,
                                    ctx.dispatcher);
      cmd = VK_NULL_HANDLE;
    }

    /// @brief Submit without waiting (returns fence for manual synchronization)
    /// @note After calling this, destructor will not submit again
    /// @note Caller is responsible for waiting on fence; command buffer is freed after fence signals
    [[nodiscard]] Fence submitAsync() {
      if (submitted || cmd == VK_NULL_HANDLE)
        throw std::runtime_error("command buffer already submitted or invalid");
      submitted = true;

      cmd.end();

      auto& pool = ctx.env().pools(queueFamily);
      Fence fence{ctx, false};

      vk::SubmitInfo submitInfo{};
      submitInfo.setCommandBufferCount(1).setPCommandBuffers(&cmd);

      if (auto res = pool.queue.submit(1, &submitInfo, fence, ctx.dispatcher);
          res != vk::Result::eSuccess) {
        throw std::runtime_error("failed to submit single-use command buffer");
      }

      // Note: Caller must wait on fence
      // Command buffer will be freed when this object is destroyed (after fence signals)
      return fence;
    }

    /// @brief Check if already submitted
    bool isSubmitted() const noexcept { return submitted; }

    /// @brief Check if command buffer is valid
    bool isValid() const noexcept { return cmd != VK_NULL_HANDLE && !submitted; }
    explicit operator bool() const noexcept { return isValid(); }

    /// @brief Access the underlying vk::CommandBuffer handle
    vk::CommandBuffer operator*() const { return cmd; }
    operator vk::CommandBuffer() const { return cmd; }
    vk::CommandBuffer get() const noexcept { return cmd; }

    VulkanContext& getContext() noexcept { return ctx; }
    const VulkanContext& getContext() const noexcept { return ctx; }

  protected:
    friend struct VulkanContext;

    VulkanContext& ctx;
    vk_queue_e queueFamily;
    vk::CommandBuffer cmd;
    bool submitted;
  };

  /// @brief RAII wrapper for static/reusable command buffer
  /// @note Command buffer can be submitted multiple times without re-recording
  /// @note Uses eSimultaneousUse flag - suitable for pre-recorded command buffers
  /// @note Command buffer is NOT automatically submitted on destruction
  /// @example
  /// @code
  /// StaticCommandBuffer cmd(ctx);
  /// (*cmd).bindPipeline(...);
  /// (*cmd).draw(...);
  /// cmd.endRecording();  // finish recording once
  /// 
  /// // Can submit multiple times
  /// cmd.submit(fence1);
  /// fence1.wait();
  /// cmd.submit(fence2);
  /// fence2.wait();
  /// @endcode
  struct ZPC_CORE_API StaticCommandBuffer {
    StaticCommandBuffer() = delete;

    /// @brief Create a static command buffer and begin recording
    /// @param ctx VulkanContext to allocate from
    /// @param queueFamily Queue family to use (default: graphics)
    /// @param beginRecording Whether to begin recording immediately (default: true)
    explicit StaticCommandBuffer(VulkanContext& ctx,
                                 vk_queue_e queueFamily = vk_queue_e::graphics,
                                 bool beginRecording = true)
        : ctx{ctx}, queueFamily{queueFamily}, cmd{VK_NULL_HANDLE}, recording{false}, ended{false} {
      auto& pool = ctx.env().pools(queueFamily);
      cmd = pool.createCommandBuffer(vk::CommandBufferLevel::ePrimary, beginRecording, nullptr,
                                     vk_cmd_usage_e::reuse);
      recording = beginRecording;
    }

    StaticCommandBuffer(const StaticCommandBuffer&) = delete;
    StaticCommandBuffer& operator=(const StaticCommandBuffer&) = delete;

    StaticCommandBuffer(StaticCommandBuffer&& o) noexcept
        : ctx{o.ctx}, queueFamily{o.queueFamily}, cmd{o.cmd}, recording{o.recording}, ended{o.ended} {
      o.cmd = VK_NULL_HANDLE;
      o.recording = false;
      o.ended = false;
    }

    StaticCommandBuffer& operator=(StaticCommandBuffer&& o) {
      if (this != &o) {
        if (&ctx != &o.ctx)
          throw std::runtime_error(
              "unable to move-assign static command buffer due to ctx mismatch");
        reset();
        cmd = o.cmd;
        queueFamily = o.queueFamily;
        recording = o.recording;
        ended = o.ended;
        o.cmd = VK_NULL_HANDLE;
        o.recording = false;
        o.ended = false;
      }
      return *this;
    }

    ~StaticCommandBuffer() { reset(); }

    /// @brief Release the command buffer resource
    void reset() {
      if (cmd != VK_NULL_HANDLE) {
        auto& pool = ctx.env().pools(queueFamily);
        ctx.device.freeCommandBuffers(pool.cmdpool(vk_cmd_usage_e::reuse), 1, &cmd, ctx.dispatcher);
        cmd = VK_NULL_HANDLE;
        recording = false;
        ended = false;
      }
    }

    /// @brief Begin recording commands
    /// @note Only call if not already recording
    void beginRecording() {
      if (recording)
        throw std::runtime_error("command buffer already recording");
      if (cmd == VK_NULL_HANDLE)
        throw std::runtime_error("command buffer is invalid");
      
      vk::CommandBufferBeginInfo beginInfo{};
      beginInfo.setFlags(vk::CommandBufferUsageFlagBits::eSimultaneousUse);
      cmd.begin(beginInfo);
      recording = true;
      ended = false;
    }

    /// @brief End recording commands
    /// @note Must be called before submit
    void endRecording() {
      if (!recording)
        throw std::runtime_error("command buffer not recording");
      cmd.end();
      recording = false;
      ended = true;
    }

    /// @brief Submit the command buffer with a fence for synchronization
    /// @param fence Fence to signal on completion
    /// @note Command buffer must have ended recording before submitting
    void submit(vk::Fence fence) {
      if (cmd == VK_NULL_HANDLE)
        throw std::runtime_error("command buffer is invalid");
      if (recording)
        throw std::runtime_error("cannot submit while still recording");
      if (!ended)
        throw std::runtime_error("command buffer recording not ended");

      auto& pool = ctx.env().pools(queueFamily);
      vk::SubmitInfo submitInfo{};
      submitInfo.setCommandBufferCount(1).setPCommandBuffers(&cmd);

      if (auto res = pool.queue.submit(1, &submitInfo, fence, ctx.dispatcher);
          res != vk::Result::eSuccess) {
        throw std::runtime_error("failed to submit static command buffer");
      }
    }

    /// @brief Submit the command buffer with a Fence RAII wrapper
    void submit(const Fence& fence) { submit(*fence); }

    /// @brief Submit and wait for completion using a temporary fence
    void submitAndWait() {
      Fence fence{ctx, false};
      submit(fence);
      fence.wait();
    }

    /// @brief Check if currently recording
    bool isRecording() const noexcept { return recording; }

    /// @brief Check if recording has ended (ready for submit)
    bool isEnded() const noexcept { return ended; }

    /// @brief Check if command buffer is valid
    bool isValid() const noexcept { return cmd != VK_NULL_HANDLE; }
    explicit operator bool() const noexcept { return isValid(); }

    /// @brief Access the underlying vk::CommandBuffer handle
    vk::CommandBuffer operator*() const { return cmd; }
    operator vk::CommandBuffer() const { return cmd; }
    vk::CommandBuffer get() const noexcept { return cmd; }

    VulkanContext& getContext() noexcept { return ctx; }
    const VulkanContext& getContext() const noexcept { return ctx; }

  protected:
    friend struct VulkanContext;

    VulkanContext& ctx;
    vk_queue_e queueFamily;
    vk::CommandBuffer cmd;
    bool recording;
    bool ended;
  };

  /// @brief RAII wrapper for resettable command buffer
  /// @note Command buffer can be reset and re-recorded after each submission
  /// @note Uses eOneTimeSubmit flag - must re-record after each submit
  /// @note Ideal for per-frame command buffers that change every frame
  /// @example
  /// @code
  /// ResetCommandBuffer cmd(ctx);
  /// 
  /// // Frame loop
  /// while (running) {
  ///   cmd.beginRecording();  // reset and begin
  ///   (*cmd).bindPipeline(...);
  ///   (*cmd).draw(...);
  ///   cmd.submitAndWait();   // submit, wait, ready for next frame
  /// }
  /// @endcode
  struct ZPC_CORE_API ResetCommandBuffer {
    ResetCommandBuffer() = delete;

    /// @brief Create a resettable command buffer
    /// @param ctx VulkanContext to allocate from
    /// @param queueFamily Queue family to use (default: graphics)
    /// @param beginRecording Whether to begin recording immediately (default: true)
    explicit ResetCommandBuffer(VulkanContext& ctx,
                                vk_queue_e queueFamily = vk_queue_e::graphics,
                                bool beginRecording = true)
        : ctx{ctx}, queueFamily{queueFamily}, cmd{VK_NULL_HANDLE}, recording{false} {
      auto& pool = ctx.env().pools(queueFamily);
      cmd = pool.createCommandBuffer(vk::CommandBufferLevel::ePrimary, beginRecording, nullptr,
                                     vk_cmd_usage_e::reset);
      recording = beginRecording;
    }

    ResetCommandBuffer(const ResetCommandBuffer&) = delete;
    ResetCommandBuffer& operator=(const ResetCommandBuffer&) = delete;

    ResetCommandBuffer(ResetCommandBuffer&& o) noexcept
        : ctx{o.ctx}, queueFamily{o.queueFamily}, cmd{o.cmd}, recording{o.recording} {
      o.cmd = VK_NULL_HANDLE;
      o.recording = false;
    }

    ResetCommandBuffer& operator=(ResetCommandBuffer&& o) {
      if (this != &o) {
        if (&ctx != &o.ctx)
          throw std::runtime_error(
              "unable to move-assign reset command buffer due to ctx mismatch");
        reset();
        cmd = o.cmd;
        queueFamily = o.queueFamily;
        recording = o.recording;
        o.cmd = VK_NULL_HANDLE;
        o.recording = false;
      }
      return *this;
    }

    ~ResetCommandBuffer() { reset(); }

    /// @brief Release the command buffer resource
    void reset() {
      if (cmd != VK_NULL_HANDLE) {
        auto& pool = ctx.env().pools(queueFamily);
        ctx.device.freeCommandBuffers(pool.cmdpool(vk_cmd_usage_e::reset), 1, &cmd, ctx.dispatcher);
        cmd = VK_NULL_HANDLE;
        recording = false;
      }
    }

    /// @brief Reset the command buffer and begin recording
    /// @note This resets the command buffer, clearing previous commands
    void beginRecording() {
      if (cmd == VK_NULL_HANDLE)
        throw std::runtime_error("command buffer is invalid");
      
      // Reset the command buffer before re-recording
      cmd.reset(vk::CommandBufferResetFlags{});
      
      vk::CommandBufferBeginInfo beginInfo{};
      beginInfo.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
      cmd.begin(beginInfo);
      recording = true;
    }

    /// @brief End recording commands
    void endRecording() {
      if (!recording)
        throw std::runtime_error("command buffer not recording");
      cmd.end();
      recording = false;
    }

    /// @brief Submit the command buffer with a fence for synchronization
    /// @param fence Fence to signal on completion
    /// @note Ends recording if still recording
    void submit(vk::Fence fence) {
      if (cmd == VK_NULL_HANDLE)
        throw std::runtime_error("command buffer is invalid");
      if (recording) {
        endRecording();
      }

      auto& pool = ctx.env().pools(queueFamily);
      vk::SubmitInfo submitInfo{};
      submitInfo.setCommandBufferCount(1).setPCommandBuffers(&cmd);

      if (auto res = pool.queue.submit(1, &submitInfo, fence, ctx.dispatcher);
          res != vk::Result::eSuccess) {
        throw std::runtime_error("failed to submit reset command buffer");
      }
    }

    /// @brief Submit the command buffer with a Fence RAII wrapper
    void submit(const Fence& fence) { submit(*fence); }

    /// @brief Submit, wait for completion, and prepare for next recording
    void submitAndWait() {
      Fence fence{ctx, false};
      submit(fence);
      fence.wait();
    }

    /// @brief Check if currently recording
    bool isRecording() const noexcept { return recording; }

    /// @brief Check if command buffer is valid
    bool isValid() const noexcept { return cmd != VK_NULL_HANDLE; }
    explicit operator bool() const noexcept { return isValid(); }

    /// @brief Access the underlying vk::CommandBuffer handle
    vk::CommandBuffer operator*() const { return cmd; }
    operator vk::CommandBuffer() const { return cmd; }
    vk::CommandBuffer get() const noexcept { return cmd; }

    VulkanContext& getContext() noexcept { return ctx; }
    const VulkanContext& getContext() const noexcept { return ctx; }

  protected:
    friend struct VulkanContext;

    VulkanContext& ctx;
    vk_queue_e queueFamily;
    vk::CommandBuffer cmd;
    bool recording;
  };

}  // namespace zs