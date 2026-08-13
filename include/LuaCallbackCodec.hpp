#pragma once

#include "LuaStateLifecycle.hpp"
#include "luasf_sol.hpp"

#include <SFML/Audio/SoundSource.hpp>
#include <SFML/Graphics/Text.hpp>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace lua_sf::callback {

template <typename Signature>
std::function<Signature>
native_thread_from_object(const sol::object &object);

struct CallbackOptions {
  std::string label;
};

class BorrowLease final {
public:
  [[nodiscard]] bool active() const noexcept {
    return active_.load(std::memory_order_acquire);
  }

  void invalidate() noexcept {
    active_.store(false, std::memory_order_release);
  }

private:
  std::atomic<bool> active_{true};
};

enum class BufferAccess { readOnly, writeOnly };

template <BufferAccess Access> class FloatBufferView final {
public:
  FloatBufferView() = default;
  FloatBufferView(std::shared_ptr<BorrowLease> lease, float *data,
                  std::size_t size) noexcept
      : lease_(std::move(lease)), data_(data), size_(size) {}

  [[nodiscard]] std::size_t size() const {
    requireActive();
    return size_;
  }

  [[nodiscard]] float get(std::size_t index) const {
    requireActive();
    requireIndex(index);
    if constexpr (Access == BufferAccess::writeOnly)
      throw std::logic_error("write-only audio buffer cannot be read");
    return data_[index - 1];
  }

  void set(std::size_t index, float value) {
    requireActive();
    requireIndex(index);
    if constexpr (Access == BufferAccess::readOnly)
      throw std::logic_error("read-only audio buffer cannot be written");
    data_[index - 1] = value;
  }

  using NativePointer =
      std::conditional_t<Access == BufferAccess::readOnly, const float *,
                         float *>;

  [[nodiscard]] NativePointer nativeData() const {
    requireActive();
    return data_;
  }

  [[nodiscard]] const BorrowLease *leaseIdentity() const {
    requireActive();
    return lease_.get();
  }

private:
  void requireActive() const {
    if (lease_ == nullptr || !lease_->active())
      throw std::logic_error("borrowed audio callback value has expired");
  }

  void requireIndex(std::size_t index) const {
    if (index == 0 || index > size_)
      throw std::out_of_range("audio buffer index is out of range");
  }

  std::shared_ptr<BorrowLease> lease_;
  float *data_{};
  std::size_t size_{};
};

using ReadOnlyFloatBufferView = FloatBufferView<BufferAccess::readOnly>;
using WriteOnlyFloatBufferView = FloatBufferView<BufferAccess::writeOnly>;

class UIntRef final {
public:
  UIntRef() = default;
  UIntRef(std::shared_ptr<BorrowLease> lease, unsigned int value,
          unsigned int capacity)
      : state_(std::make_shared<State>(
            State{std::move(lease), value, capacity})) {}

  [[nodiscard]] unsigned int value() const {
    requireActive();
    return state_->value;
  }

  void setValue(unsigned int value) {
    requireActive();
    if (value > state_->capacity)
      throw std::out_of_range("audio frame count exceeds callback capacity");
    state_->value = value;
  }

  [[nodiscard]] unsigned int capacity() const {
    requireActive();
    return state_->capacity;
  }

  [[nodiscard]] const BorrowLease *leaseIdentity() const {
    requireActive();
    return state_->lease.get();
  }

private:
  struct State {
    std::shared_ptr<BorrowLease> lease;
    unsigned int value;
    unsigned int capacity;
  };

  void requireActive() const {
    if (state_ == nullptr || state_->lease == nullptr ||
        !state_->lease->active())
      throw std::logic_error("borrowed audio callback value has expired");
  }

  std::shared_ptr<State> state_;
};

struct InterleavedFloatTransformCodec;
struct GlyphPreProcessorCodec;
struct SftpDownloadBufferCodec;
struct SftpUploadBufferCodec;
struct NativeThreadBoundaryCodec;

LUASF_API void bindCallbackViews(sol::state_view lua);

