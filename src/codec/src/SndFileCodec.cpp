#include "amt/codec/SndFileCodec.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace amt::codec {
namespace {

using SfCount = std::int64_t;
struct SfInfo {
  SfCount frames;
  int samplerate;
  int channels;
  int format;
  int sections;
  int seekable;
};
struct SndFileOpaque;
using SndFile = SndFileOpaque;

constexpr int kReadMode = 0x10;
constexpr int kWriteMode = 0x20;
constexpr int kTypeMask = 0x0FFF0000;
constexpr int kSubtypeMask = 0x0000FFFF;
constexpr int kWav = 0x010000;
constexpr int kAiff = 0x020000;
constexpr int kFlac = 0x170000;
constexpr int kOgg = 0x200000;
constexpr int kMpeg = 0x230000;
constexpr int kPcm16 = 0x0002;
constexpr int kPcm24 = 0x0003;
constexpr int kPcm32 = 0x0004;
constexpr int kFloat = 0x0006;
constexpr int kDouble = 0x0007;
constexpr int kVorbis = 0x0060;
constexpr int kOpus = 0x0064;
constexpr int kMpegLayer3 = 0x0082;
constexpr int kStringTitle = 0x01;
constexpr int kStringCopyright = 0x02;
constexpr int kStringSoftware = 0x03;
constexpr int kStringArtist = 0x04;
constexpr int kStringComment = 0x05;
constexpr int kStringDate = 0x06;
constexpr int kStringAlbum = 0x07;
constexpr int kStringLicense = 0x08;
constexpr int kStringTrackNumber = 0x09;
constexpr int kStringGenre = 0x10;

using OpenFn = SndFile* (*)(const char*, int, SfInfo*);
#ifdef _WIN32
using WcharOpenFn = SndFile* (*)(const wchar_t*, int, SfInfo*);
#endif
using CloseFn = int (*)(SndFile*);
using ReadFramesFloatFn = SfCount (*)(SndFile*, float*, SfCount);
using WriteFramesFloatFn = SfCount (*)(SndFile*, const float*, SfCount);
using SeekFn = SfCount (*)(SndFile*, SfCount, int);
using StrErrorFn = const char* (*)(SndFile*);
using ErrorFn = int (*)(SndFile*);
using FormatCheckFn = int (*)(const SfInfo*);
using GetStringFn = const char* (*)(SndFile*, int);
using SetStringFn = int (*)(SndFile*, int, const char*);
using VersionFn = const char* (*)();

#ifdef _WIN32
using ModuleHandle = HMODULE;
using SymbolHandle = FARPROC;
void close_module(ModuleHandle handle) {
  if (handle != nullptr) FreeLibrary(handle);
}
SymbolHandle load_symbol(ModuleHandle handle, const char* name) { return GetProcAddress(handle, name); }
ModuleHandle open_module() {
  constexpr std::array<const wchar_t*, 3> names = {
      L"sndfile.dll", L"libsndfile-1.dll", L"libsndfile.dll"};
  for (const auto* name : names) {
    if (auto handle = LoadLibraryW(name); handle != nullptr) return handle;
  }
  return nullptr;
}
#else
using ModuleHandle = void*;
using SymbolHandle = void*;
void close_module(ModuleHandle handle) {
  if (handle != nullptr) dlclose(handle);
}
SymbolHandle load_symbol(ModuleHandle handle, const char* name) { return dlsym(handle, name); }
ModuleHandle open_module() {
#if defined(__APPLE__)
  constexpr std::array<const char*, 3> names = {
      "libsndfile.1.dylib", "libsndfile.dylib", "/opt/homebrew/lib/libsndfile.dylib"};
#else
  constexpr std::array<const char*, 2> names = {"libsndfile.so.1", "libsndfile.so"};
#endif
  for (const auto* name : names) {
    if (auto handle = dlopen(name, RTLD_NOW | RTLD_LOCAL); handle != nullptr) return handle;
  }
  return nullptr;
}
#endif

template <typename T>
T resolve(ModuleHandle module, const char* name) {
  return reinterpret_cast<T>(load_symbol(module, name));
}

AudioContainer container_from_format(const int format) {
  switch (format & kTypeMask) {
    case kWav:
      return AudioContainer::wav;
    case kAiff:
      return AudioContainer::aiff;
    case kFlac:
      return AudioContainer::flac;
    case kOgg:
      return AudioContainer::ogg;
    case kMpeg:
      return AudioContainer::mp3;
    default:
      return AudioContainer::unknown;
  }
}

AudioSampleFormat sample_format_from_format(const int format) {
  switch (format & kSubtypeMask) {
    case kPcm16:
      return AudioSampleFormat::pcm16;
    case kPcm24:
      return AudioSampleFormat::pcm24;
    case kPcm32:
      return AudioSampleFormat::pcm32;
    case kFloat:
      return AudioSampleFormat::float32;
    case kDouble:
      return AudioSampleFormat::float64;
    case kVorbis:
    case kOpus:
    case kMpegLayer3:
      return AudioSampleFormat::compressed;
    default:
      return AudioSampleFormat::unknown;
  }
}

std::string container_name(const AudioContainer container) {
  switch (container) {
    case AudioContainer::wav:
      return "WAV";
    case AudioContainer::aiff:
      return "AIFF";
    case AudioContainer::flac:
      return "FLAC";
    case AudioContainer::ogg:
      return "OGG";
    case AudioContainer::mp3:
      return "MP3";
    case AudioContainer::opus:
      return "Opus";
    case AudioContainer::aac_m4a:
      return "AAC/M4A";
    default:
      return "unknown";
  }
}

std::string sample_format_name(const AudioSampleFormat format) {
  switch (format) {
    case AudioSampleFormat::pcm16:
      return "PCM_16";
    case AudioSampleFormat::pcm24:
      return "PCM_24";
    case AudioSampleFormat::pcm32:
      return "PCM_32";
    case AudioSampleFormat::float32:
      return "FLOAT_32";
    case AudioSampleFormat::float64:
      return "FLOAT_64";
    case AudioSampleFormat::compressed:
      return "compressed";
    default:
      return "unknown";
  }
}

int format_code(const AudioContainer container, const AudioSampleFormat sample_format) {
  int major = 0;
  switch (container) {
    case AudioContainer::wav:
      major = kWav;
      break;
    case AudioContainer::aiff:
      major = kAiff;
      break;
    case AudioContainer::flac:
      major = kFlac;
      break;
    case AudioContainer::ogg:
      major = kOgg;
      break;
    case AudioContainer::mp3:
      major = kMpeg;
      break;
    default:
      return 0;
  }
  int subtype = 0;
  switch (sample_format) {
    case AudioSampleFormat::pcm16:
      subtype = kPcm16;
      break;
    case AudioSampleFormat::pcm24:
      subtype = kPcm24;
      break;
    case AudioSampleFormat::pcm32:
      subtype = kPcm32;
      break;
    case AudioSampleFormat::float32:
      subtype = kFloat;
      break;
    case AudioSampleFormat::float64:
      subtype = kDouble;
      break;
    case AudioSampleFormat::compressed:
      subtype = container == AudioContainer::ogg ? kVorbis :
                (container == AudioContainer::mp3 ? kMpegLayer3 : 0);
      break;
    default:
      return 0;
  }
  return subtype == 0 ? 0 : major | subtype;
}

int bit_depth(const AudioSampleFormat format) {
  switch (format) {
    case AudioSampleFormat::pcm16:
      return 16;
    case AudioSampleFormat::pcm24:
      return 24;
    case AudioSampleFormat::pcm32:
    case AudioSampleFormat::float32:
      return 32;
    case AudioSampleFormat::float64:
      return 64;
    default:
      return 0;
  }
}

}  // namespace

