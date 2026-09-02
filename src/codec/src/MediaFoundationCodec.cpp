#include "amt/codec/MediaFoundationCodec.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <propvarutil.h>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "propsys.lib")

namespace amt::codec {
namespace {

class MediaFoundationRuntime {
 public:
  MediaFoundationRuntime() : result_(MFStartup(MF_VERSION)) {}
  ~MediaFoundationRuntime() { if (SUCCEEDED(result_)) MFShutdown(); }
  [[nodiscard]] bool available() const noexcept { return SUCCEEDED(result_); }

 private:
  HRESULT result_{E_FAIL};
};

MediaFoundationRuntime& media_foundation_runtime() {
  static MediaFoundationRuntime instance;
  return instance;
}

class ComApartment {
 public:
  ComApartment() : result_(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {
    usable_ = SUCCEEDED(result_) || result_ == RPC_E_CHANGED_MODE;
    owns_initialization_ = SUCCEEDED(result_);
  }
  ~ComApartment() { if (owns_initialization_) CoUninitialize(); }
  [[nodiscard]] bool usable() const noexcept { return usable_; }

 private:
  HRESULT result_{E_FAIL};
  bool usable_{false};
  bool owns_initialization_{false};
};

class MediaFoundationDecoder final : public IAudioDecoder {
 public:
  MediaFoundationDecoder(IMFSourceReader* reader, AudioMetadata metadata,
                         std::shared_ptr<ComApartment> apartment)
      : reader_(reader), metadata_(std::move(metadata)),
        apartment_(std::move(apartment)) {}

  ~MediaFoundationDecoder() override {
    if (reader_ != nullptr) {
      reader_->Release();
    }
  }

  [[nodiscard]] const AudioMetadata& metadata() const noexcept override {
    return metadata_;
  }

  [[nodiscard]] std::int64_t tell() const noexcept override {
    return current_frame_;
  }

  bool seek(std::int64_t frame, std::string& error) override {
    if (reader_ == nullptr) {
      error = "decoder is not open";
      return false;
    }
    if (frame < 0 || (metadata_.frames > 0 && frame > metadata_.frames)) {
      error = "seek frame out of bounds";
      return false;
    }

    const LONGLONG time_100ns = (metadata_.sample_rate > 0)
        ? static_cast<LONGLONG>((static_cast<double>(frame) / metadata_.sample_rate) * 10000000.0)
        : 0;

    PROPVARIANT var;
    PropVariantInit(&var);
    var.vt = VT_I8;
    var.hVal.QuadPart = time_100ns;

    HRESULT hr = reader_->SetCurrentPosition(GUID_NULL, var);
    PropVariantClear(&var);

    if (FAILED(hr)) {
      error = "MediaFoundation seek failed (HRESULT 0x" + std::to_string(hr) + ")";
      return false;
    }

    current_frame_ = frame;
    residual_samples_.clear();
    return true;
  }

