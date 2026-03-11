#pragma once
/// @file ExecutionGraph.hpp
/// @brief Backend-agnostic execution graph with resource access tracking.
///
/// Accepts a render-graph-like declaration of passes and their resource
/// access patterns, then lowers to a device-executable pipeline.
///
/// Design pillars:
///   1. Declarative — user describes WHAT each pass reads/writes, not HOW
///      synchronisation happens.
///   2. Backend-neutral — the same graph description can target CUDA streams,
///      Vulkan command buffers, CPU thread pools, or mixed heterogeneous
///      pipelines.
///   3. Zero-overhead path for the common case — if the graph is trivially
///      serial or the backend can prove no hazards, no sync is emitted.
///   4. Maximum overlap — independent passes are assigned to separate
///      execution lanes (streams / queues / threads) so they can run
///      concurrently on different hardware units.  E.g. a shadow-map pass
///      (ROP-heavy) overlaps with a GPU-driven culling pass (compute-heavy)
///      because they saturate different fixed-function blocks.
///
/// Vocabulary:
///   ResourceHandle — opaque 64-bit ID for a tracked resource.
///   AccessMode     — Read, Write, ReadWrite.
///   AccessDomain   — Where the access takes place (host, device compute,
///                    device graphics, device transfer, device copy).
///   PassNode       — A unit of work that declares resource accesses.
///   ExecutionLane  — A sequential execution timeline on one hardware unit
///                    (e.g. one CUDA stream, one Vulkan queue, one CPU
///                    thread-pool lane).  Passes within a lane execute in
///                    order; passes on different lanes execute concurrently.
///   SyncEdge       — Dependency edge between two passes.  Same-lane edges
///                    are free (ordering is implicit).  Cross-lane edges
///                    require an explicit sync primitive (CUDA event,
///                    Vulkan semaphore, CPU atomic/fence).
///   ExecutionGraph — Collection of PassNodes + resource registry.
///   CompiledGraph  — Analysed graph with hazard resolution, lane
///                    assignment, and sync edge classification.
///   GraphExecutor  — Lowers CompiledGraph to backend-specific execution.
///
/// Status: foundational API.  Lowering backends are stubs to be filled as
///         CUDA / Vulkan / CPU backends register their adapters.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "zensim/TypeAlias.hpp"
#include "zensim/ZpcFunction.hpp"
#include "zensim/execution/AsyncRuntime.hpp"
#include "zensim/execution/ConcurrencyPrimitive.hpp"
#include "zensim/types/ImplPattern.hpp"

namespace zs {

  // ═══════════════════════════════════════════════════════════════════════
  // Resource handle
  // ═══════════════════════════════════════════════════════════════════════

  /// Opaque handle identifying a resource tracked by the execution graph.
  struct ResourceHandle {
    u64 id{0};

    constexpr bool valid() const noexcept { return id != 0; }
    constexpr explicit operator bool() const noexcept { return valid(); }
    constexpr bool operator==(const ResourceHandle &o) const noexcept { return id == o.id; }
    constexpr bool operator!=(const ResourceHandle &o) const noexcept { return id != o.id; }
  };

  // ═══════════════════════════════════════════════════════════════════════
  // Access mode & domain
  // ═══════════════════════════════════════════════════════════════════════

  /// How a pass accesses a resource.
  enum class AccessMode : u8 {
    none = 0,
    read = 1,
    write = 2,
    read_write = 3
  };

  constexpr bool involves_read(AccessMode m) noexcept {
    return (u8)m & (u8)AccessMode::read;
  }
  constexpr bool involves_write(AccessMode m) noexcept {
    return (u8)m & (u8)AccessMode::write;
  }

  /// Where the access takes place.  Mirrors AsyncDomain / AsyncQueueClass
  /// granularity but is deliberately a separate enum so the graph layer
  /// can reason about hazards without importing backend headers.
  enum class AccessDomain : u8 {
    host_sequential,   ///< single-threaded CPU
    host_parallel,     ///< multi-threaded CPU (OMP / thread pool)
    device_compute,    ///< GPU compute pipeline (CUDA kernel, VkCompute, etc.)
    device_graphics,   ///< GPU graphics pipeline (rasterisation, ray-tracing)
    device_transfer,   ///< DMA / async copy engine
    device_copy,       ///< host↔device memcpy
    any                ///< unspecified — compiler chooses
  };

  // ═══════════════════════════════════════════════════════════════════════
  // Resource descriptor
  // ═══════════════════════════════════════════════════════════════════════

  /// Describes a resource registered with the graph.
  struct ResourceDescriptor {
    SmallString label{};
    size_t sizeBytes{0};
    AccessDomain initialDomain{AccessDomain::host_sequential};
    bool transient{false};   ///< lifetime contained within a single graph execution
  };

  // ═══════════════════════════════════════════════════════════════════════
  // Resource access declaration (per-pass)
  // ═══════════════════════════════════════════════════════════════════════

