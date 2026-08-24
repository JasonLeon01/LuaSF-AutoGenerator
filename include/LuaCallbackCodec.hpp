#pragma once

#include "LuaStateLifecycle.hpp"
#include "luasf_sol.hpp"

#include <SFML/Audio/SoundSource.hpp>
#include <SFML/Graphics/Text.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace lua_sf::callback {

template <typename Signature>
std::function<Signature> native_thread_from_object(const sol::object &object);

struct CallbackOptions {
  std::string label;
  bool allowNil{};
};

struct InterleavedFloatTransformCodec;
struct GlyphPreProcessorCodec;
struct SftpDownloadBufferCodec;
struct SftpUploadBufferCodec;
struct NativeThreadBoundaryCodec;
struct GenericCallbackCodec;

namespace detail {

class CallbackContext;

LUASF_API std::shared_ptr<CallbackContext>
makeCallbackContext(const sol::object &object, std::string label);

LUASF_API void invokeInterleavedFloatTransform(
    const std::shared_ptr<CallbackContext> &context, const float *inputFrames,
    unsigned int &inputFrameCount, float *outputFrames,
    unsigned int &outputFrameCount, unsigned int frameChannelCount) noexcept;

LUASF_API void
invokeGlyphPreProcessor(const std::shared_ptr<CallbackContext> &context,
                        const sf::Text::ShapedGlyph &shapedGlyph,
                        std::uint32_t &style, sf::Color &fillColor,
                        sf::Color &outlineColor, float &outlineThickness);

LUASF_API bool
invokeSftpDownload(const std::shared_ptr<CallbackContext> &context,
                   const void *data, std::size_t size);

LUASF_API bool invokeSftpUpload(const std::shared_ptr<CallbackContext> &context,
                                void *data, std::size_t &size);

template <typename NativeCallable, typename Codec> struct FromObject;

template <>
struct FromObject<sf::SoundSource::EffectProcessor,
                  InterleavedFloatTransformCodec> {
  static sf::SoundSource::EffectProcessor read(const sol::object &object,
                                               CallbackOptions options) {
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
    const auto context = makeCallbackContext(object, std::move(options.label));
    return [context](void *data, std::size_t &size) {
      return invokeSftpUpload(context, data, size);
    };
  }
};

template <typename... Arguments>
struct FromObject<std::function<void(Arguments...)>,
                  NativeThreadBoundaryCodec> {
  static std::function<void(Arguments...)> read(const sol::object &object,
                                                CallbackOptions) {
    return native_thread_from_object<void(Arguments...)>(object);
  }
};

} // namespace detail

template <typename NativeCallable, typename Codec>
NativeCallable from_object(const sol::object &object,
                           CallbackOptions options = {}) {
  const bool isNil = !object.valid() || object == lua_sf::LUASF_SOL_NIL;
  if (isNil) {
    if (options.allowNil)
      return NativeCallable{};
    throw std::invalid_argument(options.label.empty()
                                    ? "Lua callback does not allow nil"
                                    : options.label + " does not allow nil");
  }
  if (!object.is<sol::protected_function>())
    throw std::invalid_argument(options.label.empty()
                                    ? "expected a Lua callback function"
                                    : "expected " + options.label);
  return detail::FromObject<NativeCallable, Codec>::read(object,
                                                         std::move(options));
}

template <typename NativeCallable, typename Codec>
NativeCallable from_object(const sol::object &object, std::string_view label) {
  return from_object<NativeCallable, Codec>(
      object, CallbackOptions{std::string(label), false});
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
  return to_object<NativeCallable, Codec>(lua, callable,
                                          CallbackOptions{std::string(label)});
}

template <>
LUASF_API sol::object
to_object<sf::SoundSource::EffectProcessor, InterleavedFloatTransformCodec>(
    sol::state_view lua, const sf::SoundSource::EffectProcessor &callable,
    CallbackOptions options);

} // namespace lua_sf::callback