  bool read(amt::audio::AudioBuffer& output, std::size_t max_frames,
            std::size_t& frames_read, std::string& error,
            const amt::core::CancellationToken* cancellation = nullptr) override {
    frames_read = 0;
    output.clear();
    if (reader_ == nullptr) {
      error = "decoder is not open";
      return false;
    }
    if (max_frames == 0) return true;

    const std::size_t channels = metadata_.channels > 0 ? static_cast<std::size_t>(metadata_.channels) : 2U;
    output.resize(channels, max_frames);

    std::size_t written_frames = 0;

    // First drain any residual samples from previous ReadSample calls
    if (!residual_samples_.empty()) {
      const std::size_t available_frames = residual_samples_.size() / channels;
      const std::size_t to_copy = std::min(available_frames, max_frames);
      for (std::size_t c = 0; c < channels; ++c) {
        float* channel_ptr = output.channel(c).data();
        for (std::size_t f = 0; f < to_copy; ++f) {
          channel_ptr[f] = residual_samples_[f * channels + c];
        }
      }
      written_frames += to_copy;
      residual_samples_.erase(residual_samples_.begin(), residual_samples_.begin() + to_copy * channels);
    }

    while (written_frames < max_frames) {
      if (cancellation != nullptr && cancellation->is_cancelled()) {
        error = "read cancelled";
        return false;
      }

      DWORD stream_flags = 0;
      LONGLONG timestamp = 0;
      IMFSample* sample = nullptr;
      HRESULT hr = reader_->ReadSample(
          static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM),
          0, nullptr, &stream_flags, &timestamp, &sample);

      if (FAILED(hr)) {
        error = "MediaFoundation ReadSample failed";
        if (sample) sample->Release();
        return false;
      }

      if (stream_flags & MF_SOURCE_READERF_ENDOFSTREAM) {
        if (sample) sample->Release();
        break;
      }

      if (sample == nullptr) {
        continue;
      }

      IMFMediaBuffer* media_buffer = nullptr;
      hr = sample->ConvertToContiguousBuffer(&media_buffer);
      if (FAILED(hr) || media_buffer == nullptr) {
        sample->Release();
        error = "MediaFoundation could not expose a contiguous decoded buffer";
        return false;
      }

      BYTE* data = nullptr;
      DWORD max_length = 0;
      DWORD current_length = 0;
      hr = media_buffer->Lock(&data, &max_length, &current_length);
      if (FAILED(hr) || data == nullptr) {
        media_buffer->Release();
        sample->Release();
        error = "MediaFoundation could not lock a decoded audio buffer";
        return false;
      }

      const auto* float_data = reinterpret_cast<const float*>(data);
      const std::size_t total_floats = current_length / sizeof(float);
      const std::size_t sample_frames = total_floats / channels;
      const std::size_t frames_needed = max_frames - written_frames;
      const std::size_t direct_copy_frames =
          std::min(sample_frames, frames_needed);
      for (std::size_t c = 0; c < channels; ++c) {
        float* channel_ptr = output.channel(c).data();
        for (std::size_t f = 0; f < direct_copy_frames; ++f) {
          channel_ptr[written_frames + f] = float_data[f * channels + c];
        }
      }
      written_frames += direct_copy_frames;
      if (sample_frames > direct_copy_frames) {
        const std::size_t remaining_floats =
            (sample_frames - direct_copy_frames) * channels;
        residual_samples_.assign(
            float_data + direct_copy_frames * channels,
            float_data + direct_copy_frames * channels + remaining_floats);
      }
      media_buffer->Unlock();
      media_buffer->Release();
      sample->Release();
    }

    output.resize_frames(written_frames);
    frames_read = written_frames;
    current_frame_ += static_cast<std::int64_t>(written_frames);
    return true;
  }

 private:
  IMFSourceReader* reader_{nullptr};
  AudioMetadata metadata_{};
  std::int64_t current_frame_{0};
  std::vector<float> residual_samples_;
  std::shared_ptr<ComApartment> apartment_;
};

class MediaFoundationEncoder final : public IAudioEncoder {
 public:
  MediaFoundationEncoder(IMFSinkWriter* writer, DWORD stream_index,
                         int sample_rate, int channels,
                         std::shared_ptr<ComApartment> apartment)
      : writer_(writer), stream_index_(stream_index),
        sample_rate_(sample_rate), channels_(channels),
        apartment_(std::move(apartment)) {}

  ~MediaFoundationEncoder() override {
    if (writer_ != nullptr) {
      writer_->Release();
    }
  }

