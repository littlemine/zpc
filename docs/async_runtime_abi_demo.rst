Async Runtime ABI Demo
======================

The async runtime ABI now has a small human-observable demo artifact in
``test/async_runtime_abi_demo.cpp``.

Purpose
-------

The tests lock correctness, but they are not optimized for inspection. The demo target exists so a
human can quickly inspect the current stable ABI surface and confirm that the engine, validation,
and native queue extensions all behave coherently in one run.

Current Demo Coverage
---------------------

The demo prints:

* engine descriptor name, build id, and capability bits
* host-submit extension discovery and one completed host task
* validation-report extension discovery and one published validation report summary plus JSON blob
* native-queue extension discovery, one fake native queue submission, and one wait-on-signal call

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
* a host task callback line and a completed host event line
* a validation summary line and a JSON line containing ``zpc.validation.v1``
* a native event line showing non-zero signal token and incremented sync, record, and wait counts

Assessment Use
--------------

This target is intended for quick human assessment during interface reviews, upgrade checks, and
parallel development handoff. It is deliberately deterministic and host-only so it remains useful
even when CUDA or Vulkan are disabled in the current build configuration.