  /// A single resource-access declaration attached to a pass.
  struct ResourceAccessDecl {
    ResourceHandle resource{};
    AccessMode mode{AccessMode::none};
    AccessDomain domain{AccessDomain::any};

    /// Optional byte sub-range (0,0 = whole resource).
    size_t offset{0};
    size_t length{0};
  };

  // ═══════════════════════════════════════════════════════════════════════
  // Hazard classification
  // ═══════════════════════════════════════════════════════════════════════

  enum class HazardKind : u8 {
    none = 0,
    read_after_write,   ///< RAW — true dependency
    write_after_read,   ///< WAR — anti-dependency
    write_after_write   ///< WAW — output dependency
  };

  /// Detect the hazard (if any) between two accesses to the same resource.
  constexpr HazardKind classify_hazard(AccessMode earlier, AccessMode later) noexcept {
    const bool eW = involves_write(earlier);
    const bool lW = involves_write(later);
    const bool lR = involves_read(later);
    if (eW && lR && !lW) return HazardKind::read_after_write;
    if (eW && lW) return HazardKind::write_after_write;
    if (!eW && lW) return HazardKind::write_after_read;
    return HazardKind::none;
  }

  // ═══════════════════════════════════════════════════════════════════════
  // Execution lane
  // ═══════════════════════════════════════════════════════════════════════

  /// Identifies a hardware execution lane within the compiled graph.
  ///
  /// A lane is a sequential execution timeline — passes assigned to the
  /// same lane will execute in order with no explicit sync between them.
  /// Passes on *different* lanes execute concurrently.
  ///
  /// The lane concept maps to concrete backend primitives:
  ///   - CUDA:   one lane = one CUstream
  ///   - Vulkan: one lane = one VkQueue (within a queue family)
  ///   - CPU:    one lane = one worker thread or one scheduler partition
  ///
  /// Multiple lanes can share the same `queueClass`.  E.g. two compute
  /// lanes map to two CUDA streams that both submit to the compute
  /// engine — the GPU's hardware scheduler overlaps their warps.
  /// Similarly, a graphics lane and a compute lane can overlap if the
  /// passes exercise different fixed-function units (ROP vs ALU/SFU).
  struct ExecutionLane {
    u32 id{0};
    AsyncQueueClass queueClass{AsyncQueueClass::compute};
    SmallString label{};
  };

  // ═══════════════════════════════════════════════════════════════════════
  // Pass cost hint (resource requirement metric for load balancing)
  // ═══════════════════════════════════════════════════════════════════════

  /// Describes which fixed-function hardware units a pass predominantly uses.
  /// Multiple flags can be ORed together.
  enum class HardwareAffinity : u32 {
    none          = 0,
    alu           = 1 << 0,   ///< shader ALU / CUDA cores
    sfu           = 1 << 1,   ///< special function units (transcendentals)
    rop           = 1 << 2,   ///< rasterisation output (blending, depth)
    texture       = 1 << 3,   ///< texture / sampler units
    rt_core       = 1 << 4,   ///< ray-tracing hardware
    tensor_core   = 1 << 5,   ///< tensor / matrix accelerator
    dma           = 1 << 6,   ///< copy / transfer engine
    host_cpu      = 1 << 7,   ///< CPU-side work
  };
  constexpr HardwareAffinity operator|(HardwareAffinity a, HardwareAffinity b) noexcept {
    return static_cast<HardwareAffinity>(static_cast<u32>(a) | static_cast<u32>(b));
  }
  constexpr HardwareAffinity operator&(HardwareAffinity a, HardwareAffinity b) noexcept {
    return static_cast<HardwareAffinity>(static_cast<u32>(a) & static_cast<u32>(b));
  }
  constexpr bool has_affinity(HardwareAffinity set, HardwareAffinity flag) noexcept {
    return (static_cast<u32>(set) & static_cast<u32>(flag)) != 0;
  }

  /// Cost / resource-requirement hint attached to each pass.
  ///
  /// The scheduler uses these hints for load-balancing decisions:
  ///   - Passes with disjoint hardware affinity can overlap safely
  ///     (shadow map on ROP + compute cull on ALU).
  ///   - Passes with high memory bandwidth can be spread across lanes
  ///     to avoid memory bus contention.
  ///   - estimatedCycles provides a coarse relative cost for topo-sort
  ///     tie-breaking (prefer scheduling expensive passes first to
  ///     minimise critical-path length).
  ///
  /// All fields are **hints** — the compiler never rejects a graph based
  /// on cost information.  When not explicitly set, the compiler can
  /// auto-deduce conservative estimates from declared resource accesses:
  ///   - memoryBytesRead/Written from ResourceAccessDecl sizes
  ///   - affinity from AccessDomain (device_graphics → alu|rop,
  ///     device_compute → alu, device_transfer → dma, etc.)
  struct PassCostHint {
    /// Estimated compute cost in arbitrary units (higher = more expensive).
    /// Used for priority tie-breaking in topological sort: the compiler
    /// prefers scheduling passes with higher cost first to reduce
    /// critical-path latency.
    u64 estimatedCycles{0};

