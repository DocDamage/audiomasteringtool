#include "amt/instruments/InstrumentTaxonomy.h"

#include <algorithm>
#include <set>

namespace amt::instruments {
namespace {
using N = InstrumentTaxonomyNode;
const std::vector<N> kTaxonomy = {
    {"unknown", "Unknown", "", TaxonomyLevel::source_role, SourceRole::unknown, "", false, true, ModelSupport::production, 0.0},
    {"drums", "Drums / percussion", "unknown", TaxonomyLevel::source_role, SourceRole::drums, "drums", false, true, ModelSupport::production, 0.55},
    {"bass_source", "Bass source", "unknown", TaxonomyLevel::source_role, SourceRole::bass, "bass", false, true, ModelSupport::production, 0.55},
    {"vocals", "Vocals", "unknown", TaxonomyLevel::source_role, SourceRole::vocals, "vocals", false, true, ModelSupport::production, 0.55},
    {"tonal_source", "Tonal source", "unknown", TaxonomyLevel::source_role, SourceRole::tonal, "tonal", false, true, ModelSupport::production, 0.55},
    {"kick", "Kick", "drums", TaxonomyLevel::instrument, SourceRole::drums, "drums", true, false, ModelSupport::research, 0.80},
    {"snare", "Snare", "drums", TaxonomyLevel::instrument, SourceRole::drums, "drums", true, false, ModelSupport::research, 0.80},
    {"clap", "Clap", "drums", TaxonomyLevel::instrument, SourceRole::drums, "drums", false, true, ModelSupport::research, 0.80},
    {"closed_hihat", "Closed hi-hat", "drums", TaxonomyLevel::instrument, SourceRole::drums, "drums", false, true, ModelSupport::research, 0.82},
    {"open_hihat", "Open hi-hat", "drums", TaxonomyLevel::instrument, SourceRole::drums, "drums", false, true, ModelSupport::research, 0.82},
    {"sub_808", "808 / sub-bass", "bass_source", TaxonomyLevel::instrument, SourceRole::bass, "bass", true, false, ModelSupport::research, 0.82},
    {"synth_bass", "Synth bass", "bass_source", TaxonomyLevel::instrument, SourceRole::bass, "bass", true, false, ModelSupport::research, 0.80},
    {"electric_bass", "Electric bass", "bass_source", TaxonomyLevel::instrument, SourceRole::bass, "bass", true, false, ModelSupport::research, 0.80},
    {"acoustic_piano", "Acoustic piano", "tonal_source", TaxonomyLevel::instrument, SourceRole::tonal, "keyboards", true, false, ModelSupport::research, 0.82},
    {"electric_piano", "Electric piano", "tonal_source", TaxonomyLevel::family, SourceRole::tonal, "keyboards", false, true, ModelSupport::research, 0.70},
    {"rhodes", "Rhodes", "electric_piano", TaxonomyLevel::instrument, SourceRole::tonal, "keyboards", true, false, ModelSupport::research, 0.85},
    {"organ", "Organ", "tonal_source", TaxonomyLevel::instrument, SourceRole::tonal, "keyboards", true, false, ModelSupport::research, 0.82},
    {"acoustic_guitar", "Acoustic guitar", "tonal_source", TaxonomyLevel::instrument, SourceRole::tonal, "guitars", true, false, ModelSupport::research, 0.82},
    {"electric_guitar", "Electric guitar", "tonal_source", TaxonomyLevel::instrument, SourceRole::tonal, "guitars", true, false, ModelSupport::research, 0.82},
    {"strings", "Strings", "tonal_source", TaxonomyLevel::family, SourceRole::tonal, "strings", false, true, ModelSupport::research, 0.72},
    {"brass", "Brass", "tonal_source", TaxonomyLevel::family, SourceRole::tonal, "brass", false, true, ModelSupport::research, 0.72},
    {"synth_lead", "Synth lead", "tonal_source", TaxonomyLevel::instrument, SourceRole::tonal, "synths", true, false, ModelSupport::research, 0.82},
    {"synth_pad", "Synth pad", "tonal_source", TaxonomyLevel::instrument, SourceRole::tonal, "synths", true, false, ModelSupport::research, 0.82},
    {"lead_vocal", "Lead vocal", "vocals", TaxonomyLevel::instrument, SourceRole::vocals, "vocals", true, false, ModelSupport::research, 0.82},
    {"backing_vocal", "Backing vocal", "vocals", TaxonomyLevel::instrument, SourceRole::vocals, "vocals", false, true, ModelSupport::research, 0.82},
};
}
const std::vector<N>& instrument_taxonomy() { return kTaxonomy; }
const N* find_taxonomy_node(const std::string& id) { const auto it = std::find_if(kTaxonomy.begin(), kTaxonomy.end(), [&](const N& n){ return n.id == id; }); return it == kTaxonomy.end() ? nullptr : &*it; }
bool validate_instrument_taxonomy(std::string& error) {
  error.clear(); std::set<std::string> ids;
  for (const auto& node : kTaxonomy) { if (node.id.empty() || !ids.insert(node.id).second || node.minimum_confidence < 0.0 || node.minimum_confidence > 1.0) { error = "instrument taxonomy has invalid or duplicate node"; return false; } }
  for (const auto& node : kTaxonomy) { std::set<std::string> seen; auto current = &node; while (!current->parent_id.empty()) { if (!seen.insert(current->id).second) { error = "instrument taxonomy contains a cycle"; return false; } current = find_taxonomy_node(current->parent_id); if (!current) { error = "instrument taxonomy has missing parent"; return false; } } }
  return true;
}
std::string source_role_name(const SourceRole role) { switch (role) { case SourceRole::drums:return "drums"; case SourceRole::bass:return "bass"; case SourceRole::vocals:return "vocals"; case SourceRole::tonal:return "tonal"; case SourceRole::other:return "other"; case SourceRole::unknown:return "unknown"; } return "unknown"; }
}  // namespace amt::instruments
