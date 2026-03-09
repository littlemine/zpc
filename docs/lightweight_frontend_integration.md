# Lightweight Frontend Integration

The external reference work around `zs-app` points toward a useful direction for
ZPC, but not one where a single monolithic frontend target is simply absorbed into
the core project.

## Why This Page Exists

This page separates frontend-facing composition from the lower-level platform. It
answers what should remain stable inside ZPC and what should stay frontend-owned in
Python, DSL, editor, and web-facing environments.

## Design Goal

ZPC should expose a lightweight frontend-facing family of composable services rather
than another all-in-one product layer.

The long-term shape is closer to a `zpc::lite`-style package family built on top of
the modular runtime and ABI layers than to a single frontend monolith.

## Why Not A Monolith

Frontend-side concerns span different responsibilities:

- Python-first ergonomics
- graph or DSL authoring
- code generation policy
- kernel or task packaging
- artifact or runtime submission
- editor and tooling UX

Only some of those belong inside ZPC itself. If they are fused into one
heavyweight target, the result undermines the profile-driven and pay-for-what-you-
use direction.

## What Belongs In ZPC

ZPC should own the reusable platform-facing pieces:

- stable runtime-facing C ABI surface
- reflection or export support for discoverable types and capabilities
- transport-friendly task and kernel descriptor conventions
- optional backend compilation or loading service boundaries
- runtime submission and artifact-consumption contracts

These are the pieces that should remain stable across different frontend
implementations.

## What Belongs Frontend-Side

Frontend or app-side layers should retain ownership of:

- Python ergonomics and convenience wrappers
- DSL parsing and graph IR shaping
- frontend-specific code generation policy
- editor UX, property editing, and authoring workflows
- higher-level orchestration or placement policy

This separation lets different frontends target the same stable runtime and
reflection substrate without forcing one editor or DSL stack to define the whole
platform.

## JIT And SPIR-V Boundaries

JIT and SPIR-V-related workflows should remain optional and layered.

That means:

- runtime and descriptor surfaces define what can be submitted or loaded
- backend-specific compilation or loading services remain optional modules
- mobile and constrained profiles can disable those layers cleanly

The important distinction is that frontend-side authoring and backend-side artifact
execution should meet through explicit contracts, not through accidental monolithic
coupling.

## Relationship To ABI Stability

Lightweight frontend integration depends on the same compatibility model as plugins
and deployable runtime engines. The stable C ABI is therefore the right long-term
boundary for version-tolerant, frontend-facing composition.

The result should be:

- narrow stable deployment boundary
- optional higher-level C++ convenience layers for in-tree consumers
- frontend-specific wrappers and authoring environments that stay replaceable

## Sequencing Rule

This integration direction should follow the module and profile split, not precede
it.

In practical terms:

- foundation and runtime layers are stabilized first
- backend and graphics ownership become explicit
- only then does it make sense to formalize a lightweight frontend package family
  on top

That keeps frontend integration from reintroducing monolithic ownership into the
lower layers.

## Current Design Consequence

Frontend convenience should grow on top of stable runtime and ABI contracts, not by
reaching directly into unstable internal ownership. That rule matters just as much
for web-facing and WASM-oriented integrations as it does for Python and DSL layers.

## Related Pages

- [plugin_and_abi_stability.md](plugin_and_abi_stability.md) for the
  binary-boundary model this frontend path relies on
- [application_layer_design.md](application_layer_design.md) for the adjacent GUI
  and editor-oriented consumer layer
- [implementation_roadmap.md](implementation_roadmap.md) for the sequencing that
  should happen before lightweight frontend packaging is formalized
