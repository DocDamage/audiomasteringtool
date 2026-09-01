#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace amt::instruments {

enum class TaxonomyLevel { source_role, family, instrument, attribute };
enum class SourceRole { unknown, drums, bass, vocals, tonal, other };
enum class ModelSupport { unavailable, research, production };

struct InstrumentTaxonomyNode {
  std::string id;
  std::string display_label;
  std::string parent_id;
  TaxonomyLevel level{TaxonomyLevel::instrument};
  SourceRole source_role{SourceRole::unknown};
  std::string family;
  bool mutually_exclusive_with_siblings{false};
  bool may_coexist_with_siblings{true};
  ModelSupport model_support{ModelSupport::unavailable};
  double minimum_confidence{1.0};
  std::vector<std::string> aliases;

  InstrumentTaxonomyNode() = default;
  InstrumentTaxonomyNode(std::string node_id, std::string label, std::string parent,
                         TaxonomyLevel node_level, SourceRole role, std::string node_family,
                         bool exclusive, bool coexist, ModelSupport support, double minimum,
                         std::vector<std::string> node_aliases = {})
      : id(std::move(node_id)), display_label(std::move(label)), parent_id(std::move(parent)),
        level(node_level), source_role(role), family(std::move(node_family)),
        mutually_exclusive_with_siblings(exclusive), may_coexist_with_siblings(coexist),
        model_support(support), minimum_confidence(minimum), aliases(std::move(node_aliases)) {}
};

[[nodiscard]] const std::vector<InstrumentTaxonomyNode>& instrument_taxonomy();
[[nodiscard]] const InstrumentTaxonomyNode* find_taxonomy_node(const std::string& id);
[[nodiscard]] bool validate_instrument_taxonomy(std::string& error);
[[nodiscard]] std::string source_role_name(SourceRole role);

}  // namespace amt::instruments