struct SndFileCodecService::Impl {
  struct Api {
    ModuleHandle module{nullptr};
    OpenFn open{nullptr};
#ifdef _WIN32
    WcharOpenFn wchar_open{nullptr};
#endif
    CloseFn close{nullptr};
    ReadFramesFloatFn read_frames_float{nullptr};
    WriteFramesFloatFn write_frames_float{nullptr};
    SeekFn seek{nullptr};
    StrErrorFn strerror_fn{nullptr};
    ErrorFn error_fn{nullptr};
    FormatCheckFn format_check{nullptr};
    GetStringFn get_string{nullptr};
    SetStringFn set_string{nullptr};
    VersionFn version{nullptr};
    std::string load_error;

    Api() {
      module = open_module();
      if (module == nullptr) {
        load_error = "libsndfile runtime not found";
        return;
      }
      open = resolve<OpenFn>(module, "sf_open");
#ifdef _WIN32
      wchar_open = resolve<WcharOpenFn>(module, "sf_wchar_open");
#endif
      close = resolve<CloseFn>(module, "sf_close");
      read_frames_float = resolve<ReadFramesFloatFn>(module, "sf_readf_float");
      write_frames_float = resolve<WriteFramesFloatFn>(module, "sf_writef_float");
      seek = resolve<SeekFn>(module, "sf_seek");
      strerror_fn = resolve<StrErrorFn>(module, "sf_strerror");
      error_fn = resolve<ErrorFn>(module, "sf_error");
      format_check = resolve<FormatCheckFn>(module, "sf_format_check");
      get_string = resolve<GetStringFn>(module, "sf_get_string");
      set_string = resolve<SetStringFn>(module, "sf_set_string");
      version = resolve<VersionFn>(module, "sf_version_string");
      if (open == nullptr || close == nullptr || read_frames_float == nullptr ||
          write_frames_float == nullptr || seek == nullptr || strerror_fn == nullptr ||
          error_fn == nullptr || format_check == nullptr) {
        load_error = "libsndfile runtime is missing required Phase 1 symbols";
        close_module(module);
        module = nullptr;
      }
    }