    /// Estimated memory bandwidth consumption (bytes).
    u64 memoryBytesRead{0};
    u64 memoryBytesWritten{0};

    /// Hardware unit affinity flags.  The compiler uses this to determine
    /// which passes can overlap without contention.
    HardwareAffinity affinity{HardwareAffinity::none};

    /// Number of thread blocks / warps / work-groups (backend-specific).
    /// Zero means "unknown / let the compiler decide".
    u32 threadGroupCount{0};

    /// Shared memory or local memory usage per thread group (bytes).
    u32 sharedMemPerGroup{0};

    /// Whether this hint was explicitly set by the user (vs auto-deduced).
    bool userProvided{false};
  };

  /// Auto-deduce a PassCostHint from a pass's declared resource accesses.
  inline PassCostHint deduce_cost_hint(const std::vector<ResourceAccessDecl> &accesses,
                                       AccessDomain preferredDomain) {
    PassCostHint hint{};
    for (const auto &acc : accesses) {
      const size_t bytes = acc.length > 0 ? acc.length : 0;
      if (involves_read(acc.mode)) hint.memoryBytesRead += bytes;
      if (involves_write(acc.mode)) hint.memoryBytesWritten += bytes;
    }
    // Deduce hardware affinity from domain.
    switch (preferredDomain) {
      case AccessDomain::device_compute:
        hint.affinity = HardwareAffinity::alu;
        break;
      case AccessDomain::device_graphics:
        hint.affinity = HardwareAffinity::alu | HardwareAffinity::rop;
        break;
      case AccessDomain::device_transfer:
      case AccessDomain::device_copy:
        hint.affinity = HardwareAffinity::dma;
        break;
      case AccessDomain::host_sequential:
      case AccessDomain::host_parallel:
        hint.affinity = HardwareAffinity::host_cpu;
        break;
      default:
        hint.affinity = HardwareAffinity::alu;
        break;
    }
    // Rough cycle estimate from total memory footprint (1 cycle per 64 bytes
    // — intentionally coarse; only relative ordering matters).
    const u64 totalBytes = hint.memoryBytesRead + hint.memoryBytesWritten;
    hint.estimatedCycles = totalBytes > 0 ? (totalBytes + 63) / 64 : 1;
    return hint;
  }

  // ═══════════════════════════════════════════════════════════════════════
  // Pass node
  // ═══════════════════════════════════════════════════════════════════════

  /// Callback signature for pass execution.
  /// The context provides the backend-specific execution environment
  /// (stream handle, command buffer, etc.) that the compiled graph has
  /// selected for this pass.
  using PassCallback = function<void(AsyncExecutionContext &)>;

  /// A node in the execution graph representing a unit of work.
  struct PassNode {
    u32 index{0};
    SmallString label{};
    PassCallback callback{};
    std::vector<ResourceAccessDecl> accesses{};

    /// Preferred execution domain (hint; compiler may override).
    AccessDomain preferredDomain{AccessDomain::any};

    /// Priority (higher = earlier when topologically equivalent).
    /// When zero and costHint.estimatedCycles > 0, the compiler uses
    /// estimatedCycles as a secondary tie-breaker.
    int priority{0};

    /// Whether this pass can share a lane with other passes of the same
    /// queue class, or needs a dedicated lane.  Default: shared.
    /// Set to true for passes that require exclusive queue access
    /// (e.g. a present pass, or a pass with side-effects on the queue).
    bool exclusiveLane{false};

    /// Resource-requirement / cost hint for load-balancing.
    /// If not explicitly set (userProvided == false), the compiler
    /// auto-deduces from declared resource accesses at compile() time.
    PassCostHint costHint{};
  };

  // ═══════════════════════════════════════════════════════════════════════
  // Synchronisation edge (produced by compiler)
  // ═══════════════════════════════════════════════════════════════════════

  /// Describes a synchronisation action the executor must perform between
  /// two passes.
  struct SyncEdge {
    u32 srcPass{0};
    u32 dstPass{0};
    ResourceHandle resource{};
    HazardKind hazard{HazardKind::none};

    /// Whether this edge crosses lane boundaries.
    ///
    /// Same-lane edges are "free" — the lane's sequential ordering already
    /// guarantees the dependency.  The executor may still need a memory
    /// barrier (e.g. Vulkan pipeline barrier within a command buffer) but
    /// does NOT need an inter-queue semaphore / CUDA event.
    ///
    /// Cross-lane edges require an explicit synchronisation primitive:
    ///   - CUDA:   cudaEventRecord on src stream + cudaStreamWaitEvent
    ///             on dst stream.
    ///   - Vulkan: timeline semaphore signal on src queue + wait on dst.
    ///   - CPU:    atomic counter + futex wake.
    bool crossLane{false};

