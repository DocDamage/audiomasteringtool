#pragma once

#include <memory>
#include <string>
#include "amt/revision/RevisionIntent.h"

namespace amt::revision {

class ILanguageModelProvider {
 public:
  virtual ~ILanguageModelProvider() = default;
  [[nodiscard]] virtual bool is_available() const noexcept = 0;
  [[nodiscard]] virtual std::optional<RevisionIntent> parse_prompt(
      const std::string& prompt, std::string& error) = 0;
};

class RevisionParser {
 public:
  explicit RevisionParser(std::shared_ptr<ILanguageModelProvider> llm_provider = nullptr);

  [[nodiscard]] RevisionIntent parse(const std::string& prompt) const;

 private:
  [[nodiscard]] RevisionIntent parse_deterministic(const std::string& prompt) const;

  std::shared_ptr<ILanguageModelProvider> llm_provider_;
};

}  // namespace amt::revision
