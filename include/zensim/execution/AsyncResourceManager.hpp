#pragma once

#include <mutex>
#include <unordered_map>
#include <vector>

#include "zensim/ZpcFunction.hpp"
#include "zensim/ZpcResource.hpp"
#include "zensim/execution/AsyncRuntime.hpp"

namespace zs {

  struct AsyncResourceHandle {
    u64 id{0};

    constexpr explicit operator bool() const noexcept { return id != 0; }
    constexpr bool valid() const noexcept { return id != 0; }
    friend constexpr bool operator==(AsyncResourceHandle lhs, AsyncResourceHandle rhs) noexcept {
      return lhs.id == rhs.id;
    }
    friend constexpr bool operator!=(AsyncResourceHandle lhs, AsyncResourceHandle rhs) noexcept {
      return !(lhs == rhs);
    }
  };

  enum class AsyncResourceMaintenanceKind : u8 { refresh, compact, evict, custom };
  enum class AsyncResourceMaintenanceDisposition : u8 {
    scheduled,
    skipped_not_found,
    skipped_busy,
    skipped_leased,
    skipped_retired
  };

  struct AsyncResourceDescriptor {
    SmallString label{};
    SmallString executor{"inline"};
    AsyncEndpoint endpoint{};
    AsyncDomain domain{AsyncDomain::control};
    AsyncQueueClass queue{AsyncQueueClass::control};
    size_t bytes{0};
    u64 staleAfterEpochs{0};
    int priority{0};
    bool allowMaintenanceWhileLeased{false};
    bool evictWhenStale{false};
  };

  struct AsyncResourceMaintenanceRequest {
    AsyncResourceMaintenanceKind kind{AsyncResourceMaintenanceKind::refresh};
    SmallString label{};
    bool requireIdle{false};
    bool requireDirty{false};
    bool clearDirtyOnSuccess{true};
    bool retireOnSuccess{false};
  };

  struct AsyncResourceMaintenanceContext {
    AsyncResourceHandle resource{};
    const AsyncResourceDescriptor *descriptor{nullptr};
    void *payload{nullptr};
    AsyncResourceMaintenanceRequest request{};
    u64 epoch{0};
    size_t leaseCount{0};
  };

  using AsyncResourceMaintenanceStep
      = function<AsyncPollStatus(AsyncExecutionContext &, AsyncResourceMaintenanceContext &)>;
  using AsyncResourceDestroyFn = function<void(void *)>;

  struct AsyncResourceCallbacks {
    AsyncResourceMaintenanceStep maintain{};
    AsyncResourceDestroyFn destroy{};
  };

  struct AsyncResourceManagerStats {
    size_t total{0};
    size_t leased{0};
    size_t dirty{0};
    size_t busy{0};
    size_t retired{0};
  };

  struct AsyncResourceMaintenanceTicket {
    AsyncResourceHandle resource{};
    AsyncResourceMaintenanceDisposition disposition{
        AsyncResourceMaintenanceDisposition::skipped_not_found};
    AsyncSubmissionHandle submission{};

    bool scheduled() const noexcept {
      return disposition == AsyncResourceMaintenanceDisposition::scheduled && submission.valid();
    }
  };

  class AsyncResourceManager {
    struct Entry;

  public:
    class Lease {
    public:
      Lease() = default;
      ~Lease() { reset(); }

      Lease(const Lease &) = delete;
      Lease &operator=(const Lease &) = delete;

      Lease(Lease &&other) noexcept : _entry{zs::move(other._entry)} {}
      Lease &operator=(Lease &&other) noexcept {
        if (this != &other) {
          reset();
          _entry = zs::move(other._entry);
        }
        return *this;
      }

      AsyncResourceHandle handle() const noexcept;
      const AsyncResourceDescriptor *descriptor() const noexcept;
      void *payload() const noexcept;
      bool valid() const noexcept;

      void reset() noexcept;

    private:
      explicit Lease(Shared<Entry> entry) : _entry{zs::move(entry)} {}

      Shared<Entry> _entry{};

      friend class AsyncResourceManager;
    };