    ~Api() { close_module(module); }
    [[nodiscard]] bool ready() const noexcept { return module != nullptr; }

    SndFile* open_path(const std::filesystem::path& path, const int mode, SfInfo* info) const {
#ifdef _WIN32
      if (wchar_open != nullptr) return wchar_open(path.c_str(), mode, info);
#endif
      return open(path.string().c_str(), mode, info);
    }

    [[nodiscard]] std::string last_error(SndFile* file, const std::string& fallback) const {
      if (strerror_fn != nullptr) {
        if (const char* text = strerror_fn(file); text != nullptr && text[0] != '\0') return text;
      }
      return fallback;
    }
  };

  std::shared_ptr<Api> api{std::make_shared<Api>()};
};

namespace {

std::map<std::string, std::string> read_tags(
    const std::shared_ptr<SndFileCodecService::Impl::Api>& api, SndFile* file) {
  std::map<std::string, std::string> tags;
  if (api->get_string == nullptr) return tags;
  const std::array<std::pair<int, const char*>, 10> keys = {{{kStringTitle, "title"},
      {kStringCopyright, "copyright"}, {kStringSoftware, "software"}, {kStringArtist, "artist"},
      {kStringComment, "comment"}, {kStringDate, "date"}, {kStringAlbum, "album"},
      {kStringLicense, "license"}, {kStringTrackNumber, "track"}, {kStringGenre, "genre"}}};
  for (const auto& [id, name] : keys) {
    if (const char* value = api->get_string(file, id); value != nullptr && value[0] != '\0') {
      tags.emplace(name, value);
    }
  }
  return tags;
}

AudioMetadata make_metadata(
    const std::shared_ptr<SndFileCodecService::Impl::Api>& api, SndFile* file,
    const SfInfo& info) {
  const auto container = container_from_format(info.format);
  const auto sample_format = sample_format_from_format(info.format);
  return {.frames = info.frames,
          .sample_rate = info.samplerate,
          .channels = info.channels,
          .bit_depth = bit_depth(sample_format),
          .seekable = info.seekable != 0,
          .container = container,
          .sample_format = sample_format,
          .container_name = container_name(container),
          .sample_format_name = sample_format_name(sample_format),
          .tags = read_tags(api, file)};
}

class SndFileDecoder final : public IAudioDecoder {
 public:
  SndFileDecoder(std::shared_ptr<SndFileCodecService::Impl::Api> api, SndFile* file,
                 AudioMetadata metadata)
      : api_(std::move(api)), file_(file), metadata_(std::move(metadata)) {}
  ~SndFileDecoder() override {
    if (file_ != nullptr) api_->close(file_);
  }

  const AudioMetadata& metadata() const noexcept override { return metadata_; }
  std::int64_t tell() const noexcept override { return position_; }