namespace detail {

class CallbackContext;

LUASF_API std::shared_ptr<CallbackContext>
makeCallbackContext(const sol::object &object, std::string label);

LUASF_API void invokeInterleavedFloatTransform(
    const std::shared_ptr<CallbackContext> &context, const float *inputFrames,
    unsigned int &inputFrameCount, float *outputFrames,
    unsigned int &outputFrameCount, unsigned int frameChannelCount) noexcept;

LUASF_API void invokeGlyphPreProcessor(
    const std::shared_ptr<CallbackContext> &context,
    const sf::Text::ShapedGlyph &shapedGlyph, std::uint32_t &style,
    sf::Color &fillColor, sf::Color &outlineColor, float &outlineThickness);

LUASF_API bool invokeSftpDownload(
    const std::shared_ptr<CallbackContext> &context, const void *data,
    std::size_t size);

LUASF_API bool invokeSftpUpload(const std::shared_ptr<CallbackContext> &context,
                               void *data, std::size_t &size);

template <typename NativeCallable, typename Codec> struct FromObject;

template <>
struct FromObject<sf::SoundSource::EffectProcessor,
                  InterleavedFloatTransformCodec> {
  static sf::SoundSource::EffectProcessor read(const sol::object &object,
                                               CallbackOptions options) {
    if (!object.valid() || object == lua_sf::LUASF_SOL_NIL)
      return {};
    const auto context = makeCallbackContext(object, std::move(options.label));
    return [context](const float *inputFrames, unsigned int &inputFrameCount,
                     float *outputFrames, unsigned int &outputFrameCount,
                     unsigned int frameChannelCount) noexcept {
      invokeInterleavedFloatTransform(context, inputFrames, inputFrameCount,
                                      outputFrames, outputFrameCount,
                                      frameChannelCount);
    };
  }
};

template <>
struct FromObject<sf::Text::GlyphPreProcessor, GlyphPreProcessorCodec> {
  static sf::Text::GlyphPreProcessor read(const sol::object &object,
                                          CallbackOptions options) {
    if (!object.valid() || object == lua_sf::LUASF_SOL_NIL)
      return {};
    const auto context = makeCallbackContext(object, std::move(options.label));
    return [context](const sf::Text::ShapedGlyph &glyph, std::uint32_t &style,
                     sf::Color &fill, sf::Color &outline, float &thickness) {
      invokeGlyphPreProcessor(context, glyph, style, fill, outline, thickness);
    };
  }
};

template <>
struct FromObject<std::function<bool(const void *, std::size_t)>,
                  SftpDownloadBufferCodec> {
  static std::function<bool(const void *, std::size_t)>
  read(const sol::object &object, CallbackOptions options) {
    if (!object.valid() || object == lua_sf::LUASF_SOL_NIL)
      return {};
    const auto context = makeCallbackContext(object, std::move(options.label));
    return [context](const void *data, std::size_t size) {
      return invokeSftpDownload(context, data, size);
    };
  }
};

template <>
struct FromObject<std::function<bool(void *, std::size_t &)>,
                  SftpUploadBufferCodec> {
  static std::function<bool(void *, std::size_t &)>
  read(const sol::object &object, CallbackOptions options) {
    if (!object.valid() || object == lua_sf::LUASF_SOL_NIL)
      return {};
    const auto context = makeCallbackContext(object, std::move(options.label));
    return [context](void *data, std::size_t &size) {
      return invokeSftpUpload(context, data, size);
    };
  }
};

template <typename... Arguments>
struct FromObject<std::function<void(Arguments...)>, NativeThreadBoundaryCodec> {
  static std::function<void(Arguments...)> read(const sol::object &object,
                                                CallbackOptions) {
    return native_thread_from_object<void(Arguments...)>(object);
  }
};

} // namespace detail

template <typename NativeCallable, typename Codec>
NativeCallable from_object(const sol::object &object,
                           CallbackOptions options = {}) {
  return detail::FromObject<NativeCallable, Codec>::read(object,
                                                          std::move(options));
}

template <typename NativeCallable, typename Codec>
NativeCallable from_object(const sol::object &object, std::string_view label) {
  return from_object<NativeCallable, Codec>(
      object, CallbackOptions{std::string(label)});
}

template <typename NativeCallable, typename Codec>
sol::object to_object(sol::state_view, const NativeCallable &,
                      CallbackOptions = {}) {
  static_assert(!std::is_same_v<NativeCallable, NativeCallable>,
                "callback codec does not support conversion to Lua");
}

template <typename NativeCallable, typename Codec>
sol::object to_object(sol::state_view lua, const NativeCallable &callable,
                      std::string_view label) {
  return to_object<NativeCallable, Codec>(
      lua, callable, CallbackOptions{std::string(label)});
}

template <>
LUASF_API sol::object
to_object<sf::SoundSource::EffectProcessor,
          InterleavedFloatTransformCodec>(
    sol::state_view lua, const sf::SoundSource::EffectProcessor &callable,
    CallbackOptions options);

} // namespace lua_sf::callback
