Architecture And Modularization
===============================

ZPC already has several useful internal seams, but downstream consumption still behaves as if the
project were primarily one assembled product. The next architectural step is to preserve the
current compatibility surface while making the internal graph explicitly layered and pay-for-what-
you-use.

Problem Statement
-----------------

Today the main issue is not that the codebase has no structure. The issue is that structure is not
yet the dominant downstream contract.

Current friction points include:

* ``zpc`` still re-aggregates most enabled functionality into one compatibility product
* ``zpccore`` still mixes portable and optional ownership
* graphics ownership is not fully orthogonal to the portable core
* tests mostly link the assembled product rather than the narrowest valid layer

This makes it harder to validate minimal profiles, isolate platform blockers, or scale work across
runtime, backend, graphics, and tooling layers.

Proposed Layering
-----------------

Layer 0: compile contract
^^^^^^^^^^^^^^^^^^^^^^^^^

``zpcbase`` remains the header and compile-contract layer.

Responsibilities:

* public header aggregation
* common compile definitions and compile features
* low-level reflection or annotation support

It should remain dependency-light and mostly stable.

Layer 1: portable foundation
^^^^^^^^^^^^^^^^^^^^^^^^^^^^

``zpccore_foundation`` should own compiled portable substrate behavior.

Responsibilities:

* math, memory, base object and resource utilities
* platform-safe concurrency and thread support
* logging and filesystem plumbing after abstraction
* shared low-level non-backend utilities

This is the layer that ``minimal`` builds should always be able to consume.

Layer 2: validation and runtime control plane
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Two adjacent layers should become explicit:

* ``zpc_validation_core``
* ``zpc_async_runtime_core``

Validation core owns schemas, comparison, formatting, and upgrade-facing helpers.

Async runtime core owns:

* runtime submission and lifecycle machinery
* scheduler and composition primitives
* endpoint and native queue abstractions
* deployable ABI implementation helpers

Layer 3: backend modules
^^^^^^^^^^^^^^^^^^^^^^^^

Backends should become first-class composition units instead of mostly implicit passengers on the
assembled product.

Representative targets:

* ``zpc_backend_serial``
* ``zpc_backend_omp``
* ``zpc_backend_cuda``
* ``zpc_backend_musa``
* ``zpc_backend_sycl``
* ``zpc_backend_opencl``

Each backend should depend downward on foundation and, where needed, runtime core. Backends should
not depend on each other.

Layer 4: domain modules
^^^^^^^^^^^^^^^^^^^^^^^

Domain layers should own containers, geometry, simulation, IO, and tool-facing infrastructure
without inheriting backend or graphics ownership accidentally.

Representative targets:

* ``zpc_core_containers``
* ``zpc_geometry``
* ``zpc_simulation``
* ``zpc_io``
* ``zpc_tools``

Layer 5: graphics module
^^^^^^^^^^^^^^^^^^^^^^^^

Graphics should become its own optional module rather than part of the portable core.

Representative target:

* ``zpc_graphics_vulkan``

That keeps headless, runtime-only, and constrained profiles from inheriting graphics ownership by
default.

Layer 6: Python and JIT modules
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Python bindings and JIT services are useful product layers, but they should remain optional.

Representative targets:

* ``zpc_py_base``
* ``zpc_py_omp``
* ``zpc_py_cuda``
* ``zpc_jit_clang``
* ``zpc_jit_nvrtc``

Layer 7: compatibility facades
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Compatibility targets should remain during migration:

* ``zpccore``
* ``zpc``
* ``zensim``

The key change is semantic. These become facades over the modular graph rather than the place where
most ownership remains glued together.

Dependency Direction
--------------------

The intended direction is:

* compile contract -> foundation -> validation/runtime -> backends or domain modules -> graphics,
  Python, JIT, and tools -> compatibility facades

The most important forbidden patterns are:

* foundation depending on graphics or heavyweight backend SDKs
* graphics depending on compute backend internals
* backends depending on each other
* domain layers owning raw Python or JIT dependencies directly
* public ABI surfaces depending on facade targets

Migration Strategy
------------------

The lowest-risk sequence is:

* extract portable foundation and runtime or validation layers under existing facades
* split graphics ownership out of portable core assembly
* normalize backend ownership and introduce an explicit always-present host backend
* retarget tests toward narrower layers where appropriate
* keep ``zpc`` and related umbrella targets as compatibility entry points throughout the transition

This avoids breaking current consumers while still moving the codebase toward a cleaner graph.

Why This Architecture Matters
-----------------------------

This is not only about cleaner CMake. It directly supports:

* minimal and runtime-only build profiles
* portable and constrained-platform validation
* future frontend-side composition such as ``zpc::lite``-style packaging
* clearer ownership for async runtime, graphics, and tooling work
* better scaling across multiple implementation agents or contributors once interfaces stabilize

Related Pages
-------------

See also:

* :doc:`foundation_layer` for the portable always-on substrate
* :doc:`platform_and_build_profiles` for how the target graph maps to delivery profiles
* :doc:`implementation_roadmap` for the recommended sequence of modularization work