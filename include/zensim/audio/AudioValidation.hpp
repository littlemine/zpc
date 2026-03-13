#pragma once

#include <string>

#include "zensim/audio/AudioRuntimeModel.hpp"
#include "zensim/execution/ValidationSchema.hpp"

namespace zs {

  enum class AudioValidationSuite : u8 {
    playback,
    mixer,
    streaming,
    spatial,
    dialogue,
    music,
    pipeline,
    stability
  };

  inline const char *audio_validation_suite_name(AudioValidationSuite suite) noexcept {
    switch (suite) {
      case AudioValidationSuite::mixer:
        return "audio.mixer";
      case AudioValidationSuite::streaming:
        return "audio.streaming";
      case AudioValidationSuite::spatial:
        return "audio.spatial";
      case AudioValidationSuite::dialogue:
        return "audio.dialogue";
      case AudioValidationSuite::music:
        return "audio.music";
      case AudioValidationSuite::pipeline:
        return "audio.pipeline";
      case AudioValidationSuite::stability:
        return "audio.stability";
      case AudioValidationSuite::playback:
      default:
        return "audio.playback";
    }
  }

  inline const char *audio_validation_suite_code(AudioValidationSuite suite) noexcept {
    switch (suite) {
      case AudioValidationSuite::mixer:
        return "mix";
      case AudioValidationSuite::streaming:
        return "str";
      case AudioValidationSuite::spatial:
        return "spat";
      case AudioValidationSuite::dialogue:
        return "dlg";
      case AudioValidationSuite::music:
        return "mus";
      case AudioValidationSuite::pipeline:
        return "pipe";
      case AudioValidationSuite::stability:
        return "stab";
      case AudioValidationSuite::playback:
      default:
        return "play";
    }
  }

  inline SmallString make_audio_record_id(AudioValidationSuite suite, const SmallString &scenarioId,
                                          const SmallString &profile,
                                          const SmallString &platform) noexcept {
    SmallString recordId{"aud."};
    recordId = recordId + SmallString{audio_validation_suite_code(suite)};
    if (scenarioId.size()) recordId = recordId + SmallString{"."} + scenarioId;
    if (profile.size()) recordId = recordId + SmallString{"."} + profile;
    if (platform.size()) recordId = recordId + SmallString{"."} + platform;
    return recordId;
  }

  inline constexpr const char *audio_metadata_sample_rate = "audio.sampleRate";
  inline constexpr const char *audio_metadata_buffer_frames = "audio.bufferFrames";
  inline constexpr const char *audio_metadata_channels = "audio.channels";
  inline constexpr const char *audio_metadata_bits_per_sample = "audio.bitsPerSample";
  inline constexpr const char *audio_metadata_content = "audio.content";
  inline constexpr const char *audio_metadata_bus = "audio.bus";
  inline constexpr const char *audio_metadata_spatial = "audio.spatial";
  inline constexpr const char *audio_metadata_playback = "audio.playback";
  inline constexpr const char *audio_metadata_priority = "audio.priority";
  inline constexpr const char *audio_metadata_concurrency = "audio.concurrency";
  inline constexpr const char *audio_metadata_voice_physical = "audio.voice.physical";
  inline constexpr const char *audio_metadata_voice_virtual = "audio.voice.virtual";
  inline constexpr const char *audio_metadata_stream_limit = "audio.stream.limit";
  inline constexpr const char *audio_metadata_mem_preload = "audio.mem.preload";
  inline constexpr const char *audio_metadata_mem_stream = "audio.mem.stream";
  inline constexpr const char *audio_metadata_mem_decode = "audio.mem.decode";
  inline constexpr const char *audio_metadata_bus_count = "audio.bus.count";

  inline void set_audio_format_metadata(ValidationRecord &record,
                                        const AudioFormatDescriptor &format) {
    record.set_metadata(audio_metadata_sample_rate, audio_u64_string(format.sampleRate));
    record.set_metadata(audio_metadata_buffer_frames, audio_u64_string(format.framesPerBlock));
    record.set_metadata(audio_metadata_channels, audio_u64_string(format.channelCount));
    record.set_metadata(audio_metadata_bits_per_sample, audio_u64_string(format.bitsPerSample));
  }

  inline void set_audio_playback_metadata(ValidationRecord &record,
                                          const AudioPlaybackPolicy &policy) {
    record.set_metadata(audio_metadata_content, audio_content_kind_name(policy.contentKind));
    record.set_metadata(audio_metadata_bus, audio_bus_kind_name(policy.busKind));
    record.set_metadata(audio_metadata_spatial, audio_spatial_mode_name(policy.spatialMode));
    record.set_metadata(audio_metadata_playback, audio_playback_mode_name(policy.playbackMode));
    record.set_metadata(audio_metadata_priority, audio_u64_string(policy.priority));
    record.set_metadata(audio_metadata_concurrency, audio_u64_string(policy.concurrencyLimit));
  }

  inline void set_audio_runtime_metadata(ValidationSuiteReport &report,
                                         const AudioRuntimeTopology &topology) {
    report.set_metadata(audio_metadata_voice_physical,
                        audio_u64_string(topology.voiceBudget.maxPhysicalVoices));
    report.set_metadata(audio_metadata_voice_virtual,
                        audio_u64_string(topology.voiceBudget.maxVirtualVoices));
    report.set_metadata(audio_metadata_stream_limit,
                        audio_u64_string(topology.voiceBudget.maxConcurrentStreams));
    report.set_metadata(audio_metadata_mem_preload,
                        audio_u64_string(topology.memoryBudget.preloadedBytesBudget));
    report.set_metadata(audio_metadata_mem_stream,
                        audio_u64_string(topology.memoryBudget.streamingCacheBytesBudget));
    report.set_metadata(audio_metadata_mem_decode,
                        audio_u64_string(topology.memoryBudget.decodeScratchBytesBudget));
    report.set_metadata(audio_metadata_bus_count, audio_u64_string(topology.buses.size()));
  }

}  // namespace zs