  bool write(const amt::audio::AudioBuffer& input, std::string& error,
             const amt::core::CancellationToken* cancellation = nullptr) override {
    if (writer_ == nullptr) {
      error = "encoder is not open";
      return false;
    }
    if (input.frames() == 0) return true;
    if (cancellation != nullptr && cancellation->is_cancelled()) {
      error = "encode cancelled";
      return false;
    }

    const std::size_t frames = input.frames();
    const std::size_t channels = static_cast<std::size_t>(channels_);
    const std::size_t total_samples = frames * channels;
    const DWORD byte_count = static_cast<DWORD>(total_samples * sizeof(float));

    IMFMediaBuffer* media_buffer = nullptr;
    HRESULT hr = MFCreateMemoryBuffer(byte_count, &media_buffer);
    if (FAILED(hr) || media_buffer == nullptr) {
      error = "unable to create MF memory buffer for audio encoding";
      return false;
    }

    BYTE* data_ptr = nullptr;
    DWORD max_len = 0;
    DWORD cur_len = 0;
    hr = media_buffer->Lock(&data_ptr, &max_len, &cur_len);
    if (FAILED(hr) || data_ptr == nullptr) {
      media_buffer->Release();
      error = "unable to lock MF buffer";
      return false;
    }

    auto* float_ptr = reinterpret_cast<float*>(data_ptr);
    for (std::size_t f = 0; f < frames; ++f) {
      for (std::size_t c = 0; c < channels; ++c) {
        float sample_val = (c < input.channels()) ? input.channel(c)[f] : 0.0f;
        float_ptr[f * channels + c] = std::clamp(sample_val, -1.0f, 1.0f);
      }
    }
    media_buffer->Unlock();
    media_buffer->SetCurrentLength(byte_count);

    IMFSample* sample = nullptr;
    hr = MFCreateSample(&sample);
    if (FAILED(hr) || sample == nullptr) {
      media_buffer->Release();
      error = "unable to create MF sample for audio encoding";
      return false;
    }

    sample->AddBuffer(media_buffer);
    media_buffer->Release();

    const LONGLONG sample_time = static_cast<LONGLONG>(
        (static_cast<double>(total_frames_written_) / sample_rate_) * 10000000.0);
    const LONGLONG sample_duration = static_cast<LONGLONG>(
        (static_cast<double>(frames) / sample_rate_) * 10000000.0);

    sample->SetSampleTime(sample_time);
    sample->SetSampleDuration(sample_duration);

    hr = writer_->WriteSample(stream_index_, sample);
    sample->Release();

    if (FAILED(hr)) {
      error = "MediaFoundation WriteSample failed (HRESULT 0x" + std::to_string(hr) + ")";
      return false;
    }

    total_frames_written_ += frames;
    return true;
  }

  bool finalize(std::string& error) override {
    if (writer_ == nullptr) {
      error = "encoder is not open";
      return false;
    }
    HRESULT hr = writer_->Finalize();
    if (FAILED(hr)) {
      error = "MediaFoundation Finalize failed";
      return false;
    }
    return true;
  }