  bool seek(const std::int64_t frame, std::string& error) override {
    if (!metadata_.seekable) {
      error = "decoder input is not seekable";
      return false;
    }
    if (frame < 0 || frame > metadata_.frames) {
      error = "seek frame is outside the source";
      return false;
    }
    const auto result = api_->seek(file_, frame, SEEK_SET);
    if (result < 0) {
      error = api_->last_error(file_, "audio seek failed");
      return false;
    }
    position_ = result;
    return true;
  }

  bool read(amt::audio::AudioBuffer& output, const std::size_t max_frames,
            std::size_t& frames_read, std::string& error,
            const amt::core::CancellationToken* cancellation) override {
    frames_read = 0U;
    if (cancellation != nullptr && cancellation->is_cancelled()) {
      error = "audio read cancelled";
      return false;
    }
    if (max_frames == 0U) {
      output.resize(static_cast<std::size_t>(metadata_.channels), 0U);
      return true;
    }
    std::vector<float> interleaved(max_frames * static_cast<std::size_t>(metadata_.channels));
    const auto read_count = api_->read_frames_float(
        file_, interleaved.data(), static_cast<SfCount>(max_frames));
    if (read_count < 0 || (read_count == 0 && api_->error_fn(file_) != 0)) {
      error = api_->last_error(file_, "audio read failed");
      return false;
    }
    frames_read = static_cast<std::size_t>(read_count);
    interleaved.resize(frames_read * static_cast<std::size_t>(metadata_.channels));
    output = amt::audio::AudioBuffer::from_interleaved(
        interleaved, static_cast<std::size_t>(metadata_.channels));
    position_ += read_count;
    return true;
  }

 private:
  std::shared_ptr<SndFileCodecService::Impl::Api> api_;
  SndFile* file_{nullptr};
  AudioMetadata metadata_;
  std::int64_t position_{0};
};

class SndFileEncoder final : public IAudioEncoder {
 public:
  SndFileEncoder(std::shared_ptr<SndFileCodecService::Impl::Api> api, SndFile* file,
                 const int channels)
      : api_(std::move(api)), file_(file), channels_(channels) {}
  ~SndFileEncoder() override {
    if (file_ != nullptr) api_->close(file_);
  }

  bool write(const amt::audio::AudioBuffer& input, std::string& error,
             const amt::core::CancellationToken* cancellation) override {
    if (file_ == nullptr) {
      error = "encoder is already finalized";
      return false;
    }
    if (cancellation != nullptr && cancellation->is_cancelled()) {
      error = "audio write cancelled";
      return false;
    }
    if (input.channels() != static_cast<std::size_t>(channels_)) {
      error = "encoder channel-count mismatch";
      return false;
    }
    std::vector<float> interleaved;
    input.to_interleaved(interleaved);
    const auto written = api_->write_frames_float(
        file_, interleaved.data(), static_cast<SfCount>(input.frames()));
    if (written != static_cast<SfCount>(input.frames())) {
      error = api_->last_error(file_, "audio write failed");
      return false;
    }
    return true;
  }

  bool finalize(std::string& error) override {
    if (file_ == nullptr) return true;
    if (api_->close(file_) != 0) {
      file_ = nullptr;
      error = "failed to finalize encoded audio";
      return false;
    }
    file_ = nullptr;
    return true;
  }

 private:
  std::shared_ptr<SndFileCodecService::Impl::Api> api_;
  SndFile* file_{nullptr};
  int channels_{0};
};

void write_tags(const std::shared_ptr<SndFileCodecService::Impl::Api>& api, SndFile* file,
                const std::map<std::string, std::string>& tags) {
  if (api->set_string == nullptr) return;
  const std::array<std::pair<const char*, int>, 10> keys = {{{"title", kStringTitle},
      {"copyright", kStringCopyright}, {"software", kStringSoftware}, {"artist", kStringArtist},
      {"comment", kStringComment}, {"date", kStringDate}, {"album", kStringAlbum},
      {"license", kStringLicense}, {"track", kStringTrackNumber}, {"genre", kStringGenre}}};
  for (const auto& [name, id] : keys) {
    const auto iterator = tags.find(name);
    if (iterator != tags.end()) api->set_string(file, id, iterator->second.c_str());
  }
}

}  // namespace

