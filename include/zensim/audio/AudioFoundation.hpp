#pragma once

#include <string>

#include "zensim/TypeAlias.hpp"
#include "zensim/types/SmallVector.hpp"

namespace zs {

  enum class AudioContentKind : u8 { unknown, sfx, ui, dialogue, music, ambience, cinematic, custom };
  enum class AudioSpatialMode : u8 { two_d, three_d };
  enum class AudioPlaybackMode : u8 { preloaded, streamed };
  enum class AudioBusKind : u8 { master, sfx, ui, dialogue, music, ambience, reverb, custom };

  inline const char *audio_content_kind_name(AudioContentKind kind) noexcept {
    switch (kind) {
      case AudioContentKind::sfx:
        return "sfx";
      case AudioContentKind::ui:
        return "ui";
      case AudioContentKind::dialogue:
        return "dialogue";
      case AudioContentKind::music:
        return "music";
      case AudioContentKind::ambience:
        return "ambience";
      case AudioContentKind::cinematic:
        return "cinematic";
      case AudioContentKind::custom:
        return "custom";
      default:
        return "unknown";
    }
  }

  inline const char *audio_spatial_mode_name(AudioSpatialMode mode) noexcept {
    switch (mode) {
      case AudioSpatialMode::three_d:
        return "3d";
      default:
        return "2d";
    }
  }

  inline const char *audio_playback_mode_name(AudioPlaybackMode mode) noexcept {
    switch (mode) {
      case AudioPlaybackMode::streamed:
        return "streamed";
      default:
        return "preloaded";
    }
  }

  inline const char *audio_bus_kind_name(AudioBusKind kind) noexcept {
    switch (kind) {
      case AudioBusKind::master:
        return "master";
      case AudioBusKind::sfx:
        return "sfx";
      case AudioBusKind::ui:
        return "ui";
      case AudioBusKind::dialogue:
        return "dialogue";
      case AudioBusKind::music:
        return "music";
      case AudioBusKind::ambience:
        return "ambience";
      case AudioBusKind::reverb:
        return "reverb";
      case AudioBusKind::custom:
        return "custom";
      default:
        return "master";
    }
  }

  inline constexpr AudioBusKind default_audio_bus_kind(AudioContentKind kind) noexcept {
    switch (kind) {
      case AudioContentKind::ui:
        return AudioBusKind::ui;
      case AudioContentKind::dialogue:
        return AudioBusKind::dialogue;
      case AudioContentKind::music:
        return AudioBusKind::music;
      case AudioContentKind::ambience:
        return AudioBusKind::ambience;
      case AudioContentKind::cinematic:
        return AudioBusKind::dialogue;
      case AudioContentKind::sfx:
      case AudioContentKind::custom:
      case AudioContentKind::unknown:
      default:
        return AudioBusKind::sfx;
    }
  }

  inline constexpr AudioPlaybackMode default_audio_playback_mode(AudioContentKind kind) noexcept {
    switch (kind) {
      case AudioContentKind::dialogue:
      case AudioContentKind::music:
      case AudioContentKind::ambience:
      case AudioContentKind::cinematic:
        return AudioPlaybackMode::streamed;
      default:
        return AudioPlaybackMode::preloaded;
    }
  }

  struct AudioFormatDescriptor {
    u32 sampleRate{48000};
    u16 channelCount{2};
    u16 bitsPerSample{16};
    u16 framesPerBlock{256};
    bool floatingPoint{false};
    bool interleaved{true};

    bool valid() const noexcept {
      return sampleRate != 0 && channelCount != 0 && bitsPerSample != 0 && framesPerBlock != 0
          && (bitsPerSample % 8u) == 0u;
    }

    u32 bytes_per_sample() const noexcept { return valid() ? bitsPerSample / 8u : 0u; }
    u32 bytes_per_frame() const noexcept { return bytes_per_sample() * channelCount; }
    u32 bytes_per_block() const noexcept { return bytes_per_frame() * framesPerBlock; }

    u32 frames_for_ms(u32 milliseconds) const noexcept {
      return valid() ? static_cast<u32>((static_cast<u64>(sampleRate) * milliseconds + 999u) / 1000u)
                     : 0u;
    }

    u32 ms_for_frames(u32 frameCount) const noexcept {
      return valid() ? static_cast<u32>((static_cast<u64>(frameCount) * 1000u + sampleRate - 1u)
                                        / sampleRate)
                     : 0u;
    }
  };

  struct AudioVoiceBudget {
    u16 maxPhysicalVoices{64};
    u16 maxVirtualVoices{128};
    u16 maxConcurrentStreams{8};
    u16 maxVoicesPerEmitter{2};

    bool valid() const noexcept {
      return maxPhysicalVoices != 0 && maxVirtualVoices >= maxPhysicalVoices
          && maxConcurrentStreams != 0 && maxVoicesPerEmitter != 0;
    }
  };

  struct AudioMemoryBudget {
    u64 preloadedBytesBudget{64ull * 1024ull * 1024ull};
    u64 streamingCacheBytesBudget{32ull * 1024ull * 1024ull};
    u64 decodeScratchBytesBudget{8ull * 1024ull * 1024ull};

    bool valid() const noexcept {
      return preloadedBytesBudget != 0 && streamingCacheBytesBudget != 0
          && decodeScratchBytesBudget != 0;
    }

    u64 total_bytes() const noexcept {
      return preloadedBytesBudget + streamingCacheBytesBudget + decodeScratchBytesBudget;
    }
  };

  struct AudioPlaybackPolicy {
    AudioContentKind contentKind{AudioContentKind::unknown};
    AudioSpatialMode spatialMode{AudioSpatialMode::two_d};
    AudioPlaybackMode playbackMode{AudioPlaybackMode::preloaded};
    AudioBusKind busKind{AudioBusKind::sfx};
    u8 priority{128};
    u16 concurrencyLimit{4};
    bool allowVirtualization{true};
    bool enableDucking{false};
    bool loop{false};

    bool valid() const noexcept { return concurrencyLimit != 0; }
  };

  inline AudioPlaybackPolicy make_default_audio_playback_policy(AudioContentKind kind) noexcept {
    AudioPlaybackPolicy policy{};
    policy.contentKind = kind;
    policy.busKind = default_audio_bus_kind(kind);
    policy.playbackMode = default_audio_playback_mode(kind);
    policy.spatialMode = (kind == AudioContentKind::ui || kind == AudioContentKind::music)
                             ? AudioSpatialMode::two_d
                             : AudioSpatialMode::three_d;

    switch (kind) {
      case AudioContentKind::ui:
        policy.priority = 224;
        policy.concurrencyLimit = 2;
        break;
      case AudioContentKind::dialogue:
        policy.priority = 240;
        policy.concurrencyLimit = 4;
        policy.enableDucking = true;
        break;
      case AudioContentKind::music:
        policy.priority = 192;
        policy.concurrencyLimit = 2;
        policy.loop = true;
        break;
      case AudioContentKind::ambience:
        policy.priority = 96;
        policy.concurrencyLimit = 16;
        break;
      case AudioContentKind::cinematic:
        policy.priority = 248;
        policy.concurrencyLimit = 2;
        policy.enableDucking = true;
        break;
      case AudioContentKind::sfx:
        policy.priority = 160;
        policy.concurrencyLimit = 8;
        break;
      case AudioContentKind::custom:
      case AudioContentKind::unknown:
      default:
        policy.priority = 128;
        policy.concurrencyLimit = 4;
        break;
    }

    return policy;
  }

  inline SmallString audio_u64_string(u64 value) { return SmallString{std::to_string(value)}; }

}  // namespace zs
