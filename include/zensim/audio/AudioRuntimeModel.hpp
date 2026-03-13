#pragma once

#include <vector>

#include "zensim/audio/AudioFoundation.hpp"

namespace zs {

  struct AudioBusDescriptor {
    SmallString busId{};
    SmallString parentBusId{};
    AudioBusKind kind{AudioBusKind::master};
    i16 mixOrder{0};
    bool allowEffects{true};
    bool allowDucking{false};
    bool meterEnabled{true};

    bool valid() const noexcept { return busId.size() != 0; }
  };

  struct AudioRuntimeTopology {
    AudioVoiceBudget voiceBudget{};
    AudioMemoryBudget memoryBudget{};
    std::vector<AudioBusDescriptor> buses{};

    bool valid() const noexcept {
      if (!voiceBudget.valid() || !memoryBudget.valid() || buses.empty()) return false;
      const auto *master = find_bus("master");
      return master && master->kind == AudioBusKind::master;
    }

    const AudioBusDescriptor *find_bus(const SmallString &busId) const noexcept {
      for (const auto &bus : buses)
        if (bus.busId == busId) return &bus;
      return nullptr;
    }

    bool has_bus_kind(AudioBusKind kind) const noexcept {
      for (const auto &bus : buses)
        if (bus.kind == kind) return true;
      return false;
    }
  };

  inline AudioBusDescriptor make_audio_bus_descriptor(const SmallString &busId, AudioBusKind kind,
                                                      const SmallString &parentBusId = {},
                                                      i16 mixOrder = 0,
                                                      bool allowDucking = false) noexcept {
    AudioBusDescriptor descriptor{};
    descriptor.busId = busId;
    descriptor.parentBusId = parentBusId;
    descriptor.kind = kind;
    descriptor.mixOrder = mixOrder;
    descriptor.allowDucking = allowDucking;
    return descriptor;
  }

  inline AudioRuntimeTopology make_default_audio_runtime_topology() {
    AudioRuntimeTopology topology{};
    topology.buses.reserve(7);
    topology.buses.push_back(make_audio_bus_descriptor("master", AudioBusKind::master));
    topology.buses.push_back(make_audio_bus_descriptor("sfx", AudioBusKind::sfx, "master", 10));
    topology.buses.push_back(make_audio_bus_descriptor("ui", AudioBusKind::ui, "master", 20));
    topology.buses.push_back(
        make_audio_bus_descriptor("dialogue", AudioBusKind::dialogue, "master", 30, true));
    topology.buses.push_back(
        make_audio_bus_descriptor("music", AudioBusKind::music, "master", 40, true));
    topology.buses.push_back(
        make_audio_bus_descriptor("ambience", AudioBusKind::ambience, "master", 50, true));
    topology.buses.push_back(
        make_audio_bus_descriptor("reverb", AudioBusKind::reverb, "master", 60));
    return topology;
  }

}  // namespace zs
