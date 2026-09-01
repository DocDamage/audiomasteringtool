#include "amt/project/ExportRecipes.h"

#include <algorithm>

namespace amt::project {

const std::vector<ExportRecipe>& builtin_export_recipes() {
  static const std::vector<ExportRecipe> recipes = {
      {.id = ExportRecipeId::studio_master,
       .key = "studio_master",
       .name = "Studio Master",
       .description = "24-bit WAV at the source sample rate.",
       .extension = ".wav",
       .container = amt::codec::AudioContainer::wav,
       .sample_format = amt::codec::AudioSampleFormat::pcm24,
       .sample_rate = std::nullopt,
       .dither_when_reducing_integer_depth = true,
       .available = true},
      {.id = ExportRecipeId::distribution_wav,
       .key = "distribution_wav",
       .name = "Distribution WAV",
       .description = "24-bit WAV at the source sample rate for distribution delivery.",
       .extension = ".wav",
       .container = amt::codec::AudioContainer::wav,
       .sample_format = amt::codec::AudioSampleFormat::pcm24,
       .sample_rate = std::nullopt,
       .dither_when_reducing_integer_depth = true,
       .available = true},
      {.id = ExportRecipeId::distribution_flac,
       .key = "distribution_flac",
       .name = "Distribution FLAC",
       .description = "Lossless 24-bit FLAC at the source sample rate.",
       .extension = ".flac",
       .container = amt::codec::AudioContainer::flac,
       .sample_format = amt::codec::AudioSampleFormat::pcm24,
       .sample_rate = std::nullopt,
       .dither_when_reducing_integer_depth = true,
       .available = true},
      {.id = ExportRecipeId::cd,
       .key = "cd",
       .name = "CD",
       .description = "16-bit / 44.1 kHz WAV with one intentional final dither boundary.",
       .extension = ".wav",
       .container = amt::codec::AudioContainer::wav,
       .sample_format = amt::codec::AudioSampleFormat::pcm16,
       .sample_rate = 44100,
       .dither_when_reducing_integer_depth = true,
       .available = true},
      {.id = ExportRecipeId::client_preview,
       .key = "client_preview",
       .name = "Client Preview",
       .description = "High-quality MP3 preview.",
       .extension = ".mp3",
       .container = amt::codec::AudioContainer::mp3,
       .sample_format = amt::codec::AudioSampleFormat::compressed,
       .sample_rate = std::nullopt,
       .dither_when_reducing_integer_depth = false,
       .available = false,
       .unavailable_reason = "MP3 production encoding remains behind the reviewed broad-codec backend gate."},
      {.id = ExportRecipeId::archive_float,
       .key = "archive_float",
       .name = "Archive Float Master",
       .description = "32-bit float WAV at the source sample rate for archival handoff.",
       .extension = ".wav",
       .container = amt::codec::AudioContainer::wav,
       .sample_format = amt::codec::AudioSampleFormat::float32,
       .sample_rate = std::nullopt,
       .dither_when_reducing_integer_depth = false,
       .available = true}};
  return recipes;
}

const ExportRecipe* find_export_recipe(const ExportRecipeId id) {
  const auto& recipes = builtin_export_recipes();
  const auto iterator = std::find_if(recipes.begin(), recipes.end(),
      [id](const ExportRecipe& recipe) { return recipe.id == id; });
  return iterator == recipes.end() ? nullptr : &*iterator;
}

const ExportRecipe* find_export_recipe(const std::string& key) {
  const auto& recipes = builtin_export_recipes();
  const auto iterator = std::find_if(recipes.begin(), recipes.end(),
      [&](const ExportRecipe& recipe) { return recipe.key == key; });
  return iterator == recipes.end() ? nullptr : &*iterator;
}

amt::codec::ExportRequest make_export_request(const ExportRecipe& recipe) {
  amt::codec::ExportRequest request;
  request.container = recipe.container;
  request.sample_format = recipe.sample_format;
  request.sample_rate = recipe.sample_rate;
  request.dither_when_reducing_integer_depth = recipe.dither_when_reducing_integer_depth;
  return request;
}

}  // namespace amt::project
