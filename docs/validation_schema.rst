Validation Schema
=================

ZPC needs a stable, machine-readable result model that can be shared by tests,
benchmarks, validation gates, future CLI output, and future MCP tools. The first
schema is defined in ``include/zensim/execution/ValidationSchema.hpp``.

Goals
-----

The schema is designed to:

* represent validation and benchmark results with one common record format
* carry enough backend, executor, and target metadata for automation tools
* support threshold-based self-upgrade checks
* stay decoupled from transport and presentation layers

Core Types
----------

``ValidationRecordKind``
  Distinguishes validation records from benchmark records.

``ValidationOutcome``
  Stores the outcome of a record: pass, fail, skip, or error.

``ValidationThreshold``
  Describes acceptance rules for a numeric measurement. The current modes are:

  * none
  * less_equal
  * greater_equal
  * inclusive_range

``ValidationMeasurement``
  Stores one named metric with a unit, numeric value, and threshold.

``ValidationRecord``
  Stores a single validation or benchmark result with metadata fields such as:

  * recordId
  * suite
  * name
  * backend
  * executor
  * target
  * note
  * durationNs
  * measurements

``ValidationSuiteReport``
  Aggregates records for one suite and exposes a refreshed summary.

Schema Version
--------------

The initial schema version string is ``zpc.validation.v1``.

Stable Record Id
----------------

``ValidationRecord`` now supports an optional ``recordId`` field.

Current guidance:

* producers may omit ``recordId`` during the transition period
* comparison prefers ``recordId`` when both compared records provide one
* comparison falls back to the existing identity tuple when either side does not provide one

This preserves compatibility for current tests while allowing future persisted baselines, CLI
artifacts, and MCP queries to adopt explicit stable identities.

Planned Usage
-------------

This schema is intended to become the common payload model for:

* focused CTest-driven validation passes
* benchmark pipelines
* CLI result export
* MCP result queries
* self-upgrade acceptance gates based on stored baselines

Formatting Layer
----------------

The first formatting utilities are defined in ``include/zensim/execution/ValidationFormat.hpp``.
They provide:

* deterministic JSON serialization for records and suite reports
* a stable text summary format suitable for CLI-oriented status output

This keeps the validation payload model separate from transport-specific CLI and MCP entry points.

Comparison Layer
----------------

Baseline comparison utilities are defined in ``include/zensim/execution/ValidationCompare.hpp``.
They provide record matching and regression or improvement classification for self-upgrade checks.

The current matching identity is based on:

* ``recordId`` when both sides provide it
* otherwise the tuple of suite, name, backend, executor, target, and kind

This hybrid rule keeps existing producers working while allowing future workflows to migrate toward
intentional stable identities.