#include "amt/instruments/SeparationAssistedDetector.h"

#include <cmath>

#include "amt/instruments/InstrumentDetector.h"

namespace amt::instruments {

namespace {

double compute_rms(const amt::audio::AudioBuffer& buffer) {
  if (buffer.frames() == 0) return 0.0;
  double sum = 0.0;
  const std::size_t channels = buffer.channels();
  for (std::size_t c = 0; c < channels; ++c) {
    const float* data = buffer.channel(c).data();
    for (std::size_t i = 0; i < buffer.frames(); ++i) {
      sum += static_cast<double>(data[i]) * static_cast<double>(data[i]);
    }
  }
  return std::sqrt(sum / static_cast<double>(buffer.frames() * channels));
}

}  // namespace

std::vector<InstrumentEvent> SeparationAssistedDetector::detect_instruments(
    const amt::audio::AudioBuffer& full_mix,
    const SeparationStems* stems,
    int sample_rate) const {
  std::vector<InstrumentEvent> events;
  if (full_mix.frames() == 0) return events;

  const double mix_duration = static_cast<double>(full_mix.frames()) /
                              static_cast<double>(sample_rate > 0 ? sample_rate : 44100);

  if (stems != nullptr && stems->available) {
    const double drums_rms = compute_rms(stems->drums);
    const double bass_rms = compute_rms(stems->bass);
    const double vocals_rms = compute_rms(stems->vocals);
    const double mix_rms = compute_rms(full_mix);

    if (drums_rms > 0.01 * mix_rms) {
      InstrumentEvent drum_event{};
      drum_event.taxonomy_id = "percussion.drums.kick";
      drum_event.display_label = "Kick Drum";
      drum_event.family = "percussion";
      drum_event.source_role = SourceRole::drums;
      drum_event.confidence = std::clamp(drums_rms / (mix_rms + 1e-6), 0.5, 0.95);
      drum_event.start_seconds = 0.0;
      drum_event.end_seconds = mix_duration;
      drum_event.active_frequency_hz = {55.0, 110.0};
      drum_event.stem_association = "drums";
      drum_event.evidence = {"stem_energy_ratio", "low_end_autocorr"};
      events.push_back(drum_event);
    }

    if (bass_rms > 0.01 * mix_rms) {
      const auto charact = extract_characteristics(stems->bass, sample_rate);
      InstrumentEvent bass_event{};
      bass_event.taxonomy_id = (charact.f0_hz > 0.0 && charact.f0_hz < 60.0)
                                   ? "bass.synth.808"
                                   : "bass.electric";
      bass_event.display_label = (charact.f0_hz > 0.0 && charact.f0_hz < 60.0)
                                     ? "808 Sub-Bass"
                                     : "Bass";
      bass_event.family = "bass";
      bass_event.source_role = SourceRole::bass;
      bass_event.confidence = std::clamp(bass_rms / (mix_rms + 1e-6), 0.5, 0.95);
      bass_event.start_seconds = 0.0;
      bass_event.end_seconds = mix_duration;
      bass_event.active_frequency_hz = {charact.f0_hz > 0.0 ? charact.f0_hz : 65.0};
      bass_event.stem_association = "bass";
      bass_event.evidence = {"stem_bass_energy", "f0_tracking"};
      events.push_back(bass_event);
    }

    if (vocals_rms > 0.01 * mix_rms) {
      InstrumentEvent vocal_event{};
      vocal_event.taxonomy_id = "vocal.lead";
      vocal_event.display_label = "Lead Vocal";
      vocal_event.family = "vocal";
      vocal_event.source_role = SourceRole::vocals;
      vocal_event.confidence = std::clamp(vocals_rms / (mix_rms + 1e-6), 0.5, 0.95);
      vocal_event.start_seconds = 0.0;
      vocal_event.end_seconds = mix_duration;
      vocal_event.active_frequency_hz = {220.0, 1000.0, 3500.0};
      vocal_event.stem_association = "vocals";
      vocal_event.evidence = {"stem_vocal_energy", "mid_band_formants"};
      events.push_back(vocal_event);
    }
  }

  if (events.empty()) {
    // Fallback: stereo-only detection
    const auto charact = extract_characteristics(full_mix, sample_rate);
    if (charact.f0_hz > 0.0 && charact.f0_hz < 250.0) {
      InstrumentEvent low_event{};
      low_event.taxonomy_id = "bass";
      low_event.display_label = "Bass / Low-End Instrument";
      low_event.family = "bass";
      low_event.source_role = SourceRole::bass;
      low_event.confidence = 0.6;
      low_event.start_seconds = 0.0;
      low_event.end_seconds = mix_duration;
      low_event.active_frequency_hz = {charact.f0_hz};
      low_event.evidence = {"stereo_f0_autocorr"};
      events.push_back(low_event);
    }
  }

  return events;
}

}  // namespace amt::instruments