 private:
  IMFSinkWriter* writer_{nullptr};
  DWORD stream_index_{0};
  int sample_rate_{44100};
  int channels_{2};
  std::size_t total_frames_written_{0};
  std::shared_ptr<ComApartment> apartment_;
};

}  // namespace

struct MediaFoundationCodecService::Impl {
  bool initialized{false};
};

MediaFoundationCodecService::MediaFoundationCodecService()
    : impl_(std::make_unique<Impl>()) {
  impl_->initialized = media_foundation_runtime().available();
}

MediaFoundationCodecService::~MediaFoundationCodecService() = default;
MediaFoundationCodecService::MediaFoundationCodecService(MediaFoundationCodecService&&) noexcept = default;
MediaFoundationCodecService& MediaFoundationCodecService::operator=(MediaFoundationCodecService&&) noexcept = default;

bool MediaFoundationCodecService::available() const noexcept {
  return impl_ && impl_->initialized;
}

std::string MediaFoundationCodecService::backend_name() const {
  return "Windows Media Foundation (Native)";
}

std::string MediaFoundationCodecService::backend_error() const {
  return available() ? "" : "Windows Media Foundation failed to initialize";
}

std::vector<CodecCapability> MediaFoundationCodecService::capabilities() const {
  const bool ok = available();
  return {
      {.container = AudioContainer::mp3,
       .name = "MP3",
       .extensions = {"mp3"},
       .decode = ok,
       .encode = ok,
       .lossless = false},
      {.container = AudioContainer::aac_m4a,
       .name = "AAC / M4A",
       .extensions = {"m4a", "aac"},
       .decode = ok,
       .encode = ok,
       .lossless = false},
      {.container = AudioContainer::ogg,
       .name = "OGG / Vorbis",
       .extensions = {"ogg"},
       .decode = false,
       .encode = false,
       .lossless = false},
      {.container = AudioContainer::opus,
       .name = "Opus",
       .extensions = {"opus"},
       .decode = false,
       .encode = false,
       .lossless = false},
      {.container = AudioContainer::wav,
       .name = "WAV",
       .extensions = {"wav", "wave"},
       .decode = ok,
       .encode = ok,
       .lossless = true},
      {.container = AudioContainer::flac,
       .name = "FLAC",
       .extensions = {"flac"},
       .decode = ok,
       .encode = false,
       .lossless = true},
      {.container = AudioContainer::aiff,
       .name = "AIFF",
       .extensions = {"aif", "aiff"},
       .decode = ok,
       .encode = false,
       .lossless = true}
  };
}

std::optional<AudioMetadata> MediaFoundationCodecService::probe(
    const std::filesystem::path& path, std::string& error) const {
  if (!available()) {
    error = backend_error();
    return std::nullopt;
  }

  ComApartment apartment;
  if (!apartment.usable()) {
    error = "COM initialization failed for Media Foundation probing";
    return std::nullopt;
  }

  const std::wstring wpath = path.wstring();
  IMFSourceReader* reader = nullptr;
  HRESULT hr = MFCreateSourceReaderFromURL(wpath.c_str(), nullptr, &reader);
  if (FAILED(hr) || reader == nullptr) {
    error = "MediaFoundation unable to open file for probing: " + path.string();
    return std::nullopt;
  }

  // Set output format to float PCM
  IMFMediaType* media_type = nullptr;
  hr = MFCreateMediaType(&media_type);
  if (SUCCEEDED(hr) && media_type != nullptr) {
    media_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    media_type->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_Float);
    media_type->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 32);
    hr = reader->SetCurrentMediaType(
        static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), nullptr,
        media_type);
    media_type->Release();
  }
  if (FAILED(hr)) {
    reader->Release();
    error = "MediaFoundation could not negotiate float PCM for probing";
    return std::nullopt;
  }

  IMFMediaType* current_type = nullptr;
  hr = reader->GetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM),
                                   &current_type);
  if (FAILED(hr) || current_type == nullptr) {
    reader->Release();
    error = "MediaFoundation unable to get audio stream media type";
    return std::nullopt;
  }

  UINT32 channels = 2;
  UINT32 sample_rate = 44100;
  UINT32 bits_per_sample = 32;
  current_type->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &channels);
  current_type->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &sample_rate);
  current_type->GetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, &bits_per_sample);
  current_type->Release();

  PROPVARIANT var;
  PropVariantInit(&var);
  std::int64_t total_frames = 0;
  if (SUCCEEDED(reader->GetPresentationAttribute(
          static_cast<DWORD>(MF_SOURCE_READER_MEDIASOURCE), MF_PD_DURATION, &var))) {
    if (var.vt == VT_UI8 || var.vt == VT_I8) {
      const LONGLONG duration_100ns = var.vt == VT_UI8
          ? static_cast<LONGLONG>(var.uhVal.QuadPart)
          : var.hVal.QuadPart;
      const double duration_sec = static_cast<double>(duration_100ns) / 10000000.0;
      total_frames = static_cast<std::int64_t>(std::round(duration_sec * sample_rate));
    }
  }
  PropVariantClear(&var);

  reader->Release();

  AudioMetadata meta{};
  meta.channels = static_cast<int>(channels);
  meta.sample_rate = static_cast<int>(sample_rate);
  meta.bit_depth = static_cast<int>(bits_per_sample);
  meta.frames = total_frames;
  meta.seekable = true;
  meta.channel_layout = channel_layout_from_count(meta.channels);
  meta.container = container_from_extension(path);

  switch (meta.container) {
    case AudioContainer::mp3:
      meta.container_name = "MP3";
      meta.sample_format = AudioSampleFormat::compressed;
      meta.sample_format_name = "MP3";
      break;
    case AudioContainer::aac_m4a:
      meta.container_name = "AAC / M4A";
      meta.sample_format = AudioSampleFormat::compressed;
      meta.sample_format_name = "AAC";
      break;
    case AudioContainer::ogg:
      meta.container_name = "OGG";
      meta.sample_format = AudioSampleFormat::compressed;
      meta.sample_format_name = "Vorbis";
      break;
    case AudioContainer::opus:
      meta.container_name = "Opus";
      meta.sample_format = AudioSampleFormat::compressed;
      meta.sample_format_name = "Opus";
      break;
    case AudioContainer::flac:
      meta.container_name = "FLAC";
      meta.sample_format = AudioSampleFormat::pcm24;
      meta.sample_format_name = "PCM_24";
      break;
    case AudioContainer::aiff:
      meta.container_name = "AIFF";
      meta.sample_format = AudioSampleFormat::pcm24;
      meta.sample_format_name = "PCM_24";
      break;
    default:
      meta.container_name = "WAV";
      meta.sample_format = AudioSampleFormat::pcm24;
      meta.sample_format_name = "PCM_24";
      break;
  }

  return meta;
}

