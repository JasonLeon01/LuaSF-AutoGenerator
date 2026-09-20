#pragma once

#include "LuaStateLifecycle.hpp"
#include "luasf_glue.hpp"
#include <SFML/Audio/PlaybackDevice.hpp>
#include <SFML/Graphics/Shader.hpp>
#include <SFML/Graphics/Text.hpp>
#include <SFML/Network/SocketSelector.hpp>
#include <SFML/System/String.hpp>
#include <SFML/Window/Event.hpp>
#include <SFML/Window/WindowHandle.hpp>

#include "LuaCallbackCodec.hpp"
#include "lua_stub.hpp"

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

inline std::string to_utf8_string(const sf::String& value) {
    const auto utf8 = value.toUtf8();
    return std::string(reinterpret_cast<const char*>(utf8.data()), utf8.size());
}

inline lua_glue::Table sf_table(lua_glue::StateView lua) {
    return lua["sf"].get_or_create<lua_glue::Table>();
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

    [[nodiscard]] sf::WindowHandle getHandle() const {
        return handle_;
    }

    [[nodiscard]] sf::WindowHandle native() const {
        return getHandle();
    }

    [[nodiscard]] std::uintptr_t toInteger() const {
        return integerFromNative(handle_);
    }

private:
    [[nodiscard]] static sf::WindowHandle nativeFromInteger(
        std::uintptr_t value) {
#if defined(SFML_SYSTEM_WINDOWS) || defined(SFML_SYSTEM_MACOS) || \
    defined(SFML_SYSTEM_IOS) || defined(SFML_SYSTEM_ANDROID) ||   \
    defined(SFML_SYSTEM_HARMONY)
        return reinterpret_cast<sf::WindowHandle>(value);
#else
        return static_cast<sf::WindowHandle>(value);
#endif
    }

    [[nodiscard]] static std::uintptr_t integerFromNative(
        sf::WindowHandle handle) {
#if defined(SFML_SYSTEM_WINDOWS) || defined(SFML_SYSTEM_MACOS) || \
    defined(SFML_SYSTEM_IOS) || defined(SFML_SYSTEM_ANDROID) ||   \
    defined(SFML_SYSTEM_HARMONY)
        return reinterpret_cast<std::uintptr_t>(handle);
#else
        return static_cast<std::uintptr_t>(handle);
#endif
    }

    sf::WindowHandle handle_{};
};

inline sf::WindowHandle window_handle_from_integer(std::uintptr_t value) {
    return WindowHandle(value).getHandle();
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

template <typename T>
class LuaIntegral {
public:
    using value_type = T;

    LuaIntegral() = default;
    explicit LuaIntegral(T value) : value_(value) {}

    [[nodiscard]] T value() const noexcept {
        return value_;
    }
    explicit operator T() const noexcept {
        return value_;
    }

private:
    T value_{};
};

template <typename T>
using LuaNumeric = std::conditional_t<is_lua_integral_v<T>, LuaIntegral<T>, T>;

template <typename T>
T unwrapLuaNumeric(const LuaNumeric<T>& value);

template <typename T>
bool tryReadLuaIntegral(lua_State* state, int index, T& value);

template <typename T>
T object_as(const lua_glue::Object& object);

template <typename T>
std::vector<T> array_from_object(const lua_glue::Object& object);

template <typename T>
std::optional<T> optional_from_object(const lua_glue::Object& object);

template <typename T>
lua_glue::Object as_lua_object(lua_glue::StateView lua, const T& value);

template <typename T, typename Allocator>
lua_glue::Object vector_to_object(lua_glue::StateView lua,
                                  const std::vector<T, Allocator>& values);

template <typename T>
lua_glue::Object optional_to_object(lua_glue::StateView lua,
                                    const std::optional<T>& value);

template <typename T>
lua_glue::Object optional_to_object(lua_glue::StateView lua,
                                    std::optional<T>&& value);

namespace detail {

template <typename Element>
struct ShaderUniformArrayVariant {
    using element_type = Element;

    std::string methodName;
    std::string luaArrayType;
};

template <typename Element>
ShaderUniformArrayVariant<Element> shaderUniformArrayVariant(
    std::string methodName, std::string luaArrayType);

template <typename Usertype, typename... Elements>
void bindShaderUniformArrays(Usertype& usertype, std::string_view luaOwner,
                             std::string_view inferredMethod,
                             ShaderUniformArrayVariant<Elements>... variants);

}  // namespace detail

template <typename Signature>
std::function<Signature> function_from_object(const lua_glue::Object& object);

template <typename Signature>
std::function<Signature> function_from_object_at_native_thread_boundary(
    const lua_glue::Object& object);

namespace callback {

template <typename Signature>
std::function<Signature> native_thread_from_object(
    const lua_glue::Object& object) {
    return function_from_object_at_native_thread_boundary<Signature>(object);
}

}  // namespace callback

using LongLivedMemoryBuffer = std::shared_ptr<std::vector<std::byte>>;

LongLivedMemoryBuffer makeLongLivedMemoryBuffer(const lua_glue::Object& object);

void rememberLongLivedMemory(const void* owner, LongLivedMemoryBuffer buffer);

void releaseLongLivedMemory(const void* owner);

void rememberLongLivedStream(const void* owner, const lua_glue::Object& stream);

void releaseLongLivedStream(const void* owner);

void releaseLongLivedResources(const void* owner);

template <typename T>
void rememberLongLivedMemory(const T& owner, LongLivedMemoryBuffer buffer);

template <typename T>
void releaseLongLivedMemory(const T& owner);

template <typename T>
void rememberLongLivedStream(const T& owner, const lua_glue::Object& stream);

template <typename T>
void releaseLongLivedStream(const T& owner);

template <typename T>
void releaseLongLivedResources(const T& owner);

template <typename T, typename... Args>
std::shared_ptr<T> makeLongLivedMemoryObject(Args&&... args);

}  // namespace lua_sf

namespace lua_glue {
template <typename T>
struct Codec<lua_sf::LuaIntegral<T>> {
    static constexpr bool native = false;
    static bool Check(lua_State* state, int index) {
        T value{};
        return lua_sf::tryReadLuaIntegral(state, index, value);
    }
    static lua_sf::LuaIntegral<T> Read(lua_State* state, int index) {
        T value{};
        if (!lua_sf::tryReadLuaIntegral(state, index, value)) {
            throw std::runtime_error(
                "expected a finite, in-range integer value");
        }
        return lua_sf::LuaIntegral<T>(value);
    }
    static int Push(lua_State* state, lua_sf::LuaIntegral<T> value) {
        return lua_glue::Push(state, value.value());
    }
};
}  // namespace lua_glue

#include "utils.inl"
