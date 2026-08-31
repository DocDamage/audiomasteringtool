#include "amt/codec/SndFileDynamic.h"

#include <algorithm>
#include <array>
#include <cstdint>
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
constexpr int kFlac = 0x170000;
constexpr int kPcm16 = 0x0002;
constexpr int kPcm24 = 0x0003;
constexpr int kPcm32 = 0x0004;
constexpr SfCount kBlockFrames = 4096;

using OpenFn = SndFile* (*)(const char*, int, SfInfo*);
#ifdef _WIN32
using WcharOpenFn = SndFile* (*)(const wchar_t*, int, SfInfo*);
#endif
using CloseFn = int (*)(SndFile*);
using ReadFramesIntFn = SfCount (*)(SndFile*, int*, SfCount);
using WriteFramesIntFn = SfCount (*)(SndFile*, const int*, SfCount);
using StrErrorFn = const char* (*)(SndFile*);
using FormatCheckFn = int (*)(const SfInfo*);

#ifdef _WIN32
using ModuleHandle = HMODULE;
using SymbolHandle = FARPROC;
void close_module(ModuleHandle handle) {
  if (handle != nullptr) FreeLibrary(handle);
}
SymbolHandle load_symbol(ModuleHandle handle, const char* name) {
  return GetProcAddress(handle, name);
}
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
SymbolHandle load_symbol(ModuleHandle handle, const char* name) {
  return dlsym(handle, name);
}
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

std::string last_error(StrErrorFn error_fn, SndFile* file, std::string fallback) {
  if (error_fn != nullptr) {
    if (const char* message = error_fn(file); message != nullptr && message[0] != '\0') {
      return message;
    }
  }
  return fallback;
}

bool metadata_compatible(const SfInfo& a, const SfInfo& b) {
  return a.frames == b.frames && a.samplerate == b.samplerate && a.channels == b.channels &&
         (a.format & kTypeMask) == (b.format & kTypeMask) &&
         (a.format & kSubtypeMask) == (b.format & kSubtypeMask);
}

}  // namespace

struct SndFileRuntime::Impl {
  ModuleHandle module{nullptr};
  OpenFn open{nullptr};
#ifdef _WIN32
  WcharOpenFn wchar_open{nullptr};
#endif
  CloseFn close{nullptr};
  ReadFramesIntFn read_frames_int{nullptr};
  WriteFramesIntFn write_frames_int{nullptr};
  StrErrorFn strerror_fn{nullptr};
  FormatCheckFn format_check{nullptr};
  std::string error;

  Impl() {
    module = open_module();
    if (module == nullptr) {
      error = "libsndfile runtime not found";
      return;
    }

    open = resolve<OpenFn>(module, "sf_open");
#ifdef _WIN32
    wchar_open = resolve<WcharOpenFn>(module, "sf_wchar_open");
#endif
    close = resolve<CloseFn>(module, "sf_close");
    read_frames_int = resolve<ReadFramesIntFn>(module, "sf_readf_int");
    write_frames_int = resolve<WriteFramesIntFn>(module, "sf_writef_int");
    strerror_fn = resolve<StrErrorFn>(module, "sf_strerror");
    format_check = resolve<FormatCheckFn>(module, "sf_format_check");

    if (open == nullptr || close == nullptr || read_frames_int == nullptr ||
        write_frames_int == nullptr || strerror_fn == nullptr || format_check == nullptr) {
      error = "libsndfile runtime is missing required symbols";
      close_module(module);
      module = nullptr;
    }
  }

  ~Impl() { close_module(module); }

  [[nodiscard]] bool ready() const noexcept { return module != nullptr; }

  SndFile* open_path(const std::filesystem::path& path, int mode, SfInfo* info) const {
#ifdef _WIN32
    if (wchar_open != nullptr) return wchar_open(path.c_str(), mode, info);
#endif
    return open(path.string().c_str(), mode, info);
  }
};

SndFileRuntime::SndFileRuntime() : impl_(new Impl()) {}
SndFileRuntime::~SndFileRuntime() { delete impl_; }
SndFileRuntime::SndFileRuntime(SndFileRuntime&& other) noexcept : impl_(other.impl_) {
  other.impl_ = nullptr;
}
SndFileRuntime& SndFileRuntime::operator=(SndFileRuntime&& other) noexcept {
  if (this != &other) {
    delete impl_;
    impl_ = other.impl_;
    other.impl_ = nullptr;
  }
  return *this;
}

bool SndFileRuntime::available() const noexcept { return impl_ != nullptr && impl_->ready(); }
std::string SndFileRuntime::load_error() const {
  return impl_ == nullptr ? "runtime moved-from" : impl_->error;
}

bool is_phase0_baseline_format(const int format) noexcept {
  const int container = format & kTypeMask;
  const int subtype = format & kSubtypeMask;
  const bool supported_container = container == kWav || container == kFlac;
  const bool supported_subtype = subtype == kPcm16 || subtype == kPcm24 || subtype == kPcm32;
  return supported_container && supported_subtype;
}

std::string describe_container(const int format) {
  switch (format & kTypeMask) {
    case kWav:
      return "WAV";
    case kFlac:
      return "FLAC";
    default:
      return "unsupported";
  }
}