std::unique_ptr<IAudioDecoder> MediaFoundationCodecService::open_decoder(
    const std::filesystem::path& path, std::string& error) const {
  if (!available()) {
    error = backend_error();
    return nullptr;
  }

  auto apartment = std::make_shared<ComApartment>();
  if (!apartment->usable()) {
    error = "COM initialization failed for Media Foundation decoding";
    return nullptr;
  }

  const std::wstring wpath = path.wstring();
  IMFSourceReader* reader = nullptr;
  HRESULT hr = MFCreateSourceReaderFromURL(wpath.c_str(), nullptr, &reader);
  if (FAILED(hr) || reader == nullptr) {
    error = "MediaFoundation unable to open audio decoder for: " + path.string();
    return nullptr;
  }

  IMFMediaType* media_type = nullptr;
  hr = MFCreateMediaType(&media_type);
  if (FAILED(hr) || media_type == nullptr) {
    reader->Release();
    error = "MediaFoundation unable to create media type";
    return nullptr;
  }

  media_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
  media_type->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_Float);
  media_type->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 32);

  hr = reader->SetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM),
                                   nullptr, media_type);
  media_type->Release();

  if (FAILED(hr)) {
    reader->Release();
    error = "MediaFoundation unable to configure audio decoder output format";
    return nullptr;
  }

  auto probe_meta = probe(path, error);
  if (!probe_meta) {
    reader->Release();
    return nullptr;
  }

  return std::make_unique<MediaFoundationDecoder>(
      reader, std::move(*probe_meta), std::move(apartment));
}

