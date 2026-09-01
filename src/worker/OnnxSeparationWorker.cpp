#include "OnnxSeparationWorker.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "amt/audio/AudioBuffer.h"
#include "amt/codec/AudioIO.h"
#include "amt/codec/SndFileCodec.h"

#ifdef AMT_WITH_ONNX
#include <onnxruntime_cxx_api.h>
#endif

namespace amt::worker {
namespace {

constexpr std::size_t kReadBlockFrames = 8192U;
constexpr double kModelInputMaximum = 1.0;
constexpr double kModelInputTolerance = 1.0e-6;

[[nodiscard]] bool safe_stem_name(const std::string& name) {
  if (name.empty() || name.size() > 64U) return false;
  return std::all_of(name.begin(), name.end(), [](const unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '-' || c == '_';
  });
}

struct TemporaryInput {
  std::filesystem::path path;
  bool owned{false};

  TemporaryInput() = default;
  TemporaryInput(std::filesystem::path value, const bool owns)
      : path(std::move(value)), owned(owns) {}

  TemporaryInput(const TemporaryInput&) = delete;
  TemporaryInput& operator=(const TemporaryInput&) = delete;

  TemporaryInput(TemporaryInput&& other) noexcept
      : path(std::move(other.path)), owned(other.owned) {
    other.owned = false;
  }

  TemporaryInput& operator=(TemporaryInput&& other) noexcept {
    if (this == &other) return *this;
    cleanup();
    path = std::move(other.path);
    owned = other.owned;
    other.owned = false;
    return *this;
  }

  ~TemporaryInput() { cleanup(); }

  void cleanup() noexcept {
    if (!owned || path.empty()) return;
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    owned = false;
  }
};

struct StemCleanup {
  std::vector<std::filesystem::path> paths;
  bool committed{false};

