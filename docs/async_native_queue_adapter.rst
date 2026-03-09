Async Native Queue Adapter
==========================

The native queue adapter layer in
``include/zensim/execution/AsyncNativeQueueAdapter.hpp`` is the first bridge from the generic
async runtime into backend-native queue or stream handles.

Purpose
-------

The runtime already understands submission order, cancellation, suspend or resume, and endpoint
metadata. The native queue adapter adds the next piece:

* a uniform descriptor for a real backend queue or stream
* a uniform hook table for queue handle lookup, synchronization, event recording, and event waits
* an executor that projects those hooks into the async runtime without forcing every backend to
  reimplement control-plane behavior

Current Shape
-------------

The adapter is split into three parts:

``AsyncNativeQueueDescriptor``
  A reflected, primitive-field descriptor suitable for configuration surfaces, inspection tools,
  and future Python-side orchestration.

``AsyncNativeQueueBinding``
  A storage-backed bundle of native operations and opaque backend state.

``AsyncNativeQueueExecutor``
  An ``AsyncExecutor`` implementation that fills in endpoint metadata from the native binding,
  runs the submission step, and optionally records or synchronizes backend progress.

Backend Binders
---------------

CUDA binder support is available when CUDA is enabled:

* queue handle from ``Cuda::CudaContext::streamSpare``
* signal handle from ``Cuda::CudaContext::eventSpare``
* record from ``recordEventSpare``
* wait from ``spareStreamWaitForEvent``
* synchronize from ``syncStreamSpare``

Vulkan binder support is available when Vulkan is enabled:

* queue handle from ``VulkanContext::getQueue``
* synchronization through ``vk::Queue::waitIdle``

This means CUDA currently exposes a richer native event bridge while Vulkan currently exposes the
queue bridge and synchronization bridge. That matches the present state of the in-tree backend
APIs rather than inventing fake parity.

Reflection and Python
---------------------

``AsyncNativeQueueDescriptor`` is annotated for the zpc reflection pipeline because queue adapter
selection and configuration are likely to be driven by human-facing tooling. The root CMake file
now includes this header alongside the backend-profile header in the optional reflection build for
``zpc_py_interop``.