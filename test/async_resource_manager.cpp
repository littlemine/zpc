#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "zensim/execution/AsyncResourceManager.hpp"

using namespace zs;

namespace {

  struct TestPayload {
    int maintCount{0};
    int destroyCount{0};
    AsyncResourceMaintenanceKind lastKind{AsyncResourceMaintenanceKind::refresh};
  };

  void require(bool condition, const char *message) {
    if (condition) return;
    std::fprintf(stderr, "[async-resource-manager] requirement failed: %s\n", message);
    std::fflush(stderr);
    std::abort();
  }

  AsyncResourceCallbacks make_callbacks() {
    AsyncResourceCallbacks callbacks{};
    callbacks.maintain = [](AsyncExecutionContext &, AsyncResourceMaintenanceContext &context) {
      auto *payload = static_cast<TestPayload *>(context.payload);
      require(payload != nullptr, "maintenance callback missing payload");
      payload->maintCount += 1;
      payload->lastKind = context.request.kind;
      return AsyncPollStatus::completed;
    };
    callbacks.destroy = [](void *payloadPtr) {
      auto *payload = static_cast<TestPayload *>(payloadPtr);
      if (!payload) return;
      payload->destroyCount += 1;
      delete payload;
    };
    return callbacks;
  }

  void wait_for_ticket(const AsyncResourceMaintenanceTicket &ticket) {
    require(ticket.scheduled(), "maintenance ticket was not scheduled");
    require(ticket.submission.event().wait_for(2000), "maintenance did not finish");
    require(ticket.submission.status() == AsyncTaskStatus::completed,
            "maintenance did not complete successfully");
  }

}  // namespace

int main() {
  AsyncRuntime runtime{2};
  AsyncResourceManager manager{runtime};

  auto *hotPayload = new TestPayload{};
  auto hot = manager.register_resource(
      AsyncResourceDescriptor{"hot", "thread_pool",
                              make_host_endpoint(AsyncBackend::thread_pool,
                                                 AsyncQueueClass::compute, "hot"),
                              AsyncDomain::thread, AsyncQueueClass::compute, 256, 3, 1, false,
                              false},
      make_callbacks(), hotPayload);

  require(manager.contains(hot), "manager did not register hot resource");
  require(manager.mark_dirty(hot), "manager did not mark hot resource dirty");

  auto lease = manager.acquire(hot);
  require(lease.valid(), "hot resource lease is invalid");
  require(lease.payload() == hotPayload, "lease payload does not match hot resource");

  auto blocked = manager.schedule_maintenance(
      hot,
      AsyncResourceMaintenanceRequest{AsyncResourceMaintenanceKind::compact, "compact-hot", true,
                                      true, true, false});
  require(blocked.disposition == AsyncResourceMaintenanceDisposition::skipped_leased,
          "leased resource should not run idle maintenance");

  lease.reset();

  auto compact = manager.schedule_maintenance(
      hot,
      AsyncResourceMaintenanceRequest{AsyncResourceMaintenanceKind::compact, "compact-hot", true,
                                      true, true, false});
  wait_for_ticket(compact);
  require(hotPayload->maintCount == 1, "hot resource maintenance callback count mismatch");
  require(hotPayload->lastKind == AsyncResourceMaintenanceKind::compact,
          "hot resource maintenance kind mismatch");
  require(!manager.is_dirty(hot), "hot resource should be clean after maintenance");

  std::vector<AsyncResourceHandle> staleHandles;
  staleHandles.reserve(24);
  for (int i = 0; i < 24; ++i) {
    staleHandles.push_back(manager.register_resource(
        AsyncResourceDescriptor{"stale", "thread_pool",
                                make_host_endpoint(AsyncBackend::thread_pool,
                                                   AsyncQueueClass::compute, "stale"),
                                AsyncDomain::thread, AsyncQueueClass::compute, 64, 2, 0, false,
                                true},
        make_callbacks(), new TestPayload{}));
  }

  manager.advance_epoch(3);
  auto staleTickets = manager.schedule_stale_maintenance(
      AsyncResourceMaintenanceRequest{AsyncResourceMaintenanceKind::evict, "evict-stale", true,
                                      false, true, true});
  require(staleTickets.size() == staleHandles.size(),
          "stale maintenance did not cover all stale resources");
  for (const auto &ticket : staleTickets) wait_for_ticket(ticket);

  const auto removed = manager.collect_retired();
  require(removed == staleHandles.size(), "retired stale resources were not collected");
  require(manager.contains(hot), "hot resource should still be registered");

  const auto snapshot = manager.stats();
  require(snapshot.total == 1, "unexpected number of live resources after stale sweep");
  require(snapshot.busy == 0, "resource manager still reports busy resources");
  require(snapshot.retired == 0, "resource manager still reports retired resources");

  std::puts("All async resource manager tests passed.");
  return 0;
}