    /// Lanes involved (valid after lane assignment).
    u32 srcLane{0};
    u32 dstLane{0};

    /// Backend-specific lowering hint.
    /// For Vulkan: pipeline stage flags.
    /// For CUDA:   event-based stream dependency.
    /// For CPU:    memory fence or no-op.
    u64 srcStageMask{0};
    u64 dstStageMask{0};
  };

  // ═══════════════════════════════════════════════════════════════════════
  // Compiled graph
  // ═══════════════════════════════════════════════════════════════════════

  /// Result of graph compilation — topologically sorted passes, sync
  /// edges, lane assignments, and queue mappings.
  struct CompiledGraph {
    /// Passes in execution order (respecting dependencies).
    std::vector<u32> sortedPassIndices{};

    /// Synchronisation edges the executor must honour.
    std::vector<SyncEdge> syncEdges{};

    /// Per-pass assigned queue class (may differ from pass preference
    /// based on available hardware).
    std::vector<AsyncQueueClass> queueAssignments{};

    /// Per-pass lane assignment.  Passes on the same lane execute in
    /// the order they appear in sortedPassIndices.  Passes on different
    /// lanes may execute concurrently.
    std::vector<u32> laneAssignments{};

    /// The set of distinct execution lanes used by this graph.
    std::vector<ExecutionLane> lanes{};

    /// Per-lane ordered list of passes.  lanes[i] → list of pass indices
    /// in execution order.  The executor iterates each lane's list and
    /// submits passes to the corresponding stream / queue.
    std::vector<std::vector<u32>> laneTimelines{};

    /// Number of cross-lane sync edges.  Zero means the entire graph
    /// can be submitted to a single stream/queue with no inter-stream
    /// synchronisation.
    u32 numCrossLaneSyncs{0};

    bool valid() const noexcept { return !sortedPassIndices.empty(); }
  };

  // ═══════════════════════════════════════════════════════════════════════
  // Execution graph (builder + compiler)
  // ═══════════════════════════════════════════════════════════════════════

  /// @brief Backend-agnostic execution graph.
  ///
  /// Usage:
  /// @code
  ///   ExecutionGraph g;
  ///
  ///   auto buf = g.importResource({"particles", 1 << 20});
  ///   auto grid = g.importResource({"grid", 1 << 22});
  ///
  ///   auto &scatter = g.addPass("P2G-scatter");
  ///   scatter.accesses.push_back({buf,  AccessMode::read,  AccessDomain::device_compute});
  ///   scatter.accesses.push_back({grid, AccessMode::write, AccessDomain::device_compute});
  ///   scatter.callback = [](AsyncExecutionContext &ctx) { /* kernel */ };
  ///
  ///   auto &gather = g.addPass("G2P-gather");
  ///   gather.accesses.push_back({grid, AccessMode::read,  AccessDomain::device_compute});
  ///   gather.accesses.push_back({buf,  AccessMode::write, AccessDomain::device_compute});
  ///   gather.callback = [](AsyncExecutionContext &ctx) { /* kernel */ };
  ///
  ///   auto compiled = g.compile();
  ///   // hand `compiled` to a GraphExecutor to run on actual hardware
  /// @endcode
  ///
  /// Overlap example (shadow map + GPU-driven culling):
  /// @code
  ///   auto depth = g.importResource({"shadow_depth", ...});
  ///   auto draws = g.importResource({"draw_cmds", ...});
  ///   auto scene = g.importResource({"scene_data", ...});
  ///
  ///   // Shadow pass: reads scene, writes depth.  ROP-bound.
  ///   g.addPass("shadow", {
  ///     {scene, AccessMode::read,  AccessDomain::device_graphics},
  ///     {depth, AccessMode::write, AccessDomain::device_graphics}
  ///   }, shadow_callback);
  ///
  ///   // Culling pass: reads scene, writes draw commands.  ALU-bound.
  ///   g.addPass("cull", {
  ///     {scene, AccessMode::read,  AccessDomain::device_compute},
  ///     {draws, AccessMode::write, AccessDomain::device_compute}
  ///   }, cull_callback);
  ///
  ///   // No hazard between shadow and cull (different write targets,
  ///   // shared read on scene is safe).  Compiler assigns them to
  ///   // separate lanes → they overlap on GPU.
  ///   // Shadow saturates ROP; cull saturates compute ALUs → near-zero
  ///   // contention for the common case.
  ///
  ///   // Shading pass: reads depth + draws + scene.  Depends on both.
  ///   g.addPass("shade", {
  ///     {depth, AccessMode::read,  AccessDomain::device_graphics},
  ///     {draws, AccessMode::read,  AccessDomain::device_graphics},
  ///     {scene, AccessMode::read,  AccessDomain::device_graphics}
  ///   }, shade_callback);
  ///   // RAW edges: shadow→shade (depth), cull→shade (draws).
  ///   // Both are cross-lane → executor inserts semaphore/event waits.
  /// @endcode
  class ExecutionGraph {
  public:
    ExecutionGraph() = default;

