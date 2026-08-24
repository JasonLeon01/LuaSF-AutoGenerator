#pragma once

#include "LuaStateLifecycle.hpp"
#include "luasf_sol.hpp"
#include <SFML/Audio/PlaybackDevice.hpp>
#include <SFML/Graphics/Shader.hpp>
#include <SFML/Graphics/Text.hpp>
#include <SFML/Network/SocketSelector.hpp>
#include <SFML/System/String.hpp>
#include <SFML/Window/Event.hpp>
#include <SFML/Window/WindowHandle.hpp>

#include "lua_stub.hpp"
#include "LuaCallbackCodec.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace lua_sf {

inline sf::String to_sf_string(std::string_view value) {
  return sf::String::fromUtf8(value.begin(), value.end());
}

inline std::string to_utf8_string(const sf::String &value) {
  const auto utf8 = value.toUtf8();
  return std::string(reinterpret_cast<const char *>(utf8.data()), utf8.size());
}

inline sol::table sf_table(sol::state_view lua) {
  return lua["sf"].get_or_create<sol::table>();
}

class WindowHandle {
public:
  WindowHandle() = default;

  explicit WindowHandle(std::uintptr_t value)
      : handle_(nativeFromInteger(value)) {}

  [[nodiscard]] static WindowHandle fromNative(sf::WindowHandle handle) {
    WindowHandle result;
    result.handle_ = handle;
    return result;
  }

  [[nodiscard]] sf::WindowHandle native() const { return handle_; }

  [[nodiscard]] std::uintptr_t toInteger() const {
    return integerFromNative(handle_);
  }

private:
  [[nodiscard]] static sf::WindowHandle
  nativeFromInteger(std::uintptr_t value) {
#if defined(SFML_SYSTEM_WINDOWS) || defined(SFML_SYSTEM_MACOS) ||              \
    defined(SFML_SYSTEM_IOS) || defined(SFML_SYSTEM_ANDROID)
    return reinterpret_cast<sf::WindowHandle>(value);
#else
    return static_cast<sf::WindowHandle>(value);
#endif
  }

  [[nodiscard]] static std::uintptr_t
  integerFromNative(sf::WindowHandle handle) {
#if defined(SFML_SYSTEM_WINDOWS) || defined(SFML_SYSTEM_MACOS) ||              \
    defined(SFML_SYSTEM_IOS) || defined(SFML_SYSTEM_ANDROID)
    return reinterpret_cast<std::uintptr_t>(handle);
#else
    return static_cast<std::uintptr_t>(handle);
#endif
  }

  sf::WindowHandle handle_{};
};

inline sf::WindowHandle window_handle_from_integer(std::uintptr_t value) {
  return WindowHandle(value).native();
}

inline std::uintptr_t window_handle_to_integer(sf::WindowHandle handle) {
  return WindowHandle::fromNative(handle).toInteger();
}

template <typename T>
inline constexpr bool is_byte_like_v =
    std::is_same_v<std::remove_cv_t<T>, std::byte> ||
    std::is_same_v<std::remove_cv_t<T>, std::uint8_t> ||
    std::is_same_v<std::remove_cv_t<T>, unsigned char>;

template <typename T>
inline constexpr bool is_lua_integral_v =
    std::is_integral_v<std::remove_cv_t<std::remove_reference_t<T>>> &&
    !std::is_same_v<std::remove_cv_t<std::remove_reference_t<T>>, bool>;

template <typename T> class LuaIntegral {
public:
  using value_type = T;

  LuaIntegral() = default;
  explicit LuaIntegral(T value) : value_(value) {}

  [[nodiscard]] T value() const noexcept { return value_; }
  explicit operator T() const noexcept { return value_; }

private:
  T value_{};
};

template <typename T>
using LuaNumeric =
    std::conditional_t<is_lua_integral_v<T>, LuaIntegral<T>, T>;

template <typename T>
T unwrapLuaNumeric(const LuaNumeric<T> &value);

template <typename T>
bool tryReadLuaIntegral(lua_State *state, int index, T &value);

template <typename T, typename Handler>
bool sol_lua_check(sol::types<LuaIntegral<T>>, lua_State *state, int index,
                   Handler &&handler, sol::stack::record &tracking);

template <typename T>
LuaIntegral<T> sol_lua_get(sol::types<LuaIntegral<T>>, lua_State *state,
                           int index, sol::stack::record &tracking);

template <typename T> T object_as(const sol::object &object);

template <typename T>
std::vector<T> array_from_object(const sol::object &object);

template <typename T>
std::optional<T> optional_from_object(const sol::object &object);

template <typename T>
sol::object as_lua_object(sol::state_view lua, const T &value);

template <typename T, typename Allocator>
sol::object vector_to_object(sol::state_view lua,
                             const std::vector<T, Allocator> &values);

template <typename T>
sol::object optional_to_object(sol::state_view lua,
                               const std::optional<T> &value);

template <typename T>
sol::object optional_to_object(sol::state_view lua, std::optional<T> &&value);

namespace detail {

template <typename Element> struct ShaderUniformArrayVariant {
  using element_type = Element;

  std::string methodName;
  std::string luaArrayType;
};

template <typename Element>
ShaderUniformArrayVariant<Element>
shaderUniformArrayVariant(std::string methodName, std::string luaArrayType);

template <typename Usertype, typename... Elements>
void bindShaderUniformArrays(
    Usertype &usertype, std::string_view luaOwner,
    std::string_view inferredMethod,
    ShaderUniformArrayVariant<Elements>... variants);

} // namespace detail

template <typename Signature>
std::function<Signature> function_from_object(const sol::object &object);

template <typename Signature>
std::function<Signature>
function_from_object_at_native_thread_boundary(const sol::object &object);

namespace callback {

template <typename Signature>
std::function<Signature>
native_thread_from_object(const sol::object &object) {
  return function_from_object_at_native_thread_boundary<Signature>(object);
}

} // namespace callback

using LongLivedMemoryBuffer = std::shared_ptr<std::vector<std::byte>>;

LongLivedMemoryBuffer makeLongLivedMemoryBuffer(const sol::object &object);

void rememberLongLivedMemory(const void *owner, LongLivedMemoryBuffer buffer);

void releaseLongLivedMemory(const void *owner);

void rememberLongLivedStream(const void *owner, const sol::object &stream);

void releaseLongLivedStream(const void *owner);

void releaseLongLivedResources(const void *owner);

template <typename T>
void rememberLongLivedMemory(const T &owner, LongLivedMemoryBuffer buffer);

template <typename T> void releaseLongLivedMemory(const T &owner);

template <typename T>
void rememberLongLivedStream(const T &owner, const sol::object &stream);

template <typename T> void releaseLongLivedStream(const T &owner);

template <typename T> void releaseLongLivedResources(const T &owner);

template <typename T, typename... Args>
std::shared_ptr<T> makeLongLivedMemoryObject(Args &&...args);

} // namespace lua_sf

#include "utils.inl"
