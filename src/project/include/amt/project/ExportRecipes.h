#pragma once

#include <optional>
#include <string>
#include <vector>

#include "amt/codec/AudioIO.h"

namespace amt::project {

enum class ExportRecipeId {
  studio_master,
  distribution_wav,
  distribution_flac,
  cd,
  client_preview,
  archive_float
};

struct ExportRecipe {
  ExportRecipeId id{ExportRecipeId::studio_master};
  std::string key;
  std::string name;
  std::string description;
  std::string extension;
  amt::codec::AudioContainer container{amt::codec::AudioContainer::wav};
  amt::codec::AudioSampleFormat sample_format{amt::codec::AudioSampleFormat::pcm24};
  std::optional<int> sample_rate;
  bool dither_when_reducing_integer_depth{true};
  bool available{true};
  std::string unavailable_reason;
};

[[nodiscard]] const std::vector<ExportRecipe>& builtin_export_recipes();
[[nodiscard]] const ExportRecipe* find_export_recipe(ExportRecipeId id);
[[nodiscard]] const ExportRecipe* find_export_recipe(const std::string& key);
[[nodiscard]] amt::codec::ExportRequest make_export_request(const ExportRecipe& recipe);

}  // namespace amt::project