    // ── resource registration ─────────────────────────────────────────

    ResourceHandle importResource(const ResourceDescriptor &desc) {
      ResourceHandle h{_nextResourceId++};
      _resources.push_back({h, desc});
      return h;
    }

    ResourceHandle createTransientResource(SmallString label, size_t sizeBytes,
                                           AccessDomain domain = AccessDomain::device_compute) {
      ResourceDescriptor desc{label, sizeBytes, domain, /*transient=*/true};
      return importResource(desc);
    }

    // ── pass construction ─────────────────────────────────────────────

    PassNode &addPass(SmallString label) {
      auto &p = _passes.emplace_back();
      p.index = (u32)(_passes.size() - 1);
      p.label = label;
      return p;
    }

    // Convenience: declare a pass with inline access list.
    PassNode &addPass(SmallString label,
                      std::initializer_list<ResourceAccessDecl> accesses,
                      PassCallback callback) {
      auto &p = addPass(label);
      p.accesses.assign(accesses.begin(), accesses.end());
      p.callback = zs::move(callback);
      return p;
    }

    // ── accessors ─────────────────────────────────────────────────────

    size_t numPasses() const noexcept { return _passes.size(); }
    size_t numResources() const noexcept { return _resources.size(); }

    const PassNode &pass(u32 index) const { return _passes[index]; }
    PassNode &pass(u32 index) { return _passes[index]; }

    // ── compilation ───────────────────────────────────────────────────

