// Copyright (c) zs contributors. Licensed under the MIT License.
// Gpu.hpp - Umbrella header for the API-agnostic GPU abstraction layer.
//
// Include this single header to get all gpu:: types, descriptions, and the
// Device interface. Backend-specific headers (GpuVkMapping.hpp, etc.) are
// NOT included here — they are only needed by backend implementations.

#pragma once

#include "GpuTypes.hpp"
#include "GpuDescriptors.hpp"
#include "GpuDevice.hpp"