  ~StemCleanup() {
    if (committed) return;
    for (const auto& path : paths) {
      std::error_code ignored;
      std::filesystem::remove(path, ignored);
    }
  }
};

bool validate_request(const OnnxSeparationWorkerRequest& request,
                      std::string& error) {
  if (request.model_path.empty() || request.source_path.empty() ||
      request.output_directory.empty()) {
    error = "ONNX separation request is missing a required path";
    return false;
  }
  if (request.input_sample_rate <= 0) {
    error = "ONNX separation request has an invalid model sample rate";
    return false;
  }
  if (request.stem_names.empty()) {
    error = "ONNX separation request has no declared stem taxonomy";
    return false;
  }
  if (!std::all_of(request.stem_names.begin(), request.stem_names.end(), safe_stem_name)) {
    error = "ONNX separation request contains an unsafe stem name";
    return false;
  }
  for (std::size_t index = 0U; index < request.stem_names.size(); ++index) {
    if (std::find(request.stem_names.begin() + static_cast<std::ptrdiff_t>(index + 1U),
                  request.stem_names.end(), request.stem_names[index]) !=
        request.stem_names.end()) {
      error = "ONNX separation stem taxonomy contains a duplicate role name";
      return false;
    }
  }
  if (request.input_tensor_name.empty() || request.output_tensor_name.empty()) {
    error = "ONNX separation tensor names cannot be empty";
    return false;
  }
  if (request.chunk_frames < 4096U || request.chunk_frames > 2097152U) {
    error = "ONNX separation chunk size is outside the supported range";
    return false;
  }
  if (request.overlap_frames * 2U >= request.chunk_frames) {
    error = "ONNX separation overlap must be less than half the chunk size";
    return false;
  }
  if (request.execution_provider != "cpu" && request.execution_provider != "cuda") {
    error = "ONNX separation execution provider must be cpu or cuda";
    return false;
  }
  return true;
}

std::optional<TemporaryInput> prepare_model_input(
    amt::codec::SndFileCodecService& codecs,
    const OnnxSeparationWorkerRequest& request,
    std::string& error) {
  const auto source_metadata = codecs.probe(request.source_path, error);
  if (!source_metadata) return std::nullopt;
  if (source_metadata->channels < 1 || source_metadata->channels > 2) {
    error = "source separation currently supports mono or stereo program input";
    return std::nullopt;
  }

  if (source_metadata->sample_rate == request.input_sample_rate) {
    return std::optional<TemporaryInput>{
        std::in_place, request.source_path, false};
  }

  std::error_code directory_error;
  std::filesystem::create_directories(request.output_directory, directory_error);
  if (directory_error) {
    error = "unable to create separation output directory: " + directory_error.message();
    return std::nullopt;
  }

  TemporaryInput input(request.output_directory / ".amt-model-input.wav", true);
  amt::codec::ExportRequest conversion;
  conversion.sample_rate = request.input_sample_rate;
  conversion.container = amt::codec::AudioContainer::wav;
  conversion.sample_format = amt::codec::AudioSampleFormat::float32;
  conversion.dither_when_reducing_integer_depth = false;
  if (!amt::codec::export_audio(codecs, request.source_path, input.path,
                                conversion, error)) {
    return std::nullopt;
  }
  return std::optional<TemporaryInput>{std::move(input)};
}

bool read_window(amt::codec::IAudioDecoder& decoder,
                 const std::int64_t start_frame,
                 const std::size_t chunk_frames,
                 const int source_channels,
                 std::vector<float>& input,
                 std::size_t& frames_read,
                 std::string& error) {
  if (!decoder.seek(start_frame, error)) return false;
  input.assign(chunk_frames * 2U, 0.0F);
  frames_read = 0U;

  while (frames_read < chunk_frames) {
    amt::audio::AudioBuffer block;
    std::size_t read = 0U;
    const std::size_t wanted = std::min(kReadBlockFrames, chunk_frames - frames_read);
    if (!decoder.read(block, wanted, read, error)) return false;
    if (read == 0U) break;
    if (block.channels() != static_cast<std::size_t>(source_channels)) {
      error = "separation decoder changed channel topology during a model window";
      return false;
    }

    const auto left = block.channel(0U);
    const auto right = source_channels == 1 ? left : block.channel(1U);
    for (std::size_t frame = 0U; frame < read; ++frame) {
      const float left_sample = left[frame];
      const float right_sample = right[frame];
      if (!std::isfinite(left_sample) || !std::isfinite(right_sample)) {
        error = "source audio contains a non-finite sample and cannot be sent to the separation model";
        return false;
      }
      if (std::abs(static_cast<double>(left_sample)) >
              kModelInputMaximum + kModelInputTolerance ||
          std::abs(static_cast<double>(right_sample)) >
              kModelInputMaximum + kModelInputTolerance) {
        error = "source audio exceeds the separation model's documented [-1, 1] input range";
        return false;
      }
      input[frames_read + frame] = left_sample;
      input[chunk_frames + frames_read + frame] = right_sample;
    }
    frames_read += read;
  }
  return true;
}

#ifdef AMT_WITH_ONNX

void configure_execution_provider(Ort::SessionOptions& options,
                                  const std::string& provider) {
  if (provider == "cpu") return;
#ifdef AMT_ORT_ENABLE_CUDA
  const auto& api = Ort::GetApi();
  OrtCUDAProviderOptionsV2* cuda_options = nullptr;
  Ort::ThrowOnError(api.CreateCUDAProviderOptions(&cuda_options));
  try {
    Ort::ThrowOnError(api.SessionOptionsAppendExecutionProvider_CUDA_V2(
        static_cast<OrtSessionOptions*>(options), cuda_options));
  } catch (...) {
    api.ReleaseCUDAProviderOptions(cuda_options);
    throw;
  }
  api.ReleaseCUDAProviderOptions(cuda_options);
#else
  throw std::runtime_error("CUDA execution provider was not compiled into amt_worker");
#endif
}

bool validate_output_tensor(const Ort::Value& output,
                            const std::size_t stem_count,
                            const std::size_t chunk_frames,
                            std::string& error) {
  if (!output.IsTensor()) {
    error = "separation model output is not a tensor";
    return false;
  }
  const auto info = output.GetTensorTypeAndShapeInfo();
  if (info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
    error = "separation model output tensor must be float32";
    return false;
  }
  const auto shape = info.GetShape();
  if (shape.size() != 4U || shape[0] != 1 ||
      shape[1] != static_cast<std::int64_t>(stem_count) ||
      shape[2] != 2 || shape[3] != static_cast<std::int64_t>(chunk_frames)) {
    error = "separation model output must resolve to [1, stems, 2, chunk_frames]";
    return false;
  }
  const std::size_t expected = stem_count * 2U * chunk_frames;
  if (info.GetElementCount() != expected) {
    error = "separation model output tensor has an unexpected element count";
    return false;
  }
  return true;
}

void save_tail(const float* output,
               const std::size_t stem_count,
               const std::size_t chunk_frames,
               const std::size_t overlap_frames,
               std::vector<std::vector<float>>& tails) {
  if (overlap_frames == 0U) return;
  const std::size_t tail_start = chunk_frames - overlap_frames;
  for (std::size_t stem = 0U; stem < stem_count; ++stem) {
    auto& tail = tails[stem];
    tail.resize(overlap_frames * 2U);
    for (std::size_t channel = 0U; channel < 2U; ++channel) {
      const std::size_t base = (stem * 2U + channel) * chunk_frames;
      for (std::size_t frame = 0U; frame < overlap_frames; ++frame) {
        tail[channel * overlap_frames + frame] =
            output[base + tail_start + frame];
      }
    }
  }
}

[[nodiscard]] double reference_fade_fraction(const std::size_t frame,
                                             const std::size_t overlap_frames) {
  if (overlap_frames <= 1U) return 1.0;
  return static_cast<double>(frame) /
         static_cast<double>(overlap_frames - 1U);
}

bool write_output_span(const float* output,
                       const std::size_t stem_count,
                       const std::size_t chunk_frames,
                       const std::size_t output_frames,
                       const std::size_t overlap_frames,
                       const bool blend_previous_tail,
                       const std::vector<std::vector<float>>& tails,
                       std::vector<std::unique_ptr<amt::codec::IAudioEncoder>>& encoders,
                       std::string& error) {
  for (std::size_t stem = 0U; stem < stem_count; ++stem) {
    amt::audio::AudioBuffer block(2U, output_frames);
    for (std::size_t channel = 0U; channel < 2U; ++channel) {
      auto destination = block.channel(channel);
      const std::size_t source_base = (stem * 2U + channel) * chunk_frames;
      for (std::size_t frame = 0U; frame < output_frames; ++frame) {
        float value = output[source_base + frame];
        if (!std::isfinite(value)) {
          error = "separation model produced a non-finite audio sample";
          return false;
        }
        if (blend_previous_tail && frame < overlap_frames) {
          const double t = reference_fade_fraction(frame, overlap_frames);
          const float previous = tails[stem][channel * overlap_frames + frame];
          value = static_cast<float>(static_cast<double>(previous) * (1.0 - t) +
                                     static_cast<double>(value) * t);
        }
        destination[frame] = value;
      }
    }
    if (!encoders[stem]->write(block, error)) return false;
  }
  return true;
}

#endif

}  // namespace

bool onnx_separation_compiled() noexcept {
#ifdef AMT_WITH_ONNX
  return true;
#else
  return false;
#endif
}

std::optional<OnnxSeparationWorkerResult> run_onnx_separation(
    const OnnxSeparationWorkerRequest& request,
    std::string& error,
    const amt::core::ProgressCallback& progress) {
  error.clear();
  amt::core::report_progress(progress, 0.0);
  if (!validate_request(request, error)) return std::nullopt;

#ifndef AMT_WITH_ONNX
  error = "ONNX Runtime support is not compiled into amt_worker";
  return std::nullopt;
#else
  try {
    if (!std::filesystem::exists(request.model_path)) {
      error = "separation model artifact does not exist";
      return std::nullopt;
    }
    if (!std::filesystem::exists(request.source_path)) {
      error = "canonical source file does not exist";
      return std::nullopt;
    }

    amt::codec::SndFileCodecService codecs;
    if (!codecs.available()) {
      error = "audio codec backend is unavailable in the separation worker: " +
              codecs.backend_error();
      return std::nullopt;
    }

    auto model_input = prepare_model_input(codecs, request, error);
    if (!model_input) return std::nullopt;
    amt::core::report_progress(progress, 0.10);

    auto decoder = codecs.open_decoder(model_input->path, error);
    if (!decoder) return std::nullopt;
    const auto metadata = decoder->metadata();
    if (metadata.sample_rate != request.input_sample_rate ||
        metadata.channels < 1 || metadata.channels > 2 ||
        metadata.frames <= 0 || !metadata.seekable) {
      error = "prepared model input has unsupported audio geometry";
      return std::nullopt;
    }

    const auto stems_directory = request.output_directory / "stems";
    std::error_code directory_error;
    std::filesystem::create_directories(stems_directory, directory_error);
    if (directory_error) {
      error = "unable to create stem output directory: " + directory_error.message();
      return std::nullopt;
    }

    StemCleanup cleanup;
    cleanup.paths.reserve(request.stem_names.size());
    std::vector<std::unique_ptr<amt::codec::IAudioEncoder>> encoders;
    encoders.reserve(request.stem_names.size());
    for (const auto& stem : request.stem_names) {
      const auto path = stems_directory / (stem + ".wav");
      amt::codec::EncodeSettings settings;
      settings.sample_rate = request.input_sample_rate;
      settings.channels = 2;
      settings.container = amt::codec::AudioContainer::wav;
      settings.sample_format = amt::codec::AudioSampleFormat::float32;
      auto encoder = codecs.open_encoder(path, settings, error);
      if (!encoder) return std::nullopt;
      cleanup.paths.push_back(path);
      encoders.push_back(std::move(encoder));
    }

    Ort::Env environment(ORT_LOGGING_LEVEL_WARNING, "amt-separation-worker");
    Ort::SessionOptions options;
    options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
    configure_execution_provider(options, request.execution_provider);
    Ort::Session session(environment, request.model_path.c_str(), options);
    amt::core::report_progress(progress, 0.15);

    const std::size_t chunk_frames = request.chunk_frames;
    const std::size_t overlap_frames = request.overlap_frames;
    const std::size_t stride = chunk_frames - overlap_frames;
    const std::size_t stem_count = request.stem_names.size();
    std::vector<std::vector<float>> previous_tails(stem_count);
    std::vector<float> input;
    input.reserve(chunk_frames * 2U);
    auto memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    const char* input_names[] = {request.input_tensor_name.c_str()};
    const char* output_names[] = {request.output_tensor_name.c_str()};

    bool first_chunk = true;
    for (std::int64_t start = 0; start < metadata.frames;
         start += static_cast<std::int64_t>(stride)) {
      std::size_t read = 0U;
      if (!read_window(*decoder, start, chunk_frames, metadata.channels,
                       input, read, error)) {
        return std::nullopt;
      }
      if (read == 0U) break;

      const std::array<std::int64_t, 3> input_shape{
          1, 2, static_cast<std::int64_t>(chunk_frames)};
      auto input_tensor = Ort::Value::CreateTensor<float>(
          memory, input.data(), input.size(), input_shape.data(), input_shape.size());
      auto outputs = session.Run(Ort::RunOptions{nullptr}, input_names,
                                 &input_tensor, 1U, output_names, 1U);
      if (outputs.size() != 1U ||
          !validate_output_tensor(outputs.front(), stem_count, chunk_frames, error)) {
        if (error.empty()) error = "separation model returned an invalid output";
        return std::nullopt;
      }
      const float* values = outputs.front().GetTensorData<float>();

      const bool final_chunk =
          start + static_cast<std::int64_t>(read) >= metadata.frames;
      const std::size_t output_frames = final_chunk ? read : stride;

      if (!write_output_span(values, stem_count, chunk_frames, output_frames,
                             overlap_frames, !first_chunk && overlap_frames > 0U,
                             previous_tails, encoders, error)) {
        return std::nullopt;
      }

      if (!final_chunk) {
        save_tail(values, stem_count, chunk_frames, overlap_frames, previous_tails);
      }
      const auto processed = std::min<std::int64_t>(
          metadata.frames, start + static_cast<std::int64_t>(read));
      const double fraction = static_cast<double>(processed) /
                              static_cast<double>(metadata.frames);
      amt::core::report_progress(progress, 0.15 + fraction * 0.80);
      first_chunk = false;
      if (final_chunk) break;
    }

    for (auto& encoder : encoders) {
      if (!encoder->finalize(error)) return std::nullopt;
    }
    amt::core::report_progress(progress, 0.99);

    cleanup.committed = true;
    OnnxSeparationWorkerResult result;
    result.sample_rate = request.input_sample_rate;
    result.frames = metadata.frames;
    result.stem_paths = cleanup.paths;
    amt::core::report_progress(progress, 1.0);
    return result;
  } catch (const Ort::Exception& exception) {
    error = std::string("ONNX Runtime separation failed: ") + exception.what();
    return std::nullopt;
  } catch (const std::exception& exception) {
    error = std::string("separation worker failed: ") + exception.what();
    return std::nullopt;
  }
#endif
}

}  // namespace amt::worker