    /// Analyse the graph, compute a topological order, detect hazards,
    /// assign execution lanes for maximum overlap, and emit
    /// synchronisation edges with cross-lane classification.
    CompiledGraph compile() const {
      CompiledGraph result;
      const size_t N = _passes.size();
      if (N == 0) return result;

      // ── 1. Build adjacency from resource hazard analysis ──────────
      //
      // For each resource, walk the passes in declaration order and
      // compare consecutive accesses.  If a hazard exists, add a
      // directed edge (earlier → later).

      // adjacency[i] = list of successors of pass i
      std::vector<std::vector<u32>> adj(N);
      std::vector<u32> inDegree(N, 0);

      // Temporary: collect edges with hazard info before dedup.
      struct RawEdge {
        u32 src, dst;
        ResourceHandle resource;
        HazardKind hazard;
      };
      std::vector<RawEdge> rawEdges;

      for (const auto &[handle, desc] : _resources) {
        // Collect passes that touch this resource, in declaration order.
        struct TouchRecord {
          u32 passIdx;
          AccessMode mode;
        };
        std::vector<TouchRecord> touches;
        for (const auto &pass : _passes) {
          for (const auto &acc : pass.accesses) {
            if (acc.resource == handle)
              touches.push_back({pass.index, acc.mode});
          }
        }

        // Pair-wise hazard check (quadratic, but pass counts are small).
        for (size_t i = 0; i < touches.size(); ++i) {
          for (size_t j = i + 1; j < touches.size(); ++j) {
            auto hazard = classify_hazard(touches[i].mode, touches[j].mode);
            if (hazard != HazardKind::none) {
              const u32 src = touches[i].passIdx;
              const u32 dst = touches[j].passIdx;
              // Avoid duplicate adjacency edges.
              auto &succs = adj[src];
              if (std::find(succs.begin(), succs.end(), dst) == succs.end()) {
                succs.push_back(dst);
                ++inDegree[dst];
              }
              rawEdges.push_back({src, dst, handle, hazard});
            }
          }
        }
      }

      // ── 1b. Auto-deduce cost hints for passes that lack them ─────
      //
      // Mutable copy of passes for cost deduction (we don't modify the
      // original graph — cost hints only influence compilation).
      std::vector<PassCostHint> effectiveCosts(N);
      for (size_t i = 0; i < N; ++i) {
        const auto &pass = _passes[i];
        if (pass.costHint.userProvided) {
          effectiveCosts[i] = pass.costHint;
        } else {
          effectiveCosts[i] = deduce_cost_hint(pass.accesses, pass.preferredDomain);
        }
      }

      // ── 2. Topological sort (Kahn's algorithm) ────────────────────
      //
      // When multiple passes are ready simultaneously, prefer higher
      // priority.  When priorities are equal, prefer higher
      // estimatedCycles (schedule expensive work first to reduce
      // critical-path latency — a standard HEFT-like heuristic).

      std::vector<u32> readyQueue;
      readyQueue.reserve(N);
      for (u32 i = 0; i < (u32)N; ++i) {
        if (inDegree[i] == 0) readyQueue.push_back(i);
      }

      result.sortedPassIndices.reserve(N);
      size_t head = 0;
      while (head < readyQueue.size()) {
        // Among all ready passes, pick by priority (descending),
        // then by estimatedCycles (descending) as tie-breaker.
        size_t bestIdx = head;
        for (size_t k = head + 1; k < readyQueue.size(); ++k) {
          const auto &bestPass = _passes[readyQueue[bestIdx]];
          const auto &candPass = _passes[readyQueue[k]];
          if (candPass.priority > bestPass.priority) {
            bestIdx = k;
          } else if (candPass.priority == bestPass.priority &&
                     effectiveCosts[readyQueue[k]].estimatedCycles >
                     effectiveCosts[readyQueue[bestIdx]].estimatedCycles) {
            bestIdx = k;
          }
        }
        if (bestIdx != head) std::swap(readyQueue[head], readyQueue[bestIdx]);

        u32 cur = readyQueue[head++];
        result.sortedPassIndices.push_back(cur);
        for (u32 succ : adj[cur]) {
          if (--inDegree[succ] == 0) readyQueue.push_back(succ);
        }
      }

      // Cycle detection.
      if (result.sortedPassIndices.size() != N) {
        result.sortedPassIndices.clear();
        return result;  // invalid — cycle in graph
      }

      // ── 3. Queue class assignment ─────────────────────────────────

      result.queueAssignments.resize(N, AsyncQueueClass::compute);
      for (const auto &pass : _passes) {
        switch (pass.preferredDomain) {
          case AccessDomain::device_graphics:
            result.queueAssignments[pass.index] = AsyncQueueClass::graphics;
            break;
          case AccessDomain::device_transfer:
          case AccessDomain::device_copy:
            result.queueAssignments[pass.index] = AsyncQueueClass::transfer;
            break;
          case AccessDomain::host_sequential:
          case AccessDomain::host_parallel:
            result.queueAssignments[pass.index] = AsyncQueueClass::control;
            break;
          default:
            result.queueAssignments[pass.index] = AsyncQueueClass::compute;
            break;
        }
      }

      // ── 4. Lane assignment (maximise overlap) ─────────────────────
      //
      // Strategy: assign each pass to an execution lane such that:
      //   a) Passes with different queue classes ALWAYS go to different
      //      lanes (a graphics queue and a compute queue are physically
      //      separate execution engines that can overlap — shadow map
      //      on ROP + culling on ALU).
      //   b) Within the same queue class, passes that have NO mutual
      //      hazard edge can go to different lanes (multiple CUDA
      //      streams on the compute engine).  The GPU HW scheduler
      //      will interleave warps from different streams.
      //   c) Passes connected by a hazard edge SHOULD be on the same
      //      lane when possible (avoids the cost of cross-lane sync).
      //      Exception: if the graph is wide (many independent chains),
      //      we spread them across lanes to expose parallelism.
      //   d) Hardware affinity from PassCostHint is used to prefer
      //      overlap of passes with disjoint hardware unit usage
      //      (e.g. ROP-only + ALU-only → minimal contention).
      //
      // Implementation:
      //   - One lane per queue class initially (the "primary" lane).
      //   - For each pass (in topo order), check if it has a hazard
      //     predecessor on the primary lane for its queue class.
      //     * If yes → same lane (free ordering, no sync needed).
      //     * If no predecessor → eligible for a secondary lane if one
      //       exists with no conflict, otherwise create a new lane.
      //   - Exclusive-lane passes always get their own lane.

      // Build predecessor set per pass (direct predecessors via hazard).
      std::vector<std::vector<u32>> preds(N);
      for (const auto &e : rawEdges) preds[e.dst].push_back(e.src);

      // Deduplicate predecessor lists.
      for (auto &p : preds) {
        std::sort(p.begin(), p.end());
        p.erase(std::unique(p.begin(), p.end()), p.end());
      }

      result.laneAssignments.resize(N, 0);

      // Track: for each lane, the set of passes assigned so far and
      // the last pass index (for ordering within the lane).
      struct LaneState {
        ExecutionLane lane{};
        std::vector<u32> passes{};
        u32 lastPass{0};
      };
      std::vector<LaneState> laneStates;

      // Helper: find or create a lane for a given queue class.
      auto findOrCreatePrimaryLane = [&](AsyncQueueClass qc) -> u32 {
        for (u32 i = 0; i < (u32)laneStates.size(); ++i) {
          if (laneStates[i].lane.queueClass == qc) return i;
        }
        u32 id = (u32)laneStates.size();
        LaneState ls;
        ls.lane.id = id;
        ls.lane.queueClass = qc;
        laneStates.push_back(zs::move(ls));
        return id;
      };

      // Helper: check if pass `p` has any hazard predecessor on lane `laneId`.
      auto hasPredOnLane = [&](u32 p, u32 laneId) -> bool {
        for (u32 pred : preds[p]) {
          if (result.laneAssignments[pred] == laneId) return true;
        }
        return false;
      };

      // Helper: check if pass `p` has any hazard edge (pred or succ) to
      // any pass already on lane `laneId`.
      auto hasConflictOnLane = [&](u32 p, u32 laneId) -> bool {
        for (u32 existingPass : laneStates[laneId].passes) {
          // Check if there's a hazard edge in either direction.
          for (const auto &e : rawEdges) {
            if ((e.src == p && e.dst == existingPass) ||
                (e.src == existingPass && e.dst == p))
              return true;
          }
        }
        return false;
      };

      for (u32 passIdx : result.sortedPassIndices) {
        const auto &pass = _passes[passIdx];
        AsyncQueueClass qc = result.queueAssignments[passIdx];

        if (pass.exclusiveLane) {
          // Dedicated lane.
          u32 laneId = (u32)laneStates.size();
          LaneState ls;
          ls.lane.id = laneId;
          ls.lane.queueClass = qc;
          ls.lane.label = pass.label;
          ls.passes.push_back(passIdx);
          ls.lastPass = passIdx;
          laneStates.push_back(zs::move(ls));
          result.laneAssignments[passIdx] = laneId;
          continue;
        }

        u32 primaryLane = findOrCreatePrimaryLane(qc);

        // If this pass has a predecessor on the primary lane, assign it
        // there — the hazard edge is satisfied by lane-internal ordering.
        if (hasPredOnLane(passIdx, primaryLane)) {
          result.laneAssignments[passIdx] = primaryLane;
          laneStates[primaryLane].passes.push_back(passIdx);
          laneStates[primaryLane].lastPass = passIdx;
          continue;
        }

        // No predecessor on primary lane.  Can we overlap on a secondary
        // lane of the same queue class?
        bool assigned = false;

        // First check: if there's ANY hazard predecessor, prefer the
        // lane that predecessor is on (to keep the chain on one lane).
        for (u32 pred : preds[passIdx]) {
          if (result.queueAssignments[pred] == qc) {
            u32 predLane = result.laneAssignments[pred];
            result.laneAssignments[passIdx] = predLane;
            laneStates[predLane].passes.push_back(passIdx);
            laneStates[predLane].lastPass = passIdx;
            assigned = true;
            break;
          }
        }
        if (assigned) continue;

        // No same-class predecessor at all.  Try to find an existing
        // lane of this queue class with no conflict.  If none, try the
        // primary lane.  If the primary lane has no conflict, use it.
        // Otherwise create a new lane for overlap.
        if (!hasConflictOnLane(passIdx, primaryLane)) {
          result.laneAssignments[passIdx] = primaryLane;
          laneStates[primaryLane].passes.push_back(passIdx);
          laneStates[primaryLane].lastPass = passIdx;
        } else {
          // Look for another lane of the same class with no conflict.
          bool foundSecondary = false;
          for (u32 i = 0; i < (u32)laneStates.size(); ++i) {
            if (i == primaryLane) continue;
            if (laneStates[i].lane.queueClass != qc) continue;
            if (!hasConflictOnLane(passIdx, i)) {
              result.laneAssignments[passIdx] = i;
              laneStates[i].passes.push_back(passIdx);
              laneStates[i].lastPass = passIdx;
              foundSecondary = true;
              break;
            }
          }
          if (!foundSecondary) {
            // Create a new secondary lane for this queue class.
            u32 laneId = (u32)laneStates.size();
            LaneState ls;
            ls.lane.id = laneId;
            ls.lane.queueClass = qc;
            ls.passes.push_back(passIdx);
            ls.lastPass = passIdx;
            laneStates.push_back(zs::move(ls));
            result.laneAssignments[passIdx] = laneId;
          }
        }
      }

      // ── 5. Build lane timelines and classify sync edges ───────────

      result.lanes.resize(laneStates.size());
      result.laneTimelines.resize(laneStates.size());
      for (u32 i = 0; i < (u32)laneStates.size(); ++i) {
        result.lanes[i] = laneStates[i].lane;
      }

      // Build per-lane ordered pass lists (in topo order).
      for (u32 passIdx : result.sortedPassIndices) {
        u32 lane = result.laneAssignments[passIdx];
        result.laneTimelines[lane].push_back(passIdx);
      }

      // Emit sync edges with lane information.
      result.numCrossLaneSyncs = 0;
      for (const auto &raw : rawEdges) {
        SyncEdge edge;
        edge.srcPass = raw.src;
        edge.dstPass = raw.dst;
        edge.resource = raw.resource;
        edge.hazard = raw.hazard;
        edge.srcLane = result.laneAssignments[raw.src];
        edge.dstLane = result.laneAssignments[raw.dst];
        edge.crossLane = (edge.srcLane != edge.dstLane);
        if (edge.crossLane) ++result.numCrossLaneSyncs;
        result.syncEdges.push_back(edge);
      }

      return result;
    }