std::unique_ptr<IAudioEncoder> MediaFoundationCodecService::open_encoder(
    const std::filesystem::path& path, const EncodeSettings& settings,
    std::string& error) const {
  if (!available()) {
    error = backend_error();
    return nullptr;
  }
  if (settings.sample_rate <= 0 || settings.channels <= 0 ||
      container_from_extension(path) != settings.container ||
      (settings.container != AudioContainer::mp3 &&
       settings.container != AudioContainer::aac_m4a &&
       settings.container != AudioContainer::wav)) {
    error = "invalid or unsupported Media Foundation encoder settings";
    return nullptr;
  }

  auto apartment = std::make_shared<ComApartment>();
  if (!apartment->usable()) {
    error = "COM initialization failed for Media Foundation encoding";
    return nullptr;
  }

  const std::wstring wpath = path.wstring();
  IMFSinkWriter* sink_writer = nullptr;
  HRESULT hr = MFCreateSinkWriterFromURL(wpath.c_str(), nullptr, nullptr, &sink_writer);
  if (FAILED(hr) || sink_writer == nullptr) {
    error = "MediaFoundation unable to create sink writer for: " + path.string();
    return nullptr;
  }

  IMFMediaType* out_type = nullptr;
  hr = MFCreateMediaType(&out_type);
  if (FAILED(hr) || out_type == nullptr) {
    sink_writer->Release();
    error = "MediaFoundation unable to create output media type";
    return nullptr;
  }

  out_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
  if (settings.container == AudioContainer::mp3) {
    out_type->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_MP3);
    out_type->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 40000); // 320 kbps
  } else if (settings.container == AudioContainer::aac_m4a) {
    out_type->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC);
    out_type->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 32000); // 256 kbps
  } else {
    out_type->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
    out_type->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    out_type->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, static_cast<UINT32>(settings.channels * 2));
    out_type->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND,
                        static_cast<UINT32>(settings.sample_rate * settings.channels * 2));
  }

  out_type->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, static_cast<UINT32>(settings.channels));
  out_type->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, static_cast<UINT32>(settings.sample_rate));

  DWORD stream_index = 0;
  hr = sink_writer->AddStream(out_type, &stream_index);
  out_type->Release();

  if (FAILED(hr)) {
    sink_writer->Release();
    error = "MediaFoundation AddStream failed for requested audio format";
    return nullptr;
  }

  IMFMediaType* in_type = nullptr;
  hr = MFCreateMediaType(&in_type);
  if (FAILED(hr) || in_type == nullptr) {
    sink_writer->Release();
    error = "MediaFoundation unable to create input media type";
    return nullptr;
  }

  in_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
  in_type->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_Float);
  in_type->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 32);
  in_type->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, static_cast<UINT32>(settings.channels));
  in_type->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, static_cast<UINT32>(settings.sample_rate));
  in_type->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, static_cast<UINT32>(settings.channels * sizeof(float)));
  in_type->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND,
                     static_cast<UINT32>(settings.sample_rate * settings.channels * sizeof(float)));

  hr = sink_writer->SetInputMediaType(stream_index, in_type, nullptr);
  in_type->Release();

  if (FAILED(hr)) {
    sink_writer->Release();
    error = "MediaFoundation SetInputMediaType failed";
    return nullptr;
  }

  hr = sink_writer->BeginWriting();
  if (FAILED(hr)) {
    sink_writer->Release();
    error = "MediaFoundation BeginWriting failed";
    return nullptr;
  }

  return std::make_unique<MediaFoundationEncoder>(sink_writer, stream_index,
                                                 settings.sample_rate,
                                                 settings.channels,
                                                 std::move(apartment));
}

}  // namespace amt::codec

#else

namespace amt::codec {
struct MediaFoundationCodecService::Impl {};
MediaFoundationCodecService::MediaFoundationCodecService() : impl_(std::make_unique<Impl>()) {}
MediaFoundationCodecService::~MediaFoundationCodecService() = default;
MediaFoundationCodecService::MediaFoundationCodecService(MediaFoundationCodecService&&) noexcept = default;
MediaFoundationCodecService& MediaFoundationCodecService::operator=(MediaFoundationCodecService&&) noexcept = default;
bool MediaFoundationCodecService::available() const noexcept { return false; }
std::string MediaFoundationCodecService::backend_name() const { return "None (Non-Windows)"; }
std::string MediaFoundationCodecService::backend_error() const { return "Windows Media Foundation is only available on Windows"; }
std::vector<CodecCapability> MediaFoundationCodecService::capabilities() const { return {}; }
std::optional<AudioMetadata> MediaFoundationCodecService::probe(const std::filesystem::path&, std::string& error) const {
  error = backend_error();
  return std::nullopt;
}
std::unique_ptr<IAudioDecoder> MediaFoundationCodecService::open_decoder(const std::filesystem::path&, std::string& error) const {
  error = backend_error();
  return nullptr;
}
std::unique_ptr<IAudioEncoder> MediaFoundationCodecService::open_encoder(const std::filesystem::path&, const EncodeSettings&, std::string& error) const {
  error = backend_error();
  return nullptr;
}
}  // namespace amt::codec

#endif
