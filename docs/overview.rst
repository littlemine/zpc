Overview
=============

Zensim is short for Zenus Simulation, which is an open-source C++ library designed for high-performance physics-based simulations. It is built upon a highly efficient parallel computing framework called ZPC, i.e. Zenus Parallel Compute, that offers a c++stl-like and consistent programming interface for multiple compute back-ends including OpenMP, Cuda, etc., and a series of efficient, highly-customizable and commonly used data structures.

Currently, ZPC mainly handles computations within a shared-memory heterogeneous architecture. Hopefully it may extend to a distributed system in the near future.

Zensim is and will be continuously developed and maintained by Zenus Tech Co. Ltd. to provide better support for simulation-related research and development, especially for practical real-world problems. And we welcome more people to take part in the growth and improvement of this project. Please contact us (wxlwxl1993@zju.edu.cn, zhangshinshin@gmail.com) if you are interested.

This repo is currently going through rapid changes, and we do not promise ABI compatibility from commit to commit for the moment.

Current Architecture Direction
------------------------------

The current development direction is to turn the existing assembled product into a modular platform
with an explicit portable foundation, runtime control plane, optional backend or graphics modules,
and higher-level application surfaces built on top of those lower layers.

The most important design notes are:

* :doc:`foundation_layer` for the portable always-on substrate
* :doc:`runtime_core_design` for the async and execution control plane
* :doc:`architecture_and_modularization` for the proposed target graph and dependency direction
* :doc:`platform_and_build_profiles` for the intended delivery-profile matrix

The roadmap remains the strategic entry point, while those design pages carry the more specific
layering and sequencing guidance.