    // ── execution (synchronous, on the calling thread) ────────────────

    /// Execute the compiled graph inline (serial, no backend lowering).
    /// Useful for validation and as a reference baseline.
    void executeInline(const CompiledGraph &compiled) const {
      if (!compiled.valid()) return;
      AsyncExecutionContext ctx{};
      for (u32 passIdx : compiled.sortedPassIndices) {
        const auto &pass = _passes[passIdx];
        if (pass.callback) pass.callback(ctx);
      }
    }

  private:
    struct ResourceEntry {
      ResourceHandle handle{};
      ResourceDescriptor desc{};
    };

    u64 _nextResourceId{1};
    std::vector<PassNode> _passes{};
    std::vector<ResourceEntry> _resources{};
  };

  // ═══════════════════════════════════════════════════════════════════════
  // Graph executor (abstract interface for backend lowering)
  // ═══════════════════════════════════════════════════════════════════════

  /// Backend-specific executor.  One implementation per target:
  ///   - CudaGraphExecutor    (stream + event based)
  ///   - VulkanGraphExecutor  (command buffer + semaphore based)
  ///   - CpuGraphExecutor     (AsyncScheduler based)
  ///   - HeterogeneousGraphExecutor (mixed)
  ///
  /// Executor responsibilities:
  ///   1. Map each ExecutionLane to a concrete backend resource (stream,
  ///      queue, thread).
  ///   2. For each lane timeline, submit passes in order.
  ///   3. For each cross-lane SyncEdge, insert the appropriate sync
  ///      primitive (cudaEvent, timeline semaphore, atomic+fence).
  ///   4. Same-lane SyncEdges: insert memory barrier / pipeline barrier
  ///      if required by the backend (Vulkan), or nothing (CUDA — stream
  ///      ordering is sufficient).
  class GraphExecutor {
  public:
    virtual ~GraphExecutor() = default;