SndFileCodecService::SndFileCodecService() : impl_(std::make_unique<Impl>()) {}
SndFileCodecService::~SndFileCodecService() = default;
SndFileCodecService::SndFileCodecService(SndFileCodecService&&) noexcept = default;
SndFileCodecService& SndFileCodecService::operator=(SndFileCodecService&&) noexcept = default;

bool SndFileCodecService::available() const noexcept {
  return impl_ != nullptr && impl_->api != nullptr && impl_->api->ready();
}

std::string SndFileCodecService::backend_name() const {
  if (!available()) return "libsndfile unavailable";
  if (impl_->api->version != nullptr) return impl_->api->version();
  return "libsndfile";
}

std::string SndFileCodecService::backend_error() const {
  return impl_ == nullptr || impl_->api == nullptr ? "codec service moved-from" : impl_->api->load_error;
}

std::vector<CodecCapability> SndFileCodecService::capabilities() const {
  return {{.container = AudioContainer::wav,
           .name = "WAV",
           .extensions = {"wav", "wave"},
           .decode = available(),
           .encode = available(),
           .lossless = true},
          {.container = AudioContainer::aiff,
           .name = "AIFF",
           .extensions = {"aif", "aiff"},
           .decode = available(),
           .encode = available(),
           .lossless = true},
          {.container = AudioContainer::flac,
           .name = "FLAC",
           .extensions = {"flac"},
           .decode = available(),
           .encode = available(),
           .lossless = true}};
}

std::optional<AudioMetadata> SndFileCodecService::probe(
    const std::filesystem::path& path, std::string& error) const {
  if (!available()) {
    error = backend_error();
    return std::nullopt;
  }
  SfInfo info{};
  SndFile* file = impl_->api->open_path(path, kReadMode, &info);
  if (file == nullptr) {
    error = impl_->api->last_error(nullptr, "unable to open audio file");
    return std::nullopt;
  }
  auto metadata = make_metadata(impl_->api, file, info);
  impl_->api->close(file);
  if (metadata.container == AudioContainer::unknown ||
      metadata.sample_format == AudioSampleFormat::unknown) {
    error = "audio format is not supported by the Phase 1 production codec backend";
    return std::nullopt;
  }
  return metadata;
}

std::unique_ptr<IAudioDecoder> SndFileCodecService::open_decoder(
    const std::filesystem::path& path, std::string& error) const {
  if (!available()) {
    error = backend_error();
    return nullptr;
  }
  SfInfo info{};
  SndFile* file = impl_->api->open_path(path, kReadMode, &info);
  if (file == nullptr) {
    error = impl_->api->last_error(nullptr, "unable to open audio decoder");
    return nullptr;
  }
  auto metadata = make_metadata(impl_->api, file, info);
  if (metadata.container == AudioContainer::unknown ||
      metadata.sample_format == AudioSampleFormat::unknown) {
    impl_->api->close(file);
    error = "decoder format is outside the controlled Phase 1 codec set";
    return nullptr;
  }
  return std::make_unique<SndFileDecoder>(impl_->api, file, std::move(metadata));
}

std::unique_ptr<IAudioEncoder> SndFileCodecService::open_encoder(
    const std::filesystem::path& path, const EncodeSettings& settings,
    std::string& error) const {
  if (!available()) {
    error = backend_error();
    return nullptr;
  }
  if (settings.sample_rate <= 0 || settings.channels <= 0) {
    error = "invalid encoder sample rate or channel count";
    return nullptr;
  }
  SfInfo info{};
  info.samplerate = settings.sample_rate;
  info.channels = settings.channels;
  info.format = format_code(settings.container, settings.sample_format);
  if (info.format == 0 || impl_->api->format_check(&info) == 0) {
    error = "requested output format is not supported by libsndfile";
    return nullptr;
  }
  SndFile* file = impl_->api->open_path(path, kWriteMode, &info);
  if (file == nullptr) {
    error = impl_->api->last_error(nullptr, "unable to open audio encoder");
    return nullptr;
  }
  write_tags(impl_->api, file, settings.tags);
  return std::make_unique<SndFileEncoder>(impl_->api, file, settings.channels);
}

}  // namespace amt::codec
