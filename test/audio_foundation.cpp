#include <cassert>

#include "zensim/audio/AudioRuntimeModel.hpp"

int main() {
  using namespace zs;

  AudioFormatDescriptor format{};
  assert(format.valid());
  assert(format.bytes_per_sample() == 2);
  assert(format.bytes_per_frame() == 4);
  assert(format.bytes_per_block() == 1024);
  assert(format.frames_for_ms(10) == 480);
  assert(format.ms_for_frames(480) == 10);

  AudioPlaybackPolicy sfx = make_default_audio_playback_policy(AudioContentKind::sfx);
  assert(sfx.valid());
  assert(sfx.busKind == AudioBusKind::sfx);
  assert(sfx.playbackMode == AudioPlaybackMode::preloaded);
  assert(sfx.spatialMode == AudioSpatialMode::three_d);

  AudioPlaybackPolicy music = make_default_audio_playback_policy(AudioContentKind::music);
  assert(music.busKind == AudioBusKind::music);
  assert(music.playbackMode == AudioPlaybackMode::streamed);
  assert(music.loop);
  assert(music.spatialMode == AudioSpatialMode::two_d);

  AudioRuntimeTopology topology = make_default_audio_runtime_topology();
  assert(topology.valid());
  assert(topology.buses.size() == 7);
  assert(topology.has_bus_kind(AudioBusKind::dialogue));
  assert(topology.find_bus("master") != nullptr);
  assert(topology.find_bus("reverb") != nullptr);
  assert(topology.voiceBudget.valid());
  assert(topology.memoryBudget.valid());
  assert(topology.memoryBudget.total_bytes() > topology.memoryBudget.preloadedBytesBudget);

  return 0;
}