std::string describe_subtype(const int format) {
  switch (format & kSubtypeMask) {
    case kPcm16:
      return "PCM_16";
    case kPcm24:
      return "PCM_24";
    case kPcm32:
      return "PCM_32";
    default:
      return "unsupported";
  }
}

std::optional<AudioFileInfo> SndFileRuntime::probe(
    const std::filesystem::path& path, std::string& error) const {
  if (!available()) {
    error = load_error();
    return std::nullopt;
  }

  SfInfo info{};
  SndFile* file = impl_->open_path(path, kReadMode, &info);
  if (file == nullptr) {
    error = last_error(impl_->strerror_fn, nullptr, "unable to open input file");
    return std::nullopt;
  }
  impl_->close(file);

  if (!is_phase0_baseline_format(info.format)) {
    error = "Phase 0 supports integer PCM WAV/FLAC only";
    return std::nullopt;
  }

  return AudioFileInfo{.frames = info.frames,
                       .sample_rate = info.samplerate,
                       .channels = info.channels,
                       .format = info.format,
                       .container = describe_container(info.format),
                       .subtype = describe_subtype(info.format)};
}

bool SndFileRuntime::lossless_rerender(
    const std::filesystem::path& input,
    const std::filesystem::path& output,
    std::string& error) const {
  if (!available()) {
    error = load_error();
    return false;
  }

  SfInfo input_info{};
  SndFile* source = impl_->open_path(input, kReadMode, &input_info);
  if (source == nullptr) {
    error = last_error(impl_->strerror_fn, nullptr, "unable to open input file");
    return false;
  }

  if (!is_phase0_baseline_format(input_info.format)) {
    error = "Phase 0 supports integer PCM WAV/FLAC only";
    impl_->close(source);
    return false;
  }

  SfInfo output_info{};
  output_info.samplerate = input_info.samplerate;
  output_info.channels = input_info.channels;
  output_info.format = input_info.format;
  if (impl_->format_check(&output_info) == 0) {
    error = "libsndfile rejected the output format";
    impl_->close(source);
    return false;
  }

  SndFile* destination = impl_->open_path(output, kWriteMode, &output_info);
  if (destination == nullptr) {
    error = last_error(impl_->strerror_fn, nullptr, "unable to open output file");
    impl_->close(source);
    return false;
  }

  std::vector<int> buffer(static_cast<std::size_t>(kBlockFrames) *
                          static_cast<std::size_t>(input_info.channels));
  bool ok = true;
  while (true) {
    const SfCount read = impl_->read_frames_int(source, buffer.data(), kBlockFrames);
    if (read < 0) {
      error = last_error(impl_->strerror_fn, source, "audio read failed");
      ok = false;
      break;
    }
    if (read == 0) break;
    const SfCount written = impl_->write_frames_int(destination, buffer.data(), read);
    if (written != read) {
      error = last_error(impl_->strerror_fn, destination, "audio write failed");
      ok = false;
      break;
    }
  }

  if (impl_->close(destination) != 0 && ok) {
    error = "failed to finalize output file";
    ok = false;
  }
  impl_->close(source);
  return ok;
}

bool SndFileRuntime::verify_pcm_equal(
    const std::filesystem::path& a,
    const std::filesystem::path& b,
    std::string& error) const {
  if (!available()) {
    error = load_error();
    return false;
  }

  SfInfo a_info{};
  SfInfo b_info{};
  SndFile* a_file = impl_->open_path(a, kReadMode, &a_info);
  if (a_file == nullptr) {
    error = last_error(impl_->strerror_fn, nullptr, "unable to open first file");
    return false;
  }
  SndFile* b_file = impl_->open_path(b, kReadMode, &b_info);
  if (b_file == nullptr) {
    error = last_error(impl_->strerror_fn, nullptr, "unable to open second file");
    impl_->close(a_file);
    return false;
  }

  bool ok = true;
  if (!metadata_compatible(a_info, b_info)) {
    error = "audio metadata/format mismatch";
    ok = false;
  }

  const auto channels = static_cast<std::size_t>(std::max(a_info.channels, 1));
  std::vector<int> a_buffer(static_cast<std::size_t>(kBlockFrames) * channels);
  std::vector<int> b_buffer(static_cast<std::size_t>(kBlockFrames) * channels);
  std::int64_t compared_frames = 0;

  while (ok) {
    const SfCount a_read = impl_->read_frames_int(a_file, a_buffer.data(), kBlockFrames);
    const SfCount b_read = impl_->read_frames_int(b_file, b_buffer.data(), kBlockFrames);
    if (a_read != b_read) {
      error = "decoded frame-count mismatch";
      ok = false;
      break;
    }
    if (a_read <= 0) break;

    const auto sample_count = static_cast<std::size_t>(a_read) * channels;
    if (!std::equal(a_buffer.begin(), a_buffer.begin() + static_cast<std::ptrdiff_t>(sample_count),
                    b_buffer.begin())) {
      error = "decoded PCM mismatch near frame " + std::to_string(compared_frames);
      ok = false;
      break;
    }
    compared_frames += a_read;
  }

  impl_->close(a_file);
  impl_->close(b_file);
  return ok;
}

}  // namespace amt::codec
