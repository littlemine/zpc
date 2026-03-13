#include <cassert>
#include <string>

#include "zensim/audio/AudioValidation.hpp"

int main() {
  using namespace zs;

  ValidationRecord record{};
  record.suite = audio_validation_suite_name(AudioValidationSuite::playback);
  record.name = "bootstrap";
  record.recordId = make_audio_record_id(AudioValidationSuite::playback, "A01", "rt", "win");

  AudioFormatDescriptor format{};
  AudioPlaybackPolicy dialogue = make_default_audio_playback_policy(AudioContentKind::dialogue);
  set_audio_format_metadata(record, format);
  set_audio_playback_metadata(record, dialogue);

  assert(std::string(record.recordId.asChars()) == "aud.play.A01.rt.win");
  assert(std::string(record.metadata_value(audio_metadata_sample_rate).asChars()) == "48000");
  assert(std::string(record.metadata_value(audio_metadata_buffer_frames).asChars()) == "256");
  assert(std::string(record.metadata_value(audio_metadata_content).asChars()) == "dialogue");
  assert(std::string(record.metadata_value(audio_metadata_playback).asChars()) == "streamed");

  ValidationSuiteReport report{};
  report.suite = audio_validation_suite_name(AudioValidationSuite::playback);
  AudioRuntimeTopology topology = make_default_audio_runtime_topology();
  set_audio_runtime_metadata(report, topology);

  assert(std::string(report.metadata_value(audio_metadata_voice_physical).asChars()) == "64");
  assert(std::string(report.metadata_value(audio_metadata_voice_virtual).asChars()) == "128");
  assert(std::string(report.metadata_value(audio_metadata_stream_limit).asChars()) == "8");
  assert(std::string(report.metadata_value(audio_metadata_bus_count).asChars()) == "7");

  return 0;
}