    explicit AsyncResourceManager(AsyncRuntime &runtime) : _runtime{runtime} {}
    ~AsyncResourceManager();

    AsyncResourceManager(const AsyncResourceManager &) = delete;
    AsyncResourceManager &operator=(const AsyncResourceManager &) = delete;

    AsyncResourceHandle register_resource(AsyncResourceDescriptor descriptor,
                                          AsyncResourceCallbacks callbacks = {},
                                          void *payload = nullptr);
    Lease acquire(AsyncResourceHandle resource);

    bool contains(AsyncResourceHandle resource) const;
    bool touch(AsyncResourceHandle resource);
    bool mark_dirty(AsyncResourceHandle resource, bool dirty = true);
    bool is_dirty(AsyncResourceHandle resource) const;
    u64 advance_epoch(u64 delta = 1) noexcept { return _epoch.fetch_add(delta) + delta; }
    u64 current_epoch() const noexcept { return _epoch.load(); }

    AsyncResourceMaintenanceTicket schedule_maintenance(
        AsyncResourceHandle resource, AsyncResourceMaintenanceRequest request,
        std::vector<AsyncEvent> prerequisites = {}, AsyncStopToken cancellation = {},
        bool stopOnPrerequisiteFailure = true);
    std::vector<AsyncResourceMaintenanceTicket> schedule_stale_maintenance(
        AsyncResourceMaintenanceRequest request);

    size_t collect_retired();
    AsyncResourceManagerStats stats() const;

  private:
    struct Entry {
      AsyncResourceHandle handle{};
      AsyncResourceDescriptor descriptor{};
      AsyncResourceCallbacks callbacks{};
      void *payload{nullptr};
      mutable std::mutex payloadMutex{};
      Atomic<u64> leaseCount{0};
      Atomic<u64> lastAccessEpoch{0};
      atomic_bool dirty{false};
      atomic_bool retired{false};
      atomic_bool maintenanceInFlight{false};

      void *payload_snapshot() const noexcept {
        std::lock_guard<std::mutex> lock(payloadMutex);
        return payload;
      }

      void destroy_payload_if_needed() noexcept {
        std::lock_guard<std::mutex> lock(payloadMutex);
        if (!payload || !callbacks.destroy) return;
        void *current = payload;
        payload = nullptr;
        callbacks.destroy(current);
      }

      void release_lease() noexcept {
        const auto remaining = leaseCount.fetch_sub(1) - 1;
        if (remaining == 0 && retired.load() && !maintenanceInFlight.load())
          destroy_payload_if_needed();
      }
    };

    Shared<Entry> find_entry_(AsyncResourceHandle resource) const;
    static bool is_stale_(const Entry &entry, u64 epoch) noexcept;

    AsyncRuntime &_runtime;
    mutable std::mutex _mutex{};
    std::unordered_map<u64, Shared<Entry>> _entries{};
    Atomic<u64> _nextId{0};
    Atomic<u64> _epoch{0};
  };

  inline AsyncResourceHandle AsyncResourceManager::Lease::handle() const noexcept {
    return _entry ? _entry->handle : AsyncResourceHandle{};
  }

  inline const AsyncResourceDescriptor *AsyncResourceManager::Lease::descriptor() const noexcept {
    return _entry ? &_entry->descriptor : nullptr;
  }

  inline void *AsyncResourceManager::Lease::payload() const noexcept {
    return _entry ? _entry->payload_snapshot() : nullptr;
  }

  inline bool AsyncResourceManager::Lease::valid() const noexcept {
    return _entry && !_entry->retired.load();
  }

  inline void AsyncResourceManager::Lease::reset() noexcept {
    if (!_entry) return;
    auto entry = zs::move(_entry);
    entry->release_lease();
  }

  inline AsyncResourceManager::~AsyncResourceManager() {
    std::vector<Shared<Entry>> entries;
    {
      std::lock_guard<std::mutex> lock(_mutex);
      entries.reserve(_entries.size());
      for (auto &kv : _entries) entries.push_back(kv.second);
      _entries.clear();
    }

    for (auto &entry : entries)
      if (entry) entry->destroy_payload_if_needed();
  }

