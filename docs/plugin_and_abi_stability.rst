Plugin And ABI Stability
========================

ZPC needs a stable deployable boundary that can survive internal evolution. The in-tree C++ runtime
is useful for development, but it is not the right binary contract for plugins, upgrades,
frontend-side composition, or long-lived packaged deployments.

Why the Stable Boundary Matters
-------------------------------

Several long-term goals depend on a versioned and narrow ABI surface:

* deployable execution engines that can evolve without rebuilding every consumer
* plugin-style extension or embedding workflows
* lightweight frontend integration from Python, DSL, or editor-side tooling
* coordinated parallel development across runtime, backend, validation, and tooling modules

If those consumers depended directly on unstable C++ layout, STL ownership, or compiler-specific
type behavior, every runtime refactor would become a deployment break.

Chosen Boundary
---------------

The intended deployable boundary is the stable C ABI around the runtime engine surface, documented
in :doc:`async_runtime_abi`.

Its core design rules are:

* opaque handles for engine and submission state
* fixed-width transport-friendly fields
* append-only growth for descriptors and function tables
* explicit structure sizes and version checks
* queried extensions instead of widening the base table prematurely

This lets internal runtime and backend implementation move while preserving a stable contract for
deployment-facing consumers.

Compatibility Discipline
------------------------

The discipline for this boundary should remain strict:

* major-version changes gate incompatible reshaping
* minor growth happens by larger structures and appended behavior
* existing field order and function order stay intact
* optional features surface through capabilities or extension lookup

That discipline matters as much for frontend and plugin consumers as it does for shipped runtime
engines.

Why Queried Extensions Matter
-----------------------------

ZPC is expected to grow validation export, native queue submission, richer dependency propagation,
and future integration services. If every new feature were added to the base engine table, the base
surface would become too wide to stabilize confidently.

Queried extensions keep the base narrow while still allowing growth. They also map well to a plugin
mindset: consumers can discover capabilities, bind only what they understand, and degrade cleanly
when an optional extension is unavailable.

Relationship to Plugins
-----------------------

The current codebase is not yet a full plugin platform, but the ABI rules should already assume
plugin-style compatibility requirements.

That means:

* runtime implementation can change behind opaque handles
* backend-specific features should appear through extension tables or capability negotiation
* higher-level tools should not need to understand internal C++ runtime ownership
* packaging and upgrade workflows can remain compatible as long as the deployable ABI stays within
  the established rules

Relationship to Lightweight Frontends
-------------------------------------

Future lightweight frontend work, including Python or DSL-driven orchestration, should compose on
top of this stable boundary rather than requiring direct coupling to monolithic in-tree C++ APIs.

That does not mean every frontend must use only the C ABI internally. It means the stable C ABI is
the right long-term boundary for portable deployment, embedding, and version-tolerant integration.

Design Consequence
------------------

Because this boundary is meant to stay stable, implementation work should prefer:

* internal helper types behind the ABI instead of surfacing C++ runtime internals
* append-only extension growth instead of reshaping the base table
* compatibility facades for user-facing products instead of direct coupling to internal target
  topology

This is one of the reasons the modularization effort should start from foundation and runtime-core
extraction rather than from more feature-heavy product work.

Related Pages
-------------

See also:

* :doc:`async_runtime_abi` for the current engine table and extension surface
* :doc:`runtime_core_design` for the runtime layer that sits behind the stable boundary
* :doc:`lightweight_frontend_integration` for the frontend composition direction that should build
  on this compatibility model