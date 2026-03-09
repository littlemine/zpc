Async Runtime ABI Demo
======================

The async runtime ABI now has a small human-observable demo artifact in
``test/async_runtime_abi_demo.cpp``.

Purpose
-------

The tests lock correctness, but they are not optimized for inspection. The demo target exists so a
human can quickly inspect the current stable ABI surface and confirm that the engine, validation,
resource-manager, and native queue extensions all behave coherently in one run.

Current Demo Coverage
---------------------

The demo prints:

* engine descriptor name, build id, and capability bits
* host-submit extension discovery, one completed host task, and one dependent host task chained by
   a submission-event token
* validation-report extension discovery and one published validation report summary plus JSON blob
* resource-manager extension discovery, one registered resource, one lease, one explicit
   maintenance submission, one stale sweep, one retirement collection sequence, and multiple live
   resource-info snapshots showing dirty or stale or lease-count transitions, plus one dependency-
   aware maintenance submission chained from a runtime submission-event token
* native-queue extension discovery, one fake native queue submission with both a runtime
   prerequisite token and a foreign native-signal dependency, and one wait-on-signal call

How To Run
----------

Build the target:

.. code-block:: powershell

   cmake --build build-copilot --target asyncruntimeabidemo

Run the executable from the build tree:

.. code-block:: powershell

   .\build-copilot\test\asyncruntimeabidemo.exe

What To Look For
----------------

The output should show, in order:

* an engine line with the expected engine name and capability mask
* a host task callback line, a completed host event line, and a dependent-host line showing the
   prerequisite token that was consumed
* a validation summary line and a JSON line containing ``zpc.validation.v1``
* a resource extension line reporting version ``1.2`` followed by resource registration, initial
   info, lease-state, dirty-state, post-maintenance, dependent-maintenance, stale-sweep, and
   retirement lines with non-zero handles or counts
* a native extension line reporting minor version ``1``
* a native event line showing non-zero signal token, incremented sync or record or wait counts,
   and distinct ``first_wait`` or ``last_wait`` tokens so the pre-submit foreign wait and explicit
   wait call are both visible

Assessment Use
--------------

This target is intended for quick human assessment during interface reviews, upgrade checks, and
parallel development handoff. It is deliberately deterministic and host-only so it remains useful
even when CUDA or Vulkan are disabled in the current build configuration.