#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "amt/core/JobControl.h"
#include "amt/separation/ModelRegistry.h"
#include "amt/separation/WorkerSeparationProvider.h"

namespace {

bool clean_failure_directory(const std::filesystem::path& directory) {
  return !std::filesystem::exists(directory / "stems") &&
         !std::filesystem::exists(directory / ".amt-model-input.wav");
}

amt::separation::SeparationRequest request_for(
    const std::filesystem::path& source,
    const std::filesystem::path& output,
    const amt::separation::SeparationModelManifest& manifest) {
  amt::separation::SeparationRequest request;
  request.source_path = source;
  request.cache_directory = output;
  request.requested_stems = manifest.stem_taxonomy;
  request.request_stem_audio = true;
  request.request_time_frequency_masks = false;
  return request;
}

bool expect_failure(const char* label,
                    amt::separation::WorkerSeparationProviderConfig config,
                    const amt::separation::SeparationRequest& request,
                    const amt::core::CancellationToken* cancellation = nullptr,
                    const amt::core::ProgressCallback& progress = {}) {
  amt::separation::WorkerSeparationProvider provider(std::move(config));
  std::string error;
  const auto result = provider.separate(request, error, cancellation, progress);
  const bool passed = !result && !error.empty() &&
                      clean_failure_directory(request.cache_directory);
  std::cout << label << ": " << (passed ? "PASS" : "FAIL")
            << " (" << error << ")\n";
  return passed;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 6) {
    std::cerr << "usage: amt_windows_real_model_failure_acceptance "
                 "<registry> <worker> <model> <source> <output-root>\n";
    return 2;
  }

  const std::filesystem::path registry = argv[1];
  const std::filesystem::path worker = argv[2];
  const std::filesystem::path model = argv[3];
  const std::filesystem::path source = argv[4];
  const std::filesystem::path root = argv[5];
  std::filesystem::create_directories(root);

  std::string error;
  auto selection = amt::separation::load_model_registry_selection(
      registry, worker, error, root / "fallback");
  if (!selection || !selection->active_separation_model) {
    std::cerr << "unable to load active model: " << error << '\n';
    return 3;
  }
  auto base = *selection->active_separation_model;
  base.model_artifact = model;
  bool passed = true;

  {
    auto config = base;
    config.worker_executable = root / "missing-worker.exe";
    const auto output = root / "missing-worker";
    passed &= expect_failure(
        "missing worker", config,
        request_for(source, output, config.manifest));
  }

  {
    const auto corrupt_model = root / "corrupt-model.onnx";
    std::ofstream(corrupt_model, std::ios::binary | std::ios::trunc)
        << "not an ONNX model";
    auto config = base;
    config.model_artifact = corrupt_model;
    const auto output = root / "corrupt-model";
    passed &= expect_failure(
        "corrupt model", config,
        request_for(source, output, config.manifest));
  }

  {
    const auto blocking_file = root / "not-a-directory";
    std::ofstream(blocking_file, std::ios::binary | std::ios::trunc) << "x";
    auto config = base;
    const auto output = blocking_file / "child";
    passed &= expect_failure(
        "unwritable output", config,
        request_for(source, output, config.manifest));
  }

  {
    auto config = base;
    const auto output = root / "cancelled";
    amt::core::CancellationToken cancellation;
    passed &= expect_failure(
        "cancellation", config,
        request_for(source, output, config.manifest), &cancellation,
        [&cancellation](const double value) {
          if (value >= 0.12) cancellation.cancel();
        });
    passed &= cancellation.is_cancelled();
  }

  {
    auto config = base;
    config.maximum_runtime_seconds = 1U;
    const auto output = root / "timeout";
    passed &= expect_failure(
        "timeout", config,
        request_for(source, output, config.manifest));
  }

  return passed ? 0 : 1;
}