    /// Execute a compiled graph on the backend this executor targets.
    /// Returns an event that fires when all passes complete.
    virtual AsyncEvent execute(const ExecutionGraph &graph,
                               const CompiledGraph &compiled) = 0;
  };

  // ═══════════════════════════════════════════════════════════════════════
  // CPU graph executor (reference implementation)
  // ═══════════════════════════════════════════════════════════════════════

  /// Executes the compiled graph on the AsyncScheduler, honouring
  /// lane assignments.  Each lane gets its own sequential chain of
  /// tasks; cross-lane dependencies use atomic counters.
  ///
  /// On CPU, "lane overlap" means true thread-level parallelism: each
  /// lane's passes can run on different scheduler workers concurrently.
  class CpuGraphExecutor : public GraphExecutor {
  public:
    explicit CpuGraphExecutor(AsyncScheduler &scheduler)
        : _scheduler{scheduler} {}

    AsyncEvent execute(const ExecutionGraph &graph,
                       const CompiledGraph &compiled) override {
      if (!compiled.valid()) return {};

      auto completionEvent = AsyncEvent::create();
      const size_t N = compiled.sortedPassIndices.size();

      // Build per-pass predecessor counts from syncEdges.
      auto deps = std::make_shared<std::vector<Atomic<int>>>(graph.numPasses());
      for (const auto &edge : compiled.syncEdges) {
        (*deps)[edge.dstPass].fetch_add(1);
      }

      // Build successor lists.
      auto succs = std::make_shared<std::vector<std::vector<u32>>>(graph.numPasses());
      for (const auto &edge : compiled.syncEdges) {
        (*succs)[edge.srcPass].push_back(edge.dstPass);
      }

      auto remaining = std::make_shared<Atomic<u32>>((u32)N);

      // Recursive launcher: when a pass completes, decrement successors
      // and enqueue any that become ready.
      auto launch = std::make_shared<function<void(u32)>>();
      *launch = [&graph, deps, succs, remaining, completionEvent, launch,
                 this](u32 passIdx) {
        _scheduler.enqueue([&graph, deps, succs, remaining, completionEvent,
                            launch, passIdx, this]() {
          // Execute the pass.
          AsyncExecutionContext ctx{};
          const auto &pass = graph.pass(passIdx);
          if (pass.callback) pass.callback(ctx);

          // Decrement successors.
          for (u32 s : (*succs)[passIdx]) {
            if ((*deps)[s].fetch_sub(1) == 1) {
              (*launch)(s);
            }
          }

          if (remaining->fetch_sub(1) == 1)
            completionEvent.complete();
        });
      };

      // Enqueue root passes (zero dependencies).
      for (u32 idx : compiled.sortedPassIndices) {
        if ((*deps)[idx].load() == 0) (*launch)(idx);
      }

      return completionEvent;
    }

  private:
    AsyncScheduler &_scheduler;
  };

}  // namespace zs