  inline AsyncResourceHandle AsyncResourceManager::register_resource(AsyncResourceDescriptor descriptor,
                                                                     AsyncResourceCallbacks callbacks,
                                                                     void *payload) {
    auto entry = zs::make_shared<Entry>();
    entry->handle = AsyncResourceHandle{_nextId.fetch_add(1) + 1};
    entry->descriptor = zs::move(descriptor);
    entry->callbacks = zs::move(callbacks);
    entry->payload = payload;
    entry->lastAccessEpoch.store(_epoch.load());

    std::lock_guard<std::mutex> lock(_mutex);
    _entries.insert_or_assign(entry->handle.id, entry);
    return entry->handle;
  }

  inline Shared<AsyncResourceManager::Entry> AsyncResourceManager::find_entry_(
      AsyncResourceHandle resource) const {
    std::lock_guard<std::mutex> lock(_mutex);
    if (auto it = _entries.find(resource.id); it != _entries.end()) return it->second;
    return {};
  }

  inline AsyncResourceManager::Lease AsyncResourceManager::acquire(AsyncResourceHandle resource) {
    auto entry = find_entry_(resource);
    if (!entry || entry->retired.load()) return {};
    entry->leaseCount.fetch_add(1);
    entry->lastAccessEpoch.store(_epoch.load());
    return Lease{entry};
  }

  inline bool AsyncResourceManager::contains(AsyncResourceHandle resource) const {
    return static_cast<bool>(find_entry_(resource));
  }

  inline bool AsyncResourceManager::touch(AsyncResourceHandle resource) {
    auto entry = find_entry_(resource);
    if (!entry || entry->retired.load()) return false;
    entry->lastAccessEpoch.store(_epoch.load());
    return true;
  }

  inline bool AsyncResourceManager::mark_dirty(AsyncResourceHandle resource, bool dirty) {
    auto entry = find_entry_(resource);
    if (!entry || entry->retired.load()) return false;
    entry->dirty.store(dirty);
    if (dirty) entry->lastAccessEpoch.store(_epoch.load());
    return true;
  }

  inline bool AsyncResourceManager::is_dirty(AsyncResourceHandle resource) const {
    auto entry = find_entry_(resource);
    return entry && entry->dirty.load();
  }

  inline AsyncResourceMaintenanceTicket AsyncResourceManager::schedule_maintenance(
      AsyncResourceHandle resource, AsyncResourceMaintenanceRequest request,
      std::vector<AsyncEvent> prerequisites, AsyncStopToken cancellation,
      bool stopOnPrerequisiteFailure) {
    auto entry = find_entry_(resource);
    if (!entry)
      return {resource, AsyncResourceMaintenanceDisposition::skipped_not_found, {}};
    if (entry->retired.load())
      return {resource, AsyncResourceMaintenanceDisposition::skipped_retired, {}};

    const bool requireIdle = request.requireIdle || !entry->descriptor.allowMaintenanceWhileLeased;
    if (requireIdle && entry->leaseCount.load() != 0)
      return {resource, AsyncResourceMaintenanceDisposition::skipped_leased, {}};
    if (request.requireDirty && !entry->dirty.load())
      return {resource, AsyncResourceMaintenanceDisposition::skipped_not_found, {}};

    bool expected = false;
    if (!entry->maintenanceInFlight.compare_exchange_strong(expected, true))
      return {resource, AsyncResourceMaintenanceDisposition::skipped_busy, {}};

    const u64 scheduledEpoch = _epoch.load();
    AsyncSubmission submission{};
    submission.executor = entry->descriptor.executor;
    submission.desc.label = request.label.size() ? request.label : entry->descriptor.label;
    submission.desc.domain = entry->descriptor.domain;
    submission.desc.queue = entry->descriptor.queue;
    submission.desc.priority = entry->descriptor.priority;
    submission.endpoint = entry->descriptor.endpoint;
    submission.prerequisites = zs::move(prerequisites);
    submission.cancellation = cancellation;
    submission.stopOnPrerequisiteFailure = stopOnPrerequisiteFailure;
    submission.step = [entry, request, scheduledEpoch](AsyncExecutionContext &ctx) mutable {
      AsyncResourceMaintenanceContext maintenance{};
      maintenance.resource = entry->handle;
      maintenance.descriptor = &entry->descriptor;
      maintenance.payload = entry->payload_snapshot();
      maintenance.request = request;
      maintenance.epoch = scheduledEpoch;
      maintenance.leaseCount = entry->leaseCount.load();

      auto result = AsyncPollStatus::completed;
      if (entry->callbacks.maintain) result = entry->callbacks.maintain(ctx, maintenance);

      if (result == AsyncPollStatus::completed) {
        if (request.clearDirtyOnSuccess) entry->dirty.store(false);
        entry->lastAccessEpoch.store(scheduledEpoch);
        if (request.retireOnSuccess) entry->retired.store(true);
      }

      if (result != AsyncPollStatus::suspend) {
        entry->maintenanceInFlight.store(false);
        if (request.retireOnSuccess && result == AsyncPollStatus::completed
            && entry->leaseCount.load() == 0)
          entry->destroy_payload_if_needed();
      }

      if (ctx.cancellation.stop_requested() || ctx.cancellation.interrupt_requested())
        return AsyncPollStatus::cancelled;
      return result;
    };

    try {
      return {resource, AsyncResourceMaintenanceDisposition::scheduled,
              _runtime.submit(zs::move(submission))};
    } catch (...) {
      entry->maintenanceInFlight.store(false);
      throw;
    }
  }

