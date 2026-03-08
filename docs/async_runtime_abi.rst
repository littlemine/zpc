Async Runtime ABI
=================

The deployable execution engine runtime needs a stable binary boundary for upgrades, plugins,
distributed deployment, and long-lived maintenance. The C++ async runtime types are useful for
in-tree development, but they are not suitable as the primary deployable ABI because they depend on
compiler, STL, and layout details that are not stable enough for plugin-style compatibility.

Stable Boundary
---------------

The ABI-stable boundary is now defined in
``include/zensim/execution/AsyncRuntimeAbi.hpp``.

The design rules are:

* fixed-width integer fields only
* opaque engine and submission handles
* explicit ``size`` and ABI version in every top-level structure
* append-only function tables and descriptors
* major-version gate for incompatible changes
* minor-version growth through larger structure sizes and reserved fields

This follows the same compatibility discipline expected by plugin systems.

Versioning Model
----------------

The current runtime ABI version is ``1.0.0``.

Compatibility rules:

* matching major version is required
* newer implementations may append fields and functions while preserving existing order
* callers must check structure size before accessing appended fields
* reserved slots exist to reduce pressure for interface reshaping

Current Surface
---------------

The initial ABI includes:

* engine descriptor query
* submission entry point
* host-event query
* cancellation entry point
* submission release entry point
* extension query entry point

This is intentionally narrow. It is enough to support runtime discovery, submission, lifecycle
control, and future extension lookup without freezing internal C++ implementation details.

Upgrade Discipline
------------------

Future scalable compute expansion should happen behind this ABI rather than by changing the public
deployable interface directly.

That means:

* backend-specific growth should surface as capabilities or queried extensions
* new operations should be appended, not reordered
* engine internals may evolve as long as the ABI table and descriptor semantics remain compatible
* the C++ runtime API can keep improving internally while the deployable engine ABI stays stable

Testing
-------

``test/async_runtime_abi.cpp`` validates the version header, compatibility checks, and initial
engine function-table contract so ABI drift is caught early in normal development.