  inline bool AsyncResourceManager::is_stale_(const Entry &entry, u64 epoch) noexcept {
    const auto staleAfter = entry.descriptor.staleAfterEpochs;
    if (staleAfter == 0) return false;
    const auto lastAccess = entry.lastAccessEpoch.load();
    return epoch >= lastAccess && epoch - lastAccess >= staleAfter;
  }

  inline std::vector<AsyncResourceMaintenanceTicket>
  AsyncResourceManager::schedule_stale_maintenance(AsyncResourceMaintenanceRequest request) {
    std::vector<Shared<Entry>> entries;
    {
      std::lock_guard<std::mutex> lock(_mutex);
      entries.reserve(_entries.size());
      for (auto &kv : _entries) entries.push_back(kv.second);
    }

    const u64 epoch = _epoch.load();
    std::vector<AsyncResourceMaintenanceTicket> tickets;
    tickets.reserve(entries.size());
    for (auto &entry : entries) {
      if (!entry || entry->retired.load()) continue;
      if (!is_stale_(*entry, epoch)) continue;
      if (!entry->descriptor.evictWhenStale && request.kind == AsyncResourceMaintenanceKind::evict)
        continue;
      if (request.requireDirty && !entry->dirty.load()) continue;
      tickets.push_back(schedule_maintenance(entry->handle, request));
    }
    return tickets;
  }

  inline size_t AsyncResourceManager::collect_retired() {
    std::lock_guard<std::mutex> lock(_mutex);
    size_t removed = 0;
    for (auto it = _entries.begin(); it != _entries.end();) {
      auto &entry = it->second;
      if (entry && entry->retired.load() && entry->leaseCount.load() == 0
          && !entry->maintenanceInFlight.load()) {
        entry->destroy_payload_if_needed();
        it = _entries.erase(it);
        ++removed;
      } else {
        ++it;
      }
    }
    return removed;
  }

  inline AsyncResourceManagerStats AsyncResourceManager::stats() const {
    AsyncResourceManagerStats snapshot{};
    std::lock_guard<std::mutex> lock(_mutex);
    snapshot.total = _entries.size();
    for (const auto &kv : _entries) {
      const auto &entry = kv.second;
      if (!entry) continue;
      if (entry->leaseCount.load() != 0) ++snapshot.leased;
      if (entry->dirty.load()) ++snapshot.dirty;
      if (entry->maintenanceInFlight.load()) ++snapshot.busy;
      if (entry->retired.load()) ++snapshot.retired;
    }
    return snapshot;
  }

}  // namespace zs